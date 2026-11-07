# 새 키보드 이식 절차 ("테라포밍")

- 시중 매크로 키보드를 이 프로젝트 펌웨어로 바꾸는 순서. 지금까지 4개 보드(MK6K1N, MK6K1NL, 3X3V_RGB, 2X4V_RGB)에서 밟은 순서를 정리
- 보드별 결과는 [BOARDS.md](BOARDS.md)와 `docs/board/BOARD_<보드>.md`
- 아래 1~7은 칩이 **CH552** 또는 **CH32V203**일 때. 다른 칩이면 [8. 새 칩](#8-새-칩-ch552ch32v203-외)부터 확인

## 0. 한눈에 보기

| 단계 | 할 일 | 끝난 기준 |
|---|---|---|
| 1 | 식별: 분해, 칩 표기, 기판, USB ID | 칩 이름과 "기존 보드와 같은 기판인지" 판정 |
| 2 | 안전 확인: 읽기 보호, 기준 키보드 남기기 | 보호 상태·UID 기록 |
| 3 | 부트로더 진입 방법 찾기 | 업데이트 모드로 들어가는 방법 확인 |
| 4 | 핀 찾기: 시험 펌웨어 | 키 핀, LED 방식·핀·극성 표 |
| 5 | 보드 파일·GUI·스크립트·시험 추가 | 빌드와 host 시험 통과 |
| 6 | 빌드·처음 설치·장치 확인 | 확인 표 모두 통과 |
| 7 | 문서화 | 보드 문서, BOARDS.md, 그림 |

## 1. 식별

1. **분해**: 키캡·나사를 빼고 기판 앞뒤 사진을 찍어 둠
2. **칩 표기** 확인 (확대경·사진 확대)

   | 표기 | 이 프로젝트 | 예 |
   |---|---|---|
   | `CH552G` (SOP-16) | 지원 (HAL `src/hal/ch552/`) | MK6K1N, MK6K1NL |
   | `CH32V203C8U6` (QFN-48) | 지원 (HAL `src/hal/ch32v203/`) | 3X3V_RGB, 2X4V_RGB |
   | 그 밖 (CH57x, CH58x 등) | HAL 이식 필요 → [8절](#8-새-칩-ch552ch32v203-외) | – |

   - 겉모습·제품명만 보고 칩을 짐작하지 않음 (8키·9키를 처음에 CH552·CH57x로 짐작했으나 실제는 CH32V203이었음)
3. **기존 보드와 같은 기판인지**: 칩, 키 수, 노브, LED 개수·위치, 실크 인쇄를 기존 보드 문서([BOARDS.md](BOARDS.md))와 비교
   - 칩·배치가 같아 보여도 LED 배선이 다를 수 있음 (MK6K1N과 MK6K1NL은 키·노브 핀이 같고 LED만 다름)
4. **USB ID와 공장 펌웨어**: 공장 펌웨어 그대로 꽂고 장치 관리자(또는 `tools/usb_monitor.ps1 -Seconds 60`)로 VID/PID 기록
   - 시중 키보드는 키 수가 달라도 USB ID가 같을 수 있음(예: `1189:8890`) → USB ID로 보드를 가리지 않음
   - 공장 펌웨어에 업데이트 모드 진입 기능이 있는지(예: MK6K1N은 키 E를 누른 채 꽂기) 확인

## 2. ⚠️ 안전

- **공장 펌웨어는 읽기 보호가 걸려 있을 수 있음**. 보호를 풀면(unprotect) 플래시가 지워져 **되돌릴 수 없음**
  - 3X3V_RGB·2X4V_RGB 모두 공장 펌웨어는 읽기 보호(2X4V_RGB는 쓰기 보호 일부도)
- **기준 키보드 한 대는 손대지 않음**: 같은 제품이 여러 대면 하나는 공장 펌웨어 그대로 남겨 LED·동작 비교에 씀 ([BOARD_3X3V_RGB.md](board/BOARD_3X3V_RGB.md) "참고 키보드로 확인할 것")
- **쓰기 전에 상태부터 확인**
  - CH32V203: BOOT 브리지 + `wchisp info`로 칩 이름, 읽기 보호(RDPR), 쓰기 보호(WRP), **칩 UID**를 확인하고 기록
  - CH552: WCHISPTool 칩 목록 또는 업로더 출력의 `Found Device`·부트로더 버전(장입은 하지 않음)
- **UID를 보드 문서에 기록**: 개발에 쓴 장치를 나중에 구분할 수 있게 (예: [BOARD_2X4V_RGB.md](board/BOARD_2X4V_RGB.md) 개요)
- 장입은 사용자가 확인한 뒤에만 함

## 3. 부트로더 진입 방법 찾기

| 칩 | 부트로더 | 진입 방법 | 비고 |
|---|---|---|---|
| CH552 | WCH ROM 부트로더 (`4348:55E0`) | 펌웨어를 올릴 때 기록한 **진입 핀**을 누른 채 꽂기. 이 프로젝트는 P1.5(MK6K1N 계열의 키 D) | 진입 핀에 키가 없거나 공장 펌웨어가 막으면 **D+ 풀업**: 칩 12번(D+)–16번(3V3) 사이 1.5~2kΩ를 대고 꽂기 ([TROUBLESHOOTING 2-3](TROUBLESHOOTING.md#2-3-다운로드-실패-후-장치가-전혀-인식되지-않음-복구)) |
| CH32V203 | WCH ROM 부트로더 (wchisp) | 기판의 **BOOT 브리지 패드**(지금 두 보드는 BOOT1 실크 아래 패드 2개)를 이은 채 꽂기 | 소프트웨어로는 ROM 부트로더에 못 들어감 → 처음 한 번만 브리지. 이후는 자체 HID 부트로더([BOOTLOADER_CH32V203.md](BOOTLOADER_CH32V203.md)) |

- CH552: 진입 핀을 정하기 전에 그 핀이 어느 키인지 확인. P1.5에 키가 없는 보드는 진입 설정을 바꿔야 함([BOARD_INFO.md](BOARD_INFO.md) 5-3)
- CH32V203: 패드 위치를 사진에 표시해 보드 문서에 남김. Windows는 ROM 부트로더 장치에 WinUSB 드라이버(Zadig) 필요

## 4. 핀 찾기 (시험 펌웨어)

- 시험 펌웨어는 키를 누르면 핀 이름을 **키보드 입력으로** 보냄 → 메모장을 **영문 입력 상태**로 열어 두고 기록

| 칩 | 시험 펌웨어 | 확인하는 것 |
|---|---|---|
| CH552 | [test_pinmap](../tests/hw/mk6k1n/test_pinmap/README.md) | 키 핀, 노브 신호(RAW 모드), WS2812 동작 |
| CH552, LED 여러 개 | [test_mk6k1nl](../tests/hw/mk6k1nl/test_mk6k1nl/README.md) | LED 체인 순서, 색 순서(GRB) |
| CH32V203 | [test_ch32v203](../tests/hw/ch32v203/test_ch32v203/README.md) | KEYS → FIND → ANODE 모드 |

**CH32V203 순서 (test_ch32v203)**

1. 꽂으면 `RST x`, `STUCK PXnn`(시작부터 낮은 핀) 기록. 낮은 핀은 키가 아님(예: PB6 = USB D−, 2X4V_RGB의 PB2 = BOOT1 풀다운)
2. **KEYS**: 키를 1번(왼쪽 위)부터 행 순서로 **한 번씩** 누름 → 키 번호별 핀
3. **FIND**(길게 누르기): 키가 아닌 핀을 하나씩 낮춤 → 켜지는 키·색 기록 → **음극(색별, 키 묶음이 공유)** 표
4. **ANODE**(길게 누르기): 하나씩 높임 → 그 키만 켜지면 **키별 양극**
5. FIND에서 빠지는 핀(PA11·PA12 = USBD, PB6·PB7 = USB)도 LED일 수 있음 → 음극이 모자라면 그 핀으로 보고 본 펌웨어 LED 점검으로 확인

**LED 방식과 극성**

| 방식 | 판정 | 보드 |
|---|---|---|
| WS2812 체인 (데이터 선 1개) | 데이터 핀 하나로 모든 LED가 켜짐. 체인 순서는 위치마다 차례로 켜 보고 기록, 색 순서는 빨강·초록·파랑을 보내 확인 | MK6K1N(1개), MK6K1NL(6개) |
| 색별 음극 + 키별 양극 | 핀을 **낮추면** 켜짐 = 음극(같은 색을 키 여러 개가 공유). **높이면** 그 키만 켜짐 = 양극 | 3X3V_RGB, 2X4V_RGB |

- 양극을 풀업만 하면 어두움 → 본 펌웨어는 직접 구동
- R·G·B를 동시에 켜면 빨강만 보일 수 있음(빨강의 전압이 낮음) → 펌웨어는 색마다 차례로 켬
- 결과는 표로 정리해 5단계 보드 파일의 근거로 씀 (예: [test_ch32v203 결과](../tests/hw/ch32v203/test_ch32v203/README.md#결과-3x3v_rgb-2026-11-05))

## 5. 보드 파일·GUI·스크립트·시험 추가

**보드 파일** `firmware/macro_keyboard/src/boards/<보드>.h` (기존 파일을 복사해 고침)

| 항목 | 내용 |
|---|---|
| `BOARD_NAME` | 보드 이름, **8자 이하**(HELLO 24~31). GUI 보드 목록과 같아야 함. 길이 검사는 gcc 빌드(host 테스트, CH32V203)에서만. SDCC(CH552)는 코드 크기 때문에 검사 끔 |
| `BOARD_HAL_CH55X` / `BOARD_HAL_CH32V20X` | 칩별 HAL 고르기 |
| `BOARD_CHIP`, `BOARD_CHIP_ID` | 칩 이름, 칩 ID(CH552 `0x52`, CH32V203 `0x19`) |
| `BOARD_KEYS`, `BOARD_KNOBS`, `BOARD_ROWS` | 키·노브·행 수 (슬롯 = 키 + 노브 × 3) |
| `BOARD_FEATURES` | `BOARD_FEATURE_RGB_LED`, `BOARD_FEATURE_LOCK` 등 |
| 키 핀 | CH552: `PIN_*`와 스위치 비트 표(공통 기판 파일 `ch552_6k1n.h` 참고). CH32V203: `BOARD_KEY_PINS`(`{포트, 비트}`, 슬롯 순서) |
| `BOARD_BOOT_KEY` | CH32V203: 자체 부트로더 진입 키(0부터 센 슬롯 번호, 왼쪽 아래 키 권장). 부트로더와 `tools/flash.ps1` 안내가 이 값을 읽음 |
| LED | `BOARD_LED_COUNT`, `BOARD_KEY_LEDS`(1 = 키마다 LED). CH32V203: `BOARD_LED_ROWS`, `BOARD_LED_PINS`(음극 R·G·B), `BOARD_KEY_LED_PINS`(양극) |
| `BOARD_LED_SELF_TEST` | 1이면 꽂을 때 색 바퀴 점검 (지금 모든 보드 1) |

- 기판이 기존 보드와 같으면 이름과 LED만 다른 보드 파일 하나(MK6K1NL처럼). 다르면 공통 기판 파일도 새로

**이어서 고칠 곳**

| 파일 | 할 일 |
|---|---|
| `firmware/macro_keyboard/src/config.h` | `#elif defined(BOARD_<보드>)` → `#include "boards/<보드>.h"` |
| `gui/core/boards.py` | `BoardSpec` 추가: 이름, 설명, 키/노브/행, 칩 ID, 기능 비트, 진입 키(`boot_key`), 공장 펌웨어 진입 안내(`factory_boot`), CH32V203은 `hidboot=True`·`flash_text` |
| `gui/core/chips.py` | 새 칩일 때만: 칩 ID, 이름, 최대 크기 |
| `tools/build_fw.ps1` | `-Board`의 `ValidateSet`, 칩 표(`$boardChips`)에 보드 → 칩 |
| `tools/flash.ps1` | CH32V203: `-Board`의 `ValidateSet`, UID 표(우리 키보드의 칩 UID). 진입 키는 보드 파일의 `BOARD_BOOT_KEY`에서 읽음(고칠 것 없음) |
| `tests/host/run.py` | `src/boards`의 보드를 모두 자동으로 돎(HELLO·READ·WRITE·COMMIT 포함). 보드별 키·LED 시험이 필요하면 추가 (3X3V_RGB 시험 참고) |
| CH32V203 부트로더 | 따로 고칠 것 없음: `firmware/bootloader_ch32v203`이 `BOARD=<보드>`로 보드 파일의 진입 키·LED 핀을 읽음 |

- 확인: `uv run pytest` (GUI 테스트 + host 테스트 + 교차 검사). 위 표의 `config.h`·`boards.py`·`ValidateSet`·UID 표를 빠뜨리면 `tests/gui/test_consistency.py`가 실패함

## 6. 빌드·처음 설치·확인

1. **빌드**: `powershell -ExecutionPolicy Bypass -File tools\build_fw.ps1 -Board <보드>`
   - CH552: `firmware\macro_keyboard\build\macro_keyboard-<보드>.hex`
   - CH32V203: 앱 `build\ch32v203\macro_keyboard-<보드>.bin`(목적 파일은 보드별 `build\ch32v203\obj-<보드>`), 부트로더 `firmware\bootloader_ch32v203\build\bootloader-<보드>.bin`
   - CH32V203 도구 경로: 환경 변수 `MK_EVT_SDK`(WCH EVT SDK), `MK_RISCV_BIN`(riscv-none-elf-gcc `bin` 폴더), `MK_WCHISP`(wchisp.exe)가 있으면 기본 경로보다 먼저 씀. Makefile 4개(본 펌웨어, 부트로더, test_ch32v203, diag)와 `tools\build_fw.ps1`·`flash.ps1` 공통
2. **처음 설치** (장입은 사용자 확인 뒤)
   - CH552: `tools\flash.ps1 -Hex <파일>` 실행 → 3단계 방법으로 업데이트 모드에 넣음. 또는 GUI 업데이트 창에서 **장입할 보드**를 골라 장입
   - CH32V203: `tools\flash.ps1 -Board <보드> -Full` ([MANUAL 8장](MANUAL.md#처음-설치-공장-펌웨어))
     1. BOOT 브리지 이은 채 꽂기
     2. `wchisp info`로 칩 CH32V203C8U6, 보드 UID, 보호 없음 확인 → `yes`
     3. wchisp가 `bootloader-<보드>.bin`(8 KB 이하)만 씀 (wchisp는 최소 8섹터, 0x0000~0x1FFF를 지움. 부트로더 영역 안)
     4. 브리지를 떼고 **진입 키를 누른 채** 다시 꽂기(보라 LED)
     5. 앱을 HID 부트로더로 씀
     - 우리 펌웨어가 있던 칩은 설정이 남음. 공장 펌웨어(읽기 보호)는 전부 지워짐
     - 새 보드는 `tools/flash.ps1`의 UID 표에도 추가 (진입 키는 보드 파일에서 읽음)
3. **확인**
   - `uv run python tools\check_device.py`: USB, HELLO(보드 이름, 키/노브 수, 칩), 설정 읽기
     - `--board` 생략: 키보드가 HELLO로 알린 보드를 `boards.py`에서 찾아 비교(모르는 보드면 실패). `--board none`: 비교 안 함. 기능은 `boards.py`에 적힌 기능을 모두 가졌는지로 비교. 새 보드는 먼저 `boards.py`에 추가해야 통과
   - 저장 유지: `--write` → USB 다시 꽂기 → `--verify` → `--restore <백업>`
   - GUI: 보드 이름·키 배치가 자동으로 맞는지

| # | 항목 | 확인 방법 |
|---|---|---|
| 1 | USB 인식 | 장치 관리자에 키보드 + HID 장치 (`1209:C55D`) |
| 2 | HELLO | 보드 이름, 키/노브/행 수, 칩 ID, 용량 |
| 3 | 키 | 메모장에서 키마다 기본 문자열 입력 |
| 4 | 노브 (있으면) | 시계·반시계 한 칸에 한 번, 누르기 |
| 5 | 설정 저장 | GUI 저장 → 다시 꽂은 뒤 유지 |
| 6 | 긴 문자열 | 수백 자가 빠짐없이 입력 |
| 7 | LED | 점검(색 바퀴), 상태 색, 누른 키만 초록 |
| 8 | 잠금 | 패턴 저장 → 다시 꽂으면 잠김 → 2초 + 패턴으로 풀림 → 다시 잠그기 |
| 9 | 업데이트 | 진입 키로 업데이트 모드 → GUI 장입 → 설정 유지 |

## 7. 문서화

- `docs/board/BOARD_<보드>.md`: 기존 보드 문서와 같은 목차([BOARDS.md](BOARDS.md) "세부 문서 목차")
  - 개요(제품, 칩, UID, 확인 근거), 키 배치, LED, 부팅·장입, 본 펌웨어 동작, 확인 결과, 이력
- [BOARDS.md](BOARDS.md): "구성"·"펌웨어 업데이트" 표에 행 추가
- 그림: `tools/draw_pinmap.py`에 보드 값을 넣고 `uv run python tools\draw_pinmap.py` → `docs/images/buttons_<보드>.svg` 등
  - CH552: `PINS`, `KEY_PINS`. CH32V203: `RGB_BOARDS`
- 시험 펌웨어 README에 결과(핀 표, 날짜) 기록
- [README.md](../README.md) 대상 보드 목록, 필요하면 [MANUAL.md](MANUAL.md)의 보드별 진입 키 표

## 8. 새 칩 (CH552·CH32V203 외)

- 기능 코드(`src/core/`)는 칩과 무관. 칩마다 **HAL**을 새로 구현해야 함
- 인터페이스: `firmware/macro_keyboard/src/hal/hal.h`. 구현 예: `src/hal/ch552/`, `src/hal/ch32v203/`

| HAL 영역 | 할 일 |
|---|---|
| 시간 | 1 ms 단위 `hal_ms` |
| 키·노브 | 핀 읽기(풀업, 누르면 낮음) |
| LED | 보드 LED 방식에 맞는 구동 |
| 설정 저장 | 플래시(또는 DataFlash) 읽기·지우기·쓰기, 바뀐 곳만 |
| USB | HID 키보드 + Vendor HID(64바이트) 두 인터페이스 ([PROTOCOL.md](PROTOCOL.md)) |
| 칩 UID | HELLO 칩 고유 ID |
| 부트로더 | `hal_enter_bootloader` (BOOT 명령). ROM 부트로더에 소프트웨어로 못 들어가면 자체 부트로더 필요 |

- 빌드 체계(Makefile 또는 Arduino 코어), `tools/build_fw.ps1`의 칩 분기, 장입 도구, `gui/core/chips.py` 칩 표도 추가
- 참고 기록: CH32V203 이식 [old/PORT_CH32V203.md](old/PORT_CH32V203.md), 자체 부트로더 [BOOTLOADER_CH32V203.md](BOOTLOADER_CH32V203.md)
