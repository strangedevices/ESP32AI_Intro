// Stage 05 - always-listening assistant with tools the agent can call on the device.
//
// Adds two things to stage 04:
//   AUTO MODE   The mic listens locally (no connection, no credits). When it hears
//               sustained sound it opens a conversation by itself, and goes back to
//               waiting when the conversation ends.
//   AGENT TOOLS The agent can call functions on the device ("client tools"):
//                 start_recording    record the conversation to the SD card
//                 stop_recording     stop and save it
//                 get_device_status  report battery, Wi-Fi, uptime
//               You define the same tool names on your agent in the ElevenLabs dashboard.
//
// Buttons:  [WiFi] [Talk] start / end a conversation   [Auto] toggle auto mode   [Clear]
// Serial:   w, t, a, c as above.  r / s / d simulate the three tool calls for testing.
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SD.h>
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
// A conversation costs roughly 1000 ElevenLabs credits a minute while it is open.
static const uint32_t MAX_SESSION_S = 90;         // hard cap on one conversation
static const uint32_t IDLE_END_S    = 12;         // end if nobody has spoken for this long

// ---- auto mode ---------------------------------------------------------------
static const int16_t  WAKE_PEAK   = 6000;         // loudness that counts as "someone spoke" (max 32767)
static const int      WAKE_CHUNKS = 3;            // loud chunks in a row needed (3 = about 0.4 s)
static const uint32_t AUTO_COOLDOWN_MS = 4000;    // wait after a conversation before listening again

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
static bool autoMode = false, monitoring = false;
static int  loudChunks = 0;
static uint32_t autoResumeAt = 0;
static uint32_t lastAudioMs = 0, transcriptMs = 0, sessionStart = 0, lastActivityMs = 0;
static uint32_t audioBytesIn = 0, chunksOut = 0;
static uint32_t listenIdleMs = 0, lastTick = 0;   // time spent listening since you last spoke

// ---- SD recording of the conversation (driven by the agent's tools) ----------------
static const char *REC_DIR = "/recordings";
static bool sdOk = false;
static bool convRec = false;
static File convFile;
static uint32_t convBytes = 0;
static char convName[48] = "";

enum { BTN_WIFI, BTN_TALK, BTN_AUTO, BTN_CLEAR, N_BTN };
static ui::Button buttons[N_BTN] = {
  { "WiFi",  0, 0, 0, 0, ui::COL_GREEN },
  { "Talk",  0, 0, 0, 0, ui::COL_BLUE },
  { "Auto",  0, 0, 0, 0, ui::COL_GREY },
  { "Clear", 0, 0, 0, 0, ui::COL_GREY },
};

static void say(uint32_t color, const char *who, const char *text) {
  ui::logColor = color;
  ui::log("%s%s", who, text);
  ui::logColor = TFT_LIGHTGREY;
}

// ---- status bar ----------------------------------------------------------------
static void drawMeter(bool showThreshold) {
  int w = 60, x = ui::W - w - 6, y = 8, h = ui::BAR_H - 16;
  M5.Display.fillRect(x, y, w, h, TFT_BLACK);
  M5.Display.fillRect(x, y, (int)((long)w * peak / 32767), h, TFT_WHITE);
  if (showThreshold) M5.Display.drawFastVLine(x + (int)((long)w * WAKE_PEAK / 32767), y - 2, h + 4, TFT_RED);
}

static void updateStatusBar() {
  char buf[32];
  unsigned secs = (millis() - sessionStart) / 1000;
  const char *rec = convRec ? " REC" : "";
  if (mode == LISTENING) {
    snprintf(buf, sizeof buf, "LISTEN %us%s", secs, rec);
    ui::status(buf, convRec ? ui::COL_RED : ui::COL_GREEN);
    drawMeter(false);
  } else if (mode == SPEAKING) {
    snprintf(buf, sizeof buf, "AGENT %us%s", secs, rec);
    ui::status(buf, convRec ? ui::COL_RED : ui::COL_PURPLE);
  } else if (monitoring) {
    ui::status("AUTO waiting", TFT_NAVY);
    drawMeter(true);
  } else if (WiFi.status() == WL_CONNECTED) {
    ui::status(WiFi.localIP().toString().c_str(), TFT_DARKGREEN);
  } else {
    ui::status(wantWifi ? "connecting..." : "WiFi off", TFT_NAVY);
  }
}

// ---- conversation recording --------------------------------------------------------
static void writeWavHeader(File &f, uint32_t dataBytes) {
  uint8_t h[44];
  const uint32_t rate = 16000;
  auto put32 = [&](int o, uint32_t v) { h[o]=v; h[o+1]=v>>8; h[o+2]=v>>16; h[o+3]=v>>24; };
  auto put16 = [&](int o, uint16_t v) { h[o]=v; h[o+1]=v>>8; };
  memcpy(h, "RIFF", 4); put32(4, 36 + dataBytes); memcpy(h + 8, "WAVEfmt ", 8);
  put32(16, 16); put16(20, 1); put16(22, 1); put32(24, rate); put32(28, rate * 2);
  put16(32, 2); put16(34, 16); memcpy(h + 36, "data", 4); put32(40, dataBytes);
  f.seek(0);
  f.write(h, 44);
}

// Both sides of the conversation go into one file, in the order they happen.
static void convWrite(const int16_t *s, size_t n) {
  if (!convRec) return;
  convFile.write((const uint8_t *)s, n * 2);
  convBytes += n * 2;
}

// ---- tools the agent can call ---------------------------------------------------------
static String toolStartRecording() {
  if (!sdOk) return "error: no SD card in the device";
  if (convRec) return "already recording";
  if (micRate != 16000 || spkRate != 16000) return "error: recording needs 16 kHz audio";
  if (!SD.exists(REC_DIR)) SD.mkdir(REC_DIR);
  for (int n = 0; n < 1000; n++) {
    snprintf(convName, sizeof convName, "%s/conv_%03d.wav", REC_DIR, n);
    if (!SD.exists(convName)) break;
  }
  convFile = SD.open(convName, FILE_WRITE);
  if (!convFile) return "error: could not create the file";
  writeWavHeader(convFile, 0);
  convBytes = 0;
  convRec = true;
  updateStatusBar();
  return String("recording started, file ") + convName;
}

static String toolStopRecording() {
  if (!convRec) return "not recording";
  convRec = false;
  writeWavHeader(convFile, convBytes);
  convFile.close();
  updateStatusBar();
  char buf[96];
  snprintf(buf, sizeof buf, "recording saved, %.1f seconds, file %s", convBytes / 32000.0f, convName);
  return buf;
}

static String toolDeviceStatus() {
  char buf[200];
  snprintf(buf, sizeof buf,
           "battery %d%%, wifi signal %d dBm, uptime %lu seconds, sd card %s, recording %s, free memory %u KB",
           (int)M5.Power.getBatteryLevel(), (int)WiFi.RSSI(), millis() / 1000,
           sdOk ? "present" : "missing", convRec ? "on" : "off", (unsigned)(ESP.getFreeHeap() / 1024));
  return buf;
}

// Add your own tools here: match on the name, do the thing, return a short sentence
// the agent can read out. Set isError for failures.
static String runTool(const char *name, JsonObject params, bool &isError) {
  isError = false;
  if (!strcmp(name, "start_recording"))   return toolStartRecording();
  if (!strcmp(name, "stop_recording"))    return toolStopRecording();
  if (!strcmp(name, "get_device_status")) return toolDeviceStatus();
  isError = true;
  return String("unknown tool: ") + name;
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
static void micStart() {
  M5.Speaker.end();
  M5.Mic.begin();
  micIdx = 0;
  M5.Mic.record(micBuf[0], MIC_CHUNK, micRate);
  M5.Mic.record(micBuf[1], MIC_CHUNK, micRate);
}
static void micStop() {
  while (M5.Mic.isRecording()) delay(1);
  M5.Mic.end();
}

static void enterListening() {
  micStart();
  mode = LISTENING;
  updateStatusBar();
}

static void enterSpeaking() {
  micStop();
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
    convWrite(playBuf[playIdx], n);
    M5.Speaker.playRaw(playBuf[playIdx], n, spkRate, false, 1, 0);
    playIdx = (playIdx + 1) % 3;
  }
}

// Take the next finished mic chunk, or nullptr if none is ready. Updates `peak`.
static int16_t *nextMicChunk() {
  if (M5.Mic.isRecording() >= 2) return nullptr;
  int16_t *b = micBuf[micIdx];
  int16_t pk = 0;
  for (size_t i = 0; i < MIC_CHUNK; i++) { int16_t v = b[i] < 0 ? -b[i] : b[i]; if (v > pk) pk = v; }
  peak = pk;
  return b;
}
static void requeueMicChunk(int16_t *b) {
  M5.Mic.record(b, MIC_CHUNK, micRate);
  micIdx ^= 1;
}

// Send finished mic chunks to the agent.
static void pumpMic() {
  if (mode != LISTENING) return;
  int16_t *b = nextMicChunk();
  if (!b) return;
  convWrite(b, MIC_CHUNK);
  int n = snprintf(txBuf, sizeof txBuf, "{\"user_audio_chunk\":\"");
  size_t b64 = 0;
  mbedtls_base64_encode((unsigned char *)txBuf + n, sizeof txBuf - n - 3, &b64,
                        (const unsigned char *)b, MIC_CHUNK * 2);
  n += b64;
  txBuf[n++] = '"'; txBuf[n++] = '}';
  ws.sendText(txBuf, n);
  chunksOut++;
  requeueMicChunk(b);
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
  } else if (!strcmp(type, "client_tool_call")) {
    // The agent wants the device to do something. Run it and send back the result.
    JsonObject call = doc["client_tool_call"];
    const char *name = call["tool_name"] | "";
    const char *id   = call["tool_call_id"] | "";
    bool isError = false;
    String result = runTool(name, call["parameters"], isError);
    ui::logColor = TFT_ORANGE;
    ui::log("Tool %s -> %s", name, result.c_str());
    ui::logColor = TFT_LIGHTGREY;
    listenIdleMs = 0;
    if (strcmp(id, "test") != 0) {                   // "test" = simulated from the serial port
      JsonDocument reply;
      reply["type"] = "client_tool_result";
      reply["tool_call_id"] = id;
      reply["result"] = result;
      reply["is_error"] = isError;
      char buf[400];
      size_t n = serializeJson(reply, buf, sizeof buf);
      ws.sendText(buf, n);
    }
  }
}

static void simulateTool(const char *name) {
  char msg[160];
  int n = snprintf(msg, sizeof msg,
                   "{\"type\":\"client_tool_call\",\"client_tool_call\":{\"tool_name\":\"%s\",\"tool_call_id\":\"test\",\"parameters\":{}}}", name);
  onMessage(msg, n);
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

static void monitorStop() {
  if (!monitoring) return;
  micStop();
  monitoring = false;
}

static void endSession(const char *why) {
  if (!session) return;
  session = false;
  if (convRec) ui::log("Tool (auto) -> %s", toolStopRecording().c_str());
  ws.close();
  micStop();
  M5.Speaker.end();
  mode = IDLE;
  ringCount = 0;
  buttons[BTN_TALK].label = "Talk";
  buttons[BTN_TALK].draw();
  ui::log("Conversation ended (%s): %lu s, %lu KB audio in, %lu chunks out", why,
          (millis() - sessionStart) / 1000, audioBytesIn / 1024, chunksOut);
  autoResumeAt = millis() + AUTO_COOLDOWN_MS;
  updateStatusBar();
}

static void startSession() {
  monitorStop();
  autoResumeAt = millis() + 10000;                  // if this fails, do not retry instantly
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
    monitorStop(); autoMode = false;
    buttons[BTN_AUTO].color = ui::COL_GREY; buttons[BTN_AUTO].draw();
    wantWifi = false; WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
    ui::log("WiFi: disconnected");
  } else {
    waitForWifi();
  }
  updateStatusBar();
}

// ---- auto mode -------------------------------------------------------------------------
static void autoToggle() {
  autoMode = !autoMode;
  if (autoMode) {
    if (!waitForWifi()) { autoMode = false; return; }   // connect now so waking is quick
    ui::log("Auto: on. Speak near the device to start (level above the red line).");
    autoResumeAt = millis();
  } else {
    monitorStop();
    ui::log("Auto: off");
  }
  buttons[BTN_AUTO].color = autoMode ? ui::COL_PURPLE : ui::COL_GREY;
  buttons[BTN_AUTO].draw();
  updateStatusBar();
}

// Listen locally. No network traffic and no credits until something is heard.
static void pumpMonitor() {
  if (!monitoring) {
    if (millis() < autoResumeAt) return;
    micStart();
    monitoring = true;
    loudChunks = 0;
    updateStatusBar();
    return;
  }
  int16_t *b = nextMicChunk();
  if (!b) return;
  requeueMicChunk(b);
  loudChunks = peak > WAKE_PEAK ? loudChunks + 1 : 0;
  static uint32_t lastBar = 0;
  if (millis() - lastBar > 150) { lastBar = millis(); updateStatusBar(); }
  if (loudChunks >= WAKE_CHUNKS) {
    ui::log("Auto: heard you (level %d%%), starting", peak * 100 / 32767);
    startSession();
  }
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
  SPI.begin(GPIO_NUM_36, GPIO_NUM_35, GPIO_NUM_37, GPIO_NUM_4);
  sdOk = SD.begin(GPIO_NUM_4, SPI, 25000000);
  ws.onText = onMessage;
  ws.onIdle = pumpSpeaker;                          // keep audio flowing during big downloads
  updateStatusBar();
  ui::log("Stage 05 ready. SD card %s", sdOk ? "ok" : "MISSING (recording tools will fail)");
  ui::log("Talk = start now. Auto = start when it hears you.");
}

void loop() {
  M5.update();
  int b = ui::tappedButton(buttons, N_BTN);
  char c = Serial.available() ? Serial.read() : 0;
  if (b >= 0) { buttons[b].draw(true); delay(60); buttons[b].draw(false); }
  if (b == BTN_TALK  || c == 't') { if (session) endSession("you ended it"); else startSession(); }
  if (b == BTN_WIFI  || c == 'w') wifiToggle();
  if (b == BTN_AUTO  || c == 'a') { if (!session) autoToggle(); }
  if (b == BTN_CLEAR || c == 'c') ui::logClear();
  if (c == 'r') simulateTool("start_recording");
  if (c == 's') simulateTool("stop_recording");
  if (c == 'd') simulateTool("get_device_status");

  if (!session) {
    if (autoMode) pumpMonitor();
    delay(5);
    return;
  }

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
