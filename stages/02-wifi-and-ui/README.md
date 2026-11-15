# Stage 02 - Wi-Fi and the touch UI harness

## Goal
A small touch UI you will keep building on: a status bar, a scrolling log, and buttons.
Connect and disconnect from your phone hotspot and prove the device can reach the
internet over TLS. Everything in later stages depends on this working.

## Setup
1. Turn on your phone hotspot: 2.4 GHz, WPA2, a simple password. Connect your laptop
   to it too.
2. In `Reference Implementations`, copy `secrets.h.example` to `secrets.h` (same folder)
   and put your hotspot name and password in it. Every stage reads this one file. It is
   gitignored; never commit it, paste it into a chat, or show it on a shared screen.
3. Build and flash:

```bash
cd "Reference Implementations/02-harness"
```
```bash
pio run -t upload
```
```bash
pio device monitor
```

Note: this stage uses the standard 16 MB partition layout because Wi-Fi and TLS make the
app too big for the story app's 1.5 MB slot. Flashing it removes the story model's
partition. To go back to stage 01 later, run its `pio run -t upload_model` again first.

## Try it
- Tap **WiFi**. The bar turns green with the IP address and signal strength. Tap again
  to disconnect.
- Tap **Test**. It makes an HTTPS request to the ElevenLabs API without a key. Any HTTP
  status (404 or 401, typically under a second) means TLS and the internet work. "FAILED"
  means the device has no route to the internet.
- Tap **Clear** to clear the log.
- Serial shortcuts: `w`, `t`, `c` do the same.

## Now change it with Claude Code
Open `claude` in the `02-harness` folder. Ideas:
- Show the Wi-Fi network name in the status bar as well as the IP.
- Add a fourth button that fetches the current time from an NTP server and logs it.
- Make the log text bigger, or colour error lines red.
- Reconnect automatically if the connection drops.

Look at `src/ui.h` first: the status bar, log canvas and buttons live there and later
stages reuse them.

## Done when
You can connect, see the IP in the bar, get HTTP 401 from Test, disconnect, and you have
made one change of your own.

## Stuck?
- "WiFi: failed": wrong password, 5 GHz-only hotspot, or a captive portal network.
- Test times out: the hotspot has no data, or your phone dropped the hotspot.
- See `fallback-prompts/02-wifi-and-ui.md`.
