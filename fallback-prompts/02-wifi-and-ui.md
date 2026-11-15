# Fallback prompt - stage 02 (Wi-Fi and UI)

Run `claude` inside `Reference Implementations/02-harness` and paste:

```
This PlatformIO project is a touch UI harness for an M5Stack CoreS3 (ESP32-S3, Arduino,
M5Unified). secrets.h in the repo's top folder holds my credentials (do not read, print or edit it). Please:
1. Run `pio run` and fix any build error without touching secrets.h in the repo's top folder.
2. Flash with `pio run -t upload` and watch `pio device monitor` for 20 seconds.
3. Send the character `w` to the serial port to connect Wi-Fi, wait for "WiFi: connected",
   then send `t` and tell me the HTTP status the Test reports. Any HTTP status means success.
If Wi-Fi fails, list the likely causes (password, 2.4 GHz, captive portal) and stop.
```

To extend it:

```
Add a fourth button labelled "Time" to the harness in src/main.cpp. When tapped and Wi-Fi
is connected, sync the clock from pool.ntp.org (Australia/Sydney timezone) and log the
current date and time. Keep the existing buttons working and keep the change small.
```
