# test_ch32v203: 8키·9키 키보드 하드웨어 확인용 테스트 펌웨어

- **대상**: 8키(2×4, **2X4V_RGB**), 9키(3×3, **3X3V_RGB**) 매크로 키보드. 둘 다 **CH32V203C8U6**(RISC-V, USB 풀스피드, 플래시 64 KB, RAM 20 KB)
- **확인 항목**: 키 핀, LED 핀과 구동 방식
- GPIO를 **모두 감시**하고, 키가 아닌 핀을 하나씩 직접 구동해 LED를 찾음

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
| **KEYS** (처음) | 누르면 핀 이름, 떼면 `up` | `PA04`, `PA04 up` |
| **PWR** | (9키) 모든 줄 파랑. 1단계: 남는 핀 15개를 하나씩 3초 풀다운. 2단계: 모두 풀다운 후 하나씩 3초 풀업. 구동은 안 함 | `DOWN PB05`, `ALL DOWN`, `UP PB05` |
| **FIND** | 키가 아닌 핀을 **낮음 1초 → 높음 1초**로 구동 | `LED? PA00`, `L`, `H` |
| **BOOT** | BOOT 브리지 없이 ISP 진입 실험. 누르는 즉시, 키 1: 앱에서 점프, 키 2: RAM 표시 후 리셋 → 시작 코드가 점프, 키 3: FLASH_STATR 14번 비트 후 리셋 | `1 JUMP, 2 RESET, 3 FLASH`, `FLASH 전 후` |

- 순서: KEYS → PWR → FIND → DUMP → BOOT(점프) → KEYS
- PWR: LED 전원 핀 찾기. 핀마다 어느 키 LED가 꺼지는지(1단계)·켜지는지(2단계) 기록. 키별 LED 제어가 가능한지 판단용
- FIND는 KEYS에서 눌러 본 핀을 건너뜀 → 3번 키(PA4)만 눌러 들어가면 나머지 키 핀도 후보가 됨
- PA11·PA12는 USB 핀이라 FIND에서 제외(과거 USBPIN 모드로 확인: 1줄 빨강 PA11, 초록 PA12)

## 빌드

필요: xPack `riscv-none-elf-gcc` 15.x, `mingw32-make`, WCH EVT SDK(기본 경로 `C:/Users/user/tools/sdk/EVT`, `make SDK=...`로 변경 가능)

```bash
export PATH=/c/Users/user/tools/gcc/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH
cd tests/hw/ch32v203/test_ch32v203
mingw32-make
```

- 결과(2026-11-05): 약 17.5 KB, 경고 0건
- SDK 소스는 복사하지 않고 `build/src/`에 sed로 변환한 사본만 씀
  - `WCH-Interrupt-fast` → `machine`
  - `#define SYSCLK_FREQ_96MHz_HSE` 주석 처리 (켜져 있으면 `-DSYSCLK_FREQ_48MHz_HSI`를 덮어써 USB가 안 잡힘)
  - `NVIC_SystemReset()` 앞에서 예외 원인을 RAM(0x20001800)에 기록 → 다음 부팅에 `FLT …` 입력
- Makefile을 고친 뒤에는 `rm -rf build` 후 빌드 (make가 `build/src/*.c`를 지움)
- QingKe V4는 즉시값 CSR 명령(`csrrci` 등)이 없음 → 전역 인터럽트는 CSR 0x800으로 제어

## 장입 방법

> ⚠️ 제조사 펌웨어를 덮어쓰면 되돌릴 수 없음(읽기 보호 해제 때 지워짐)

1. **wchisp** 0.3.0 준비 (`C:\Users\user\tools\wchisp\wchisp-win-x64\wchisp.exe`). Windows는 WinUSB 드라이버 필요
2. **BOOT 브리지 연결**: BOOT1 실크 아래 패드 2개를 이은 채로 USB 연결 → ISP 부트로더 진입
3. `wchisp probe`로 칩 확인 (처음에는 읽기 보호 해제 필요: RDPR 0xA5, WRP 0xFFFFFFFF)
4. `wchisp flash build/test_ch32v203.bin` → Verify OK
5. **브리지를 제거**하고 USB를 다시 연결 (브리지가 있으면 매번 부트로더로 들어감)

## 결과 (3X3V_RGB, 2026-11-05)

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

### 2X4V_RGB (8키)

- 제품: SayoDevice Keyb T8B7 (같은 계열 9키 = 3X3V_RGB)

**확인된 것** (사용자 확인, 장치 시험 전)

| 항목 | 내용 |
|---|---|
| 칩 | CH32V203C8U6 (칩 표기 확인. `wchisp probe` 아직 안 함) |
| 배치 | 2행 × 4열, 노브 없음 |
| LED | 키마다 1개, 3색 한 칩(RGB) |

**시험할 것** (BOOT 브리지 필요, 순서대로)

1. 부트로더 진입: BOOT1 패드 위치가 9키와 같은지, `wchisp probe`로 칩·읽기 보호 확인
   - ⚠️ 제조사 펌웨어는 읽기 보호 해제 때 지워짐(되돌릴 수 없음)
2. 장입: `test_ch32v203.bin`, 시작 메시지 `RST`, `STUCK` 기록(PB6처럼 낮게 읽히는 핀)
3. KEYS 모드: 키 1~8 핀 (번호는 왼쪽 위부터 행 순서)
4. FIND 모드: LED 핀
   - 9키처럼 줄(4키)마다 R/G/B 공유인지, 키마다 따로인지
   - active-low인지, 색별 밝기 차이(초록이 밝은지)
5. PA11·PA12(USB 핀)를 LED가 쓰는지
6. 키 핀이 BOOT1(PB2)이나 SWD(PA13·PA14·PA15)와 겹치는지
7. 결과로 `src/boards/2x4v_rgb.h` 작성 → 본 펌웨어(`Makefile.ch32v203`, `BOARD=2X4V_RGB`) 빌드·장입 → GUI 연결, 키 입력, 저장, LED 확인

**9키와 공통으로 아직 확인 못 한 것**

- 본 펌웨어의 USB + 소프트웨어 PWM(TIM3) 동작, 설정 저장(0x0800FF00), 칩 ID 0x19
- 8키도 키별 애노드인지 (PWR 모드로 확인)

## 공통 확인 사항

- 3X3V_RGB, 2X4V_RGB 모두 **노브 없음**(사용자 확인)

## 실험: 소프트웨어로 부트로더 진입 (2026-11-05)

- BOOT 모드: 앱이 시스템 플래시(0x1FFF8000)로 점프
- 결과: 부트로더 코드까지는 실행됨, 그러나 **illegal instruction**(`FLT cause 2 pc 1ffface4 val bc029073`)
  - 실패한 명령은 `csrw 0xbc0, t0`(WCH 전용 CSR 쓰기)
  - 리셋 직후 상태가 아니면 이 CSR 쓰기가 막히는 것으로 보임(원인 미확인)
- 결론: 점프만으로는 부트로더에 못 들어감. 진입은 계속 BOOT 브리지(하드웨어)로 함
- 원인 추정(2026-11-07): SDK 시작 코드가 `mstatus = 0x88`(MPP 0)로 `main`을 **사용자 모드**에서 실행 → 부트로더의 머신 CSR 쓰기가 예외
  - Makefile: `mstatus 0x1888`로 바꿔 머신 모드로 실행
- 재시험 준비(2026-11-07, BOOT 모드 키 1~3)

| 키 | 방법 | 내용 | 결과 |
|---|---|---|---|
| 1 | JUMP | 앱에서 점프(머신 모드로 수정) | 예외 없음, 곧바로 앱으로 돌아옴(`RST` 원인 없음 = 리셋 없이 점프) |
| 2 | RESET | RAM 0x20001810에 표시 → 리셋 → `boot_check.inc`가 시작 코드 맨 앞에서 점프 | 앱으로 돌아옴(`RST S`) |
| 3 | FLASH | FLASH_STATR 14번 비트(다른 WCH 칩의 BOOT_MODE) → 리셋. 전후 STATR 입력 | 비트가 안 써짐(`FLASH 00000000 00000000`), 앱 시작 |

- 결론(2026-11-07): WCH 부트로더는 소프트웨어로 들어가도 BOOT 핀(하드웨어)을 보고 앱으로 돌려보내는 것으로 보임 → 브리지 없는 진입은 **자체 부트로더(IAP)**가 필요
