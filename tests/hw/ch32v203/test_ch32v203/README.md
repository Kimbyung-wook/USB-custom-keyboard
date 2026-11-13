# test_ch32v203: CH32V203 키보드 하드웨어 확인용 테스트 펌웨어

- **대상**: CH32V203C8U6(RISC-V, USB 풀스피드, 플래시 64 KB, RAM 20 KB) 매크로 키보드. 8키(2×4, **SAYO_2X4**), 9키(3×3, **SAYO_3X3**), 12키(2×6, **SAYO_2X6**)
- **확인 항목**: 키 핀, LED 핀과 구동 방식
- GPIO를 **모두 감시**하고, 키가 아닌 핀을 하나씩 직접 구동해 LED를 찾음. 키 수·보드를 몰라도 됨(보드별 빌드 없음)

## 파일

| 파일 | 내용 |
|---|---|
| `test_ch32v203.c` | 테스트 로직(키 감시, LED 핀 탐색, 모드). 칩과 무관 |
| `ch32v203_hal.h` | 칩 계층 인터페이스(GPIO, USB HID, 시간, 리셋 원인) |
| `ch32v203_hal.c` | 칩 계층 구현(WCH EVT SDK 사용) |
| `Makefile` | 빌드 (`build/test_ch32v203.bin` 생성) |
| `diag/` | USB 없이 PC13을 깜빡이는 진단용(칩 동작 확인) |

## 동작

- **짧게 누르기**: 1초 안에 뗌. **길게 누르기**: 아무 키나 1초 → 다음 모드, `MODE: …` 입력
- 시작 시 `RST x`(리셋 원인), `STUCK PXnn`(시작부터 낮은 핀)을 입력

| 모드 | 동작 | 입력 |
|---|---|---|
| **KEYS** (처음) | 누르면 핀 이름, 떼면 `up`. 두 핀 사이 키(행·열 매트릭스)는 `M` + 두 핀 | `PA04`, `PA04 up`, `M PA04 PB10` |
| **FIND** (음극 찾기) | 키가 아닌 핀을 **낮음 1초 → 높음 1초**로 구동. 나머지 핀은 풀업 → 음극(줄 R/G/B)이면 L 동안 그 줄이 그 색으로 희미하게 켜짐 | `LED? PA00`, `L`, `H` |
| **ANODE** (양극 찾기) | 키가 아닌 핀을 모두 약하게 풀다운, 하나씩 **2초 높음** 구동 → 키별 양극이면 그 키 LED만 희미하게 켜짐 | `HI PB03`, `LOOP` |

- 순서: KEYS → FIND → ANODE → KEYS
- **매트릭스 감지**: 20 ms마다 핀을 하나씩 몇 µs 낮추고 따라 낮아지는 핀을 찾음 → 그 두 핀 사이의 키. 시작할 때(아무것도 안 누름) 이미 이어진 쌍(LED 등)은 기억해 뺌 → **꽂을 때 키를 누르지 않기**
  - 매트릭스 키도 길게 누르면 모드가 넘어감. 한 번 눌린 두 핀은 키 핀으로 보고 FIND·ANODE에서 건드리지 않음
  - 두 키를 함께 누르면 가짜 쌍이 보일 수 있음 → 한 번에 하나씩
- **KEYS에서 키를 1번부터 차례로 한 번씩 누를 것**: FIND·ANODE는 눌러 본 핀을 건드리지 않음. 안 누른 키 핀은 구동될 수 있음 → FIND·ANODE 중에는 모드 넘기는 길게 누르기 말고 키를 누르지 않기
- 제외 핀: PA11·PA12(USBD), PB6·PB7(USBFS D−/D+, 이 펌웨어의 USB). `STUCK` 핀은 ANODE에서 제외
- 펌웨어 위치 0x08000000: 자체 부트로더도 덮어씀 → 시험 뒤 `tools\flash.ps1 -Board … -Full`로 다시 설치
- 식별 문자열(`MKFW1:`)이 없음 → 자체 부트로더(`hidboot`, GUI)는 거부. `--force`(`tools\flash.ps1 -Force`)로만 HID 장입 가능. 이 시험 펌웨어는 0x08000000용이므로 장입은 아래 wchisp(BOOT 브리지)로

**이력**

- 2026-11-07: 모드 개편. PWR(9키 전용)·DUMP·BOOT 실험 모드 삭제, 그 결과는 아래 "결과"·"실험" 절
- 2026-11-13: 행·열 매트릭스 키 감지(`M` 쌍) 추가, 12키(SAYO_2X6) 시험 순서 추가

## 빌드

필요: xPack `riscv-none-elf-gcc` 15.x, `mingw32-make`, WCH EVT SDK(기본 경로 `C:/Users/user/tools/sdk/EVT`, `make SDK=...`로 변경 가능)

- 도구 경로: 환경 변수 `MK_EVT_SDK`(WCH EVT SDK), `MK_RISCV_BIN`(riscv-none-elf-gcc `bin` 폴더), `MK_WCHISP`(wchisp.exe)가 있으면 기본 경로보다 먼저 씀. Makefile 4개(본 펌웨어, 부트로더, test_ch32v203, diag)와 `tools\build_fw.ps1`·`flash.ps1` 공통
  - `MK_RISCV_BIN`을 주면 아래 `PATH` 설정 불필요

```bash
export PATH=/c/Users/user/tools/gcc/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH
cd tests/hw/ch32v203/test_ch32v203
mingw32-make
```

- 결과(2026-11-13): 약 18.7 KB, 경고 0건 (`APP=1`도 같음)
- SDK 소스는 복사하지 않고 `build/src/`에 sed로 변환한 사본만 씀
  - `WCH-Interrupt-fast` → `machine`
  - `#define SYSCLK_FREQ_96MHz_HSE` 주석 처리 (켜져 있으면 `-DSYSCLK_FREQ_48MHz_HSI`를 덮어써 USB가 안 잡힘)
  - `NVIC_SystemReset()` 앞에서 예외 원인을 RAM(0x20001800)에 기록 → 다음 부팅에 `FLT …` 입력
- Makefile을 고친 뒤에는 `rm -rf build` 후 빌드 (make가 `build/src/*.c`를 지움)
  - 본 펌웨어(`Makefile.ch32v203`)·부트로더 Makefile은 헤더 의존성(`-MMD -MP`)이 있어 헤더만 고치면 다시 빌드됨. Makefile을 고쳤을 때는 둘 다 `rm -rf build`
- QingKe V4는 즉시값 CSR 명령(`csrrci` 등)이 없음 → 전역 인터럽트는 CSR 0x800으로 제어

## 장입 방법

### 부트로더 뒤에 올리기 (`APP=1`, 브리지 없이)

- 자체 부트로더([BOOTLOADER_CH32V203.md](../../../../docs/BOOTLOADER_CH32V203.md))가 있는 보드는 시험 펌웨어를 앱 자리(0x08002000)에 올릴 수 있음. 지금 SAYO_2X6
- 빌드: `mingw32-make APP=1` → `build/app/test_ch32v203-app.bin` (본 펌웨어와 같은 배치, RAM 끝 16 B는 부트로더 표시용)
- 장입: 진입 키를 누른 채 꽂기 → `uv run python -m gui.core.hidboot flash --force tests/hw/ch32v203/test_ch32v203/build/app/test_ch32v203-app.bin` (저장소 루트에서). 식별 문자열이 없어 `--force` 필요
- 부트로더를 처음 넣을 때 시험 펌웨어와 함께: `python firmware/bootloader_ch32v203/mkfull.py <bootloader-보드.bin> <test_ch32v203-app.bin> <full.bin>` → 브리지 + `wchisp flash <full.bin>`
- 시험 펌웨어가 도는 동안에도 진입 키를 누른 채 꽂으면 부트로더로 들어감 → 본 펌웨어로 바꾸기

### ROM 부트로더로 올리기 (0x08000000, BOOT 브리지)

> ⚠️ 제조사 펌웨어를 덮어쓰면 되돌릴 수 없음(읽기 보호 해제 때 지워짐)

1. **wchisp** 0.3.0 준비 (`C:\Users\user\tools\wchisp\wchisp-win-x64\wchisp.exe`). Windows는 WinUSB 드라이버 필요
2. **BOOT 브리지 연결**: BOOT1 실크 아래 패드 2개를 이은 채로 USB 연결 → ISP 부트로더 진입
3. `wchisp probe`로 칩 확인 (처음에는 읽기 보호 해제 필요: RDPR 0xA5, WRP 0xFFFFFFFF)
4. `wchisp flash build/test_ch32v203.bin` → Verify OK
5. **브리지를 제거**하고 USB를 다시 연결 (브리지가 있으면 매번 부트로더로 들어감)

## 결과 (SAYO_3X3, 2026-11-05)

- 칩: CH32V203C8U6, 부트로더 진입은 BOOT 브리지

### 키 핀 (누르면 낮음)

| 키 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|
| 핀 | PB15 | PA14 | PA4 | PB10 | PA15 | PA3 | PB2 | PB11 | PB1 |

- 키 7의 PB2는 BOOT1과 같은 핀
- PA14·PA15는 SWD 핀 → SWJ를 꺼야 GPIO로 쓸 수 있음
- PB6은 시작부터 낮게 읽힘(키 아님, 용도 미확인)

### LED (WS2812가 아님)

- **줄마다 색별 핀 1개**, 줄의 키 3개가 같은 핀을 공유
- **낮추면 켜지고 높이면 꺼짐**(active-low)

| 줄 | 키 | 빨강 | 초록 | 파랑 |
|---|---|---|---|---|
| 1 | 1·2·3 | PA11 | PA12 | PA13 |
| 2 | 4·5·6 | PA8 | PA9 | PA10 |
| 3 | 7·8·9 | PA0 | PA1 | PA2 |

- PA11·PA12는 USBD 핀일 뿐, 이 펌웨어의 USB(USBFS)는 PB6/PB7 → LED로 사용 가능 (자료표, 2026-11-07. PB6 낮음 = USB D−)
- 키 핀과 LED 핀이 겹치지 않음(PA14·PA15는 키, PA13은 LED)
- **키별 애노드**(PWR 모드, 2026-11-07): 풀다운하면 그 키 LED만 꺼짐, 풀업하면 켜짐

| 키 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|
| 애노드 | PB3 | PB4 | PB9 | PB14 | PB5 | PB8 | PB13 | PB12 | PB0 |

- PA5~PA7, PC13~PC15: 변화 없음. PB14는 2단계에서 "6"으로 기록됐으나 1단계(4)와 PB8(6)로 보아 키 4

### SAYO_2X4 (8키)

- 제품: SayoDevice Keyb T8B7 (같은 계열 9키 = SAYO_3X3)
- **결과 (2026-11-07)**: 키 PA6·PA15·PB5·PB9·PA5·PA7·PB0·PA4, 음극은 9키와 같은 핀(묶음 1·2·3 / 5·6·7 / 4·8), 키별 양극 PB14·PB3·PB4·PB8·PB13·PB15·PB12·PB11, `STUCK PB02` → [BOARD_SAYO_2X4V_RGB.md](../../../../docs/board/BOARD_SAYO_2X4V_RGB.md). 아래는 시험 전에 적은 계획

**확인된 것** (사용자 확인, 장치 시험 전)

| 항목 | 내용 |
|---|---|
| 칩 | CH32V203C8U6 (칩 표기 확인. `wchisp probe` 아직 안 함) |
| 배치 | 2행 × 4열, 노브 없음 |
| LED | 키마다 1개, 3색 한 칩(RGB) |

**시험할 것** (BOOT 브리지 필요, 순서대로)

1. 부트로더 진입: BOOT1 패드 위치가 9키와 같은지, `wchisp info`로 칩·읽기 보호·UID 확인(UID 기록)
   - ⚠️ 제조사 펌웨어는 읽기 보호 해제 때 지워짐(되돌릴 수 없음). 원본으로 남길 8키가 있으면 그것은 건드리지 않기
2. 장입: `wchisp flash build/test_ch32v203.bin` → 브리지 떼고 메모장 열고 다시 꽂기. `RST`, `STUCK` 기록(PB6처럼 낮게 읽히는 핀)
3. KEYS: 키 1~8을 차례로 (번호는 왼쪽 위부터 행 순서: 1~4 윗줄, 5~8 아랫줄) → 키별 핀
4. FIND(길게 누르기 1번): 핀마다 L 동안 켜지는 키·색 기록 → 줄(4키)마다 R/G/B 음극인지
5. ANODE(길게 누르기 1번 더): 핀마다 켜지는 키 기록 → 키별 양극인지
6. 확인: PA11·PA12를 LED가 쓰는지(FIND에서 빠짐 → 음극이 2개 모자라면 이 핀), 키 핀이 BOOT1(PB2)·SWD와 겹치는지
7. 결과로 `src/boards/sayo_2x4.h` 작성
   - **부트로더 키**: `BOARD_BOOT_KEY` = 왼쪽 아래 키(키 5, 번호 4). 부트로더는 보드 파일의 키·LED 핀을 그대로 씀(`firmware/bootloader_ch32v203`, `BOARD=SAYO_2X4`)
   - `config.h` 보드 선택, `tools\build_fw.ps1`·`flash.ps1`의 `-Board`, `gui/core/boards.py`(`boot_key="5"`, `hidboot=True`)에 추가
8. 빌드 → 브리지 + `flash.ps1 -Board sayo_2x4 -Full` → GUI 연결, 키 입력, 저장, LED, 키 5 부트로더 확인

### SAYO_2X6 (12키)

- 사용자 확인: 2행 × 6열, 칩 **CH32V203C8U6**(2026-11-13). 아래 1~7단계 끝(2026-11-13): 보드 파일 작성, 본 펌웨어는 키 7 + HID로 설치.
- **키 결과 (2026-11-13)**: `RST NO`, `STUCK PB02`. 모두 직접 연결(매트릭스 아님)
  - 키 1~6: PB0·PA15·PC13·PA14·PB8·PC14, 키 7~12: PA7·PA6·PA5·PA4·PA3·PC15 → [BOARD_SAYO_2X6.md](../../../../docs/board/BOARD_SAYO_2X6.md#2-버튼-배치와-매핑)
- **LED 결과 (2026-11-13)**: 음극은 9키와 같은 핀(묶음 1·2·3·7 / 9·10·11 / 4·5·6·12), 키별 양극 PB13·PB14·PB3·PB4·PB5·PB9·PB1·PB2·PB10·PB15·PB12·PB11. 키 8은 두 모드 모두 안 켜짐 → PB2(`STUCK`, 시험 제외)로 추정, 본 펌웨어 LED 점검으로 확인(PA11·PA12 포함) → [BOARD_SAYO_2X6.md](../../../../docs/board/BOARD_SAYO_2X6.md#4-led)
- 자체 부트로더 + 앱 자리용 시험 펌웨어 설치(2026-11-13): 이후 시험 펌웨어 교체는 브리지 없이 키 7 → 아래 "부트로더 뒤에 올리기". 부트로더는 그 뒤 LED 핀이 들어간 빌드(4,472 B)로 교체(wchisp, 검증 OK)

**시험할 것** (순서대로)

1. 공장 펌웨어 상태로 꽂아 VID/PID 기록(장치 관리자). 같은 12키가 여러 대면 한 대는 손대지 않음
2. BOOT 패드 찾기(8키·9키는 BOOT1 실크 아래 패드 2개) → 이은 채 꽂기 → `wchisp info`로 칩, 읽기·쓰기 보호, **UID** 기록 (여기까지는 지우지 않음)
3. 보호가 걸려 있으면 `wchisp unprotect` 필요 = **공장 펌웨어 영구 삭제**. 사용자 승인 뒤에만
4. 장입: `wchisp flash build/test_ch32v203.bin` → Verify OK → 브리지 떼고, 메모장을 **영문 입력**으로 열고 다시 꽂기. `RST`, `STUCK` 기록
5. KEYS: 키 1~12를 차례로 한 번씩 (1~6 윗줄 왼쪽부터, 7~12 아랫줄). 핀 이름 또는 `M` 쌍 기록
6. FIND(길게 누르기 1번): 켜지는 키·색 → 음극. ANODE(1번 더): 켜지는 키 → 키별 양극. LED가 안 켜지면 WS2812일 수 있음(데이터 선은 FIND·ANODE에 안 보임)
7. 결과로 `src/boards/sayo_2x6.h` 작성(진입 키 = 왼쪽 아래 키 7 권장) → `-Full`로 본 펌웨어 설치

## 공통 확인 사항

- SAYO_3X3, SAYO_2X4 모두 **노브 없음**(사용자 확인)

## 실험: 소프트웨어로 부트로더 진입 (2026-11-05)

- BOOT 모드: 앱이 시스템 플래시(0x1FFF8000)로 점프
- 결과: 부트로더 코드까지는 실행됨, 그러나 **illegal instruction**(`FLT cause 2 pc 1ffface4 val bc029073`)
  - 실패한 명령은 `csrw 0xbc0, t0`(WCH 전용 CSR 쓰기)
  - 리셋 직후 상태가 아니면 이 CSR 쓰기가 막히는 것으로 보임(원인 미확인)
- 결론: 점프만으로는 부트로더에 못 들어감. ROM 부트로더 진입은 BOOT 브리지(하드웨어)뿐
- 원인 추정(2026-11-07): SDK 시작 코드가 `mstatus = 0x88`(MPP 0)로 `main`을 **사용자 모드**에서 실행 → 부트로더의 머신 CSR 쓰기가 예외
  - Makefile: `mstatus 0x1888`로 바꿔 머신 모드로 실행
- 재시험 준비(2026-11-07, BOOT 모드 키 1~3)

| 키 | 방법 | 내용 | 결과 |
|---|---|---|---|
| 1 | JUMP | 앱에서 점프(머신 모드로 수정) | 예외 없음, 곧바로 앱으로 돌아옴(`RST` 원인 없음 = 리셋 없이 점프) |
| 2 | RESET | RAM 0x20001810에 표시 → 리셋 → `boot_check.inc`가 시작 코드 맨 앞에서 점프 | 앱으로 돌아옴(`RST S`) |
| 3 | FLASH | FLASH_STATR 14번 비트(다른 WCH 칩의 BOOT_MODE) → 리셋. 전후 STATR 입력 | 비트가 안 써짐(`FLASH 00000000 00000000`), 앱 시작 |

- 결론(2026-11-07): WCH 부트로더는 소프트웨어로 들어가도 BOOT 핀(하드웨어)을 보고 앱으로 돌려보내는 것으로 보임 → 브리지 없는 진입은 **자체 부트로더(IAP)**가 필요
- 후속(2026-11-07): 자체 HID 부트로더 구현, 하드웨어 시험 완료 → [BOOTLOADER_CH32V203.md](../../../../docs/BOOTLOADER_CH32V203.md). 본 펌웨어는 처음 한 번만 브리지
