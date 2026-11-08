# Workshop participant repo - guidance for Claude Code

This is a PlatformIO project for the M5Stack CoreS3 / CoreS3 SE (ESP32-S3).
- Framework: Arduino. Libraries: M5Unified (display, touch, mic, speaker, SD, RTC) and M5GFX.
- Board config lives in platformio.ini; build with `pio run`, flash with `pio run -t upload`,
  serial with `pio device monitor -b 115200`.
- Wi-Fi and API credentials go in include/secrets.h (gitignored). Never commit keys.
- Prefer M5.Mic / M5.Speaker from M5Unified over raw I2S for audio.
- Screen orientation: `M5.Display.setRotation(1)` gives portrait with the USB-C port at the
  bottom and the two microphones on the right edge. Keep that in every stage.
- Scrolling text (logs, transcripts, stories): draw into an `M5Canvas` sprite in PSRAM with
  `setTextScroll(true)` and `setScrollRect`, then `pushSprite`. Printing straight to
  `M5.Display` wraps back to the top when it reaches the bottom. Keep fixed elements
  (status bar, buttons) on the display itself. See `Reference Implementations/01-story-app`.
- Keep each stage buildable on its own. Do not pull in ESP-IDF-only components.
- Partitions: 01-story-app uses a custom table with a 1.5 MB app and a 15 MB model
  partition. Stages 02+ use `default_16MB.csv` (6.5 MB app) because Wi-Fi + TLS alone is
  about 1.5 MB. Do not try to fit Wi-Fi stages into the story app's table.
- Stage 02's `src/ui.h` (status bar, scrolling log, button row) is the base for later
  stages; copy it forward rather than reinventing it.
- The user is learning; explain what you change in one or two lines and keep diffs small.
