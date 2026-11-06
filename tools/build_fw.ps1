<#
.SYNOPSIS
  Builds the macro_keyboard firmware with the build date written into it.

.DESCRIPTION
  Runs the arduino-cli bundled with Arduino IDE 2 (or the one on PATH) with
  the board options from firmware\macro_keyboard\README.md, and passes the
  current time as -DFW_BUILD_YEAR=... flags so HELLO (and the GUI) report it.
  The version itself is FW_MAJOR/FW_MINOR/FW_PATCH in src\config.h.

  A build from the Arduino IDE works too, but has no build date (the GUI
  shows "날짜 없음").

  -Board picks the board file (firmware\macro_keyboard\src\boards\<board>.h,
  docs\BOARD_INFO.md); gui\core\boards.py uses the same board names.

  Result: firmware\macro_keyboard\build\macro_keyboard.ino.hex, ready for
  tools\flash.ps1, and a copy named macro_keyboard-<BOARD>.hex, the file the
  GUI's firmware update window offers first for that board.

  CH32V203 boards (3x3v_rgb) build with Makefile.ch32v203 instead (xPack
  riscv-none-elf-gcc, mingw32-make, WCH EVT SDK; paths in -GccBin, -Sdk).
  Result: firmware\macro_keyboard\build\ch32v203\macro_keyboard-<BOARD>.bin,
  flashed with tools\flash.ps1 -Board 3x3v_rgb (wchisp, BOOT bridge).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\build_fw.ps1
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\build_fw.ps1 -Board mk6k1nl
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File tools\build_fw.ps1 -Board 3x3v_rgb
#>
param(
    [ValidateSet('mk6k1n', 'mk6k1nl', '3x3v_rgb')]
    [string]$Board = 'mk6k1n',
    # CH32V203 only
    [string]$GccBin = 'C:\Users\user\tools\gcc\xpack-riscv-none-elf-gcc-15.2.0-1\bin',
    [string]$Sdk = 'C:/Users/user/tools/sdk/EVT'
)

# The chip of each board file (BOARD_CHIP), and the build options of each
# chip (the arduino-cli board and its menu settings)
$boardChips = @{
    mk6k1n     = 'ch552'
    mk6k1nl    = 'ch552'
    '3x3v_rgb' = 'ch32v203'
}
$chipFqbns = @{
    ch552 = 'CH55xDuino:mcs51:ch552:usb_settings=user266,bootloader_pin=p15'
}
$boardName = $Board.ToUpper()

$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent
$sketch = Join-Path $root 'firmware\macro_keyboard'
$outDir = Join-Path $sketch 'build'

# Plain numbers without leading zeros (08 would be an invalid octal literal in C)
$now = Get-Date
$dateFlags = "-DFW_BUILD_YEAR=$($now.Year) -DFW_BUILD_MONTH=$($now.Month) -DFW_BUILD_DAY=$($now.Day)" +
             " -DFW_BUILD_HOUR=$($now.Hour) -DFW_BUILD_MINUTE=$($now.Minute)"

if ($boardChips[$Board] -eq 'ch32v203') {
    if (-not (Test-Path (Join-Path $GccBin 'riscv-none-elf-gcc.exe'))) {
        throw "riscv-none-elf-gcc not found in $GccBin (-GccBin)"
    }
    if (-not (Get-Command mingw32-make -ErrorAction SilentlyContinue)) {
        throw "mingw32-make not found on PATH"
    }
    # The Makefile recipes use sh, sed and wc: Git for Windows has them
    $env:PATH = "$GccBin;C:\Program Files\Git\usr\bin;$env:PATH"
    # Fresh build so the new date (and any Makefile change) is used
    $chOut = Join-Path $outDir 'ch32v203'
    if (Test-Path $chOut) { Remove-Item -Recurse -Force $chOut }
    Push-Location $sketch
    try {
        & mingw32-make -f Makefile.ch32v203 "BOARD=$boardName" "SDK=$Sdk" "EXTRA_DEFS=$dateFlags"
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($code -ne 0) {
        Write-Host "Build failed (exit code $code)"
        exit $code
    }
    $bin = Join-Path $chOut "macro_keyboard-$boardName.bin"
    Write-Host ""
    Write-Host "Built $boardName with build date $($now.ToString('yyyy-MM-dd HH:mm')) -> $bin ($((Get-Item $bin).Length) bytes)"
    exit 0
}

$Fqbn = $chipFqbns[$boardChips[$Board]]
$cli = Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
if (-not (Test-Path $cli)) {
    $onPath = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if (-not $onPath) { throw "arduino-cli not found (install Arduino IDE 2 or put arduino-cli on PATH)" }
    $cli = $onPath.Source
}

$flags = "$dateFlags -DBOARD_$boardName"

# build.extra_flags is taken by the board menu (USB settings), so use the
# compiler hooks that CH55xDuino leaves empty
& $cli compile --fqbn $Fqbn --build-property "compiler.c.extra_flags=$flags" `
    --build-property "compiler.cpp.extra_flags=$flags" --output-dir $outDir $sketch
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed (exit code $LASTEXITCODE)"
    exit $LASTEXITCODE
}
$boardHex = Join-Path $outDir "macro_keyboard-$boardName.hex"
Copy-Item (Join-Path $outDir 'macro_keyboard.ino.hex') $boardHex -Force
Write-Host ""
Write-Host "Built $boardName with build date $($now.ToString('yyyy-MM-dd HH:mm')) -> $boardHex"
