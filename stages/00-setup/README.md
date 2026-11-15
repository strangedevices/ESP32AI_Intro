# Stage 00 - Setup

Do the "At home" part before the workshop, on good Wi-Fi. It downloads several gigabytes
of tools (the PlatformIO folder ends up around 6 GB). Doing it at the venue over a phone
hotspot is not realistic.

## Goal
Your laptop can build and flash firmware to an M5Stack CoreS3 / CoreS3 SE, Claude Code
runs, and you have the accounts you need.

## At home: install (Windows)

Open **PowerShell** (not Command Prompt) and run each line. Say yes to any prompts.

```powershell
winget install --id Git.Git -e
```
```powershell
winget install --id Microsoft.VisualStudioCode -e
```
```powershell
winget install --id GitHub.cli -e
```
```powershell
winget install --id Anthropic.ClaudeCode -e
```

Then **close and reopen PowerShell** so the new tools are on your PATH.

Install the PlatformIO IDE extension inside VS Code (Extensions sidebar, search
"PlatformIO IDE", install, then restart VS Code when it asks). This also installs
PlatformIO Core in your user folder. Add it to your PATH so `pio` works in a terminal:

```powershell
[Environment]::SetEnvironmentVariable('Path', [Environment]::GetEnvironmentVariable('Path','User') + ";$env:USERPROFILE\.platformio\penv\Scripts", 'User')
```

Notes for Windows:
- Git for Windows is required by Claude Code (it needs Git Bash). Keep the default
  install options.
- Windows 11 has the USB serial driver for the CoreS3 built in. Windows 10 may need the
  CH9102 driver from M5Stack: https://docs.m5stack.com/en/download
- If `pio` says "command not found" after the PATH step, reopen the terminal.
- Antivirus can slow the first build a lot. Let it finish; it is a one-off.

## At home: install (macOS)

Install Homebrew if you do not have it (https://brew.sh), then:

```bash
brew install git gh node
```
```bash
brew install --cask visual-studio-code
```
```bash
npm install -g @anthropic-ai/claude-code
```

Install the PlatformIO IDE extension inside VS Code (as above). Then add PlatformIO to
your PATH by adding this line to `~/.zshrc`:

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
```

Apple Silicon Macs need no serial driver. Intel Macs on older macOS may need the CH9102
driver from the M5Stack download page.

## At home: accounts
- **Claude**: Claude Code needs a Claude Pro (or higher) subscription, or an API key.
  Sign in once by running `claude` in a terminal and following the browser prompt.
- **GitHub**: a free account. Run `gh auth login` and follow the prompts.
- **ElevenLabs**: a free account at https://elevenlabs.io. Do NOT spend the free credits
  before the workshop, or create a second "burner" account for the day. Workshop API
  keys will be supplied on the day if you need them.

## At home: clone and build once

```bash
git clone https://github.com/strangedevices/ESP32-AI-Workshop-Participant.git
```
```bash
cd "ESP32-AI-Workshop-Participant/Reference Implementations/00-hello"
```
```bash
pio run
```

The first `pio run` downloads the ESP32 platform, toolchains and libraries and takes
5-15 minutes on a fast connection (about 5 minutes on a 100 Mbit link). Later builds take
about a minute. It should end with `SUCCESS`.

Then run the environment check:

- Windows: `tools\envcheck.ps1`
- macOS: `tools/envcheck.sh`

Everything should say OK.

## Connecting the module
- Use a good-quality, full-featured USB-C **data** cable (not a charge-only cable) plugged
  **directly into a port on your laptop**, not through a hub or dock. Hubs and cheap cables
  are the most common cause of "no serial port" and failed uploads.
- Bring a USB-C to USB-C cable and a USB-A to USB-C cable so you have whichever your
  laptop needs.
- When the module is not plugged into your laptop, power it from a USB-C power bank. The
  CoreS3 SE has no battery.

## On the day
1. Plug in the CoreS3 with the data cable, directly into your laptop. Run the factory demo, check the
   screen, speaker and touch work.
2. Insert the microSD card (FAT32). The factory demo should show it. **This matters:**
   later stages record to it.
3. Turn on your phone hotspot (2.4 GHz, WPA2, simple password with no spaces). Connect
   your laptop to it as well. Hotel Wi-Fi will not work for the device.
4. In the `00-hello` folder, run `pio run -t upload` then `pio device monitor`. You should
   see the hello screen on the device and an uptime counter in the terminal. The device
   appears as "USB Serial Device (COMx)" on Windows and `/dev/cu.usbmodem*` on a Mac;
   PlatformIO finds it by itself. Uploading takes about 30 seconds.
   The screen should show PSRAM 8192 KB, flash 16 MB, and SD card OK once a card is in.
5. Have your ElevenLabs API key to hand. It goes in `Reference Implementations/secrets.h`
   (copy `secrets.h.example`), along with your hotspot name and password. That one file
   is used by every stage and is never committed.

If the upload fails with "no serial port": hold the reset button on the CoreS3 for about
three seconds until the internal LED turns green (download mode), then retry.

## Done when
`00-hello` is running on your device, `claude` starts, and the envcheck script is all OK.

## Stuck?
See `fallback-prompts/00-setup.md`.
