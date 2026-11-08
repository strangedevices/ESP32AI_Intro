# Fallback prompt - stage 03 (voice recorder)

Run `claude` inside `Reference Implementations/03-recorder` and paste:

```
This PlatformIO project records audio from an M5Stack CoreS3's microphones to a WAV file
on the SD card and plays it back (M5Unified, Arduino). Please:
1. Run `pio run` and fix any build error.
2. Flash with `pio run -t upload`, then open the serial port at 115200.
3. Send `r`, wait 4 seconds, send `r` again, then send `p`, then `l`. Report the "Rec: saved"
   line (length and peak level) and the list of files.
If the peak level is 0%, investigate why the microphone is not capturing before anything else.
```

To extend it:

```
Add a "Prev" button to the harness in src/main.cpp that plays the recording before the
last one. Keep the existing buttons working, reuse the ui.h button layout, and keep the
change small. Rebuild and flash it.
```
