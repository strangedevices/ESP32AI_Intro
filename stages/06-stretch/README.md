# Stage 06 - Ideas to take home

Nothing to flash here. These are directions that will not fit in the workshop but are a
natural next step with the same device and the same way of working.

## Extend what you built
- **More client tools.** Screen colours, volume, playing back a recording, showing text.
  Each one is a few lines in `runTool()` plus a tool definition on your agent.
- **Barge-in.** Let a tap on the screen stop the agent mid-sentence and start listening.
- **Transcripts to SD.** Save each conversation as text next to its recording.
- **A better wake.** Replace the loudness check with a double clap, or with a rhythm check
  that tells speech from a door slam.
- **Wi-Fi setup on the device.** An on-screen keyboard or a captive portal, so the network
  is not compiled into the firmware.

## Harder, and worth knowing about
- **Local wake word.** Espressif's ESP-SR (WakeNet and MultiNet) can spot a wake word and
  a small set of commands on the device with no network at all. It needs the ESP-IDF
  toolchain and model partitions, which is why it is not a workshop stage.
- **Microphone focus.** The CoreS3 has two microphones. Combining them (beamforming) can
  favour sound from one direction. This is signal-processing work rather than plumbing.
- **Run more locally.** Stage 01 showed a language model running on the chip. Small speech
  models are heading the same way. What would a fully offline assistant need?

## Keep the habits
- Describe the outcome you want and how to test it, then let the assistant build and test.
- Keep a `CLAUDE.md` of what you learn about your hardware. It is what turned the
  wrinkles in this workshop into one-line fixes.
- Verify on the real device. A build that compiles has proved very little.
