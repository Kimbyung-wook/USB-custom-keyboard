# test_ac6956c: AC6956C 키보드 하드웨어 확인용 테스트 펌웨어 (미검증 초안)

> ⚠️ **미검증 초안**(2026-11-15). 빌드·장입 안 함. JieLi 컴파일러·SDK 미확보(내려받기는 사용자 허락 뒤)
>
> ⚠️ **장입하면 공장 펌웨어가 사라짐.** 장입 전 `jl-uboot-tool`로 플래시 전체 백업, 사용자 승인 뒤에만([공장 펌웨어 보존](../../../../docs/board/BOARD_KEY12_JUZHEN_V01.md#공장-펌웨어-보존-지킬-것))

- **대상**: JieLi AC6956C(QFN32 4×4, BR23 계열, BLE, USB 풀스피드) 12키 + 노브 2 무선 키보드 **JUZH_3B4**. 보드 문서: [BOARD_KEY12_JUZHEN_V01.md](../../../../docs/board/BOARD_KEY12_JUZHEN_V01.md)
- **확인 항목**: 키(행렬 여부)·노브·버튼 핀, LED 핀과 구동 방식, 배터리·충전 입력 전압(ADC)
- 시험 논리는 CH32V203 시험과 **같은 파일**(`tests/hw/ch32v203/test_ch32v203/test_ch32v203.c`)을 씀. 이 폴더는 AC6956C 칩 계층과 핀 표만 둠

## 파일

| 파일 | 내용 |
|---|---|
| `ac6956c_hal.h` | 칩 계층 인터페이스(공용 논리가 부르는 GPIO, USB HID, 시간, 리셋 원인, ADC, `hal_idle`) |
| `ac6956c_hal.c` | 칩 계층 구현. SDK 호출은 모두 앞쪽 "SDK glue" 묶음의 `SDK_*` 함수 안에만 둠(고칠 때 여기만). `app_main`이 시험 작업(`test_main`)을 시작 |
| `ac6956c_pins.h` | 시험 핀 17개(아래) |
| `Makefile` | 빌드 메모: 소스를 SDK 앱 폴더로 복사하고 SDK의 make를 부름(`check`·`copy`·`build`). **장입 대상 없음**(일부러) |

- 공용 논리: `-DTEST_HAL_H='"ac6956c_hal.h"' -DTEST_PINS_H='"ac6956c_pins.h"'`로 칩 계층·핀 표를 바꿔 끼움
- 공용 논리의 `main`은 `test_main`으로 이름이 바뀜(`ac6956c_hal.h`). SDK가 `main`을 가지므로 시험은 RTOS 작업 하나로 돎
- `HAL_HAS_IDLE`: 공용 반복문이 한 바퀴마다 `hal_idle()`을 부름(감시 타이머, 몇 ms마다 시스템 작업에 양보). 정의한 칩만 들어가므로 다른 칩 바이너리는 영향 없음
- SDK 기준: 공개 `fw-AC63_BT_SDK` master, `cpu/br23` 헤더(웹에서 읽기만). 쓰는 이름

| 쓰임 | SDK 이름 |
|---|---|
| GPIO | `gpio_set_die`, `gpio_set_direction`, `gpio_set_pull_up`, `gpio_read`, `gpio_set_hd` |
| ADC | `adc_add_sample_ch`, `adc_get_voltage`, `AD_CH_VBAT`·`AD_CH_LDO5V`(1/4), `AD_CH_PA1` 등 |
| 시간·작업 | `sys_timer_get_ms`, `os_time_dly`, `os_task_create`, `clr_wdt` |
| 리셋 원인 | `power_reset_source_dump` |
| USB HID | `hid_send_data`(8바이트 부트 키보드 보고서) |

## 동작

- KEYS·FIND·ANODE는 CH32V203 시험과 같음 → [test_ch32v203 README "동작"](../../ch32v203/test_ch32v203/README.md#동작)
  - 순서: KEYS → FIND → ANODE → **ADC** → KEYS, 아무 키나 1초 길게 누르기로 모드 넘김
  - 시작 시 `RST x`, `STUCK Pxnn`, 행·열 매트릭스 키는 `M` + 두 핀
- 다른 점
  - 시험 핀 17개: PA0·PA1·PA9·PA10, PB0~PB3, PB8~PB11, PC2~PC5, PC7(칩 GPIO 18개에서 PB5 뺌)
  - 제외 핀: USBDM·USBDP(23·24, 이 펌웨어의 USB), PB5(9, 아래), 전원·RF·크리스털·오디오 핀(보드 문서 [3절](../../../../docs/board/BOARD_KEY12_JUZHEN_V01.md#3-칩-핀맵))
  - PB5: 충전 입력 LDO_IN과 같은 핀(USB 5 V가 걸릴 수 있음). 공용 논리는 시험 핀을 출력으로도 몰므로 뺌. 전압은 ADC 모드의 `LDO5V`로 봄
  - PB1: **Long Press Reset**(길게 낮추면 칩 리셋, 시간은 SDK·`isd_config` 설정, 미확인). 시험이 낮게 모는 것은 매트릭스 주사(ms)와 FIND(1초)뿐. FIND 중 리셋되면 길게 누르기 리셋으로 봄(전원·부트 키일 가능성, 추정)
  - 키 12 + 노브 6(A·B·누름 × 2) = 입력 18개 > 시험 핀 17개 → 키 행렬(`M PA00 PB01` 꼴) 또는 저항 사다리 ADC 키(KEYS 반응 없음, ADC 값 변함)로 예상
  - 포트 번호: 0 = A, 1 = B, 2 = C. 핀 이름은 `PA00`·`PB10`·`PC07`처럼 입력
  - USB ID·제품 문자열: 미정(USB 장치 시작 코드가 TODO)

| 모드 | 동작 | 입력 |
|---|---|---|
| **ADC** (전압 재기) | 2초마다 배터리·충전 입력(내부 1/4 채널, ×4)과 ADC가 있는 시험 핀 8개를 잼 | `VBAT n LDO5V n PA01 n PA10 n PB01 n PB03 n PB08 n PB10 n PC04 n PC05 n` (mV) |

- ADC 핀: PA1 ADC0, PA10 ADC3, PB1 ADC5, PB3 ADC6, PB8 ADC8, PB10 ADC9, PC4 ADC10, PC5 ADC13. USBDP(ADC12)는 USB라 뺌
- 재는 동안 핀은 디지털 입력·풀업 그대로(저항 사다리 키를 누르면 값이 내려감). 디지털 입력 상태로 ADC가 읽히는지는 TODO
- 배터리 분압 저항이 있다면 ADC 핀 8개 중 하나에 이어졌을 것으로 봄(확인 전)

## 빌드

- **아직 못 함**: JieLi SDK·전용 컴파일러(JL clang, Windows) 미확보. 내려받기는 사용자 허락 뒤
- 생각(Makefile 메모): SDK는 자기 makefile·도구로 앱 전체를 빌드하고 `isd_download`·`isd_config.ini`로 펌웨어 이미지를 만듦 → 이 폴더는 소스를 SDK 앱 폴더에 복사해 SDK의 make를 부름
- 환경 변수

| 변수 | 뜻 |
|---|---|
| `MK_JL_SDK` | SDK 루트(`Makefile`, `apps/`, `cpu/`, `tools/`가 있는 폴더) |
| `MK_JL_APP` | 시험을 넣을 SDK 앱 폴더(기본 `$(MK_JL_SDK)/apps/hid`). 소스는 그 안 `pintest/`로 복사 |
| `MK_JL_TARGET` | SDK make 대상(미정) |

```bash
cd tests/hw/ac6956c/test_ac6956c
mingw32-make check   # 경로 보기, SDK 없으면 멈춤
mingw32-make copy    # 소스를 $(MK_JL_APP)/pintest/ 로 복사
mingw32-make build   # 복사 뒤 SDK에서 make $(MK_JL_TARGET)
```

- SDK를 받은 뒤 할 일(Makefile 메모)
  - 앱 소스 목록에 `pintest/test_ch32v203.c`, `pintest/ac6956c_hal.c` 추가, 위 `-D` 두 개
  - 앱의 `app_main`·작업을 빼거나 시험 작업을 부르게 함. `task_info_table[]`에 `pintest` 추가
  - 앱의 블루투스·키·LED·전원 끄기 모듈을 끔(`board_*_cfg.h`의 `TCFG_*_ENABLE 0`): 시험 핀을 몰지 않게
  - `isd_config.ini`: 길게 누르기 리셋(PB1)·USB 업데이트 설정 유지

## 장입 방법

> ⚠️ **지금은 장입하지 않음.** 장입하면 공장 펌웨어가 사라짐(백업 방법은 있을 수 있으나 미검증)

- 상태: **장입 안 함**. 장입 도구·`isd_config` 미정
- 예정 경로(미검증, [보드 문서 JieLi SDK·장입](../../../../docs/board/BOARD_KEY12_JUZHEN_V01.md#jieli-sdk장입-조사))
  1. 사용자 승인(첫 장입은 별도 승인)
  2. 칩 ROM의 USB 다운로드 모드(UBOOT)로 들어감: USB 선에 USB_KEY 신호를 보냄(USB 업데이터 동글 또는 자작 송신기)
  3. `jl-uboot-tool`로 **플래시 전체 백업**(읽기만, 용량 확인 뒤). 백업 전 쓰기·지우기 금지
  4. 장입(도구 미정)

## 할 일 (코드의 TODO(SDK)와 같음)

| 항목 | 내용 | 코드 위치 |
|---|---|---|
| 감시 타이머 | `clr_wdt()`가 br23 헤더에서 안 보임(SDK 앱에서는 씀) | `SDK_watchdog`, `SDK_busy_ms` |
| OS 틱 | `os_time_dly` 1틱 = 10 ms로 가정 | `hal_delay_ms` |
| 시간 정밀도 | `sys_timer_get_ms` 증가 단위 미확인. µs 지연 함수 미확인(1 ms 바쁜 대기로 대신) | `SDK_ms_now`, `SDK_busy_ms` |
| ADC 단위·초기화 | `adc_get_voltage` 단위(mV 가정), `adc_init()`·배경 측정을 시스템이 이미 하는지 | `SDK_adc_mv`, `SDK_adc_init` |
| ADC 핀 상태 | 디지털 입력(`gpio_set_die(gpio, 1)`)인 핀을 ADC가 읽는지. 아니면 재는 동안 아날로그로 바꾸고 되돌림 | `hal_adc_report` |
| 구동 세기 | `gpio_set_hd(gpio, 0)` = 보통 구동인지(`gpio_set_hd0`도 있음) | `hal_gpio_output` 쪽 |
| 리셋 원인 | `power_reset_source_dump` 반환값 해석(1 핀, 2 전원, 4 소프트웨어, 8 감시 타이머로 옮기기, PB1 길게 누르기 = 핀). 지금은 0(`?`) | `SDK_reset_cause` |
| USB HID | 장치 시작 방법(HID 클래스만), 키보드 보고서 서술자(SDK 기본은 Consumer일 수 있음), 호스트 구성 여부, `hid_send_data` 반환값 뜻 | `SDK_usb_start`, `SDK_usb_ready`, `SDK_usb_send` |
| 작업 | `os_task_create` 우선순위·스택 단위(지금 1, 1024) | `app_main` |
| 앱 연결 | SDK 앱의 `app_main`·`task_info_table[]`에 연결 | `app_main`, Makefile |
| 이미지 | `isd_config.ini`(길게 누르기 리셋, USB 업데이트), make 대상 이름 | Makefile |
| 포함 경로 | `asm/gpio.h` 등 SDK 포함 경로 | `ac6956c_hal.c` 앞 |

## 결과

- 없음(빌드·장입 전)
