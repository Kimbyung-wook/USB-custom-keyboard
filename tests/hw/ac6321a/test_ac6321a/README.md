# test_ac6321a: AC6321A 키보드 하드웨어 확인용 테스트 펌웨어 (미검증 초안)

> ⚠️ **미검증 초안**(2026-11-15). SDK 앱으로 링크해 `build/app.bin`(앱만)까지 만듦. 장치 없이 `isd_download`를 한 번 돌려 플래시 이미지도 만듦(사용자 승인). **장입함**(사용자 승인, RAM 로더, 검증 일치) → 리셋 뒤 USB·BLE 안 잡힘, **보드 복구 불가·보류**([보드 문서](../../../../docs/board/BOARD_KEY12_JUZHEN_V01.md#공장-펌웨어-지워도-됨))
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
| `jl_build.sh` | 빌드(bash): SDK 필요 부분을 작업 사본으로 복사·설정 수정, 시험 소스 넣고 SDK make, `app.bin` 묶기(오프라인 `isd_download` 산출물도 `build/`). 원본 SDK는 그대로, 빌드 뒤 장입 스크립트 끔 |
| `Makefile` | `mingw32-make build` = `bash jl_build.sh`. **장입 대상 없음**(일부러) |

- 공용 논리: `TEST_HAL_H`·`TEST_PINS_H`로 칩 계층·핀 표를 바꿔 끼움(`jl_build.sh`가 만드는 `pintest_main.c`가 정의하고 `test_ch32v203.c`를 포함)
- 공용 논리의 `main`은 `test_main`으로 이름이 바뀜(`ac6321a_hal.h`). SDK가 `main`을 가지므로 시험은 RTOS 작업 하나로 돎
- `HAL_HAS_IDLE`: 공용 반복문이 한 바퀴마다 `hal_idle()`을 부름(감시 타이머, 몇 ms마다 시스템 작업에 양보). 정의한 칩만 들어가므로 다른 칩 바이너리는 영향 없음
- `HAL_HAS_FLASH_INFO`: 시작 때 공용 논리가 `hal_flash_report()`를 불러 플래시 정보 한 줄을 입력(아래)
- SDK 기준: 공식 `fw-AC63_BT_SDK`(커밋 `fdc018de`, `C:\Users\user\tools\sdk\fw-AC63_BT_SDK`), `cpu/bd19`(AC632N). 쓰는 이름

| 쓰임 | SDK 이름 |
|---|---|
| GPIO | `gpio_set_die`, `gpio_set_direction`, `gpio_set_pull_up`, `gpio_read`, `gpio_set_hd`, `usb1_iomode(1)`(USB1 두 핀을 GPIO로) |
| ADC | `adc_add_sample_ch`, `adc_get_voltage`(mV), `AD_CH_VBAT`·`AD_CH_LDO5V`(1/4), `AD_CH_PA1` 등 |
| 시간·작업 | `sys_timer_get_ms`, `os_time_dly`(1틱 10 ms), `os_task_create`, `clr_wdt`(`asm/cpu.h`) |
| 리셋 원인 | `get_reset_source_value`(`power_reset_source_dump`는 bd19 라이브러리에 없음) |
| USB HID | SDK 사용자 HID(`usb_hid_devices.c`): 키보드 Report ID 1(입력 9 B, LED 출력), 벤더 Report ID 3(usage page 0xFF00, 출력 64 B) |
| 플래시 정보 | `boot_info`(ROM UBOOT가 넘긴 플래시 크기·칩 ID), `norflash_get_uuid`(플래시 고유 ID) |
| 그 밖 | `VCM_DET_EN(0)`(PB2 VCM 리셋 막기), `go_mask_usb_updata`(UBOOT 복귀) |

## 동작

- KEYS·FIND·ANODE는 CH32V203 시험과 같음 → [test_ch32v203 README "동작"](../../ch32v203/test_ch32v203/README.md#동작)
  - 순서: KEYS → FIND → ANODE → **ADC** → KEYS, 아무 키나 1초 길게 누르기로 모드 넘김
  - 시작 시 `RST x`, `STUCK Pxnn`, 행·열 매트릭스 키는 `M` + 두 핀
  - 시작 때 한 줄 더: `FLS size … chip … uid …` = 플래시 크기(바이트)·칩 ID(`boot_info`), 플래시 고유 ID 16바이트. JEDEC ID는 라이브러리 안 변수라 못 읽음(크기 = 용량 바이트에서 계산)
- 다른 점
  - 시험 핀 21개: PA0~PA8, PB0~PB9, USB1DP·USB1DM(`usb1_iomode(1)` 뒤 GPIO, 표기 `PU00`·`PU01`)
  - 제외 핀: USB0DM·USB0DP(14·15, 이 펌웨어의 USB), PA9(7, 아래), LDOIN/PP0(4, 충전 5 V 입력), 전원 2·3·5·6, RF 8, 크리스털 9·10(보드 문서 [3절](../../../../docs/board/BOARD_KEY12_JUZHEN_V01.md#3-칩-핀맵))
  - PA9: 기본 풀업, **Long Press Reset**(길게 낮추면 칩 리셋). 공용 논리는 시험 핀을 낮게 몰므로(FIND 1초, 매트릭스 주사) 뺌. 전원·부트 키일 가능성(추정), 손으로 확인
  - 고전압 내성: PB0·PA0·PB7·PB5·PB3. 기본 풀업: PB2·PB1. PB8·PB9는 32.768 kHz 크리스털 핀으로도 쓰임(그 크리스털이 있으면 핀이 멈춤·잡음으로 보임, 미확인)
  - 키 12 + 노브 6(A·B·누름 × 2) = 입력 18개, 시험 핀 21개. 그래도 키 행렬(`M PA00 PB01` 꼴)이나 저항 사다리 ADC 키(KEYS 반응 없음, ADC 값 변함)일 수 있음
  - 포트 번호: 0 = A, 1 = B, USB1 = `U`. 핀 이름은 `PA00`·`PB09`·`PU01`처럼 입력
  - USB: 장치 `4A4C:4155`(SDK 기본값, 공장 `514C:8850`과 다름). 키보드 Report ID 1(입력 9 B·LED 출력), 벤더 HID Report ID 3(출력 64 B). 출력은 인터럽트 OUT으로만 받음(SDK가 SET_REPORT 무시). 디버그 UART0은 끔(TX가 USB0 DP와 같은 핀)
  - **UBOOT 복귀 두 경로**(칩에서 미확인): ① 벤더 HID(Report ID 3)에 `03 EF EF` → `go_mask_usb_updata()`(공장 펌웨어와 같은 명령, GUI 진입 HID 목록에 `4A4C:4155` 있음) ② Scroll Lock을 3초 안에 6번 바꿈. 버튼·드라이버 없이 GUI가 키 입력 흉내로 할 수 있음
  - PB2: 바꾸면 VCM 리셋이 걸리는 칩 특성(SDK FAQ #37) → 시작 때 `VCM_DET_EN(0)`으로 끔. PB3는 자료표상 MCLR
  - 리셋 원인 `RST x`: `get_reset_source_value()` 비트를 1 핀(PA9 길게 누르기·VCM), 2 전원, 4 소프트웨어, 8 감시 타이머, 32 깨우기로 묶음(실물 확인 필요)

| 모드 | 동작 | 입력 |
|---|---|---|
| **ADC** (전압 재기) | 2초마다 배터리·충전 입력(내부 1/4 채널, ×4)과 ADC가 있는 시험 핀 10개를 잼 | `VBAT n LDO5V n PA01 n …` (mV) |

- ADC 핀: PA1 ADC0, PA3 ADC1, PA5 ADC2, PA7 ADC3, PA8 ADC4, USB1DP ADC5, USB1DM ADC6, PB1 ADC7, PB4 ADC9, PB6 ADC12(SDK bd19 `adc_api.h`, 데이터시트와 같음). PA9(ADC8)는 시험 핀이 아니라 뺌
- 재는 동안 핀은 디지털 입력·풀업 그대로(저항 사다리 키를 누르면 값이 내려감). 디지털 입력 상태로 ADC가 읽히는지는 TODO
- 배터리 분압 저항이 있다면 ADC 핀 10개 중 하나에 이어졌을 것으로 봄(확인 전)

## 빌드

- 상태: `bash jl_build.sh`(또는 `mingw32-make build`)로 SDK 앱 링크 → `build/sdk.elf`, `build/app.bin`(71,980 B) 확인(2026-11-15). 실물 미시험
- 도구: JieLi 툴체인 2.5.3(`C:\JL`, clang 4.0.1 pi32, 대상 q32s), SDK fw-AC63_BT_SDK(`cpu/bd19`, `ac632n_hid`)
- 환경 변수

| 변수 | 뜻 |
|---|---|
| `MK_JL_SDK` | SDK 원본(손대지 않음). 기본 `C:/Users/user/tools/sdk/fw-AC63_BT_SDK` |
| `MK_JL_WORK` | 작업 사본(SDK의 apps/common·apps/hid·cpu/bd19·include_lib·tools만 복사). 기본 `C:/Users/user/tools/jieli/pintest_sdk` |
| `MK_JL_TOOLCHAIN` | 툴체인 `bin` 폴더(`clang.exe`). 기본 `C:/JL/pi32/bin` |

```bash
cd tests/hw/ac6321a/test_ac6321a
bash jl_build.sh
```

- 하는 일
  1. 작업 사본이 없으면 SDK에서 필요한 부분만 복사
  2. 설정 파일을 매번 SDK에서 새로 복사한 뒤 고침(늘 같은 결과): 보드 `AC6321A_STAND_KEYBOARD`, 앱 `STANDARD_KEYBOARD` 자리에 시험 앱 등록, USB 장치(PC) 켬, 디버그 UART0·행렬 키·외부 MCU·블루투스 끔
     - 전원: 고치지 않음(SDK 공장값 DC-DC `DCDC15`, LVD 원래값). 앞의 LDO 패치는 "2번 핀 인덕터 없음" 오판이라 뺌(L1 있음)
  3. 시험 소스를 작업 사본의 `apps/hid/pintest/`에 넣고 보드 Makefile에서 SDK 키보드 앱 파일과 바꿔 끼움
  4. SDK make, 빌드 뒤 스크립트(`RUN_POST_SCRIPT`) 끔: `cpu/bd19/tools/download.bat`이 `isd_download -tonorflash`로 꽂힌 칩에 쓰기 때문
  5. `download.bat`의 objcopy 부분만 따라 해 `app.bin` 묶기
- `app.bin`은 **앱만**. 플래시 이미지(uboot·설정·파티션 포함)는 `isd_download`가 만듦
- 오프라인 플래시 이미지(장치 없이 `isd_download` 한 번, 사용자 승인, 2026-11-15): `build/`에 `jl_isd.bin`(원시 플래시 이미지 0x15000 B), `jl_isd.fw`, `update.ufw`. GUI 쓰기 방식은 분석 중

## 장입 방법

> ⚠️ **장입함 → 복구 불가, 보류**(2026-11-15, 사용자 승인). 리셋 뒤 USB·BLE 안 잡힘. 다음 장입은 플래시 쓰기 전에 로더로 RAM 실행 시험(`FB06` + 점프)부터. 다시 장입은 "ㄱㄱ" 뒤. 공장 펌웨어는 지워도 됨

- 상태: **장입함**(PM, 2026-11-15): `jl_isd.bin`(0x15000 B)을 BD19 RAM 로더로 씀. `FB00`으로 0·0x10000·0x20000 지움 → 4 KiB씩 `FB04` → 조각 CRC(`FC0E` = crc16 0x1021, 초기 0)·전체 `FD05` 일치, 0x3E000 BTIF 보존, `FC0C` 리셋. 리셋 뒤 USB·BLE 모두 안 잡힘(장치 관리자에 알 수 없는 장치도 없음) → **복구 불가, 보류**. 전원·클럭은 원인 아님(L1 인덕터·배터리·크리스털 24.000 MHz 확인). 남은 후보: 이 앱의 USB 시작 경로·HID 서술자(칩에서 미검증). 지금 소스는 전원 공장값(`app.bin` 72,076 B, 빌드만). GUI 쓰기는 아직 자리만. 사용자 결정 남은 1개(관리자 권한, [보드 문서](../../../../docs/board/BOARD_KEY12_JUZHEN_V01.md#사용자-결정-대기))
- 예정 경로(미검증, [보드 문서 JieLi SDK·장입](../../../../docs/board/BOARD_KEY12_JUZHEN_V01.md#jieli-sdk장입-조사))
  1. 사용자 "ㄱㄱ"
  2. 칩 ROM의 USB 다운로드 모드(UBOOT, PC에는 USB 저장장치)로 들어감. 공장 펌웨어 상태는 벤더 HID에 `03 EF EF`(확인, 2026-11-15). 우리 펌웨어 설치 뒤는 BOOT 명령(`go_mask_usb_updata()`)
  3. (권장, 필수 아님) 플래시 전체 백업: **완료**(PM, 2026-11-15, BD19 RAM 로더로 512 KiB 두 번 읽어 일치, 저장소 밖 보관, [보드 문서](../../../../docs/board/BOARD_KEY12_JUZHEN_V01.md#장입-usb-다운로드-모드uboot))
  4. 장입: `jl_isd.bin`을 RAM 로더로 씀(위 "상태", 지금은 PM 도구, GUI 쓰기는 자리만)
- 시험 펌웨어에서 UBOOT로 돌아가기: Scroll Lock 3초 안 6번(위 "동작")

## 할 일 (코드의 TODO(SDK)와 같음)

- SDK 원본으로 확인해 뺀 것: `clr_wdt()`(`asm/cpu.h`), RTOS 틱 10 ms(`configTICK_RATE_HZ` 100), `adc_get_voltage` = mV, USB 시작 `usb_start()`(HID_CLASS)·키보드 서술자

| 항목 | 내용 | 코드 위치 |
|---|---|---|
| 시간 정밀도 | `sys_timer_get_ms` 증가 단위 미확인. µs 지연 함수 미확인(1 ms 바쁜 대기로 대신) | `SDK_ms_now`, `SDK_busy_ms` |
| ADC 초기화 | `adc_init()`·추가 채널 배경 측정을 시스템이 이미 하는지(`adc_get_voltage`는 마지막 측정값) | `SDK_adc_mv`, `SDK_adc_init` |
| ADC 핀 상태 | 디지털 입력(`gpio_set_die(gpio, 1)`)인 핀을 ADC가 읽는지. 아니면 재는 동안 아날로그로 바꾸고 되돌림 | `hal_adc_report` |
| 구동 세기 | `gpio_set_hd(gpio, 0)` = 보통 구동인지(`gpio_set_hd0`도 있음) | `hal_gpio_output` 쪽 |
| 리셋 원인 | `get_reset_source_value()` 값을 비트 묶음으로 본 것이 맞는지(알려진 리셋으로 확인) | `SDK_reset_cause` |
| USB HID | `usb_start()` 전에 PC 작업 메시지(USB 꽂힘 이벤트)가 필요한지, 호스트 구성 여부, 바쁠 때 `hid_tx_data` 반환값, IN 전송 끝 기다리기(지금 2 ms 대기) | `SDK_usb_start`, `SDK_usb_ready`, `SDK_usb_send`, `sendReport` |
| 작업 | `os_task_create` 우선순위·스택 단위(지금 1, 1024) | `app_main` |
| 앱 연결 | `task_info_table[]` 항목(실물 확인) | `app_main` |
| 이미지 | 오프라인 이미지 `jl_isd.bin`(0x15000 B)을 어떻게 쓸지(주소·`isd_config.ini` 설정: 길게 누르기 리셋, USB 업데이트) | `jl_build.sh` |
| 포함 경로 | `asm/gpio.h` 등 SDK 포함 경로 | `ac6321a_hal.c` 앞 |

## 결과

- 없음(빌드·장입 전)
