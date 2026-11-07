# CH32V203 이식 계획

> 문서 성격: **이식 기록** (결정·점검 결과). 보드별 값은 [BOARDS.md](BOARDS.md), 사용 절차는 [MANUAL 8장](MANUAL.md#8-펌웨어-업데이트)

- 두 보드 모두 완료 (2026-11-07): 9키 [3X3V_RGB](BOARD_3X3V_RGB.md), 8키 [2X4V_RGB](BOARD_2X4V_RGB.md)
- 제품: SayoDevice 계열 (8키 = Keyb T8B7). 키마다 3색 한 칩 RGB LED

## 9키 (3X3V_RGB): 상태 (2026-11-07)

- 펌웨어 **구현·빌드 완료** (약 12 KB, 경고 0). 호스트 테스트, GUI 테스트 통과
- 하드웨어 장입·동작 확인 (2026-11-07): 키, GUI, 잠금, LED, 자체 부트로더(설치·업데이트·복구)
- 남은 일: 칩 ID 확인(아래 8번), LED 발열
- 빌드: `tools\build_fw.ps1 -Board 3x3v_rgb` (앱 + 부트로더 + 전체 이미지, [BOOTLOADER_CH32V203.md](BOOTLOADER_CH32V203.md) 빌드). 앱만: `cd firmware/macro_keyboard; mingw32-make -f Makefile.ch32v203` (PATH에 xPack gcc) → `build/ch32v203/macro_keyboard-3X3V_RGB.bin`
- 정한 동작
  - 1줄 LED도 R·G·B 모두 사용: USB는 USBFS(PB6/PB7), PA11/PA12는 안 쓰는 USBD 핀 (실물 확인 2026-11-07: 1줄 빨강·초록·흰색, USB 정상)
  - 잠금(패턴 암호)·자동 잠금 있음 (2026-11-06 추가). 노브가 없어 **아무 키 2초**로 패턴 입력 시작
    - 패턴 단계는 키만 (A~I)
    - 키는 **누를 때** 입력 (2026-11-07 변경). 풀린 상태에서 2초 눌러 다시 잠그면 그 키가 한 번 입력됨
    - 실물 확인 (2026-11-07, v1.7.0): 저장 → 다시 꽂으면 잠김 → 2초(2·3줄 빨강) → 패턴으로 풀림(파랑) → 다시 잠김
    - 구현: `src/core/input.c`
  - 시작 때 남는 핀을 모두 풀업 (`hal_init`). 안 하면 LED가 전혀 안 켜짐 (2026-11-07, [BOARD_3X3V_RGB.md](BOARD_3X3V_RGB.md) LED)
  - 자체 HID 부트로더 (2026-11-07, 하드웨어 시험 완료): 키 7 누른 채 꽂기 또는 BOOT 명령 → 브리지 없이 GUI 업데이트, 설정 유지. [BOOTLOADER_CH32V203.md](BOOTLOADER_CH32V203.md)
  - LED 키별 구동 (2026-11-07): 애노드 핀 9개를 직접 구동, 누른 키만 초록. 색별 밝기 보정 R 10 / G 7 / B 14 (16단계 중). 실물 확인: 밝아짐, 키별 초록·잠금 색 정상. 저장 흰색 맞음(실물 확인, R 12 / G 3 / B 8 → R 12 / G 5 / B 14 → R 10 / G 7 / B 14). 발열은 모름
    - 이후 (2026-11-07): 64단계 PWM(TIM3 약 67 kHz), 세 색 같은 최대 듀티 56/64, 색별 보정 없앰 ([BOARD_3X3V_RGB.md](BOARD_3X3V_RGB.md) LED)
  - 색마다 차례로 켬(동시에 켜면 빨강만 보임, [BOARD_3X3V_RGB.md](BOARD_3X3V_RGB.md) LED)

### 구현된 것

| 영역 | 내용 |
|---|---|
| 기능 코드 | 노브 0개 허용(`BOARD_KNOBS 0`), 키 상태 16비트(`hal_sw_t`), 저장 후 `hal_store_flush` |
| HAL | `src/hal/ch32v203/`: TIM2 시간, 키 9개, TIM3 소프트웨어 PWM LED, 플래시 끝 4 KB(256 B 페이지 16개, 0x0800F000~) 저장(RAM 사본), 바뀐 페이지만 지우고 씀, 칩 UID, 자체 부트로더로 재시작(`hal_enter_bootloader`) |
| USB | EVT USBFS 드라이버 수정본(`src/hal/ch32v203/usb/`): 키보드 + Vendor HID(64바이트) |
| GUI | 보드 목록(연결 안 됨이면 선택, 연결되면 자동 전환), `3X3V_RGB` 보드·칩 `0x19` 추가, 업데이트 창에서 자체 부트로더로 `.bin` 장입(`gui/core/hidboot.py`) |
| 부트로더 | `firmware/bootloader_ch32v203/` (8 KB, Vendor HID `1209:C55E`). [BOOTLOADER_CH32V203.md](BOOTLOADER_CH32V203.md) |

### 할 것

1. ✅ 장입, 아래 "확인할 것" 시험 (2026-11-07). 8번 칩 ID만 남음
2. 결과를 문서에 반영: `BOARD_INFO.md` 칩 ID, 핀 그림(그림은 사용자 추가)
3. ✅ `tools/build_fw.ps1`에 CH32V203 빌드 추가

### 확인할 것 (장입 후, 순서대로. 8번 외 확인 완료)

| # | 항목 | 확인 방법 | 안 되면 |
|---|---|---|---|
| 1 | USB 인식 | 장치 관리자에 키보드 + HID 장치 | 클럭·디스크립터 점검 |
| 2 | 키 9개 입력 | 메모장에서 기본 설정 입력 확인 | 핀·풀업 점검 |
| 3 | GUI 연결 | 보드 `3X3V_RGB`, 키 9개 화면으로 자동 전환 | Vendor HID 응답 점검 |
| 4 | 설정 쓰기·저장 | GUI 저장 → 뽑았다 꽂은 뒤 유지 | 저장 영역 주소(0x0800F000~0x0800FFFF) 점검 |
| 5 | 긴 문자열 입력 | 수백 자 매크로가 빠짐없이 입력 | 키 전송 타이밍 점검 |
| 6 | LED | 2·3줄 색, 1줄 파랑, 색 밝기 균형 | PWM 듀티 조정 |
| 7 | USB + PWM 공존 | 입력 중 LED 깜빡임·끊김 없음 | TIM3 주기·우선순위 조정 |
| 8 | 칩 ID | GUI의 칩 표시 (`0x19`는 추정값) | `wchisp probe` 값으로 수정 |
| 9 | 시간 | 키 반복·LED 시간 정상 (TIM2 1 kHz) | `hal_ms` 점검 |

- 보드 자료: [BOARD_3X3V_RGB.md](BOARD_3X3V_RGB.md)

### 남은 의문 (기능에는 영향 적음)

- 해결(2026-11-07): LED 애노드 = 키마다 핀 1개 ([BOARD_3X3V_RGB.md](BOARD_3X3V_RGB.md) LED). 원본 8키도 키별 동작·밝음 → 8키도 같은 방식일 가능성
- 애노드를 직접 구동할 때 전류 제한 저항이 있는지 모름 → 밝기·발열 확인 필요

### 키보드 없이 할 것 (준비 작업)

| # | 작업 | 내용 | 끝난 기준 |
|---|---|---|---|
| A ✅ | host_test 9키 구성 | `run.py`에 `3X3V_RGB` 시험 16개(키 9개, 동시 입력, 떨림, 저장, 잠금 거부). 잠금 없는 보드는 펌웨어가 패턴 암호를 거부(`BAD_ARG`), GUI도 저장 막음, HELLO에 자동 잠금 비트 안 보냄 | 완료 (2026-11-06) |
| B ✅ | 빌드 스크립트 | `tools/build_fw.ps1 -Board 3x3v_rgb` → `Makefile.ch32v203` 호출(빌드 날짜 포함, 매번 새로 빌드) | 완료: `build/ch32v203/macro_keyboard-3X3V_RGB.bin` 11 KB |
| C ✅ | 장입 스크립트 | `tools/flash.ps1 -Board 3x3v_rgb`: 자체 부트로더로 HID 장입(`-Enter`: 앱에 BOOT). `-Full`: 브리지 안내 → `wchisp info` → `yes` 입력 시 전체 이미지 `wchisp flash` | 완료, 실제 장입 확인 (2026-11-07) |
| D ✅ | HAL 코드 점검 | 아래 "HAL 점검 결과" | 완료 (2026-11-06) |
| E ✅ | 확인용 도구 | `tools/check_device.py`: HELLO·보드·칩·설정 읽기. `--write`(백업 후 시험 문자열) → 재연결 → `--verify` → `--restore <백업>`, `--long N` | 완료, 시험 7개 |
| F ✅ | GUI 업데이트 창 | (처음) CH32V203은 장입 불가 안내 → (2026-11-07) 자체 부트로더로 `.bin` 장입, [업데이트 모드로 전환] 사용 | 완료, 실제 장치 확인 (2026-11-07) |
| G ✅ | 사용자 문서 | `MANUAL.md` 10장, `TROUBLESHOOTING.md` 2-6·3-8·진단 도구 | 완료 |
| H | 참고 키보드 점검표 | [BOARD_3X3V_RGB.md](BOARD_3X3V_RGB.md) "참고 키보드로 확인할 것" | 준비됨 |

- 권장 순서: A → D → E → B·C → F → G

#### HAL 점검 결과 (D)

| 항목 | 위험 | 조치 |
|---|---|---|
| 플래시 지우기·쓰기 중 인터럽트 | 인터럽트가 플래시에서 실행되면 오동작 | 앞뒤로 `__disable_irq`/`__enable_irq` (SDK 예제와 같음). 수 ms 동안 LED PWM 멈춤, USB는 NAK |
| `hal_ms` 65 ms 제한 | TIM2 1 MHz가 65 ms마다 돌아 호출이 늦으면 시간 손실 | TIM2를 1 kHz로 → `TIM2->CNT`가 곧 ms (65536 ms 순환, 제한 없음) |
| 저장 실패 | 검증 실패 때 RAM 사본이 플래시와 달라짐 | 실패하면 플래시에서 사본 다시 읽음 → `ST_FLASH_ERR` 후 실제 저장값 사용 |
| 빈 플래시 | 지운 값이 0xE339E339 (0xFF 아님) | CRC 불일치로 기본 설정 사용 (수정 불필요) |
| 이미지가 설정 페이지 침범 | 링커 스크립트는 64 KB 전체 허용 | Makefile에서 `.bin`이 상한을 넘으면 실패: `BOOTLOADER=1`(기본) 52992바이트, `BOOTLOADER=0` 61440바이트 (지금 약 12 KB) |
| 플래시 접근 클럭 | `FLASH_Access_Clock_Cfg` | SDK 예제는 96 MHz에서도 안 씀 → 48 MHz는 불필요 |
| 응답 대기 중 USB 버스 리셋 | 요청 처리 중 리셋 | 드라이버가 `Vendor_Req`·EP2 상태 초기화 → 위험 낮음 |
| 칩 ID | wchisp는 칩 `0x31`, 계열 `0x19`로 표시할 가능성 | 장입 때 `wchisp probe` 값으로 확정 (확인할 것 8번) |
- 하드웨어가 필요한 일: 칩 ID("확인할 것" 8), 참고 키보드 LED 확인

## 8키 (2X4V_RGB): 완료 (2026-11-07)

- 핀: [BOARD_2X4V_RGB.md](BOARD_2X4V_RGB.md) (test_ch32v203 KEYS·FIND·ANODE)
- 보드 파일 `src/boards/2x4v_rgb.h`, 빌드 `toolsuild_fw.ps1 -Board 2x4v_rgb`, GUI 보드 목록 추가
- 하드웨어 확인: LED 점검, 키 1~8, 잠금, HELLO, 자체 부트로더(키 5) 설치·HID 장입
- 함께 고친 것: 부트로더 빌드 목적 파일을 보드별로(`build/obj-<보드>`), USB 시리얼을 보드 이름으로(전에는 `3X3V_RGB` 고정)

## 장입 절차

- 처음 한 번: BOOT 브리지 연결 → USB 연결 → `tools\flash.ps1 -Board 3x3v_rgb -Full`(부트로더 + 앱, ⚠️ 설정도 지워짐) → 브리지 제거 → USB 재연결
- 이후: 왼쪽 아래 키(9키 7, 8키 5) 누른 채 꽂거나 GUI [업데이트 모드로 전환] → GUI 업데이트 창 또는 `tools\flash.ps1 -Board 3x3v_rgb` (설정 유지)
- 자세히·복구: [BOOTLOADER_CH32V203.md](BOOTLOADER_CH32V203.md) "설치·업데이트"

## 메모리와 저장 용량 (2026-11-06)

- 칩: CH32V203C8U6, 코드 플래시 64 KB, RAM 20 KB (EVT `Link.ld`, 스택 2 KB)
- 설정 블롭: `BLOB_CAPACITY` 4096 (`config.h`, `Makefile.ch32v203`의 `STORE_SIZE`와 같아야 함)
  - 플래시 0x0800F000~0x0800FFFF. 앱은 0x08002000부터 최대 52992 B (앞 8 KB 부트로더, [플래시 배치](BOOTLOADER_CH32V203.md#플래시-배치-64-kb-srchalch32v203boot_layouth))
  - RAM에 사본 3개(storage blob·stage, HAL shadow) = 12 KB
- 빌드 결과: text 11060, bss 14876 → RAM 약 5 KB 남음. RAM이 한계라 4 KB가 실용 최대
- 문자열 합계 최대: 9키 4096 − 8 − 9×2 = **4070자**, 8키 4096 − 8 − 8×2 = **4072자**
  - 패턴 암호·자동 잠금을 쓰면 그만큼(슬롯 길이 2바이트씩 + 암호 2~8 + 설정 1바이트) 줄어듦
- 블롭 위치·길이는 `blob_pos_t`(용량 > 255면 16비트). CH552는 8비트 그대로라 코드 크기 같음
- HELLO 7~8바이트가 4096을 알려 GUI가 자동으로 맞춤. 프로토콜 변경 없음
- 시험: host_test `3X3V_RGB` 4096바이트 블롭 4070자 입력, 4097바이트 거부, 300자 슬롯
