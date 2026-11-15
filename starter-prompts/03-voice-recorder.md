# Starter prompt - stage 03 (voice recorder)

```
Build stage 03 of this workshop as a new PlatformIO project in my-work/03-recorder,
starting from a copy of my stage 02 project in my-work/02-harness. Read CLAUDE.md and
stages/03-voice-recorder/README.md first. Do not open or copy anything from
Reference Implementations/03-recorder or later: I want to build this myself.

What I want on the device, keeping the WiFi and Clear buttons:
- A "Rec" button that starts recording from the microphones to a new WAV file in a
  recordings folder on the SD card (16 kHz, mono, 16-bit). The same button, now labelled
  "Stop", ends the recording. While recording, show a timer and a live level meter in the
  status bar. When it stops, log the file name, the length in seconds and the peak level.
- A "Play" button that plays the most recent recording through the speaker.
- Serial keys: r for record/stop, p for play, l to list the recordings with their sizes.

Build it, flash it, and test it over the serial port: record for about four seconds, stop,
play it back, list the files. Check that the file size matches the recording length. Tell
me what you verified and what I need to check by ear.
```
