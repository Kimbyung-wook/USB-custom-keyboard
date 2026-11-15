# test_ac6321a: AC6321A 키보드 하드웨어 확인용 테스트 펌웨어 (미검증 초안)

> ⚠️ **미검증 초안**(2026-11-15). SDK `cpu/bd19` 플래그로 두 소스 컴파일만 확인(앱 링크·이미지 아직), 장입 안 함. JieLi 툴체인 설치는 진행 중(사용자 결정)
>
> ⚠️ **장입하면 공장 펌웨어가 사라짐**(지워도 됨, 사용자 결정 2026-11-15). 장입은 사용자가 "ㄱㄱ"라고 한 뒤에만, 백업은 권장([공장 펌웨어](../../../../docs/board/BOARD_KEY12_JUZHEN_V01.md#공장-펌웨어-지워도-됨))

- **대상**: JieLi AC6321A(QFN32 4×4, BD19 = AC632N 계열, BLE 전용) 12키 + 노브 2 무선 키보드 **JUZH_3B4**. 보드 문서: [BOARD_KEY12_JUZHEN_V01.md](../../../../docs/board/BOARD_KEY12_JUZHEN_V01.md)
- **확인 항목**: 키(행렬 여부)·노브·버튼 핀, LED 핀과 구동 방식, 배터리·충전 입력 전압(ADC)
- 시험 논리는 CH32V203 시험과 **같은 파일**(`tests/hw/ch32v203/test_ch32v203/test_ch32v203.c`)을 씀. 이 폴더는 AC6321A 칩 계층과 핀 표만 둠

## 파일

| 파일 | 내용 |
|---|---|
| `ac6321a_hal.h` | 칩 계층 인터페이스(공용 논리가 부르는 GPIO, USB HID, 시간, 리셋 원인, ADC, `hal_idle`) |
| `ac6321a_hal.c` | 칩 계층 구현. SDK 호출은 모두 앞쪽 "SDK glue" 묶음의 `SDK_*` 함수 안에만 둠(고칠 때 여기만). `app_main`이 시험 작업(`test_main`)을 시작 |
| `ac6321a_pins.h` | 시험 핀 21개(아래) |
| `Makefile` | 빌드 메모: 소스를 SDK 앱 폴더로 복사하고 SDK의 make를 부름(`check`·`copy`·`build`). **장입 대상 없음**(일부러) |

- 공용 논리: `-DTEST_HAL_H='"ac6321a_hal.h"' -DTEST_PINS_H='"ac6321a_pins.h"'`로 칩 계층·핀 표를 바꿔 끼움
- 공용 논리의 `main`은 `test_main`으로 이름이 바뀜(`ac6321a_hal.h`). SDK가 `main`을 가지므로 시험은 RTOS 작업 하나로 돎
- `HAL_HAS_IDLE`: 공용 반복문이 한 바퀴마다 `hal_idle()`을 부름(감시 타이머, 몇 ms마다 시스템 작업에 양보). 정의한 칩만 들어가므로 다른 칩 바이너리는 영향 없음
- SDK 기준: 공식 `fw-AC63_BT_SDK`(커밋 `fdc018de`, `C:\Users\user\tools\sdk\fw-AC63_BT_SDK`), `cpu/bd19`(AC632N). 쓰는 이름

| 쓰임 | SDK 이름 |
|---|---|
| GPIO | `gpio_set_die`, `gpio_set_direction`, `gpio_set_pull_up`, `gpio_read`, `gpio_set_hd`, `usb1_iomode(1)`(USB1 두 핀을 GPIO로) |
| ADC | `adc_add_sample_ch`, `adc_get_voltage`(mV), `AD_CH_VBAT`·`AD_CH_LDO5V`(1/4), `AD_CH_PA1` 등 |
| 시간·작업 | `sys_timer_get_ms`, `os_time_dly`(1틱 10 ms), `os_task_create`, `clr_wdt`(`asm/cpu.h`) |
| 리셋 원인 | `power_reset_source_dump` |
| USB HID | `usb_start`(→ `usb_device_mode(usbfd, HID_CLASS)`), `hid_send_data`(8바이트 부트 키보드 보고서). 키보드 서술자는 SDK `apps/hid/examples/standard_keyboard/usb_hid_devices.c`를 함께 빌드 |

## 동작

- KEYS·FIND·ANODE는 CH32V203 시험과 같음 → [test_ch32v203 README "동작"](../../ch32v203/test_ch32v203/README.md#동작)
  - 순서: KEYS → FIND → ANODE → **ADC** → KEYS, 아무 키나 1초 길게 누르기로 모드 넘김
  - 시작 시 `RST x`, `STUCK Pxnn`, 행·열 매트릭스 키는 `M` + 두 핀
- 다른 점
  - 시험 핀 21개: PA0~PA8, PB0~PB9, USB1DP·USB1DM(`usb1_iomode(1)` 뒤 GPIO, 표기 `PU00`·`PU01`)
  - 제외 핀: USB0DM·USB0DP(14·15, 이 펌웨어의 USB), PA9(7, 아래), LDOIN/PP0(4, 충전 5 V 입력), 전원 2·3·5·6, RF 8, 크리스털 9·10(보드 문서 [3절](../../../../docs/board/BOARD_KEY12_JUZHEN_V01.md#3-칩-핀맵))
  - PA9: 기본 풀업, **Long Press Reset**(길게 낮추면 칩 리셋). 공용 논리는 시험 핀을 낮게 몰므로(FIND 1초, 매트릭스 주사) 뺌. 전원·부트 키일 가능성(추정), 손으로 확인
  - 고전압 내성: PB0·PA0·PB7·PB5·PB3. 기본 풀업: PB2·PB1. PB8·PB9는 32.768 kHz 크리스털 핀으로도 쓰임(그 크리스털이 있으면 핀이 멈춤·잡음으로 보임, 미확인)
  - 키 12 + 노브 6(A·B·누름 × 2) = 입력 18개, 시험 핀 21개. 그래도 키 행렬(`M PA00 PB01` 꼴)이나 저항 사다리 ADC 키(KEYS 반응 없음, ADC 값 변함)일 수 있음
  - 포트 번호: 0 = A, 1 = B, USB1 = `U`. 핀 이름은 `PA00`·`PB09`·`PU01`처럼 입력
  - USB ID·제품 문자열: 미정(USB 장치 시작 코드가 TODO)

| 모드 | 동작 | 입력 |
|---|---|---|
| **ADC** (전압 재기) | 2초마다 배터리·충전 입력(내부 1/4 채널, ×4)과 ADC가 있는 시험 핀 10개를 잼 | `VBAT n LDO5V n PA01 n …` (mV) |

- ADC 핀: PA1 ADC0, PA3 ADC1, PA5 ADC2, PA7 ADC3, PA8 ADC4, USB1DP ADC5, USB1DM ADC6, PB1 ADC7, PB4 ADC9, PB6 ADC12(SDK bd19 `adc_api.h`, 데이터시트와 같음). PA9(ADC8)는 시험 핀이 아니라 뺌
- 재는 동안 핀은 디지털 입력·풀업 그대로(저항 사다리 키를 누르면 값이 내려감). 디지털 입력 상태로 ADC가 읽히는지는 TODO
- 배터리 분압 저항이 있다면 ADC 핀 10개 중 하나에 이어졌을 것으로 봄(확인 전)

## 빌드

- **앱 링크·이미지는 아직**: 두 소스는 SDK `cpu/bd19` 플래그로 컴파일 확인. JieLi 툴체인(`jl_win_toolchain_2.5.3.exe`, JL clang, 대상 q32s) 설치 진행 중(사용자 결정)
- 생각(Makefile 메모): SDK는 자기 makefile·도구로 앱 전체를 빌드하고 `isd_download`·`isd_config.ini`로 펌웨어 이미지를 만듦 → 이 폴더는 소스를 SDK 앱 폴더에 복사해 SDK의 make를 부름
- 환경 변수

| 변수 | 뜻 |
|---|---|
| `MK_JL_SDK` | SDK 루트(`Makefile`, `apps/`, `cpu/`, `tools/`가 있는 폴더) |
| `MK_JL_APP` | 시험을 넣을 SDK 앱 폴더(기본 `$(MK_JL_SDK)/apps/hid`). 소스는 그 안 `pintest/`로 복사 |
| `MK_JL_TARGET` | SDK make 대상(기본 `ac632n_hid`) |
| `MK_JL_TOOLCHAIN` | 툴체인 `bin` 폴더(`clang.exe`, 기본 `C:/JL/pi32/bin`). SDK make 때 SDK의 tools 안 utils 폴더(mkdir_win, fixbat)와 함께 PATH 앞에 넣음 |

```bash
cd tests/hw/ac6321a/test_ac6321a
mingw32-make check   # 경로 보기, SDK 없으면 멈춤
mingw32-make copy    # 소스를 $(MK_JL_APP)/pintest/ 로 복사
mingw32-make build   # 복사 뒤 SDK에서 make $(MK_JL_TARGET)
```

- `build`는 SDK의 빌드 뒤 스크립트(`cpu/bd19/tools/download.bat`, `isd_download -tonorflash`: 꽂힌 칩에 씀)를 `RUN_POST_SCRIPT`로 끔. 장입 대상은 일부러 없음

- SDK를 받은 뒤 할 일(Makefile 메모)
  - 앱 소스 목록(`apps/hid/board/bd19/Makefile`)에 `pintest/test_ch32v203.c`, `pintest/ac6321a_hal.c` 추가, 위 `-D` 두 개
  - 앱의 `app_main`·작업을 빼거나 시험 작업을 부르게 함. `task_info_table[]`에 `pintest` 추가
  - 앱의 블루투스·키·LED·전원 끄기 모듈을 끔(`board_ac6321a_*_cfg.h`의 `TCFG_*_ENABLE 0`): 시험 핀을 몰지 않게
  - `isd_config.ini`: 길게 누르기 리셋(PA9)·USB 업데이트 설정 유지

## 장입 방법

> ⚠️ **지금은 장입하지 않음**(툴체인 설치 진행 중, 이미지 없음). 장입은 "ㄱㄱ" 뒤. 공장 펌웨어는 지워도 됨

- 상태: **장입 안 함**. 장입 도구·`isd_config` 미정. 사용자 결정 2개 대기([보드 문서](../../../../docs/board/BOARD_KEY12_JUZHEN_V01.md#사용자-결정-대기))
- 예정 경로(미검증, [보드 문서 JieLi SDK·장입](../../../../docs/board/BOARD_KEY12_JUZHEN_V01.md#jieli-sdk장입-조사))
  1. 사용자 "ㄱㄱ"
  2. 칩 ROM의 USB 다운로드 모드(UBOOT, PC에는 USB 저장장치)로 들어감. 공장 펌웨어 상태는 벤더 HID에 `03 EF EF`(확인, 2026-11-15). 우리 펌웨어 설치 뒤는 BOOT 명령(`go_mask_usb_updata()`)
  3. (권장, 필수 아님) 플래시 전체 백업. 단 BD19 RAM 로더가 이 칩에서 동작 안 해(PM 시험) 지금은 방법 없음([보드 문서](../../../../docs/board/BOARD_KEY12_JUZHEN_V01.md#장입-usb-다운로드-모드uboot))
  4. 장입(도구 미정)

## 할 일 (코드의 TODO(SDK)와 같음)

- SDK 원본으로 확인해 뺀 것: `clr_wdt()`(`asm/cpu.h`), RTOS 틱 10 ms(`configTICK_RATE_HZ` 100), `adc_get_voltage` = mV, USB 시작 `usb_start()`(HID_CLASS)·키보드 서술자

| 항목 | 내용 | 코드 위치 |
|---|---|---|
| 시간 정밀도 | `sys_timer_get_ms` 증가 단위 미확인. µs 지연 함수 미확인(1 ms 바쁜 대기로 대신) | `SDK_ms_now`, `SDK_busy_ms` |
| ADC 초기화 | `adc_init()`·추가 채널 배경 측정을 시스템이 이미 하는지(`adc_get_voltage`는 마지막 측정값) | `SDK_adc_mv`, `SDK_adc_init` |
| ADC 핀 상태 | 디지털 입력(`gpio_set_die(gpio, 1)`)인 핀을 ADC가 읽는지. 아니면 재는 동안 아날로그로 바꾸고 되돌림 | `hal_adc_report` |
| 구동 세기 | `gpio_set_hd(gpio, 0)` = 보통 구동인지(`gpio_set_hd0`도 있음) | `hal_gpio_output` 쪽 |
| 리셋 원인 | `power_reset_source_dump` 반환값 해석(1 핀, 2 전원, 4 소프트웨어, 8 감시 타이머로 옮기기, PA9 길게 누르기 = 핀). 지금은 0(`?`) | `SDK_reset_cause` |
| USB HID | `usb_start()` 전에 PC 작업 메시지(USB 꽂힘 이벤트)가 필요한지, 호스트 구성 여부, 바쁠 때 `hid_tx_data` 반환값, IN 전송 끝 기다리기(지금 2 ms 대기) | `SDK_usb_start`, `SDK_usb_ready`, `SDK_usb_send`, `sendReport` |
| 작업 | `os_task_create` 우선순위·스택 단위(지금 1, 1024) | `app_main` |
| 앱 연결 | SDK 앱의 `app_main`·`task_info_table[]`에 연결 | `app_main`, Makefile |
| 이미지 | `isd_config.ini`(길게 누르기 리셋, USB 업데이트), make 대상 이름(`MK_JL_TARGET`), 툴체인 위치(`MK_JL_TOOLCHAIN`) | Makefile |
| 포함 경로 | `asm/gpio.h` 등 SDK 포함 경로 | `ac6321a_hal.c` 앞 |

## 결과

- 없음(빌드·장입 전)
