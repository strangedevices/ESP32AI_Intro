# AI-Powered ESP32 Development with PlatformIO

Participant materials for the Web Directions AI Engineer Sydney workshop (9 Dec 2026).

You will connect Claude Code to an M5Stack CoreS3 / CoreS3 SE and build, from a first
upload, a personal voice recorder and voice assistant.

## Before the workshop (do this at home, on good Wi-Fi)
See `stages/00-setup/README.md`. The PlatformIO toolchain download is large; doing it
at the venue will cost you most of the first hour.

## Stages
| # | Stage | What you end up with |
|---|---|---|
| 00 | Setup | Device booted, SD card seen, repo cloned, toolchain installed, hotspot working, ElevenLabs key ready |
| 01 | First upload | A local AI story generator running on your device |
| 02 | Wi-Fi and UI | A touch UI harness that connects and disconnects from Wi-Fi |
| 03 | Voice recorder | Record from the mics to SD and play back |
| 04 | Voice assistant | Push-to-talk conversation with an ElevenLabs agent |
| 05 | Always listening | Activity-triggered conversation and tool calls |
| 06 | Stretch | Ideas to take home |

Each stage folder has a README with the goal and setup. Try each stage yourself first in
`my-work/`, using `starter-prompts/` or your own words. If you get stuck, `fallback-prompts/`
and `Reference Implementations/` hold a working build of every stage.

## Layout
- `stages/` - one folder per stage, in order: what to do and what "done" looks like.
- `my-work/` - where you build each stage yourself.
- `starter-prompts/` - prompts to start building a stage yourself.
- `fallback-prompts/` - prompts to get the reference implementation running if you are stuck.
- `Reference Implementations/` - complete PlatformIO projects for each stage.
- `tools/` - environment-check scripts.
- `presentation/` - the slides used in the workshop. Open `presentation/index.html` in a browser.
- `secrets.h.example` - copy to `secrets.h` (never committed) for Wi-Fi and ElevenLabs details.
