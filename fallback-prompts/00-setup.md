# Fallback prompt - stage 00 (setup)

Use this if your environment is not working. Run `claude` in the top folder of the repo
and paste:

```
I am setting up for an ESP32 workshop on this laptop and something is not working. Please:
1. Run the environment check for my operating system (tools/envcheck.ps1 on Windows,
   bash tools/envcheck.sh on macOS or Linux) and show me the output.
2. For each MISSING line, tell me the exact command to fix it and ask before running
   anything that installs software.
3. Then build Reference Implementations/00-hello with `pio run`. If PlatformIO is installed
   but `pio` is not found, look for it in the .platformio/penv folder in my home folder.
4. If a board is plugged in, run `pio device list`, flash with `pio run -t upload` and show
   me ten seconds of serial output.
Stop and explain if a download looks like it will take more than a few minutes.
```

Common causes, in order of how often they bite:
- The terminal was open before the tools were installed. Close it and open a new one.
- A charge-only USB cable, or a hub or dock between the laptop and the device.
- A serial monitor still open in another window, which blocks uploads.
- The repo cloned into a OneDrive or iCloud synced folder, which makes builds slow and
  flaky. Move it somewhere plain, like `C:\dev` or `~/dev`.
