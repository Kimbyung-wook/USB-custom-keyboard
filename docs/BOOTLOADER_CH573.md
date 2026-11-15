# CH573 자체 부트로더

> 문서 성격: **설계** + 개발자용 절차. 명령 형식은 CH32V203 부트로더와 같음 → [BOOTLOADER_CH32V203.md](BOOTLOADER_CH32V203.md#usb명령). 보드 쪽 내용(핀, 전원, ROM 부트로더 관찰): [BOARD_SAYO_2X3B_RGB 5절](board/BOARD_SAYO_2X3B_RGB.md#5-부팅장입)

- 목적: ROM 부트로더(PB22 점퍼) 없이 USB로 펌웨어 업데이트
- 대상: SAYO_2B3(6키 블루투스, CH573F). 코드 `firmware/bootloader_ch573`
- 상태: 설치·하드웨어 확인(2026-11-13): 키 4 진입, `hidboot` info·flash(데모 앱 10,176 B)·run → 앱 `1209:C55D` 실행. 부트로더 5,204 B

## 결정

- 명령·리포트는 CH32V203 부트로더와 같음(HELLO/ERASE/WRITE/CHECK/RUN, 64 B 리포트) → `gui/core/hidboot.py`·GUI 업데이트 창 그대로
- USB ID `1209:C55E`(앱은 `1209:C55D`)
- 진입 키: **키 4(PA9, 아랫줄 왼쪽)**: 다른 보드처럼 좌측 최하단(사용자 결정 2026-11-13, 전에는 키 6)
- LED: 머무는 동안 **L5 빨강(PA15) 깜빡임**(1초 주기, 지우기·쓰기 직후 빠르게). 다른 LED는 리셋 상태(꺼짐)
- 32 kHz 크리스털 기능은 켜지 않음(PA10·PA11은 키 5·6)
- 보드 의존은 보드 파일 `boards/sayo_2b3.h`(`BOARD_NAME`, `BOARD_BOOT_KEY_PIN`, `BOARD_BOOT_LED_PINS`, `BOARD_BOOT_LED_HIGH`). 칩 의존은 `chip_ch57x.c`·`usb_hid.c`(CH582 재사용 대비: `CHIP_ID`와 ISP 라이브러리만 다름, 추정)

## 플래시 배치 (448 KB, `boot_layout.h`)

| 주소 | 크기 | 내용 |
|---|---|---|
| 0x00000 ~ 0x01FFF | 8 KB | 부트로더 |
| 0x02000 ~ 0x02FFF | 4 KB(지움 단위 1개) | 앱 정보: 매직 `APP1`, 길이, CRC32(zlib), 검사값(앞 16 B만 씀) |
| 0x03000 ~ 0x6FFFF | 446,464 B | 앱(시작 주소 0x3000) |

- 코드 플래시는 4 KB 단위로 지움, 쓰기 버퍼 256 B
- Data-Flash(32 KB, 따로)는 안 건드림: 앱 설정과 BLE 본딩 영역이 여기 있음 → 업데이트해도 유지
- 앱은 링크 스크립트의 `ORIGIN`만 0x3000으로 바꾸면 됨(SDK 시작 코드 그대로)
- Makefile과 `mkfull.py`는 숫자를 `boot_layout.h`에서 읽음

## 시작 순서

1. 진입 표시 확인: `R8_GLOB_RESET_KEEP`(소프트 리셋에도 남고 전원을 켜면 지워짐) = 0xB0이면 머묾(표시는 지움). 앱의 BOOT 명령이 0xB0을 쓰고 USB를 끊고 200 ms 뒤 리셋
2. 진입 키: PA9 풀업, 1 ms 간격 20번 모두 낮음이면 머묾. 읽은 뒤 핀은 리셋 상태로
3. 앱 정보가 맞으면(매직·검사값·길이·CRC32) 핀·타이머를 리셋 상태로 돌리고 0x3000으로 점프
4. 아니면 부트로더에 머묾: USB 연결, L5 깜빡임, 명령 대기

## USB·명령

- 장치: 제조사 "MacroKeyboard", 제품 "MacroKeyboard Bootloader", 벤더 HID 인터페이스 1개(usage page 0xFF00), EP2 IN·OUT 64 B, 1 ms
- 인터럽트 방식: EP0 처리는 앱(`src/hal/ch573/hal_ch573.c`)과 같음. 처리기와 거기서 부르는 함수는 RAM(`.highcode`)
- 플래시 지움·쓰기·UID 읽기 동안 인터럽트 끔(그동안 USB는 NAK)
- CH32V203과 다른 점
  - ERASE는 앱 정보 블록만 지우고 바로 응답. 앱 블록은 WRITE가 처음 닿을 때 지움
  - CHECK는 ERASE 뒤 쓴 범위 안의 길이만 받음. CRC가 맞으면 앱 정보를 씀
  - HELLO [45] = 칩 ID 0x73, UID [21..28] 8 B([29..32]는 0)
  - HELLO UID는 ROM 명령 GET_UNIQUE_ID(0x07) 값 → `wchisp info`의 UID와 **다름**. 앱 HELLO도 같은 명령이라 GUI의 "방금 연결된 키보드" 표시는 맞음(기판 확인, 2026-11-13)
- RUN: 응답을 보낸 뒤 USB 인터럽트 끄고 끊음 → 200 ms → 소프트 리셋(진입 표시가 없으니 앱으로)

## 빌드

- `firmware/bootloader_ch573`에서 `mingw32-make` → `build/bootloader-SAYO_2B3.bin`(8 KB를 넘으면 오류). 도구 경로는 `MK_CH57X_SDK`, `MK_RISCV_BIN`, `MK_WCHISP`
- 다른 목표

| 명령 | 결과 |
|---|---|
| `mingw32-make test-app` | 0x3000에 링크한 핀 시험 펌웨어(`tests/hw/ch573/test_ch573`) |
| `mingw32-make ble-app` | 0x3000에 링크한 SDK 예제 BLE `HID_Keyboard` |
| `mingw32-make full APP=test` 또는 `APP=ble` | `build/full-SAYO_2B3-<APP>.bin`: 부트로더 + 앱 정보 + 앱(`mkfull.py`) |
| `mingw32-make flash APP=…` | 위 이미지를 wchisp로 씀(ROM 부트로더 상태여야 함) |

- 본 펌웨어: `tools\build_fw.ps1 -Board sayo_2b3` → 앱 `build/ch573/macro_keyboard-SAYO_2B3.bin`, 이 부트로더, 첫 설치 이미지 `…-SAYO_2B3-full.bin`을 한 번에(`Makefile.ch573 full`)
- Makefile을 고친 뒤에는 `rm -rf build`

## 설치·업데이트

- 보통은 `tools\flash.ps1 -Board sayo_2b3`(업데이트) / `-Full`(처음 설치·복구). 아래는 그 스크립트가 하는 일과 직접 하는 방법

### 처음 설치·복구 (ROM 부트로더, wchisp)

1. USB를 꽂은 채 **PB22를 GND에 대고 SW1 켜기** → ROM 부트로더 `4348:55E0`(BTVER 02.90)
   - 드라이버: WinUSB(Zadig). CH375 드라이버면 wchisp가 못 엶
   - 시간 제한: 통신이 없으면 약 6~15초 뒤 앱으로 돌아감(2026-11-13 관찰). 그 안에 다음 단계. 정확한 시간은 확인 필요
2. `wchisp info`로 칩 CH573 확인(읽기만)
3. `wchisp flash <full 이미지>`(`mingw32-make flash APP=…` 또는 본 펌웨어 `…-full.bin`)
- `flash.ps1 -Board sayo_2b3 -Full`: 먼저 실행해 두고 안내가 나오면 진입 → Enter. `wchisp info`의 UID가 개발 보드 `1A-73-38-72-AB-3C-FD-21`이 아니면 쓰지 않고 멈춤
- 이 기판은 읽기 보호가 켜져 있었음 → 처음 장입이 공장 펌웨어를 지움(되돌릴 수 없음). 장입은 사용자 승인 뒤

### 업데이트 (자체 부트로더, hidboot)

- `flash.ps1 -Board sayo_2b3`: SW1을 끄고 키 4를 누른 채 켜기(또는 `-Enter`) → L5 깜빡이면 Enter → `hidboot`로 앱 `.bin` 씀. GUI 업데이트 창도 같은 안내

1. 부트로더로 들어감: **키 4를 누른 채 SW1 켜기**(L5 빨강 깜빡임), 또는 앱이 돌고 있으면 `--enter`(BOOT 명령)
2. 저장소 폴더에서

```bash
uv run python -m gui.core.hidboot info
uv run python -m gui.core.hidboot flash firmware/macro_keyboard/build/ch573/macro_keyboard-SAYO_2B3.bin
uv run python -m gui.core.hidboot run
```

- `info`: 부트로더 버전, 보드 이름, UID, 앱 유무. `flash`: 지움 → 쓰기 → CHECK → RUN. `run`: 앱 실행
- `--enter`: 먼저 돌고 있는 앱에 BOOT를 보냄. SAYO_2B3 앱에서 쓴 기록은 확인 필요
- `--force`: 다른 보드·식별 문자열 없는 이미지(시험 펌웨어, `test-app`)도 씀

### 보드 이름 확인과 별칭

- `flash`는 이미지의 식별 문자열(`MKFW1:<보드>:…`) 보드가 HELLO 보드와 같아야 씀(다르면 지우기 전에 거부)
- 옛 이름 `BT_2X3`: 이름을 바꾸기 전에 설치한 부트로더가 보고함. `gui/core/boards.py` SAYO_2B3의 `aliases`로 같은 보드로 봄 → 새 이름 앱도 `--force` 없이 통과(기판 확인: 부트로더가 `BT_2X3`을 보고하는 상태에서 `hidboot`로 SAYO_2B3 앱 129,464 B·129,792 B 장입 성공, 2026-11-13~14)
- 별칭 없이 막히면 `--force`, 또는 새 부트로더가 든 full 이미지를 wchisp로 다시 씀

## Windows 코드 10 (고친 문제, 2026-11-13)

- 증상: 키 4 진입, 장치는 열거되나 "이 장치를 시작할 수 없습니다(코드 10)". 다시 꽂아도 같음
- 원인: Windows가 문자열 디스크립터 다음에 device qualifier를 GET_DESCRIPTOR로 물음 → 부트로더가 STALL → 그다음 SETUP(장치 디스크립터)의 IN 단계가 끝나지 않아 호스트가 포기
- 해결: 모르는 표준 GET_DESCRIPTOR(qualifier, other speed, BOS, 없는 문자열)에 **STALL 대신 0 B 응답**. USB 처리도 앱과 같은 인터럽트 방식으로 바꿈
- 확인: 고친 정식판 장입 → 키 4 진입 정상(코드 10 없음), hidboot info·flash·run 정상(2026-11-13)

## 진단 빌드 (평소 안 씀)

- `mingw32-make BUILD=build/diag BL_USB_DIAG=1`
  - 상태 LED로 USB 단계 표시: L3 STALL, L4 SETUP, L6 주소, L7 구성, L8 리포트 디스크립터. 트랩이면 6개 모두 켜짐(L5 깜빡임 멈춤)
  - USB 사건 기록: 헤더 16 B + 기록 24 B씩(종류, 결과, µs 시각, 레지스터, SETUP 8 B). 호스트가 2초 조용하면 Data-Flash 0~2047(앱 설정 자리)에 저장
  - 진입 키는 보드 파일 그대로. 키 5(PA10)를 함께 누르면 모르는 GET_DESCRIPTOR에 0 B 응답, 안 누르면 예전처럼 STALL(비교용)
  - 읽기: 진입 키 없이 켜서 앱을 띄운 뒤 저장소 폴더에서 `uv run python firmware/bootloader_ch573/diag_read.py [--raw out.bin]`(읽기만). 기록은 다음 설정 쓰기 때 덮어씀
- `mingw32-make BUILD=build/comp BL_USB_COMPOSITE=1`: 앱과 똑같은 2인터페이스 디스크립터(부트 키보드 + 벤더 HID)를 `1209:C55E`로. 디스크립터 탓인지 가르는 시험판

## 이력

- 2026-11-14: `build_fw.ps1`·`flash.ps1 -Board sayo_2b3`(-Full은 UID 확인) 반영, 별칭은 보드 목록 `aliases`로
- 2026-11-13: 문서 만듦(BOOTLOADER_CH32V203.md의 CH573 절을 옮겨 늘림). 보드 이름 SAYO_2B3(옛 이름 BT_2X3)
