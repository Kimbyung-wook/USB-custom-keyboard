# 변경 기록

- 저장소 전체의 버전별 요약. 문서마다의 자세한 변경은 그 문서의 "이력" 절
- 출시 순서: [docs/RELEASE.md](docs/RELEASE.md)

### 양식 규칙 (검사 도구가 읽음)

- `##` 절 제목: 맨 위 `## 미출시` 하나, 그 아래 `## <버전> (<YYYY-MM-DD>)`를 새 버전부터. 버전은 `X.Y.Z` 또는 `X.Y`
- `## 미출시`: 마지막 배포판 뒤부터 지금 시험판 번호(예: 1.7.1)까지의 변경. 버전 규칙은 [docs/RELEASE.md](docs/RELEASE.md#버전-규칙)
- `## 미출시` 항목 한 줄 형식:

```
- [영역] 변경 한 줄 — 문서 영향: 경로#앵커, 경로 — 반영: ☐
```

| 칸 | 규칙 |
|---|---|
| `[영역]` | `펌웨어`, `부트로더`, `GUI`, `도구`, `테스트`, `문서`, `저장소` 중 하나 |
| 변경 한 줄 | 무엇이 바뀌었나. ` — `(공백, U+2014, 공백)를 넣지 않음 |
| `문서 영향:` | 고쳐야 할 문서. 저장소 루트 기준 경로, 앵커는 `#` 뒤에 GitHub 방식(소문자, 공백 → `-`). 여러 개는 `, `로 구분. 없으면 `없음` |
| `반영:` | `☐` 아직, `☑` 문서까지 고침. 출시 전 모두 `☑` |

- 칸 구분은 ` — 문서 영향: `과 ` — 반영: `. 한 항목은 한 줄
- 출시 때 `## 미출시` 항목은 새 버전 절로 옮기고 ` — 문서 영향: …`부터 줄 끝까지 지움(버전 절은 `- [영역] 변경 한 줄`만). 미출시 절은 비워 둠(`- 없음`)
- 정규식(참고): `^- \[(펌웨어|부트로더|GUI|도구|테스트|문서|저장소)\] (.+?) — 문서 영향: (없음|[^ ,]+(, [^ ,]+)*) — 반영: (☐|☑)$`

## 미출시

- [문서] 보드 문서 파일 이름에서 의미 없는 V 삭제(사용자 지시 2026-11-13): BOARD_SAYO_3X3V_RGB.md → BOARD_SAYO_3X3_RGB.md, BOARD_SAYO_2X4V_RGB.md → BOARD_SAYO_2X4_RGB.md, 6키 BOARD_SAYO_2B3.md → BOARD_SAYO_2X3B_RGB.md(보드 이름은 SAYO_2B3 그대로). md 링크 고침 — 문서 영향: docs/board/BOARD_SAYO_3X3_RGB.md, docs/board/BOARD_SAYO_2X4_RGB.md, docs/board/BOARD_SAYO_2X3B_RGB.md, docs/BOARDS.md — 반영: ☑
- [부트로더] BT_2X3 자체 부트로더 진입 키를 키 6(PA11)에서 키 4(PA9, 아랫줄 왼쪽)로 바꿈: 다른 보드처럼 좌측 최하단(사용자 결정 2026-11-13). 부트로더 보드 파일 BOARD_BOOT_KEY_PIN 0, 9, 앱 보드 파일 BOARD_BOOT_KEY 3, host 시험이 둘이 같은 키인지 확인. 진단판(build/diag)은 키 6 그대로. 배터리 확인 키 1+6은 그대로 — 문서 영향: docs/board/BOARD_SAYO_2X3B_RGB.md#자체-부트로더-ch573, docs/board/BOARD_SAYO_2X3B_RGB.md#2-버튼-배치와-매핑, docs/BOOTLOADER_CH32V203.md#ch573-sayo_2b3 — 반영: ☑
- [부트로더] CH573 부트로더 USB를 앱(hal_ch573.c)과 같은 인터럽트 방식으로 바꿈: 기판에서 1209:C55E는 열거되나 HidUsb 시작 실패(코드 10, 재연결해도 같음). EP0 처리는 앱 setup·usbIsr 그대로(HID 클래스 GET_IDLE·GET_PROTOCOL·SET_REPORT, SET_FEATURE 받음, GET_CONFIGURATION은 설정값), 전용 HID 끝점을 앱처럼 EP2 IN·OUT으로, 인터럽트 처리기·호출 함수 모두 RAM(.highcode), 플래시 지움·쓰기·UID 읽기 동안 인터럽트 끔, usb_stop에서 USB 인터럽트 끔(앱 점프는 usb_init 전, RUN은 리셋). 모르는 표준 GET_DESCRIPTOR(qualifier·other speed·BOS·없는 문자열)에 STALL 대신 0 B 응답: 기판 진단 기록에서 Windows가 문자열 다음 qualifier를 묻고, 그 STALL 뒤 장치 디스크립터 요청의 IN 단계가 끝나지 않아 호스트가 포기(코드 10). 0 B 응답 판은 기판에서 열거·HID 시작 성공(2026-11-13). LED 그대로, 5204 B. 기판 확인(2026-11-13): 정식판 장입 → 키 4 진입 정상(코드 10 없음), hidboot info·flash(데모 앱 10176 B)·run → 앱 1209:C55D 정상. 원인 가르기 시험판 `make BUILD=build/comp BL_USB_COMPOSITE=1`(앱과 똑같은 2인터페이스 디스크립터, 기판에서 장치 설명자부터 실패) 추가, 진단판 `make BUILD=build/diag BL_USB_DIAG=1`(USB 단계·트랩을 LED로 표시. 기판: 주소까지 되고 SET_CONFIGURATION 안 옴, 사용자 모드도 같음) 추가. 진단판은 이제 USB 사건 기록(SETUP 패킷·응답·STALL, 2 s 조용하면 Data-Flash 0에 저장, 앱 실행 뒤 `firmware/bootloader_ch573/diag_read.py`로 읽음) + 키 5 함께 누르면 모르는 GET_DESCRIPTOR에 STALL 대신 빈 응답. 기록은 24 B(µs 시각, 진입 INT_FG, DEV_AD, T_LEN, 처리기 진입 수, 응답 전에 복사한 SETUP 8 B; diag2까지는 SETUP 칸이 응답 바이트였음), 일반판 5204 B 그대로 — 문서 영향: docs/board/BOARD_SAYO_2X3B_RGB.md#자체-부트로더-ch573 — 반영: ☑
- [부트로더] CH573 HELLO의 칩 UID는 ROM 명령 GET_UNIQUE_ID(0x07) 값 8 B(+0 4 B)라 wchisp가 보여주는 UID(ISP 설정 읽기의 다른 ROM 칸)와 다름. 앱 HELLO도 같은 명령이라 GUI의 "방금 연결된 키보드" 표시는 맞음. 코드 변경 없음(기판 확인 2026-11-13) — 문서 영향: docs/BOOTLOADER_CH32V203.md#ch573-sayo_2b3, docs/board/BOARD_SAYO_2X3B_RGB.md#자체-부트로더-ch573 — 반영: ☑
- [펌웨어] 보드 이름 BT_2X3 → SAYO_2B3(사용자 결정 2026-11-13: Sayo 제품 SAYO_ 양식, 2B3 = 2x3 블루투스, 앱 HELLO 이름 칸 8자 제한). 앱·부트로더 보드 파일 sayo_2b3.h, BOARD_NAME(HELLO·USB 시리얼·MKFW1 ID), 출력 macro_keyboard-SAYO_2B3*.bin·bootloader-SAYO_2B3.bin, host·CH573 핀 시험 참조. 펌웨어엔 옛 이름 없음(별칭은 gui/core/boards.py 몫). 기판의 현재 부트로더는 BT_2X3를 보고하므로 새 이름 앱은 hidboot 보드 확인에 걸림(별칭 없으면 --force 또는 wchisp full) — 문서 영향: docs/BOARDS.md#준비-중-보드, docs/board/BOARD_SAYO_2X3B_RGB.md#1-개요, docs/BOOTLOADER_CH32V203.md#ch573-sayo_2b3 — 반영: ☑
- [테스트] 동글 core PC 시험을 `uv run pytest`에 넣음: `tests/gui/test_dongle_host.py`가 `firmware/dongle_ch582/test/run.py`를 실행(gcc 없으면 건너뜀) — 문서 영향: firmware/dongle_ch582/README.md#빌드 — 반영: ☑
- [펌웨어] 배터리 추정 `src/core/battery.c`(보드 파일에 BOARD_BATTERY 있을 때만, 다른 보드 0 B): 배터리 = VDD + 650 mV(상수), 4회 평활, Li-Po 곡선 %(3.0 V=0, 4.2 V=100, BLE BAS용), 저전압 3.4 V·위험 3.1 V 플래그(히스테리시스 100 mV), CH573 VBAT 채널 읽기 `src/hal/ch573/battery_ch573.c`(-12 dB, 16회 평균), host 시험. LED 표시 없음. BT_2X3 본 펌웨어가 아직 없어 BAS 연결·측정 시점은 미적용 — 문서 영향: docs/board/BOARD_SAYO_2X3B_RGB.md#6-본-펌웨어에서의-동작, firmware/macro_keyboard/README.md — 반영: ☑
- [펌웨어] BT_2X3 상태 LED `src/core/bt_led.c`(BOARD_BT_LED 보드만, 다른 보드 0 B): 파랑 L6~L8 = BLE 슬롯 1~3(페어링 0.2 s·재연결 1 s 깜빡임, 연결 3 s, 슬롯 바꿈 1 s), 초록 L4 = 동글(연결 3 s, 페어링·재연결도 같은 깜빡임)·USB 유선(계속), 빨강 L5 = 저전압 5 s마다 50 ms·위험 3번 깜빡인 뒤 절전 요청, 흰색 L3 = 키 30 ms(끄기 플래그), 키 1+6 1 s → 파랑 개수로 잔량 2 s. 동시에 최대 3개(낮은 우선순위부터 뺌), 시간·값은 `bt_led.h` 상수. CH573 핀 구동 `src/hal/ch573/led_ch573.c`, host 시험. 본 펌웨어 없어 앱 연결은 미적용 — 문서 영향: docs/board/BOARD_SAYO_2X3B_RGB.md#4-led, firmware/macro_keyboard/README.md — 반영: ☑
- [펌웨어] BT_2X3 본 펌웨어 1단계(CH573, BLE 없음) `firmware/macro_keyboard/Makefile.ch573`: 부트로더 뒤 0x3000 링크, `src/hal/ch573/`(HAL: TMR0 ms, 키 6개, 설정 Data-Flash 0~2 KB(BLOB 2048), USB 부트 키보드 + 벤더 HID 1209:C55D, BOOT 명령 = 부트로더 진입 표시 0xB0 후 리셋, 절전), 보드 파일 `src/hal/ch573/bt_2x3.h`(GUI boards.py 등록 전이라 src/boards 밖). `src/core/bt_app.c`: 키 흰색, USB 연결 초록, 배터리 10 s마다 측정·저전압 빨강·위험 시 절전(USB 연결 중엔 안 함), 키 1+6 1 s 잔량 파랑. 시험 빌드 `DEMO=1`: 키가 입력 대신 LED 상태를 바꿈(키 1 슬롯, 2 페어링, 3 재연결, 4 연결됨, 5 동글/USB, 6 배터리 정상→부족→위험). BLE 슬롯 파랑은 USB 연결 중에도 보임. 동글↔USB 전환 때는 초록만(슬롯 파랑 1초가 끼어들던 것 고침, 고친 뒤 기판에서 정상 확인 2026-11-13). 첫 설치 이미지 `-full.bin`(mkfull.py), host 시험. 앱 12,460 B, 데모판 장입·기판 확인 — 문서 영향: docs/board/BOARD_SAYO_2X3B_RGB.md#6-본-펌웨어에서의-동작, docs/board/BOARD_SAYO_2X3B_RGB.md#4-led, firmware/macro_keyboard/README.md — 반영: ☑
- [펌웨어] BT_2X3 USB 동글(CH582F) 펌웨어 `firmware/dongle_ch582`: 일반 BLE HID 키보드에 중앙기기로 붙어 USB HID(1209:C55F 확정, 부트 키보드 + 소비자·시스템·마우스)로 넘김. 동글에 버튼·LED 없음(확정): 짝짓기 = 본딩 없음 또는 3번 빠르게 재꽂기, 본딩 지우기 = 5번 재꽂기, 탐색 결과 캐시로 바로 재연결, 끊기면 키 모두 뗌, 32 kHz 크리스털 자동 판별. core 논리 PC 시험 `firmware/dongle_ch582/test`(93개). 플래시 169,424 B, 미장입 — 문서 영향: docs/board/BOARD_SAYO_2X3B_RGB.md#5-부팅장입, docs/board/BOARD_SAYO_2X3B_RGB.md#6-본-펌웨어에서의-동작, docs/BOARDS.md#준비-중-보드 — 반영: ☑
- [테스트] CH582F 동글 핀 시험 펌웨어 `tests/hw/ch582_dongle`: USB 키보드로 사용 USB 포트, 32 kHz 크리스털 유무, 풀업·풀다운 상태, 핀별 H/L 구동(LED 찾기), 버튼 핀을 입력. 7,780 B, 미장입 — 문서 영향: docs/board/BOARD_SAYO_2X3B_RGB.md#5-부팅장입 — 반영: ☑
- [부트로더] CH573(BT_2X3) 자체 부트로더 `firmware/bootloader_ch573`: CH32V203과 같은 HID 명령(1209:C55E, HELLO/ERASE/WRITE/CHECK/RUN, hidboot 그대로), 배치 부트로더 0x0000(8 KB, 4,852 B), 앱 정보 0x2000(4 KB 블록), 앱 0x3000~0x6FFFF(446,464 B). 진입: 키 6(PA11)을 누른 채 SW1, 앱의 BOOT(`R8_GLOB_RESET_KEEP`=0xB0 뒤 소프트 리셋), 앱 정보 없음·CRC 틀림. 머무는 동안 L5 빨강(PA15) 깜빡임, 32 kHz 크리스털 기능은 켜지 않음(PA10·PA11은 키). ERASE는 정보 블록만 지우고 앱 블록은 WRITE가 처음 닿을 때 지움. HELLO [45] = 칩 ID 0x73, UID 8 B. 칩 의존은 `chip_ch57x.c`·`usb_hid.c`(CH582 재사용 대비). 0x3000에 링크한 시험 펌웨어·BLE HID_Keyboard 예제 빌드, `mkfull.py` 첫 설치 이미지 — 문서 영향: docs/BOOTLOADER_CH32V203.md, docs/board/BOARD_SAYO_2X3B_RGB.md#5-부팅장입, docs/BOARDS.md#준비-중-보드 — 반영: ☑
- [테스트] CH573 시험 펌웨어에 ADC 모드(4번째, ANODE 다음): 2초마다 칩 전원(VBAT 채널)과 PA8·PB0·PB6(AIN12·AIN8·AIN9) 전압을 mV로 입력. 공용 논리는 `HAL_HAS_ADC`가 있을 때만 이 모드를 넣음(CH32V203 바이너리 같음) — 문서 영향: tests/hw/ch573/test_ch573/README.md — 반영: ☑
- [문서] BT_2X3 LED 핀: 단색, 높음 = 켜짐, L3~L8 = PA4·PA5·PA15·PA14·PA13·PA12, L1·L2는 충전 IC(MCU 핀 아님), 키별 LED 아님(L3~L8은 옆면 상태 표시 6개), 키·LED 아닌 시험 핀 8개 — 문서 영향: docs/board/BOARD_SAYO_2X3B_RGB.md#4-led, docs/board/BOARD_SAYO_2X3B_RGB.md#3-칩-핀맵, tests/hw/ch573/test_ch573/README.md#결과 — 반영: ☑
- [문서] BT_2X3 키 핀: 키 1~6 = PB14·PB13·PB4·PA9·PA10·PA11(키마다 핀, 누르면 낮음), PA10·PA11이 키라 32 kHz 크리스털 없음. 자체 부트로더 진입 키 = 키 6(PA11, 사용자 결정) — 문서 영향: docs/board/BOARD_SAYO_2X3B_RGB.md#2-버튼-배치와-매핑, docs/board/BOARD_SAYO_2X3B_RGB.md#3-칩-핀맵, tests/hw/ch573/test_ch573/README.md#결과 — 반영: ☑
- [문서] BT_2X3 실물 확인: L5 빨강, CH573 시험 펌웨어 장입 성공(`RST O`, `MODE: KEYS`, USB HID·TMR0 동작), 공장 펌웨어 지워짐 — 문서 영향: docs/board/BOARD_SAYO_2X3B_RGB.md#4-led, docs/board/BOARD_SAYO_2X3B_RGB.md#7-확인-결과, tests/hw/ch573/test_ch573/README.md#결과 — 반영: ☑
- [테스트] CH573(BT_2X3 키보드) 핀 시험 펌웨어 `tests/hw/ch573/test_ch573`: CH32V203 시험과 같은 논리(KEYS·FIND·ANODE, 매트릭스 감지), CH573 HAL(60 MHz, TMR0 1 ms, USB HID 키보드)과 핀 20개(PB10·PB11 USB 제외). 공용 논리에 HAL·핀 표를 바꿔 끼우는 `TEST_HAL_H`·`TEST_PINS_H` 추가(CH32V203 빌드는 같은 바이너리) — 문서 영향: tests/hw/ch573/test_ch573/README.md, docs/board/BOARD_SAYO_2X3B_RGB.md#5-부팅장입, tests/hw/ch32v203/test_ch32v203/README.md#빌드 — 반영: ☑
- [문서] 12키 기판 표기 `2x6_RGB` 확인: 보드 ID `SAYO_2X6` 확정(임시 표현 삭제), 문서 파일 이름 `BOARD_SAYO_2X6.md` → `BOARD_SAYO_2X6_RGB.md`, 링크 고침 — 문서 영향: docs/board/BOARD_SAYO_2X6_RGB.md#1-개요, docs/BOARDS.md#준비-중-보드, docs/NEW_BOARD.md#9-식별-전-보드 — 반영: ☑
- [저장소] 시험판 1.7.1: 배포 뒤 수정은 revision만 올려 시험(1.7.1, 1.7.2 …), 배포가 확정되면 minor를 올리고 revision 0(→ 1.8.0) — 문서 영향: docs/RELEASE.md, CHANGELOG.md — 반영: ☑
- [문서] 보드별 작업 시간 기록(`docs/WORK_TIME.md`): 측정 방법, 보드별 표(오래된 순), 공통 기능, 간트 차트 — 문서 영향: docs/WORK_TIME.md, README.md#폴더-구조 — 반영: ☑
- [문서] BT_2X3(6키 블루투스) 사용자 관찰: 기판 `2x3M_BLE_Ver1.1`, 칩 CH573F, 안테나 핀 위치, SW1, LED L3~L8 색. 칩 표 "준비 중" 열에 CH573F 추정 값 — 문서 영향: docs/board/BOARD_SAYO_2X3B_RGB.md, docs/CHIPS.md#칩-특성-표, docs/BOARDS.md#준비-중-보드, docs/NEW_BOARD.md#9-식별-전-보드 — 반영: ☑
- [문서] SAYO_2X6(12키) 확인 결과 반영: 칩·UID·키·LED 핀, 칩 핀맵 표, 자체 부트로더 설치·LED 핀 빌드 교체, 본 펌웨어 LED 점검(빛 2개). 보드 목록·업데이트 표·부트로더 대상에 추가 — 문서 영향: docs/board/BOARD_SAYO_2X6_RGB.md, docs/BOARDS.md, README.md, docs/MANUAL.md, firmware/macro_keyboard/README.md, docs/NEW_BOARD.md, docs/BOOTLOADER_CH32V203.md, docs/CHIPS.md, tests/hw/ch32v203/test_ch32v203/README.md — 반영: ☑
- [펌웨어] 꽂을 때 LED 점검: `BOARD_LED_RING_CHASE n`이면 빛 n개가 둘레에 고르게 떨어져 돎(둘레 칸 수가 n으로 나눠져야 함). SAYO_2X6은 2개(마주 보며 돎), 다른 보드는 1개 그대로 — 문서 영향: firmware/macro_keyboard/README.md, docs/MANUAL.md, docs/board/BOARD_SAYO_2X6_RGB.md#6-본-펌웨어에서의-동작 — 반영: ☑
- [펌웨어] SAYO_2X6(12키, 2×6) 보드 파일 추가: 키 12개, 진입 키 7(PA7), 키별 RGB LED(키 8 양극 PB2, 실물 확인), 꽂을 때 LED 점검 — 문서 영향: docs/board/BOARD_SAYO_2X6_RGB.md#4-led, docs/board/BOARD_SAYO_2X6_RGB.md#6-본-펌웨어에서의-동작, docs/BOARDS.md — 반영: ☑
- [펌웨어] CH32V203 HAL·부트로더: LED 핀이 없는 보드 파일도 빌드(LED 구동 안 함) — 문서 영향: docs/board/BOARD_SAYO_2X6_RGB.md#4-led — 반영: ☑
- [GUI] 보드 목록에 SAYO_2X6 추가(키별 RGB LED, 진입 키 7) — 문서 영향: docs/BOARDS.md — 반영: ☑
- [도구] `build_fw.ps1`·`flash.ps1`에 `-Board sayo_2x6`, 12키 UID 추가 — 문서 영향: docs/board/BOARD_SAYO_2X6_RGB.md#5-부팅장입 — 반영: ☑
- [테스트] host 시험은 LED 기능이 없는 보드의 LED 검사를 건너뜀. 시험 펌웨어 `APP=1`(부트로더 뒤 0x08002000) 빌드 추가 — 문서 영향: tests/hw/ch32v203/test_ch32v203/README.md#장입-방법 — 반영: ☑
- [테스트] CH32V203 시험 펌웨어에 행·열 매트릭스 키 감지(`M` 쌍) 추가, 12키(SAYO_2X6) 시험 순서 정리 — 문서 영향: tests/hw/ch32v203/test_ch32v203/README.md#동작 — 반영: ☑
- [펌웨어] HID 장입 전에 `.bin` 식별 문자열의 보드가 부트로더 HELLO 보드와 같은지 검사, 다르거나 없으면 지우기 전에 거부(`--force`로만) — 문서 영향: docs/BOOTLOADER_CH32V203.md#설치업데이트, docs/MANUAL.md#gui에서-올리기-권장, docs/TROUBLESHOOTING.md#2-6-ch32v203-보드-업데이트-모드와-wchisp, gui/README.md#펌웨어-업데이트 — 반영: ☑
- [GUI] CH32V203 보드에서 맞지 않는 펌웨어는 빨간 경고와 "강제 장입" 확인(기본 취소) — 문서 영향: docs/MANUAL.md#gui에서-올리기-권장, gui/README.md#펌웨어-업데이트 — 반영: ☑
- [부트로더] GUI·hidboot은 부트로더 1.x만 받음(`--force`로도 안 넘어감), WRITE 최대 56 B — 문서 영향: docs/BOOTLOADER_CH32V203.md#설치업데이트 — 반영: ☑
- [도구] 두 CH32V203 Makefile에 헤더 의존성(`-MMD -MP`), 부트로더 `make full`이 `EXTRA_DEFS`를 받음 — 문서 영향: firmware/macro_keyboard/README.md#빌드, tests/hw/ch32v203/test_ch32v203/README.md#빌드 — 반영: ☑
- [도구] 환경 변수 `MK_EVT_SDK`, `MK_RISCV_BIN`, `MK_WCHISP`가 기본 도구 경로보다 먼저 — 문서 영향: firmware/macro_keyboard/README.md#빌드, tests/hw/ch32v203/test_ch32v203/README.md#빌드, docs/NEW_BOARD.md#6-빌드처음-설치확인 — 반영: ☑
- [도구] `flash.ps1`이 진입 키를 보드 파일의 `BOARD_BOOT_KEY`에서 읽음 — 문서 영향: docs/NEW_BOARD.md#5-보드-파일gui스크립트시험-추가 — 반영: ☑
- [도구] `check_device.py`가 `--board` 없이 HELLO 보드를 `boards.py`에서 찾아 비교(`--board none`은 비교 안 함) — 문서 영향: docs/MANUAL.md#처음-설치-공장-펌웨어, docs/TROUBLESHOOTING.md#진단-도구, docs/NEW_BOARD.md#6-빌드처음-설치확인 — 반영: ☑
- [테스트] `uv run pytest` 한 번으로 GUI 테스트, host 테스트(protocol.c, 모든 보드), 교차 검사(`test_consistency.py`)를 모두 실행(257개, host 검사 198개 포함) — 문서 영향: gui/README.md#테스트, firmware/macro_keyboard/README.md#pc-검사-testshost — 반영: ☑
- [펌웨어] CH32V203 보드는 저장 중 전원이 끊기면 다음에 빈 설정으로 시작(CRC 불일치, host `torn` 모드로 확인) — 문서 영향: docs/MANUAL.md#장치에-저장, docs/TROUBLESHOOTING.md#3-9-꽂았더니-설정이-비어-있음 — 반영: ☑
- [저장소] `.gitattributes` 추가, `gui/.venv` 삭제(루트 `.venv`만) — 문서 영향: 없음 — 반영: ☑
- [문서] 보드 문서 양식 통일(`docs/board/_TEMPLATE.md`), CH32V203 48핀 칩 핀맵 그림 — 문서 영향: docs/board/_TEMPLATE.md, docs/BOARDS.md#세부-문서-목차-보드-공통 — 반영: ☑
- [문서] 9키·8키 실측: `-Full`은 0x0000~0x1FFF만 지우고 설정 유지, 8키도 1.7.0 HID 장입 확인 — 문서 영향: docs/BOOTLOADER_CH32V203.md#하드웨어-시험-2026-11-07-sayo_3x3, docs/board/BOARD_SAYO_3X3_RGB.md#7-확인-결과, docs/board/BOARD_SAYO_2X4_RGB.md#7-확인-결과 — 반영: ☑
- [문서] 변경 기록(`CHANGELOG.md`)과 출시 순서(`docs/RELEASE.md`) — 문서 영향: README.md#폴더-구조 — 반영: ☑
- [문서] CH32V203 핀 번호 확인(자료표 V2.6 표 3-1-1, LQFP48/QFN48 공통, QFN48 0번 = 바닥 패드 VSS), BOOT 브리지 = BOOT0 쪽 기능 확인, 칩 핀맵 그림 문구 갱신 — 문서 영향: docs/board/BOARD_SAYO_3X3_RGB.md#3-칩-핀맵, docs/board/BOARD_SAYO_2X4_RGB.md#3-칩-핀맵, docs/PINMAP.md — 반영: ☑
- [도구] 문서 생성 구역: `tools/gen_docs.py`가 versions·boards·chips·pins-<보드> 표를 코드에서 채움(`--check`로 검사) — 문서 영향: README.md#현재-버전-2026-11-07, docs/BOARDS.md#구성, docs/BOARD_INFO.md#5-1-보드-목록-guicoreboardspy-새-파일, docs/BOARD_INFO.md#4-칩-표와-펌웨어-업데이트, docs/board/BOARD_MK6K1N.md#2-버튼-배치와-매핑, docs/board/BOARD_MK6K1NL.md#2-버튼-배치와-매핑, docs/board/BOARD_SAYO_3X3_RGB.md#2-버튼-배치와-매핑, docs/board/BOARD_SAYO_2X4_RGB.md#2-버튼-배치와-매핑, docs/board/_TEMPLATE.md, docs/RELEASE.md — 반영: ☑
- [문서] 식별 전 보드 SAYO_2X6(12키 2×6)·BT_2X3(6키 2×3 무선, 별도 USB 동글) 준비: 파일 구조 안, 보드 문서 초안과 식별 체크리스트(동글 연결 방식 확인 포함) — 문서 영향: docs/NEW_BOARD.md#9-식별-전-보드, docs/board/BOARD_SAYO_2X6_RGB.md, docs/board/BOARD_SAYO_2X3B_RGB.md, docs/BOARDS.md#준비-중-보드, README.md#폴더-구조 — 반영: ☑
- [저장소] Sayo 보드 이름 통일: 3X3V_RGB → SAYO_3X3, 2X4V_RGB → SAYO_2X4(보드 파일 sayo_3x3.h·sayo_2x4.h, `-Board sayo_3x3`·`sayo_2x4`, 산출물·그림 파일 이름). 옛 이름을 알리는 장치(부트로더 1.0, 펌웨어 1.7.0)는 `boards.py` 별칭으로 같은 보드(GUI, check_device, hidboot 보드 검사는 `--force` 없이 통과), 설정은 남음 — 문서 영향: README.md, docs/BOARDS.md, docs/BOARD_INFO.md, docs/BOOTLOADER_CH32V203.md, docs/MANUAL.md, docs/TROUBLESHOOTING.md, docs/NEW_BOARD.md, docs/PINMAP.md, docs/board/BOARD_SAYO_3X3_RGB.md, docs/board/BOARD_SAYO_2X4_RGB.md, firmware/macro_keyboard/README.md, gui/README.md, tests/hw/ch32v203/test_ch32v203/README.md — 반영: ☑
- [문서] 칩 특성 표(`docs/CHIPS.md`, CH552G·CH32V203C8U6, 자료표 값과 코드 값 구분), 보드 문서 1절은 칩 표 링크만 — 문서 영향: docs/CHIPS.md#칩-특성-표, docs/board/_TEMPLATE.md, README.md#폴더-구조 — 반영: ☑
- [문서] 9키 장치 확인: 옛 이름 부트로더에 새 이름 앱(SAYO_3X3)을 `--force` 없이 HID 장입, check_device·설정 유지·LED 눈 확인 정상 — 문서 영향: docs/board/BOARD_SAYO_3X3_RGB.md#7-확인-결과, docs/BOOTLOADER_CH32V203.md#하드웨어-시험-2026-11-07-sayo_3x3 — 반영: ☑
- [문서] 멀티에이전트 작업 방식 문서(`docs/WORKFLOW.md`): 역할·소유권, 작업 흐름, 추적 장치, 검사 관문, 승인, 도입 체크리스트 — 문서 영향: docs/WORKFLOW.md, README.md#폴더-구조 — 반영: ☑
- [문서] MK6K1N 실제 장치 1.7.0 확인: 장입·check_device·눈 확인 정상, 설정 유지, 개발 보드 UID 기록 — 문서 영향: docs/board/BOARD_MK6K1N.md#1-개요, docs/board/BOARD_MK6K1N.md#7-확인-결과 — 반영: ☑
- [문서] MK6K1NL 실제 장치 1.7.0 확인(UID 9C-B3-6E-BF): 장입·check_device·눈 확인 정상, 설정 유지 — 문서 영향: docs/board/BOARD_MK6K1NL.md#1-개요, docs/board/BOARD_MK6K1NL.md#7-확인-결과 — 반영: ☑
- [펌웨어] SAYO_3X3·SAYO_2X4·MK6K1NL 전원 인가 점검이 키 둘레를 1번 키부터 시계방향으로 흐르는 빛으로 회전(이웃한 두 칸 교차 페이드, 한 바퀴 1초, 빛은 색 바퀴 색, 색 주기·횟수는 그대로, 9키 가운데 키는 계속 켜짐, MK6K1N은 LED 1개라 그대로; `BOARD_LED_RING_CHASE 0`이면 무지개 회전) — 문서 영향: docs/board/BOARD_SAYO_3X3_RGB.md, docs/board/BOARD_SAYO_2X4_RGB.md, docs/board/BOARD_MK6K1NL.md, docs/MANUAL.md, firmware/macro_keyboard/README.md — 반영: ☑

## 1.7.0 (2026-11-07)

- [펌웨어] CH32V203C8U6 보드 지원: 3X3V_RGB(9키), 2X4V_RGB(8키). 노브 없는 보드는 키를 누를 때 입력, 아무 키 2초로 패턴 입력
- [부트로더] CH32V203 자체 HID 부트로더 1.0(`1209:C55E`), 진입 키 또는 BOOT 명령. `-Full`은 부트로더만 wchisp로 쓰고 앱은 HID로(설정 유지)
- [펌웨어] CH552 보드의 "노브를 누른 채 꽂기" 진입 삭제
- [펌웨어] 초록 LED = 키를 누르는 동안. 키마다 LED인 보드는 나머지 LED 꺼짐
- [펌웨어] 꽂을 때 색 바퀴 LED 점검을 모든 보드에서(CH552 포함)
- [GUI] CH32V203 보드와 자체 부트로더 장입(`hidboot`)

## 1.6.0 (2026-10-31)

- [펌웨어] 패턴 암호 2~8단계(전에는 4~8), 패턴 입력 중 누른 키만 잠깐 꺼짐(키마다 LED)
- [GUI] 업데이트 창에서 장입할 보드 고르기(처음 설치 포함), 입력 표시
- [펌웨어] HAL 분리(`src/core`, `src/hal`)

## 1.5.0 (2026-10-31)

- [펌웨어] MK6K1NL 보드(키마다 LED 6개 체인)
- [펌웨어] 노브 한 칸 판정: 같은 방향 2단계 이상

## 1.4.0 (2026-10-27)

- [펌웨어] 자동 잠금(설정 슬롯). 1.4.1: USB 전송 멈춤 처리
- [GUI] 자동 잠금 설정

## 1.3.0 (2026-10-26)

- [펌웨어] HELLO 보드 정보(바이트 17~35), 펌웨어 식별 문자열 `MKFW1:`. 1.3.1: 노브 길게 누르기 3초 → 2초
- [GUI] 보드 정보로 화면 구성, 업데이트 창 보드 목록·칩 표·식별 문자열 확인

## 1.2.0 (2026-10-26)

- [펌웨어] 잠긴 동안 키 무시, 노브 길게 누르기로 패턴 입력 시작, 다시 잠그기. 1.2.1: 크기 11,587 → 8,151바이트

## 1.1.0 (2026-10-26)

- [펌웨어] 패턴 암호(프로토콜 v2), 부트로더 진입 LED 보라
- [GUI] 패턴 암호 설정

## 1.0.0 (2026-10-26)

- [펌웨어] 키마다 긴 문자열 HID 입력(MK6K1N)
- [GUI] 슬롯 편집, 장치에 저장·장치에서 읽기
