<#
.SYNOPSIS
  Flashes a keyboard: CH552 boards with vnproch55x (.hex), CH32V203 boards with our HID bootloader or wchisp (.bin).

.DESCRIPTION
  CH552 (default, -Hex): waits for the ROM bootloader (4348:55E0) and flashes a
  .hex the moment it appears.
  The bootloader leaves after about 5 seconds without traffic, so the uploader
  (CH55xduino's vnproch55x) is started first and keeps retrying (-r) until the
  device shows up and its WCH driver is ready. Polling Windows for the device first
  is not enough: it can be listed before the driver opens, and the upload then fails
  with "CH375GetUsbID 0 / No CH55x USB Found".
  The bootloader entry pin is written together with the firmware:
    P15 (default) = hold key D (P1.5) while plugging in
    P36           = D+ pull-up (1.5-2 kOhm between chip pin 12 and pin 16)

  CH32V203 (-Board 3x3v_rgb): our own HID bootloader (docs/BOOTLOADER_CH32V203.md).
  Plug in while holding the bottom-left key (3x3v_rgb: 7, 2x4v_rgb: 5; purple LEDs), or pass -Enter when the keyboard runs
  unlocked; then gui\core\hidboot.py writes -Bin (default: the tools\build_fw.ps1
  result, linked at 0x08002000) and starts it. The settings stay.
  -Full: first install through the ROM bootloader. Connect the BOOT bridge, plug in,
  and wchisp writes bootloader + app (firmware\bootloader_ch32v203\build\full-<BOARD>.bin,
  `mingw32-make full` there). Afterwards remove the bridge and replug.
  A keyboard with the factory firmware is read-protected: flashing removes the
  protection and ERASES the factory firmware for good. Never on a reference keyboard.

.EXAMPLE
  .\tools\flash.ps1 -Hex tests\hw\mk6k1n\test_pinmap\build\CH55xDuino.mcs51.ch552\test_pinmap.ino.hex
.EXAMPLE
  .\tools\flash.ps1 -Board 3x3v_rgb
.EXAMPLE
  .\tools\flash.ps1 -Board 3x3v_rgb -Full
#>
param(
    [string]$Hex,
    [ValidateSet('P15', 'P36')][string]$BootPin = 'P15',
    [ValidateRange(1, 255)][int]$TimeoutSec = 60,
    # CH32V203 boards
    [ValidateSet('', '3x3v_rgb', '2x4v_rgb')][string]$Board = '',
    [string]$Bin,
    [switch]$Full,     # bootloader + app with wchisp (BOOT bridge), first install
    [switch]$Enter,    # send BOOT to the running app instead of holding the boot key
    [string]$Wchisp = 'C:\Users\user\tools\wchisp\wchisp-win-x64\wchisp.exe'
)

$ErrorActionPreference = 'Stop'

if ($Board) {
    $root = Split-Path $PSScriptRoot -Parent
    $name = $Board.ToUpper()
    $bootKey = @{ "3x3v_rgb" = "7"; "2x4v_rgb" = "5" }[$Board] # bottom-left key, BOARD_BOOT_KEY
    if (-not $Full) {
        if (-not $Bin) { $Bin = Join-Path $root "firmware\macro_keyboard\build\ch32v203\macro_keyboard-$name.bin" }
        if (-not (Test-Path $Bin)) { throw "$Bin not found (build it: tools\build_fw.ps1 -Board $Board)" }
        $binPath = (Resolve-Path $Bin).Path
        Write-Host "bin : $binPath ($((Get-Item $binPath).Length) bytes)"
        if ($Enter) {
            $extra = @('--enter')
        } else {
            $extra = @()
            Write-Host "1. Unplug the keyboard, hold key $bootKey and plug it in (all LEDs purple)."
            Read-Host "   Press Enter when the LEDs are purple" | Out-Null
        }
        Push-Location $root
        try {
            & uv run python -m gui.core.hidboot flash $binPath @extra
            $code = $LASTEXITCODE
        } finally {
            Pop-Location
        }
        if ($code -ne 0) {
            Write-Host "Failed (exit code $code). No bootloader yet? First install: -Full"
        }
        exit $code
    }

    if (-not $Bin) { $Bin = Join-Path $root "firmware\bootloader_ch32v203\build\full-$name.bin" }
    if (-not (Test-Path $Bin)) { throw "$Bin not found (build it: mingw32-make full in firmware\bootloader_ch32v203)" }
    if (-not (Test-Path $Wchisp)) { throw "wchisp not found: $Wchisp (-Wchisp)" }
    $binPath = (Resolve-Path $Bin).Path
    # full image: bootloader 0x0000, app 0x2000, app info 0xEF00 (16 bytes)
    if ((Get-Item $binPath).Length -ne 0xEF10) {
        throw "$binPath is not a full image (bootloader + app, 61200 bytes): use mkfull.py / mingw32-make full"
    }

    Write-Host "bin    : $binPath ($((Get-Item $binPath).Length) bytes, bootloader + app)"
    Write-Host "wchisp : $Wchisp"
    Write-Host ""
    Write-Host "1. Connect the BOOT bridge (2 pads under the BOOT1 silk), then plug in USB."
    Read-Host "   Press Enter when plugged in" | Out-Null

    & $Wchisp info
    if ($LASTEXITCODE -ne 0) {
        Write-Host "wchisp info failed: bootloader not found (bridge? WinUSB driver?)"
        exit $LASTEXITCODE
    }
    Write-Host ""
    Write-Host "WARNING: a read-protected chip (factory firmware) is ERASED for good."
    Write-Host "         Never flash a reference keyboard."
    Write-Host "         The saved settings are erased too (back them up in the GUI first)."
    $answer = Read-Host "2. Type yes to flash"
    if ($answer -ne 'yes') {
        Write-Host "Cancelled"
        exit 1
    }
    & $Wchisp flash $binPath
    $code = $LASTEXITCODE
    Write-Host "wchisp exit code: $code"
    if ($code -eq 0) {
        Write-Host "3. Remove the BOOT bridge and replug USB (with the bridge it starts the ROM bootloader again)."
        Write-Host "4. Check: uv run python tools\check_device.py"
        Write-Host "   Later updates: tools\flash.ps1 -Board $Board (key $bootKey, no bridge)"
    }
    exit $code
}

if (-not $Hex) { throw "-Hex is required (or -Board for a CH32V203 board)" }
$hexPath = (Resolve-Path $Hex).Path

# Use the newest installed MCS51Tools
$toolsRoot = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\CH55xDuino\tools\MCS51Tools'
$toolDir = Get-ChildItem $toolsRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
if (-not $toolDir) { throw "CH55xDuino MCS51Tools not found under $toolsRoot" }
$exe = Join-Path $toolDir.FullName 'win\vnproch55x.exe'

# vnproch55x config byte: 1 = P1.5 pull-down, 3 = P3.6 (D+) pull-up
$bootCfg = if ($BootPin -eq 'P15') { 1 } else { 3 }

Write-Host "hex      : $hexPath"
Write-Host "uploader : $exe"
Write-Host "boot pin : $BootPin (config $bootCfg)"
Write-Host ""
if ($BootPin -eq 'P15') {
    Write-Host "Plug the keyboard in while holding key D (P1.5)..."
} else {
    Write-Host "Plug the keyboard in with the D+ pull-up connected..."
}

Write-Host "(waiting up to $TimeoutSec s, started $(Get-Date -Format HH:mm:ss))"

# vnproch55x retries every ~0.1 s for -r seconds, then flashes as soon as it opens the device
& $exe -r $TimeoutSec -t CH552 -c $bootCfg $hexPath
$code = $LASTEXITCODE
Write-Host "vnproch55x exit code: $code"
exit $code
