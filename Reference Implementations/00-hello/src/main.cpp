// Stage 00 - "Hello" environment check for M5Stack CoreS3 / CoreS3 SE.
// Shows a message on the LCD, reports PSRAM and SD card status, counts seconds.
#include <M5Unified.h>
#include <SD.h>

static bool sdOk = false;

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  M5.Display.setRotation(1);
  M5.Display.setTextSize(2);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(8, 8);
  M5.Display.println("ESP32 AI Workshop");
  M5.Display.println("Stage 00: hello");

  // SD card on CoreS3: SPI, CS = GPIO4
  sdOk = SD.begin(GPIO_NUM_4, SPI, 25000000);

  unsigned psramKb = (unsigned)(ESP.getPsramSize() / 1024);
  unsigned flashMb = (unsigned)(ESP.getFlashChipSize() / (1024 * 1024));
  M5.Display.printf("PSRAM: %u KB\n", psramKb);
  M5.Display.printf("Flash: %u MB\n", flashMb);
  M5.Display.printf("SD card: %s\n", sdOk ? "OK" : "not found");
  Serial.printf("PSRAM %u KB, flash %u MB, SD %s\n", psramKb, flashMb,
                sdOk ? "OK" : "missing");
}

void loop() {
  M5.update();
  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();
    M5.Display.fillRect(8, 140, 300, 30, TFT_BLACK);
    M5.Display.setCursor(8, 140);
    M5.Display.printf("Uptime: %lu s", millis() / 1000);
    Serial.printf("uptime %lu s\n", millis() / 1000);
  }
  if (M5.Touch.getCount() > 0) {
    M5.Display.setCursor(8, 180);
    M5.Display.println("Touch OK  ");
  }
}
