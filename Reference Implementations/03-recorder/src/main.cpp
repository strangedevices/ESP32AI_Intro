// Stage 03 - voice recorder: mics -> WAV on SD card, playback through the speaker.
// Built on the stage 02 harness (ui.h). Wi-Fi stays available for stage 04.
//
// Buttons:  [WiFi] connect / disconnect     [Rec]  start / stop recording
//           [Play] play the last recording   [Clear] clear the log
// Serial:   w, r, p, c as above; l = list recordings
//
// Audio: 16 kHz, mono, 16-bit, via M5Unified's M5.Mic and M5.Speaker. On the CoreS3 the
// mic and speaker share the I2S bus, so only one is active at a time.
#include <M5Unified.h>
#include <WiFi.h>
#include <SD.h>
#include "ui.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif

// ---- audio settings ---------------------------------------------------------
static const uint32_t SAMPLE_RATE = 16000;
static const size_t   CHUNK = 1024;             // samples per I2S chunk (64 ms)
static int16_t recBuf[2][CHUNK];
static int16_t playBuf[3][CHUNK];               // speaker queue holds 2; the third is free to fill
static const char *REC_DIR = "/recordings";

// ---- UI ------------------------------------------------------------------------
enum { BTN_WIFI, BTN_REC, BTN_PLAY, BTN_CLEAR, N_BTN };
static ui::Button buttons[N_BTN] = {
  { "WiFi",  0, 0, 0, 0, ui::COL_GREEN },
  { "Rec",   0, 0, 0, 0, ui::COL_RED },
  { "Play",  0, 0, 0, 0, ui::COL_BLUE },
  { "Clear", 0, 0, 0, 0, ui::COL_GREY },
};

// ---- state ---------------------------------------------------------------------
static bool sdOk = false;
static bool recording = false;
static File recFile;
static int  recIdx = 0;
static uint32_t recBytes = 0, recStart = 0;
static int16_t peak = 0, maxPeak = 0;
static char lastFile[48] = "";
static bool wantWifi = false;
static uint32_t connectStart = 0;
static wl_status_t lastStatus = WL_NO_SHIELD;

// ---- WAV header ------------------------------------------------------------------
static void writeWavHeader(File &f, uint32_t dataBytes) {
  uint8_t h[44];
  uint32_t byteRate = SAMPLE_RATE * 2;
  auto put32 = [&](int o, uint32_t v) { h[o]=v; h[o+1]=v>>8; h[o+2]=v>>16; h[o+3]=v>>24; };
  auto put16 = [&](int o, uint16_t v) { h[o]=v; h[o+1]=v>>8; };
  memcpy(h, "RIFF", 4); put32(4, 36 + dataBytes); memcpy(h + 8, "WAVEfmt ", 8);
  put32(16, 16); put16(20, 1); put16(22, 1); put32(24, SAMPLE_RATE); put32(28, byteRate);
  put16(32, 2); put16(34, 16); memcpy(h + 36, "data", 4); put32(40, dataBytes);
  f.seek(0);
  f.write(h, 44);
}

// ---- status bar ------------------------------------------------------------------
static void drawLevel(int16_t pk) {
  // small bar on the right of the status bar
  int w = 60, x = ui::W - w - 6, y = 8, h = ui::BAR_H - 16;
  int fill = (int)((long)w * pk / 32767);
  M5.Display.fillRect(x, y, w, h, TFT_BLACK);
  M5.Display.fillRect(x, y, fill, h, pk > 28000 ? TFT_RED : TFT_GREEN);
}

static void updateStatusBar() {
  char buf[40];
  if (recording) {
    snprintf(buf, sizeof buf, "REC %.1fs", (millis() - recStart) / 1000.0f);
    ui::status(buf, TFT_MAROON);
    drawLevel(peak);
  } else if (WiFi.status() == WL_CONNECTED) {
    snprintf(buf, sizeof buf, "%s", WiFi.localIP().toString().c_str());
    ui::status(buf, TFT_DARKGREEN);
  } else if (wantWifi) {
    ui::status("connecting...", TFT_OLIVE);
  } else {
    ui::status(sdOk ? "SD ok  ready" : "NO SD CARD", sdOk ? TFT_NAVY : TFT_RED);
  }
}

// ---- recording --------------------------------------------------------------------
static int nextFileNumber() {
  int n = 0;
  File dir = SD.open(REC_DIR);
  if (!dir) return 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    int k = 0;
    if (sscanf(f.name(), "rec_%d.wav", &k) == 1 && k >= n) n = k + 1;
    f.close();
  }
  dir.close();
  return n;
}

static void startRecording() {
  if (!sdOk) { ui::log("Rec: no SD card"); return; }
  if (!SD.exists(REC_DIR)) SD.mkdir(REC_DIR);
  snprintf(lastFile, sizeof lastFile, "%s/rec_%03d.wav", REC_DIR, nextFileNumber());
  recFile = SD.open(lastFile, FILE_WRITE);
  if (!recFile) { ui::log("Rec: cannot create %s", lastFile); return; }
  writeWavHeader(recFile, 0);
  recBytes = 0; peak = 0; maxPeak = 0; recIdx = 0;
  M5.Speaker.end();
  M5.Mic.begin();
  M5.Mic.record(recBuf[0], CHUNK, SAMPLE_RATE);
  M5.Mic.record(recBuf[1], CHUNK, SAMPLE_RATE);
  recording = true;
  recStart = millis();
  buttons[BTN_REC].label = "Stop";
  buttons[BTN_REC].draw();
  ui::log("Rec: %s", lastFile);
  updateStatusBar();
}

static void stopRecording() {
  recording = false;
  while (M5.Mic.isRecording()) delay(1);
  M5.Mic.end();
  writeWavHeader(recFile, recBytes);
  recFile.close();
  buttons[BTN_REC].label = "Rec";
  buttons[BTN_REC].draw();
  ui::log("Rec: saved %lu KB, %.1f s, peak %d%%", recBytes / 1024,
          recBytes / (float)(SAMPLE_RATE * 2), maxPeak * 100 / 32767);
  updateStatusBar();
}

// Called every loop while recording: when the oldest chunk is done, write it and requeue.
static void pumpRecording() {
  if (M5.Mic.isRecording() < 2) {
    int16_t *b = recBuf[recIdx];
    int16_t pk = 0;
    for (size_t i = 0; i < CHUNK; i++) { int16_t v = b[i] < 0 ? -b[i] : b[i]; if (v > pk) pk = v; }
    peak = pk;
    if (pk > maxPeak) maxPeak = pk;
    recFile.write((uint8_t *)b, CHUNK * 2);
    recBytes += CHUNK * 2;
    M5.Mic.record(b, CHUNK, SAMPLE_RATE);
    recIdx ^= 1;
  }
  static uint32_t lastBar = 0;
  if (millis() - lastBar > 200) { lastBar = millis(); updateStatusBar(); }
}

// ---- playback ---------------------------------------------------------------------
static void playLast() {
  if (!sdOk) { ui::log("Play: no SD card"); return; }
  if (!lastFile[0]) { ui::log("Play: nothing recorded yet"); return; }
  File f = SD.open(lastFile, FILE_READ);
  if (!f) { ui::log("Play: cannot open %s", lastFile); return; }
  f.seek(44);
  ui::log("Play: %s", lastFile);
  ui::status("PLAYING", ui::COL_PURPLE);
  M5.Mic.end();
  M5.Speaker.begin();
  M5.Speaker.setVolume(128);
  int idx = 0;
  while (f.available()) {
    // playRaw does not copy: a buffer must not be refilled while it is playing or
    // queued. Wait for a free queue slot first, then fill a buffer that is not in use.
    while (M5.Speaker.isPlaying(0) >= 2) delay(1);
    size_t n = f.read((uint8_t *)playBuf[idx], CHUNK * 2) / 2;
    if (n == 0) break;
    M5.Speaker.playRaw(playBuf[idx], n, SAMPLE_RATE, false, 1, 0);
    idx = (idx + 1) % 3;
    M5.update();
  }
  while (M5.Speaker.isPlaying(0)) delay(1);
  f.close();
  M5.Speaker.end();
  ui::log("Play: done");
  updateStatusBar();
}

static void listRecordings() {
  File dir = SD.open(REC_DIR);
  if (!dir) { ui::log("List: no %s folder", REC_DIR); return; }
  int n = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    ui::log("  %s  %lu KB", f.name(), (unsigned long)(f.size() / 1024));
    n++; f.close();
  }
  dir.close();
  ui::log("List: %d file(s)", n);
}

// ---- Wi-Fi (same as stage 02) ---------------------------------------------------------
static void wifiToggle() {
  if (wantWifi) {
    wantWifi = false; WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
    ui::log("WiFi: disconnected");
  } else {
    if (strlen(WIFI_SSID) == 0) { ui::log("No credentials: create include/secrets.h"); return; }
    wantWifi = true; connectStart = millis();
    WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASS);
    ui::log("WiFi: connecting");
  }
  updateStatusBar();
}

// ---- setup / loop -----------------------------------------------------------------------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  ui::begin();
  ui::layoutButtons(buttons, N_BTN);

  SPI.begin(GPIO_NUM_36, GPIO_NUM_35, GPIO_NUM_37, GPIO_NUM_4);
  sdOk = SD.begin(GPIO_NUM_4, SPI, 25000000);
  updateStatusBar();
  ui::log("Stage 03 recorder ready");
  if (sdOk) {
    ui::log("SD: %llu MB card", SD.cardSize() / (1024ULL * 1024ULL));
    if (!SD.exists(REC_DIR)) SD.mkdir(REC_DIR);
    int n = nextFileNumber();
    if (n > 0) snprintf(lastFile, sizeof lastFile, "%s/rec_%03d.wav", REC_DIR, n - 1);
    ui::log("SD: %d recording(s)%s%s", n, n ? ", last " : "", n ? lastFile : "");
  } else {
    ui::log("SD: no card found - insert one and reset");
  }
}

void loop() {
  M5.update();
  int b = ui::tappedButton(buttons, N_BTN);
  char c = Serial.available() ? Serial.read() : 0;
  if (b >= 0) { buttons[b].draw(true); delay(60); buttons[b].draw(false); }

  if (b == BTN_REC || c == 'r') { if (recording) stopRecording(); else startRecording(); }
  else if (!recording) {
    if (b == BTN_PLAY  || c == 'p') playLast();
    if (b == BTN_CLEAR || c == 'c') ui::logClear();
    if (b == BTN_WIFI  || c == 'w') wifiToggle();
    if (c == 'l') listRecordings();
  }

  if (recording) { pumpRecording(); return; }

  wl_status_t st = WiFi.status();
  if (st != lastStatus) {
    lastStatus = st;
    if (st == WL_CONNECTED)
      ui::log("WiFi: connected in %lu ms, IP %s", millis() - connectStart, WiFi.localIP().toString().c_str());
    updateStatusBar();
  }
  if (wantWifi && st != WL_CONNECTED && millis() - connectStart > 10000) {
    ui::log("WiFi: retrying");
    WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASS); connectStart = millis();
  }
  delay(5);
}
