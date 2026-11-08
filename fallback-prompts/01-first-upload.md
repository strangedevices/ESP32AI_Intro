# Fallback prompt - stage 01 (first upload)

Use this if the build or upload is not working and you want Claude Code to drive. Run
`claude` inside `Reference Implementations/01-story-app` and paste:

```
This is a PlatformIO project for an M5Stack CoreS3 (ESP32-S3) that runs a small language
model from a custom flash partition. Please:
1. Run `pio run` and fix any build error you find without changing the inference code in llm.h.
2. Run `pio device list` to find my board, then `pio run -t upload_model` (flashes model/model.bin
   to the model partition, takes about 2 minutes) and `pio run -t upload`.
3. Run `pio device monitor` for 40 seconds and show me the first story it prints.
If the monitor shows "model partition not found" or "bad model magic", run the upload_model
target again. Do not edit platformio.ini or partitions.csv.
```

Then, to make your first change:

```
In src/main.cpp, make the status bar show the number of tokens generated so far next to the
tokens-per-second figure. Keep the change small, rebuild with `pio run`, and flash it with
`pio run -t upload`.
```
