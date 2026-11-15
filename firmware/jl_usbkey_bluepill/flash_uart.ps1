# Blue Pill(STM32F103C8)에 jl_usbkey_bluepill.bin을 ROM 시리얼 부트로더로 쓴다.
# 준비: BOOT0 = 1, BOOT1 = 0, USB2UART(3.3 V) TX→PA10, RX←PA9, GND, 리셋 누름.
# 사용: powershell -ExecutionPolicy Bypass -File flash_uart.ps1 -Port COM5
# 장입은 사용자가 "ㄱㄱ"라고 한 뒤에만.
param(
    [Parameter(Mandatory = $true)][string]$Port,
    [int]$Baud = 115200,
    [string]$Bin = "$PSScriptRoot\build\jl_usbkey_bluepill.bin",
    [switch]$Yes
)
$ErrorActionPreference = 'Stop'

if (-not (Test-Path $Bin)) { throw "바이너리 없음: $Bin (먼저 mingw32-make)" }
$size = (Get-Item $Bin).Length
Write-Host "포트 $Port, $Baud bps"
Write-Host "파일 $Bin ($size B) → 0x08000000, 칩 전체 지우기 뒤 쓰기·검증"
Write-Host "확인: BOOT0 점퍼 = 1, 3.3 V, TX→PA10, RX←PA9, GND, 리셋 눌렀음"
if (-not $Yes) {
    $a = Read-Host "Blue Pill에 쓸까요? (y/N)"
    if ($a -ne 'y') { Write-Host "취소"; exit 1 }
}

# stm32loader(PyPI, 버전 고정): -e 지우기, -w 쓰기, -v 검증, -a 시작 주소
uv run --no-project --with stm32loader==0.7.1 stm32loader `
    -p $Port -b $Baud -e -w -v -a 0x08000000 $Bin
if ($LASTEXITCODE -ne 0) { throw "stm32loader 실패 ($LASTEXITCODE)" }
Write-Host "완료. BOOT0 점퍼를 0으로 돌리고 리셋 → PC13 LED 켜짐(10초 송신)"
