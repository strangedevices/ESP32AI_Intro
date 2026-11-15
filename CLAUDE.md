# Workshop participant repo - guidance for Claude Code

This is a PlatformIO project for the M5Stack CoreS3 / CoreS3 SE (ESP32-S3).
- Framework: Arduino. Libraries: M5Unified (display, touch, mic, speaker, SD, RTC) and M5GFX.
- Board config lives in platformio.ini; build with `pio run`, flash with `pio run -t upload`,
  serial with `pio device monitor -b 115200`.
- Credentials (Wi-Fi, ElevenLabs API key and agent id) live in ONE gitignored file shared by
  every project: `secrets.h` in the repo's top folder, created from `secrets.h.example`.
  Every project sits two folders down (`Reference Implementations/NN-name` or
  `my-work/NN-name`) and has `build_flags = -I../..` so `#include "secrets.h"` finds it.
  Never commit it, never print its contents, and do not copy keys into source files.
- Prefer M5.Mic / M5.Speaker from M5Unified over raw I2S for audio. Mic and speaker share
  one I2S bus on the CoreS3: end one before beginning the other.
- `M5.Speaker.playRaw()` does not copy the samples. Its queue holds two chunks, so stream
  with three buffers and wait for `isPlaying(ch) < 2` before refilling one, or the audio
  crackles. See `03-recorder/src/main.cpp` playLast().
- ElevenLabs agent WebSocket (stage 04): do not use the links2004 WebSockets library, it
  rejects messages over 15 KB and agent audio messages are hundreds of KB. Use
  `04-assistant/src/ws_client.h`. Answer every `ping` with a `pong`. Audio is base64 PCM at
  the rate named in the `conversation_initiation_metadata` event (16 kHz by default).
- Credits: an open ElevenLabs conversation costs about 1000 credits a minute. Never start one
  to test unless the user asks. Keep the guards (MAX_SESSION_S, IDLE_END_S). The serial key
  `t` toggles a conversation: read the log to know the state before sending it. Stage 05 has
  serial keys r / s / d that exercise the tools without a conversation.
- Agent client tools (stage 05): the agent sends `client_tool_call`; reply with
  `client_tool_result` carrying the same tool_call_id, a text result and is_error. Each tool
  must also be defined on the agent in the ElevenLabs dashboard with the same name.
- Colours: pure TFT_BLUE / TFT_DARKGREEN are nearly black on this panel. Use the `ui::COL_*`
  constants in ui.h. Button labels use the 12 px DejaVu font; status bar uses Font0 size 2.
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
