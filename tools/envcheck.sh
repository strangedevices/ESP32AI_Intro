#!/usr/bin/env bash
# Environment check for the ESP32 AI Workshop (macOS / Linux).
# Run:  bash tools/envcheck.sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
ok=1
check() {
  if command -v "$2" >/dev/null 2>&1; then
    printf 'OK      %-14s %s\n' "$1" "$("$2" --version 2>&1 | head -1)"
  else
    printf 'MISSING %-14s (%s not found)\n' "$1" "$2"; ok=0
  fi
}
echo "ESP32 AI Workshop - environment check"; echo
check Git git
check "GitHub CLI" gh
check "VS Code" code
check "Claude Code" claude
check PlatformIO pio
if [ -d "$HOME/.platformio/platforms/espressif32" ] && ls -d "$HOME"/.platformio/packages/toolchain-xtensa-esp* >/dev/null 2>&1; then
  echo "OK      ESP32 toolchain downloaded"
else
  echo "MISSING ESP32 toolchain  (run 'pio run' in 'Reference Implementations/00-hello')"; ok=0
fi
ports=$(ls /dev/cu.usbmodem* /dev/cu.wchusbserial* /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | tr '\n' ' ')
if [ -n "$ports" ]; then echo "OK      Serial ports   $ports"; else echo "INFO    Serial ports   none (plug in the CoreS3 to check)"; fi
[ -d "$HOME/.claude" ] && echo "OK      Claude config  found" || echo "INFO    Claude config  not found (run 'claude' once and sign in)"
echo
[ $ok = 1 ] && echo "All required tools present." || echo "Some tools are missing. See stages/00-setup/README.md"
