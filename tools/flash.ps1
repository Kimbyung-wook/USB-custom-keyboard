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

  CH32V203 (-Board sayo_3x3): our own HID bootloader (docs/BOOTLOADER_CH32V203.md).
  Plug in while holding the bottom-left key (sayo_3x3: 7, sayo_2x4: 5, sayo_2x6: 7; purple LEDs where the board has them), or pass -Enter when the keyboard runs
  unlocked; then gui\core\hidboot.py writes -Bin (default: the tools\build_fw.ps1
  result, linked at 0x08002000) and starts it. The settings stay.
  The .bin must carry the firmware ID of the board the bootloader reports
  (MKFW1:<BOARD>:...); -Force writes another board's or test firmware anyway.
  -Full: first install, or a new bootloader, through the ROM bootloader. Connect the
  BOOT bridge, plug in, and wchisp writes the bootloader only
  (firmware\bootloader_ch32v203\build\bootloader-<BOARD>.bin, <= 8 KB, -Bootloader).
  Then remove the bridge, hold the boot key while plugging in again, and
  gui\core\hidboot.py writes the app (-Bin) as above.
  wchisp erases only the first 8 KB (8 sectors, at least that many), so the
  settings (0x0800F000) stay on a chip that already runs our firmware.
  A keyboard with the factory firmware is read-protected: flashing removes the
  protection, which ERASES the whole chip (factory firmware and settings) for good.
  Never on a reference keyboard.

  CH573 (-Board sayo_2b3): our own HID bootloader too (docs/BOOTLOADER_CH573.md).
  Turn SW1 off, hold key 4 and turn SW1 on (L5 red blinks), or -Enter; then
  gui\core\hidboot.py writes -Bin (default build\ch573\macro_keyboard-SAYO_2B3.bin).
  -Full: the full image (bootloader + app info + app, -Bin defaults to
  ...-SAYO_2B3-full.bin) with wchisp through the ROM bootloader: PB22 to GND
  while SW1 goes on, USB plugged in. The ROM bootloader leaves after about 6-15 s
  without traffic, so confirm first, then enter it: wchisp info checks the UID
  and wchisp flash follows at once. Data-Flash (settings, BLE bonds) stays.

.EXAMPLE
  .\tools\flash.ps1 -Hex firmware\macro_keyboard\build\macro_keyboard-MK6K1NL.hex
.EXAMPLE
  .\tools\flash.ps1 -Board sayo_3x3
.EXAMPLE
  .\tools\flash.ps1 -Board sayo_3x3 -Full
#>
param(
    [string]$Hex,
    [ValidateSet('P15', 'P36')][string]$BootPin = 'P15',
    [ValidateRange(1, 255)][int]$TimeoutSec = 60,
    # CH32V203 boards
    [ValidateSet('', 'sayo_3x3', 'sayo_2x4', 'sayo_2x6', 'sayo_2b3')][string]$Board = '',
    [string]$Bin,        # the app (default: tools\build_fw.ps1 result)
    [switch]$Full,       # bootloader with wchisp (BOOT bridge), then the app with hidboot; settings stay
    [string]$Bootloader, # -Full: the bootloader image (default: bootloader-<BOARD>.bin)
    [switch]$Enter,    # send BOOT to the running app instead of holding the boot key
    [switch]$Force,    # write a .bin for another board or without a firmware ID (hidboot --force)
    # environment MK_WCHISP first, else this default
    [string]$Wchisp = $(if ($env:MK_WCHISP) { $env:MK_WCHISP } else { 'C:\Users\user\tools\wchisp\wchisp-win-x64\wchisp.exe' })
)

$ErrorActionPreference = 'Stop'

if ($Board) {
    $root = Split-Path $PSScriptRoot -Parent
    $name = $Board.ToUpper()
    $ch573 = $Board -eq 'sayo_2b3'
    # The boot key (bottom left) from the board file: BOARD_BOOT_KEY is 0-based
    # (a CH573 board file sits beside its HAL)
    $boardDir = if ($ch573) { 'hal\ch573' } else { 'boards' }
    $boardFile = Join-Path $root "firmware\macro_keyboard\src\$boardDir\$Board.h"
    $m = Select-String -Path $boardFile -Pattern '^#define\s+BOARD_BOOT_KEY\s+(\d+)'
    if (-not $m) { throw "BOARD_BOOT_KEY not found in $boardFile" }
    $bootKey = [int]$m.Matches[0].Groups[1].Value + 1
    # The keyboards we own (chip UID, as wchisp info and hidboot show it)
    $knownUid = @{ "sayo_3x3" = "CD-AB-EE-F7-7D-BC-38-60"; "sayo_2x4" = "CD-AB-30-92-4E-BD-4B-FB"; "sayo_2x6" = "CD-AB-6B-22-66-BD-9E-8B"; "sayo_2b3" = "1A-73-38-72-AB-3C-FD-21" }[$Board]
    $chipDir = if ($ch573) { 'ch573' } else { 'ch32v203' }
    $fullSuffix = if ($ch573 -and $Full) { '-full' } else { '' }
    if (-not $Bin) { $Bin = Join-Path $root "firmware\macro_keyboard\build\$chipDir\macro_keyboard-$name$fullSuffix.bin" }
    if (-not (Test-Path $Bin)) { throw "$Bin not found (build it: tools\build_fw.ps1 -Board $Board)" }
    $binPath = (Resolve-Path $Bin).Path

    if ($ch573 -and $Full) {
        # The full image with wchisp (ROM bootloader): bootloader, app info and app at once
        if (-not (Test-Path $Wchisp)) { throw "wchisp not found: $Wchisp (-Wchisp)" }
        Write-Host "full image : $binPath ($((Get-Item $binPath).Length) bytes)"
        Write-Host "wchisp     : $Wchisp"
        Write-Host "UID        : $knownUid (our $name keyboard; another UID stops here)"
        Write-Host "A read-protected chip (factory firmware) is ERASED completely and for good."
        $answer = Read-Host "1. Type yes to flash"
        if ($answer -ne 'yes') {
            Write-Host "Cancelled"
            exit 1
        }
        Write-Host "2. SW1 off. USB plugged in, hold PB22 to GND and turn SW1 on (ROM bootloader 4348:55E0)."
        Read-Host "   Press Enter right after (about 6-15 s before it leaves)" | Out-Null
        $info = & $Wchisp info 2>&1 | Out-String
        $code = $LASTEXITCODE
        Write-Host $info
        if ($code -ne 0) {
            Write-Host "wchisp info failed: ROM bootloader not found (PB22? timed out? WinUSB driver?)"
            exit $code
        }
        if ($info -notmatch [regex]::Escape($knownUid)) {
            Write-Host "UID $knownUid not in the lines above: not our $name keyboard, nothing written"
            exit 1
        }
        & $Wchisp flash $binPath
        $code = $LASTEXITCODE
        Write-Host "wchisp exit code: $code"
        if ($code -eq 0) {
            Write-Host "Check: uv run python tools\check_device.py"
            Write-Host "Later updates: tools\flash.ps1 -Board $Board (key $bootKey held while SW1 goes on)"
        }
        exit $code
    }

    if ($Full) {
        # Step 1: the bootloader alone with wchisp (ROM bootloader, BOOT bridge)
        if (-not $Bootloader) { $Bootloader = Join-Path $root "firmware\bootloader_ch32v203\build\bootloader-$name.bin" }
        if (-not (Test-Path $Bootloader)) { throw "$Bootloader not found (build it: tools\build_fw.ps1 -Board $Board)" }
        if (-not (Test-Path $Wchisp)) { throw "wchisp not found: $Wchisp (-Wchisp)" }
        $blPath = (Resolve-Path $Bootloader).Path
        $blLen = (Get-Item $blPath).Length
        # wchisp erases 1 KB sectors from the flash start: those the image covers
        # plus one, at least 8. Measured: the old full image (bootloader + app +
        # app info, 0xEF10 B) -> 61 sectors, 0x0000-0xF3FF, which took the settings
        # (0xF000) with it; the bootloader alone (4472 B, 2026-11-08 on SAYO_3X3)
        # -> "Erased 8 code flash sectors", 0x0000-0x1FFF: just the bootloader's
        # 8 KB, below the app (0x2000), the app info (0xEF00) and the settings.
        # An image over 8 KB would reach the app (and more): refused below.
        if ($blLen -gt 0x2000) {
            throw "$blPath is $blLen bytes: not a bootloader image (at most 8192 B; a full image would erase the settings)"
        }
        $pages = [math]::Max(8, [math]::Ceiling($blLen / 1024) + 1)
        Write-Host "bootloader : $blPath ($blLen bytes; wchisp erases about $pages KB from 0x08000000)"
        Write-Host "app        : $binPath ($((Get-Item $binPath).Length) bytes, written with hidboot after)"
        Write-Host "wchisp     : $Wchisp"
        Write-Host ""
        Write-Host "1. Connect the BOOT bridge (2 pads under the BOOT1 silk), then plug in USB."
        Read-Host "   Press Enter when plugged in" | Out-Null

        & $Wchisp info
        if ($LASTEXITCODE -ne 0) {
            Write-Host "wchisp info failed: bootloader not found (bridge? WinUSB driver?)"
            exit $LASTEXITCODE
        }
        Write-Host ""
        Write-Host "Check the lines above:"
        Write-Host "  - chip CH32V203C8U6"
        Write-Host "  - UID $knownUid (our $name keyboard)"
        Write-Host "  - not read-protected"
        Write-Host "Not protected (our firmware): only the bootloader pages are erased, the settings stay."
        Write-Host "WARNING: a read-protected chip (factory firmware) is ERASED completely and for good:"
        Write-Host "         factory firmware and settings. Never flash a reference keyboard."
        $answer = Read-Host "2. Type yes to write the bootloader"
        if ($answer -ne 'yes') {
            Write-Host "Cancelled"
            exit 1
        }
        & $Wchisp flash $blPath
        $code = $LASTEXITCODE
        Write-Host "wchisp exit code: $code"
        if ($code -ne 0) { exit $code }
        # Step 2: the app through the new bootloader, below
        Write-Host "3. Remove the BOOT bridge. Unplug, hold key $bootKey and plug in again (all LEDs purple)."
        Write-Host "   (Without an app the bootloader stays on by itself.)"
        Read-Host "   Press Enter when the LEDs are purple" | Out-Null
        $extra = @()
    } elseif ($Enter) {
        Write-Host "bin : $binPath ($((Get-Item $binPath).Length) bytes)"
        $extra = @('--enter')
    } elseif ($ch573) {
        Write-Host "bin : $binPath ($((Get-Item $binPath).Length) bytes)"
        $extra = @()
        Write-Host "1. Turn SW1 off, hold key $bootKey and turn SW1 on (USB plugged in, L5 red blinks)."
        Read-Host "   Press Enter when L5 blinks" | Out-Null
    } else {
        Write-Host "bin : $binPath ($((Get-Item $binPath).Length) bytes)"
        $extra = @()
        Write-Host "1. Unplug the keyboard, hold key $bootKey and plug it in (all LEDs purple)."
        Read-Host "   Press Enter when the LEDs are purple" | Out-Null
    }

    # The app with our HID bootloader: the settings stay
    $env:PYTHONIOENCODING = 'utf-8'
    Push-Location $root
    try {
        if ($Force) { $extra += '--force' }
        & uv run python -m gui.core.hidboot flash $binPath @extra
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($code -ne 0) {
        if ($Full) {
            Write-Host "Failed (exit code $code). The bootloader is in: tools\flash.ps1 -Board $Board (key $bootKey) writes the app."
        } else {
            Write-Host "Failed (exit code $code). No bootloader yet? First install: -Full"
        }
    } elseif ($Full) {
        Write-Host "Check: uv run python tools\check_device.py"
        Write-Host "Later updates: tools\flash.ps1 -Board $Board (key $bootKey, no bridge)"
    }
    exit $code
}

if (-not $Hex) { throw "-Hex is required (or -Board for a CH32V203 or CH573 board)" }
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
