// Stage 04 - voice assistant: talk to an ElevenLabs agent over a WebSocket.
//
// Buttons:  [WiFi] connect / disconnect     [Talk] start / end a conversation
//           [Clear] clear the log
// Serial:   w, t, c as above
//
// How a conversation works:
//   1. Ask the ElevenLabs API for a signed WebSocket URL for our agent (uses the API key).
//   2. Open the WebSocket. The agent greets us: audio arrives as base64 PCM in JSON.
//   3. Take turns. The CoreS3 mic and speaker share one I2S bus, so the device is
//      half-duplex: while the agent's audio is playing the mic is off; when playback
//      finishes the mic comes back on and streams to the agent, which decides by itself
//      when you have stopped speaking.
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"
#include "ui.h"
#include "ws_client.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif
#ifndef ELEVENLABS_API_KEY
#define ELEVENLABS_API_KEY ""
#define ELEVENLABS_AGENT_ID ""
#endif

static const char *EL_HOST = "api.elevenlabs.io";

// ---- credit guards -----------------------------------------------------------
// A conversation costs ElevenLabs credits for as long as it is open (roughly 1000 credits
// a minute in our tests; a free account has 10,000). These limits stop a forgotten
// conversation from emptying your account.
static const uint32_t MAX_SESSION_S = 90;         // hard cap on one conversation
static const uint32_t IDLE_END_S    = 20;         // end if nobody has spoken for this long

// ---- speaker -----------------------------------------------------------------
static const uint8_t  SPEAKER_VOLUME = 100;       // 0-255. Agent speech is loud; higher clips.
static const uint32_t PREBUFFER_MS   = 300;       // audio to collect before starting to play

// ---- audio -------------------------------------------------------------------
static const size_t MIC_CHUNK  = 2048;            // samples per mic chunk (128 ms at 16 kHz)
static const size_t PLAY_CHUNK = 2048;
static const size_t RING_SAMPLES = 1 << 20;       // agent audio waiting to play (65 s at 16 kHz)
static int16_t micBuf[2][MIC_CHUNK];
static int16_t playBuf[3][PLAY_CHUNK];            // speaker queue holds 2; third is free to fill
static int16_t *ring = nullptr;
static size_t ringHead = 0, ringCount = 0;
static uint32_t micRate = 16000, spkRate = 16000;
static int micIdx = 0, playIdx = 0;
static int16_t peak = 0;
static char txBuf[MIC_CHUNK * 2 * 4 / 3 + 64];    // one mic chunk as base64 inside JSON

// ---- state -------------------------------------------------------------------
enum Mode { IDLE, LISTENING, SPEAKING };
static Mode mode = IDLE;
static WsClient ws;
static bool session = false;
static bool wantWifi = false;
static uint32_t lastAudioMs = 0, transcriptMs = 0, sessionStart = 0, lastActivityMs = 0;
static uint32_t audioBytesIn = 0, chunksOut = 0;
static uint32_t listenIdleMs = 0, lastTick = 0;   // time spent listening since you last spoke

enum { BTN_WIFI, BTN_TALK, BTN_CLEAR, N_BTN };
static ui::Button buttons[N_BTN] = {
  { "WiFi",  0, 0, 0, 0, ui::COL_GREEN },
  { "Talk",  0, 0, 0, 0, ui::COL_BLUE },
  { "Clear", 0, 0, 0, 0, ui::COL_GREY },
};

static void say(uint32_t color, const char *who, const char *text) {
  ui::logColor = color;
  ui::log("%s%s", who, text);
  ui::logColor = TFT_LIGHTGREY;
}

// ---- status bar ----------------------------------------------------------------
static void updateStatusBar() {
  char buf[32];
  unsigned secs = (millis() - sessionStart) / 1000;
  if (mode == LISTENING) {
    snprintf(buf, sizeof buf, "LISTEN %us", secs);
    ui::status(buf, ui::COL_GREEN);
    int w = 60, x = ui::W - w - 6, y = 8, h = ui::BAR_H - 16;
    M5.Display.fillRect(x, y, w, h, TFT_BLACK);
    M5.Display.fillRect(x, y, (int)((long)w * peak / 32767), h, TFT_WHITE);
  } else if (mode == SPEAKING) {
    snprintf(buf, sizeof buf, "AGENT %us", secs);
    ui::status(buf, ui::COL_PURPLE);
  } else if (WiFi.status() == WL_CONNECTED) {
    ui::status(WiFi.localIP().toString().c_str(), TFT_DARKGREEN);
  } else {
    ui::status(wantWifi ? "connecting..." : "WiFi off", TFT_NAVY);
  }
}

// ---- agent audio ring buffer -----------------------------------------------------
static void ringPush(const int16_t *s, size_t n) {
  if (n > RING_SAMPLES - ringCount) { n = RING_SAMPLES - ringCount; ui::log("audio buffer full, dropping"); }
  size_t tail = (ringHead + ringCount) % RING_SAMPLES;
  size_t first = min(n, RING_SAMPLES - tail);
  memcpy(ring + tail, s, first * 2);
  memcpy(ring, s + first, (n - first) * 2);
  ringCount += n;
}
static size_t ringPop(int16_t *dst, size_t n) {
  n = min(n, ringCount);
  size_t first = min(n, RING_SAMPLES - ringHead);
  memcpy(dst, ring + ringHead, first * 2);
  memcpy(dst + first, ring, (n - first) * 2);
  ringHead = (ringHead + n) % RING_SAMPLES;
  ringCount -= n;
  return n;
}

// ---- switching between mic and speaker --------------------------------------------
static void enterListening() {
  M5.Speaker.end();
  M5.Mic.begin();
  micIdx = 0;
  M5.Mic.record(micBuf[0], MIC_CHUNK, micRate);
  M5.Mic.record(micBuf[1], MIC_CHUNK, micRate);
  mode = LISTENING;
  updateStatusBar();
}

static void enterSpeaking() {
  while (M5.Mic.isRecording()) delay(1);
  M5.Mic.end();
  M5.Speaker.begin();
  M5.Speaker.setVolume(SPEAKER_VOLUME);
  playIdx = 0;
  mode = SPEAKING;
  if (transcriptMs) { ui::log("(agent replied %lu ms after your words)", millis() - transcriptMs); transcriptMs = 0; }
  updateStatusBar();
}

// Feed the speaker from the ring buffer. Safe to call very often.
static void pumpSpeaker() {
  if (mode != SPEAKING) return;
  while (ringCount && M5.Speaker.isPlaying(0) < 2) {
    size_t n = ringPop(playBuf[playIdx], PLAY_CHUNK);
    M5.Speaker.playRaw(playBuf[playIdx], n, spkRate, false, 1, 0);
    playIdx = (playIdx + 1) % 3;
  }
}

// Send finished mic chunks to the agent.
static void pumpMic() {
  if (mode != LISTENING || M5.Mic.isRecording() >= 2) return;
  int16_t *b = micBuf[micIdx];
  int16_t pk = 0;
  for (size_t i = 0; i < MIC_CHUNK; i++) { int16_t v = b[i] < 0 ? -b[i] : b[i]; if (v > pk) pk = v; }
  peak = pk;

  int n = snprintf(txBuf, sizeof txBuf, "{\"user_audio_chunk\":\"");
  size_t b64 = 0;
  mbedtls_base64_encode((unsigned char *)txBuf + n, sizeof txBuf - n - 3, &b64,
                        (const unsigned char *)b, MIC_CHUNK * 2);
  n += b64;
  txBuf[n++] = '"'; txBuf[n++] = '}';
  ws.sendText(txBuf, n);
  chunksOut++;

  M5.Mic.record(b, MIC_CHUNK, micRate);
  micIdx ^= 1;
}

// ---- messages from the agent ---------------------------------------------------------
static uint32_t rateFromFormat(const char *fmt) {      // "pcm_16000" -> 16000
  if (strncmp(fmt, "pcm_", 4) != 0) return 0;
  return atoi(fmt + 4);
}

static void onMessage(char *msg, size_t len) {
  // Audio first, without a JSON parser: these messages are huge.
  char *a = strstr(msg, "\"audio_base_64\"");
  if (a) {
    a = strchr(a + 15, '"');
    if (!a) return;
    a++;
    char *e = strchr(a, '"');
    if (!e) return;
    size_t cap = (e - a) * 3 / 4 + 4, out = 0;
    uint8_t *pcm = (uint8_t *)ps_malloc(cap);
    if (!pcm) { ui::log("no memory for audio"); return; }
    if (mbedtls_base64_decode(pcm, cap, &out, (const unsigned char *)a, e - a) == 0) {
      ringPush((const int16_t *)pcm, out / 2);
      audioBytesIn += out;
      lastAudioMs = lastActivityMs = millis();
    }
    free(pcm);
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, msg, len)) return;
  const char *type = doc["type"] | "";

  if (!strcmp(type, "ping")) {
    char pong[64];
    int n = snprintf(pong, sizeof pong, "{\"type\":\"pong\",\"event_id\":%d}", (int)(doc["ping_event"]["event_id"] | 0));
    ws.sendText(pong, n);
  } else if (!strcmp(type, "conversation_initiation_metadata")) {
    JsonObject m = doc["conversation_initiation_metadata_event"];
    const char *out = m["agent_output_audio_format"] | "?";
    const char *in  = m["user_input_audio_format"] | "?";
    ui::log("Agent audio %s, mic %s", out, in);
    uint32_t r = rateFromFormat(out);
    if (r) spkRate = r; else ui::log("!! unsupported agent audio format");
    r = rateFromFormat(in);
    if (r) micRate = r; else ui::log("!! unsupported mic format");
  } else if (!strcmp(type, "user_transcript")) {
    const char *said = doc["user_transcription_event"]["user_transcript"] | "";
    say(TFT_CYAN, "You: ", said);
    transcriptMs = millis();
    if (strcmp(said, "...") != 0) listenIdleMs = 0;   // "..." means it heard only noise
  } else if (!strcmp(type, "agent_response")) {
    say(TFT_YELLOW, "Agent: ", doc["agent_response_event"]["agent_response"] | "");
  } else if (!strcmp(type, "interruption")) {
    ringCount = 0;                                   // drop audio we have not played yet
  } else if (!strcmp(type, "client_error")) {
    ui::log("!! agent error: %s", (const char *)(doc["error_event"]["message"] | "?"));
  }
}

// ---- conversation start / end -----------------------------------------------------------
static bool waitForWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (strlen(WIFI_SSID) == 0) { ui::log("No Wi-Fi details: edit ../secrets.h"); return false; }
  wantWifi = true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  ui::log("WiFi: connecting");
  updateStatusBar();
  for (int attempt = 0; attempt < 2; attempt++) {
    uint32_t t0 = millis();
    while (millis() - t0 < 10000) { if (WiFi.status() == WL_CONNECTED) break; delay(100); }
    if (WiFi.status() == WL_CONNECTED) { ui::log("WiFi: connected, %s", WiFi.localIP().toString().c_str()); return true; }
    WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
  ui::log("WiFi: failed");
  return false;
}

// Ask the API for a signed wss:// URL. Returns the path part, or "" on failure.
static String getSignedPath() {
  WiFiClientSecure tls;
  tls.setInsecure();                                // workshop only: skip certificate check
  HTTPClient http;
  http.setTimeout(10000);
  String url = String("https://") + EL_HOST + "/v1/convai/conversation/get-signed-url?agent_id=" + ELEVENLABS_AGENT_ID;
  if (!http.begin(tls, url)) return "";
  http.addHeader("xi-api-key", ELEVENLABS_API_KEY);
  int code = http.GET();
  String body = http.getString();
  http.end();
  if (code != 200) { ui::log("Signed URL: HTTP %d %s", code, body.substring(0, 100).c_str()); return ""; }
  JsonDocument doc;
  if (deserializeJson(doc, body)) return "";
  String signedUrl = doc["signed_url"] | "";
  int slash = signedUrl.indexOf('/', 6);            // skip "wss://"
  return slash > 0 ? signedUrl.substring(slash) : "";
}

static void endSession(const char *why) {
  if (!session) return;
  session = false;
  ws.close();
  while (M5.Mic.isRecording()) delay(1);
  M5.Mic.end();
  M5.Speaker.end();
  mode = IDLE;
  ringCount = 0;
  buttons[BTN_TALK].label = "Talk";
  buttons[BTN_TALK].draw();
  ui::log("Conversation ended (%s): %lu s, %lu KB audio in, %lu chunks out", why,
          (millis() - sessionStart) / 1000, audioBytesIn / 1024, chunksOut);
  updateStatusBar();
}

static void startSession() {
  if (strlen(ELEVENLABS_API_KEY) < 10 || strlen(ELEVENLABS_AGENT_ID) < 10) {
    ui::log("No ElevenLabs key/agent: edit ../secrets.h");
    return;
  }
  if (!waitForWifi()) return;
  uint32_t t0 = millis();
  ui::log("Asking for a signed URL...");
  String path = getSignedPath();
  if (path.isEmpty()) {                             // public agents work without a signature
    path = String("/v1/convai/conversation?agent_id=") + ELEVENLABS_AGENT_ID;
    ui::log("Trying unsigned (public agent) URL");
  }
  ui::log("Opening WebSocket...");
  if (!ws.connect(EL_HOST, 443, path.c_str())) {
    ui::log("WebSocket failed: %s", ws.lastError.c_str());
    return;
  }
  char hello[] = "{\"type\":\"conversation_initiation_client_data\"}";
  ws.sendText(hello, strlen(hello));
  ui::log("Connected in %lu ms. Say hello!", millis() - t0);

  session = true;
  sessionStart = lastActivityMs = lastTick = millis();
  listenIdleMs = 0;
  audioBytesIn = 0; chunksOut = 0; ringCount = 0; ringHead = 0; transcriptMs = 0;
  buttons[BTN_TALK].label = "End";
  buttons[BTN_TALK].draw();
  enterListening();
}

static void wifiToggle() {
  if (session) return;
  if (wantWifi) {
    wantWifi = false; WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
    ui::log("WiFi: disconnected");
  } else {
    waitForWifi();
  }
  updateStatusBar();
}

// ---- setup / loop -------------------------------------------------------------------------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  ui::begin();
  ui::layoutButtons(buttons, N_BTN);
  ring = (int16_t *)ps_malloc(RING_SAMPLES * sizeof(int16_t));
  // Larger I2S buffers for the speaker: more tolerant of the loop being busy with Wi-Fi.
  auto spk = M5.Speaker.config();
  spk.dma_buf_count = 16;
  spk.dma_buf_len = 256;
  M5.Speaker.config(spk);
  ws.onText = onMessage;
  ws.onIdle = pumpSpeaker;                          // keep audio flowing during big downloads
  updateStatusBar();
  ui::log("Stage 04 assistant ready");
  ui::log("Tap Talk to start a conversation");
}

void loop() {
  M5.update();
  int b = ui::tappedButton(buttons, N_BTN);
  char c = Serial.available() ? Serial.read() : 0;
  if (b >= 0) { buttons[b].draw(true); delay(60); buttons[b].draw(false); }
  if (b == BTN_TALK  || c == 't') { if (session) endSession("you ended it"); else startSession(); }
  if (b == BTN_WIFI  || c == 'w') wifiToggle();
  if (b == BTN_CLEAR || c == 'c') ui::logClear();

  if (!session) { delay(10); return; }

  ws.poll();
  if (!ws.connected()) {
    char why[64];
    snprintf(why, sizeof why, "%s, code %u", ws.lastError.c_str(), ws.closeCode);
    endSession(why);
    return;
  }

  // Credit guards.
  if (millis() - sessionStart > MAX_SESSION_S * 1000) { endSession("time limit, tap Talk to go again"); return; }
  // Count only time spent listening. The agent asking "are you still there?" does not
  // reset it; only your words (or a tool call) do.
  uint32_t now = millis();
  if (mode == LISTENING) listenIdleMs += now - lastTick;
  lastTick = now;
  if (listenIdleMs > IDLE_END_S * 1000) { endSession("nobody spoke"); return; }

  // Turn-taking. Wait for a little audio to build up before playing (or for the whole of
  // a short reply), so playback does not stutter if the next message is slow to arrive.
  if (mode == LISTENING && ringCount &&
      (ringCount >= spkRate * PREBUFFER_MS / 1000 || millis() - lastAudioMs > 150))
    enterSpeaking();
  pumpSpeaker();
  if (mode == SPEAKING && ringCount == 0 && !M5.Speaker.isPlaying(0) && millis() - lastAudioMs > 300) {
    lastActivityMs = millis();                      // the idle clock starts when the agent stops
    enterListening();
  }
  pumpMic();

  static uint32_t lastBar = 0;
  if (millis() - lastBar > (mode == LISTENING ? 150 : 1000)) { lastBar = millis(); updateStatusBar(); }
}
