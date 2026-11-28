#!/usr/bin/env bash
# Environment check for the ESP32 AI Workshop (macOS / Linux).
# Run:  bash tools/envcheck.sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
ok=1
check() {
  if command -v "$2" >/dev/null 2>&1; then
    # A tool can be on PATH but not run (for example an old Intel or Python 2
    # PlatformIO install on Apple Silicon), so check the exit status too.
    if v=$("$2" --version 2>&1); then
      printf 'OK      %-14s %s\n' "$1" "$(echo "$v" | head -1)"
    else
      printf 'BROKEN  %-14s %s\n' "$1" "$(echo "$v" | tail -1)"; ok=0
      [ "$2" = pio ] && echo "        fix: rm -rf ~/.platformio/penv, then reopen VS Code so PlatformIO reinstalls it"
    fi
  else
    printf 'MISSING %-14s (%s not found)\n' "$1" "$2"; ok=0
  fi
}
echo "ESP32 AI Workshop - environment check"; echo
check Git git
check "GitHub CLI" gh
check "VS Code" code
# Other tools (Cursor, coder) also install a 'code' command.
if command -v code >/dev/null 2>&1 && ! code --version 2>/dev/null | head -1 | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
  echo "WARN    VS Code        'code' on PATH is not VS Code ($(command -v code))"
fi
check "Claude Code" claude
check PlatformIO pio
if [ -d "$HOME/.platformio/platforms/espressif32" ] && ls -d "$HOME"/.platformio/packages/toolchain-xtensa-esp* >/dev/null 2>&1; then
  echo "OK      ESP32 toolchain downloaded"
else
  echo "MISSING ESP32 toolchain  (run 'pio run' in 'Reference Implementations/00-hello')"; ok=0
fi
# Sync folders lock files mid-build and cause random failures.
repo=$(cd "$(dirname "$0")/.." && pwd)
case "$repo" in
  *"Mobile Documents"*|*CloudStorage*|*Dropbox*|*OneDrive*)
    echo "WARN    Repo folder    $repo is in a synced folder; clone it somewhere like ~/dev instead" ;;
esac
ports=$(ls /dev/cu.usbmodem* /dev/cu.wchusbserial* /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | tr '\n' ' ')
if [ -n "$ports" ]; then echo "OK      Serial ports   $ports"; else echo "INFO    Serial ports   none (plug in the CoreS3 to check)"; fi
[ -d "$HOME/.claude" ] && echo "OK      Claude config  found" || echo "INFO    Claude config  not found (run 'claude' once and sign in)"
# Hosts the workshop needs. Any HTTP reply counts as reachable.
for h in api.anthropic.com api.elevenlabs.io github.com api.registry.platformio.org; do
  if curl -s -o /dev/null --max-time 8 "https://$h"; then
    printf 'OK      %-14s %s\n' "Network" "$h"
  else
    printf 'WARN    %-14s %s not reachable (firewall, proxy or no internet?)\n' "Network" "$h"
  fi
done
echo
[ $ok = 1 ] && echo "All required tools present." || echo "Some tools are missing or broken. See stages/00-setup/README.md"
