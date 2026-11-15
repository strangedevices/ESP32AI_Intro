# Starter prompt - stage 02 (Wi-Fi and touch UI)

```
Build stage 02 of this workshop for my M5Stack CoreS3 as a new PlatformIO project in
my-work/02-harness. Read CLAUDE.md and stages/02-wifi-and-ui/README.md first. Use
Reference Implementations/00-hello as the example of a working project for this board, but
do not open or copy anything from Reference Implementations/02-harness or later: I want to
build this myself.

What I want on the device:
- A status bar at the top, a scrolling log in the middle, and a row of touch buttons at
  the bottom, in portrait with the USB port at the bottom.
- A "WiFi" button that connects to and disconnects from my network. The name and password
  are WIFI_SSID and WIFI_PASS in secrets.h in the top folder of the repo. Never read,
  print or change that file. Show the IP address in the status bar when connected.
- A "Test" button that makes an HTTPS GET to https://api.elevenlabs.io/v1/models and logs
  the HTTP status and how long it took.
- A "Clear" button that clears the log.
- Serial keys w, t and c that do the same as the three buttons, and every log line also
  printed to serial, so it can be tested without touching the screen.

Build it, flash it to the device, and test it over the serial port: boot, connect Wi-Fi,
run the test, disconnect. Tell me what you verified and what I need to check by eye.
```
