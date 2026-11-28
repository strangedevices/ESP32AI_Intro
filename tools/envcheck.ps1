# Environment check for the ESP32 AI Workshop (Windows).
# Run from PowerShell:  tools\envcheck.ps1
$ErrorActionPreference = 'SilentlyContinue'
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
$pioPath = "$env:USERPROFILE\.platformio\penv\Scripts"
if (Test-Path $pioPath) { $env:Path += ";$pioPath" }

function Check($name, $cmd, $versionArgs = '--version') {
  $c = Get-Command $cmd
  if ($c) {
    # A tool can be on PATH but fail to run, so check the exit code too.
    $out = @(& $cmd $versionArgs 2>&1)
    if ($LASTEXITCODE -ne 0) {
      Write-Host ("BROKEN  {0,-14} {1}" -f $name, ($out | Select-Object -Last 1)) -ForegroundColor Red
      if ($cmd -eq 'pio') { Write-Host "        fix: delete $env:USERPROFILE\.platformio\penv, then reopen VS Code so PlatformIO reinstalls it" -ForegroundColor Cyan }
      return $false
    }
    Write-Host ("OK      {0,-14} {1}" -f $name, ($out | Select-Object -First 1)) -ForegroundColor Green
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

# Long paths: ESP32 builds create very deep paths and fail in confusing ways without this.
$lp = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem').LongPathsEnabled
if ($lp -eq 1) { Write-Host "OK      Long paths     enabled" -ForegroundColor Green }
else {
  Write-Host "WARN    Long paths     not enabled. In an Administrator PowerShell run:" -ForegroundColor Yellow
  Write-Host "        New-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name LongPathsEnabled -Value 1 -PropertyType DWORD -Force" -ForegroundColor Cyan
}

# OneDrive locks files mid-build. New laptops often sync Desktop and Documents to it.
$repo = (Resolve-Path "$PSScriptRoot\..").Path
if ($repo -match 'OneDrive') { Write-Host "WARN    Repo folder    $repo is in OneDrive; clone it to C:\dev instead" -ForegroundColor Yellow }

# Serial device
$ports = Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM\d+\)' } | Select-Object -ExpandProperty Name
if ($ports) { Write-Host "OK      Serial ports   $($ports -join ', ')" -ForegroundColor Green }
else { Write-Host "INFO    Serial ports   none (plug in the CoreS3 to check)" -ForegroundColor Yellow }

# Claude signed in?
if (Test-Path "$env:USERPROFILE\.claude") { Write-Host "OK      Claude config  found" -ForegroundColor Green }
else { Write-Host "INFO    Claude config  not found (run 'claude' once and sign in)" -ForegroundColor Yellow }

# Hosts the workshop needs (TCP connect to port 443, 5 second timeout each).
foreach ($h in 'api.anthropic.com', 'api.elevenlabs.io', 'github.com', 'api.registry.platformio.org') {
  $tcp = New-Object System.Net.Sockets.TcpClient
  $reached = $false
  try { $reached = $tcp.ConnectAsync($h, 443).Wait(5000) -and $tcp.Connected } catch { }
  $tcp.Close()
  if ($reached) { Write-Host ("OK      {0,-14} {1}" -f 'Network', $h) -ForegroundColor Green }
  else { Write-Host ("WARN    {0,-14} {1} not reachable (firewall, proxy or no internet?)" -f 'Network', $h) -ForegroundColor Yellow }
}

Write-Host ""
if ($ok) { Write-Host "All required tools present." -ForegroundColor Green; exit 0 } else { Write-Host "Some tools are missing or broken. See stages\00-setup\README.md" -ForegroundColor Red; exit 1 }
