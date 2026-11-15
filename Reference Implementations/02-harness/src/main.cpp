// Stage 02 - the workshop harness: status bar, scrolling log, touch buttons, Wi-Fi.
//
// Buttons:  [WiFi]  connect / disconnect using secrets.h
//           [Test]  HTTPS GET to api.elevenlabs.io to prove internet + TLS work
//           [Clear] clear the log
//
// Later stages add buttons to this harness (record, play, talk).
#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "ui.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "secrets.h not found - copy secrets.h.example to secrets.h in the repo top folder"
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif

enum { BTN_WIFI, BTN_TEST, BTN_CLEAR, N_BTN };
static ui::Button buttons[N_BTN] = {
  { "WiFi",  0, 0, 0, 0, ui::COL_GREEN },
  { "Test",  0, 0, 0, 0, ui::COL_BLUE },
  { "Clear", 0, 0, 0, 0, ui::COL_GREY },
};

static bool wantWifi = false;
static wl_status_t lastStatus = WL_NO_SHIELD;
static uint32_t connectStart = 0;

static void updateStatusBar() {
  char buf[48];
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(buf, sizeof buf, "%s %ddBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    ui::status(buf, TFT_DARKGREEN);
  } else if (wantWifi) {
    ui::status("connecting...", TFT_OLIVE);
  } else {
    ui::status("WiFi off  tap WiFi", TFT_NAVY);
  }
}

static void wifiToggle() {
  if (wantWifi) {
    wantWifi = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    ui::log("WiFi: disconnected");
  } else {
    if (strlen(WIFI_SSID) == 0) {
      ui::log("No credentials: create secrets.h (repo top folder)");
      return;
    }
    wantWifi = true;
    connectStart = millis();
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    ui::log("WiFi: connecting to %s", WIFI_SSID);
  }
  updateStatusBar();
}

static void internetTest() {
  if (WiFi.status() != WL_CONNECTED) { ui::log("Test: not connected"); return; }
  ui::log("Test: HTTPS GET api.elevenlabs.io ...");
  WiFiClientSecure client;
  client.setInsecure();          // workshop only: skip certificate check
  HTTPClient http;
  http.setTimeout(8000);
  uint32_t t0 = millis();
  if (!http.begin(client, "https://api.elevenlabs.io/v1/models")) {
    ui::log("Test: begin failed"); return;
  }
  int code = http.GET();
  if (code > 0) ui::log("Test: HTTP %d in %lu ms - TLS + internet OK", code, millis() - t0);
  else          ui::log("Test: FAILED (%d) after %lu ms - no internet or DNS?", code, millis() - t0);
  http.end();
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  ui::begin();
  ui::layoutButtons(buttons, N_BTN);
  updateStatusBar();
  ui::log("Stage 02 harness ready");
  ui::log("PSRAM %u KB, free heap %u KB", (unsigned)(ESP.getPsramSize() / 1024),
          (unsigned)(ESP.getFreeHeap() / 1024));
}

void loop() {
  M5.update();
  int b = ui::tappedButton(buttons, N_BTN);
  if (b >= 0) {
    buttons[b].draw(true); delay(80); buttons[b].draw(false);
    if (b == BTN_WIFI)  wifiToggle();
    if (b == BTN_TEST)  internetTest();
    if (b == BTN_CLEAR) ui::logClear();
  }

  // Track Wi-Fi state changes.
  wl_status_t st = WiFi.status();
  if (st != lastStatus) {
    lastStatus = st;
    if (st == WL_CONNECTED) {
      ui::log("WiFi: connected in %lu ms, IP %s", millis() - connectStart,
              WiFi.localIP().toString().c_str());
    } else if (wantWifi && (st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL)) {
      ui::log("WiFi: failed (%d) - check SSID/password, 2.4 GHz", (int)st);
    }
    updateStatusBar();
  }
  // Not connected after 10 s? Try again. Hotspots often refuse the first attempt
  // right after a reset.
  if (wantWifi && st != WL_CONNECTED && millis() - connectStart > 10000) {
    ui::log("WiFi: no connection after 10 s, retrying");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    connectStart = millis();
  }
  // Refresh RSSI in the bar every few seconds while connected.
  static uint32_t lastBar = 0;
  if (st == WL_CONNECTED && millis() - lastBar > 5000) { lastBar = millis(); updateStatusBar(); }

  // Serial shortcuts for testing without touching: w = wifi toggle, t = test, c = clear
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'w') wifiToggle();
    if (c == 't') internetTest();
    if (c == 'c') ui::logClear();
  }
  delay(10);
}
