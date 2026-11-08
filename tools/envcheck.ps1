# Environment check for the ESP32 AI Workshop (Windows).
# Run from PowerShell:  tools\envcheck.ps1
$ErrorActionPreference = 'SilentlyContinue'
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
$pioPath = "$env:USERPROFILE\.platformio\penv\Scripts"
if (Test-Path $pioPath) { $env:Path += ";$pioPath" }

function Check($name, $cmd, $versionArgs = '--version') {
  $c = Get-Command $cmd
  if ($c) {
    $v = (& $cmd $versionArgs 2>&1 | Select-Object -First 1)
    Write-Host ("OK      {0,-14} {1}" -f $name, $v) -ForegroundColor Green
    return $true
  } else {
    Write-Host ("MISSING {0,-14} ({1} not found)" -f $name, $cmd) -ForegroundColor Red
    return $false
  }
}

Write-Host "ESP32 AI Workshop - environment check (Windows)`n"
$ok = $true
$ok = (Check 'Git' 'git') -and $ok
$ok = (Check 'GitHub CLI' 'gh') -and $ok
$ok = (Check 'VS Code' 'code') -and $ok
$ok = (Check 'Claude Code' 'claude') -and $ok
$ok = (Check 'PlatformIO' 'pio') -and $ok

# Toolchain downloaded?
$plat = "$env:USERPROFILE\.platformio\platforms\espressif32"
$tc = Get-ChildItem "$env:USERPROFILE\.platformio\packages" -Directory | Where-Object { $_.Name -like 'toolchain-xtensa-esp32s3*' -or $_.Name -like 'toolchain-xtensa-esp-elf*' }
if ((Test-Path $plat) -and $tc) { Write-Host "OK      ESP32 toolchain downloaded" -ForegroundColor Green }
else { Write-Host "MISSING ESP32 toolchain  (run 'pio run' in 'Reference Implementations\00-hello')" -ForegroundColor Red; $ok = $false }

# Serial device
$ports = Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM\d+\)' } | Select-Object -ExpandProperty Name
if ($ports) { Write-Host "OK      Serial ports   $($ports -join ', ')" -ForegroundColor Green }
else { Write-Host "INFO    Serial ports   none (plug in the CoreS3 to check)" -ForegroundColor Yellow }

# Claude signed in?
if (Test-Path "$env:USERPROFILE\.claude") { Write-Host "OK      Claude config  found" -ForegroundColor Green }
else { Write-Host "INFO    Claude config  not found (run 'claude' once and sign in)" -ForegroundColor Yellow }

Write-Host ""
if ($ok) { Write-Host "All required tools present." -ForegroundColor Green; exit 0 } else { Write-Host "Some tools are missing. See stages\00-setup\README.md" -ForegroundColor Red; exit 1 }
