# 지원 보드 요약

- 보드마다 다른 점만 모은 표. 사용법·업데이트 절차는 공통 문서 [MANUAL.md](MANUAL.md)
- 핀·LED·확인 결과는 보드별 세부 문서
- 보드 이름 = HELLO 보드 이름 = 펌웨어 보드 파일(`firmware/macro_keyboard/src/boards/<보드>.h`) = GUI 보드 목록(`gui/core/boards.py`)

## 구성

| 보드 | 칩 | 키 / 노브 / 행 | LED | 저장 용량 (문자열 합계 최대) | 세부 문서 |
|---|---|---|---|---|---|
| **MK6K1N** | CH552G | 6 / 1 / 2 | WS2812 1개 | 128바이트 (102자) | [BOARD_MK6K1N.md](board/BOARD_MK6K1N.md) |
| **MK6K1NL** | CH552G | 6 / 1 / 2 | WS2812 키마다 (6개 체인) | 128바이트 (102자) | [BOARD_MK6K1NL.md](board/BOARD_MK6K1NL.md) |
| **3X3V_RGB** | CH32V203C8U6 | 9 / 0 / 3 | 키마다 RGB (색별 캐소드 + 키별 애노드) | 4096바이트 (4070자) | [BOARD_3X3V_RGB.md](board/BOARD_3X3V_RGB.md) |
| **2X4V_RGB** | CH32V203C8U6 | 8 / 0 / 2 | 키마다 RGB (9키와 같은 방식) | 4096바이트 (4072자) | [BOARD_2X4V_RGB.md](board/BOARD_2X4V_RGB.md) |

- 노브 없는 보드: 패턴 입력 시작은 **아무 키 2초**, 단계는 키만, 키는 누를 때 입력 ([MANUAL 10장](MANUAL.md#10-노브-없는-보드에서-다른-점))
- 패턴 암호·자동 잠금: 모든 보드에 있음
- 키마다 LED인 보드: 풀린 상태에서 누르는 동안 누른 키만 초록, 나머지는 꺼짐(노브는 모두 초록)
- 꽂을 때 LED 점검(`BOARD_LED_SELF_TEST`): 모든 보드. 그동안 키 입력은 되지만 키 LED 표시는 없음. 색 바퀴(빨강→노랑→초록→청록→파랑→보라→빨강)를 한 바퀴 3초씩 2번 → 0.3초 꺼짐 (약 6.3초)

### 키 배치 그림

| MK6K1N | MK6K1NL |
|---|---|
| ![MK6K1N 키 배치](images/buttons_mk6k1n.svg) | ![MK6K1NL 키 배치](images/buttons_mk6k1nl.svg) |
| **3X3V_RGB** | **2X4V_RGB** |
| ![3X3V_RGB 키 배치](images/buttons_3x3v_rgb.svg) | ![2X4V_RGB 키 배치](images/buttons_2x4v_rgb.svg) |

- 주황 = 부트로더 진입 키. 그림 다시 그리기: [BOARD_MK6K1N.md](board/BOARD_MK6K1N.md) "그림 다시 그리기"

## 펌웨어 업데이트

| 보드 | 부트로더 | 진입 (누른 채 꽂기) | 처음 설치 (공장 펌웨어) | 장입 도구 | 펌웨어 파일 |
|---|---|---|---|---|---|
| **MK6K1N** | WCH ROM (USB `4348:55E0`) | **키 D**(P1.5) | 키 E를 누른 채 꽂기 | GUI(vnproch55x), `flash.ps1 -Hex` | `macro_keyboard-MK6K1N.hex` |
| **MK6K1NL** | WCH ROM (USB `4348:55E0`) | **키 D**(P1.5) | D+ 풀업: 칩 12번–16번에 1.5~2kΩ | GUI(vnproch55x), `flash.ps1 -Hex` | `macro_keyboard-MK6K1NL.hex` |
| **3X3V_RGB** | 자체 HID (USB `1209:C55E`) | **키 7**(왼쪽 아래, PB2) | BOOT 브리지 + `flash.ps1 -Board 3x3v_rgb -Full` → 키 7 누른 채 다시 꽂기 | GUI(hidboot), `flash.ps1 -Board 3x3v_rgb` | `ch32v203/macro_keyboard-3X3V_RGB.bin` |
| **2X4V_RGB** | 자체 HID (USB `1209:C55E`) | **키 5**(왼쪽 아래, PA5) | BOOT 브리지 + `flash.ps1 -Board 2x4v_rgb -Full` → 키 5 누른 채 다시 꽂기 | GUI(hidboot), `flash.ps1 -Board 2x4v_rgb` | `ch32v203/macro_keyboard-2X4V_RGB.bin` |

- 모든 보드: 잠금 풀린 키보드는 GUI [업데이트 모드로 전환] 단추(BOOT 명령)으로 키 없이 진입. 업데이트 모드 LED는 **보라**
- 펌웨어 파일 위치: `firmware/macro_keyboard/build/` (`tools\build_fw.ps1 -Board <보드>`)
- 앱 USB: 모든 보드 `1209:C55D`
- 펌웨어 최대 크기: CH552 14,336바이트, CH32V203 52,992바이트(앱 영역, [플래시 배치](BOOTLOADER_CH32V203.md))
- 설정 유지: CH552는 DataFlash라 항상 유지. CH32V203은 자체 부트로더 업데이트와 `-Full`(부트로더만 wchisp, 앱은 HID) 모두 유지. 공장 펌웨어(읽기 보호)는 처음 설치 때 전부 지워짐
- ⚠️ CH32V203 보드의 제조사 펌웨어는 읽기 보호 → 처음 설치 때 지워짐(되돌릴 수 없음)

## 새 보드 추가

- 전체 절차(식별, 안전, 진입 방법, 핀 찾기, 설치·확인): [NEW_BOARD.md](NEW_BOARD.md). 아래는 요약

1. 칩 확인 (**다운로드는 하지 않음**)
   - CH55x: WCHISPTool 칩 목록, 또는 D+ 풀업으로 업데이트 모드에 넣고 업로더 출력의 `Found Device`
   - CH32V203: BOOT 브리지 + `wchisp info` (읽기 보호 상태만 확인)
2. 시험 펌웨어로 키 핀, 노브 신호, LED 순서·극성 확인
   - MK6K1N 계열: [test_pinmap](../tests/hw/mk6k1n/test_pinmap/README.md)
   - LED가 여러 개(CH552): [test_mk6k1nl](../tests/hw/mk6k1nl/test_mk6k1nl/README.md)
   - CH32V203·핀을 전혀 모를 때: [test_ch32v203](../tests/hw/ch32v203/test_ch32v203/README.md). GPIO 전부 감시, LED 핀 탐색
3. 추가할 것 ([BOARD_INFO 6~7절](BOARD_INFO.md))
   - 보드 파일 `src/boards/<보드>.h` (기판이 다르면 `<칩>_<배치>.h` 공통 파일도). CH32V203은 부트로더 진입 키 `BOARD_BOOT_KEY`
   - GUI 보드 목록 `gui/core/boards.py`
   - 이 문서의 표 두 개, 세부 문서 `board/BOARD_<보드>.md`(아래 목차)

### 세부 문서 목차 (보드 공통)

1. 개요: 제품, 칩, 구성, 보드 파일, 확인 근거
2. 키 배치 (그림: `tools/draw_pinmap.py`로 그려 `docs/images/`에 둠)
3. LED
4. 부팅·장입: 진입 방법, 처음 설치
5. 본 펌웨어에서의 동작
6. 확인 결과
