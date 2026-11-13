# test_ch573: CH573 키보드 하드웨어 확인용 테스트 펌웨어

- **대상**: CH573F(RISC-V, BLE, USB 풀스피드) 6키 무선 키보드 **SAYO_2B3**. 보드 문서: [BOARD_SAYO_2X3B_RGB.md](../../../../docs/board/BOARD_SAYO_2X3B_RGB.md)
- **확인 항목**: 키 핀, LED 핀과 구동 방식, SW1·충전 표시(L1·L2) 핀, 칩 전원·배터리 전압(ADC)
- 시험 논리는 CH32V203 시험과 **같은 파일**(`tests/hw/ch32v203/test_ch32v203/test_ch32v203.c`)을 씀. 이 폴더는 CH573 칩 계층과 핀 표만 둠

## 파일

| 파일 | 내용 |
|---|---|
| `ch573_hal.h` | 칩 계층 인터페이스(공용 논리가 부르는 GPIO, USB HID, 시간, 리셋 원인) |
| `ch573_hal.c` | 칩 계층 구현: 60 MHz PLL, TMR0 1 ms 틱, USB 풀스피드 HID 부트 키보드(EP1). SDK 예제 `USB/Device/CompoundDev` 바탕 |
| `ch573_pins.h` | 시험 핀 20개(아래) |
| `Makefile` | 빌드 (`build/test_ch573.bin` 생성), `mingw32-make info`(= `wchisp info`) |

- 공용 논리: `tests/hw/ch32v203/test_ch32v203/test_ch32v203.c`. `-DTEST_HAL_H='"ch573_hal.h"' -DTEST_PINS_H='"ch573_pins.h"'`로 칩 계층·핀 표를 바꿔 끼움

## 동작

- KEYS·FIND·ANODE는 CH32V203 시험과 같음 → [test_ch32v203 README "동작"](../../ch32v203/test_ch32v203/README.md#동작)
  - 순서: KEYS → FIND → ANODE → **ADC** → KEYS, 아무 키나 1초 길게 누르기로 모드 넘김
  - 시작 시 `RST x`, `STUCK Pxnn`, 행·열 매트릭스 키는 `M` + 두 핀
- 다른 점
  - 시험 핀 20개: PA4, PA5, PA8~PA15, PB0, PB4, PB6, PB7, PB12~PB15, PB22, PB23
  - 제외 핀: PB10·PB11(USB D−/D+, 이 펌웨어의 USB)
  - 28핀 CH573F에 안 나온 핀은 반응 없음. PB0·PB6은 QFN28에 없음(자료표 CH573DS1 v1.12, [BOARD_SAYO_2X3B_RGB 3절](../../../../docs/board/BOARD_SAYO_2X3B_RGB.md#3-칩-핀맵))
  - PA10·PA11: 32 kHz 크리스털 핀(X32KI·X32KO). SAYO_2B3에서는 키 5·6으로 쓰임 → 크리스털 없음(2026-11-13)
  - PB22: ROM 부트로더 다운로드 핀(GND에 대고 켜면 부트로더로 들어감)
  - USB ID `413D:2107`(SDK 예제 값, 시험용), 제품 문자열 "TEST CH57"
  - 시험 중 전원: USB를 꽂고 SW1 켬(SW1이 꺼져 있으면 칩 전원 없음)

| 모드 | 동작 | 입력 |
|---|---|---|
| **ADC** (전압 재기, CH573만) | 2초마다 칩 전원(VBAT 내부 채널)과 키·LED에 안 쓰는 ADC 핀 PA8·PB0·PB6(AIN12·AIN8·AIN9)을 잼 | `VBAT n PA08 n PB00 n PB06 n` (mV) |

- 배터리 분압 저항이 있다면 PA8·PB0·PB6 중 하나에 이어졌을 것으로 봄(확인 전)
- 측정: −12 dB 게인, 러프 보정, 16회 평균. 재는 동안 핀은 플로팅, 끝나면 풀업으로 돌아감
- 공용 논리는 HAL에 `HAL_HAS_ADC`가 정의됐을 때만 ADC 모드를 넣음 → CH32V203 시험 펌웨어는 그대로 3모드(바이너리 같음)

## 빌드

필요: xPack `riscv-none-elf-gcc` 15.x, `mingw32-make`, WCH CH573 EVT SDK(기본 경로 `C:/Users/user/tools/sdk_ch573/EVT`)

- 도구 경로: 환경 변수 `MK_CH57X_SDK`(CH573 EVT SDK), `MK_RISCV_BIN`(riscv-none-elf-gcc `bin` 폴더), `MK_WCHISP`(wchisp.exe)가 있으면 기본 경로보다 먼저 씀
  - `MK_RISCV_BIN`을 주면 아래 `PATH` 설정 불필요

```bash
export PATH=/c/Users/user/tools/gcc/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH
cd tests/hw/ch573/test_ch573
mingw32-make
```

- 결과(2026-11-13): `build/test_ch573.bin` 7,040 B. 경고는 SDK `CH57x_clk.c`의 maybe-uninitialized 2건뿐
- SDK 소스는 복사하지 않고 `build/src/`에 sed로 변환한 사본만 씀: `WCH-Interrupt-fast` → `machine`
- Makefile은 `VPATH` 대신 종류별 `vpath`를 씀. `VPATH`면 공용 논리 폴더의 `build/src`(CH32V203 SDK 사본)를 잘못 찾음
- Makefile을 고친 뒤에는 `rm -rf build` 후 빌드

## 장입 방법

> ⚠️ **읽기 보호 켜짐**(`CFG_ROM_READ`=0, 2026-11-13) → 공장 펌웨어를 백업할 수 없음. 장입하면 **공장 펌웨어와 동글 짝짓기가 영구 삭제**(되돌릴 수 없음). 장입은 사용자 승인 뒤에만

- 상태: **장입함**(2026-11-13). 공장 펌웨어·동글 짝짓기는 이것으로 지워짐(되돌릴 수 없음)
- 드라이버: ROM 부트로더(`4348:55E0`)에 WinUSB(Zadig으로 설치, 2026-11-13)
1. USB를 꽂은 채 **PB22를 GND에 대고 SW1 켜기** → ROM 부트로더. 통신이 없으면 약 6~15초 뒤 앱으로 돌아가므로 그 안에 다음 단계
2. `mingw32-make info`(= `wchisp info`, 읽기만)로 칩 CH573, UID `1A-73-38-72-AB-3C-FD-21` 확인
3. `wchisp flash build/test_ch573.bin`

## 결과

- 장입 (2026-11-13): wchisp, 8섹터 지움, 7,168 B 쓰기, 검증 OK
- 시작 시 `RST O`, `MODE: KEYS` 입력 확인 → CH573 HAL의 USB HID·TMR0 동작 확인
- 키 핀 (KEYS 모드, 사용자 시험, 2026-11-13): 키마다 핀 하나, 누르면 GND(낮음), 매트릭스 아님

| 키 | 위치 | 핀 |
|---|---|---|
| 1 | 윗줄 왼쪽 | PB14 |
| 2 | 윗줄 가운데 | PB13 |
| 3 | 윗줄 오른쪽 | PB4 |
| 4 | 아랫줄 왼쪽 | PA9 |
| 5 | 아랫줄 가운데 | PA10 |
| 6 | 아랫줄 오른쪽 | PA11 |

- PA10·PA11이 키 → 32 kHz 크리스털 없음(BLE 저속 클록은 내부 LSI)
- LED 핀 (FIND 모드, 사용자 시험, 2026-11-13): 단색 LED, 핀 높음 = 켜짐(양극을 핀이 직접 구동, 음극 GND)

| LED | 색 | 핀 |
|---|---|---|
| L3 | 흰색 | PA4 |
| L4 | 초록 | PA5 |
| L5 | 빨강 | PA15 |
| L6 | 파랑 | PA14 |
| L7 | 파랑 | PA13 |
| L8 | 파랑 | PA12 |
| L1 / L2 | 초록 / 빨강 | MCU 핀 반응 없음 → 충전 IC가 직접 켜는 표시등으로 봄 |

- FIND 중 구동하지 않는 핀은 풀업이라 LED가 희미하게 켜지고, 다른 핀을 조작할 때 밝기가 바뀌어 보임 → 정상(높음 = 켜짐 구조의 근거)
- 키·LED 아님: PA8, PB0, PB6, PB7, PB12, PB15, PB22, PB23
- ADC 모드 (사용자 측정과 함께, 2026-11-13): VBAT 3,515~3,519 mV(멀티미터 칩 전원 3.46 V보다 약 +55 mV). PA8·PB0·PB6은 약 2.75~2.8 V(풀업 잔류 전압, 연결 없음) → 배터리는 내부 VBAT 채널로 추정([BOARD_SAYO_2X3B_RGB 5절](../../../../docs/board/BOARD_SAYO_2X3B_RGB.md#전원과-배터리))
- 키와 LED의 위치: 키별 LED 아님. L3~L8은 옆면 상태 표시 6개([BOARD_SAYO_2X3B_RGB 4절](../../../../docs/board/BOARD_SAYO_2X3B_RGB.md#4-led))

**이력**

- 2026-11-13: 처음 만듦(CH573 칩 계층, 핀 20개, 공용 논리 재사용)
- 2026-11-13: SAYO_2B3에 장입, USB HID·TMR0 동작 확인
- 2026-11-13: 키 핀 6개 확인
- 2026-11-13: LED 핀 6개 확인, L1·L2는 MCU 핀 아님
- 2026-11-13: ADC 모드 추가(4번째)
- 2026-11-13: ADC 측정 결과(VBAT, 분압 핀 없음)
- 2026-11-13: 보드 이름 BT_2X3 → SAYO_2B3, 문서 링크 고침
