# Stage 03 - Voice recorder

## Goal
Record from the CoreS3's microphones to a WAV file on the SD card, with a live level
meter, and play the recording back through the speaker. This is the audio plumbing the
voice assistant in stage 04 is built on.

## Setup
1. Make sure the microSD card is in (stage 00). Recordings go in a `recordings` folder.
2. Nothing to copy: this stage reads the same `Reference Implementations/secrets.h` you
   made in stage 02 (Wi-Fi is optional here, but stage 04 needs it).
3. Build and flash:

```bash
cd "Reference Implementations/03-recorder"
```
```bash
pio run -t upload
```
```bash
pio device monitor
```

## Try it
- Tap **Rec**, talk, tap **Stop**. The bar turns red with a timer and a level meter while
  recording. The log shows the file name, length and peak level.
- Tap **Play** to hear the last recording.
- Serial shortcuts: `r` record/stop, `p` play, `l` list recordings, `w` Wi-Fi, `c` clear.
- Take the card out and open the WAV files on your laptop if you want to check them.

## Now change it with Claude Code
Ideas, roughly in order of difficulty:
- Show the recording count in the status bar.
- Add a "Prev" button that plays the previous recording, not just the last one.
- Change the sample rate to 8 kHz and listen to the difference.
- Draw a waveform of the last recording in the log area.
- Add a hold-to-record mode: record while the Rec button is held down.

## How it works
`M5.Mic.record()` is asynchronous and queues up to two buffers. The loop waits until the
older buffer is finished, writes it to the SD card, and queues it again, so capture never
stops while the card is being written. Playback does the same with `M5.Speaker.playRaw()`.
The mic and speaker share one I2S bus on the CoreS3, so the code ends one before beginning
the other. The WAV header is written with a size of zero at the start and patched when
recording stops.

## Done when
You have a recording on the card, it plays back, and you have made one change of your own.

## Stuck?
- "NO SD CARD": reseat the card, make sure it is FAT32, and reset the device.
- Peak 0%: the mics are not capturing; check nothing else initialised the speaker.
- See `fallback-prompts/03-voice-recorder.md`.
