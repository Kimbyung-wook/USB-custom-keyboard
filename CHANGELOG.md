# 변경 기록

- 저장소 전체의 버전별 요약. 문서마다의 자세한 변경은 그 문서의 "이력" 절
- 출시 순서: [docs/RELEASE.md](docs/RELEASE.md)

### 양식 규칙 (검사 도구가 읽음)

- `##` 절 제목: 맨 위 `## 미출시` 하나, 그 아래 `## <버전> (<YYYY-MM-DD>)`를 새 버전부터. 버전은 `X.Y.Z` 또는 `X.Y`
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
- [문서] 9키·8키 실측: `-Full`은 0x0000~0x1FFF만 지우고 설정 유지, 8키도 1.7.0 HID 장입 확인 — 문서 영향: docs/BOOTLOADER_CH32V203.md#하드웨어-시험-2026-11-07-sayo_3x3, docs/board/BOARD_SAYO_3X3V_RGB.md#7-확인-결과, docs/board/BOARD_SAYO_2X4V_RGB.md#7-확인-결과 — 반영: ☑
- [문서] 변경 기록(`CHANGELOG.md`)과 출시 순서(`docs/RELEASE.md`) — 문서 영향: README.md#폴더-구조 — 반영: ☑
- [문서] CH32V203 핀 번호 확인(자료표 V2.6 표 3-1-1, LQFP48/QFN48 공통, QFN48 0번 = 바닥 패드 VSS), BOOT 브리지 = BOOT0 쪽 기능 확인, 칩 핀맵 그림 문구 갱신 — 문서 영향: docs/board/BOARD_SAYO_3X3V_RGB.md#3-칩-핀맵, docs/board/BOARD_SAYO_2X4V_RGB.md#3-칩-핀맵, docs/PINMAP.md — 반영: ☑
- [도구] 문서 생성 구역: `tools/gen_docs.py`가 versions·boards·chips·pins-<보드> 표를 코드에서 채움(`--check`로 검사) — 문서 영향: README.md#현재-버전-2026-11-07, docs/BOARDS.md#구성, docs/BOARD_INFO.md#5-1-보드-목록-guicoreboardspy-새-파일, docs/BOARD_INFO.md#4-칩-표와-펌웨어-업데이트, docs/board/BOARD_MK6K1N.md#2-버튼-배치와-매핑, docs/board/BOARD_MK6K1NL.md#2-버튼-배치와-매핑, docs/board/BOARD_SAYO_3X3V_RGB.md#2-버튼-배치와-매핑, docs/board/BOARD_SAYO_2X4V_RGB.md#2-버튼-배치와-매핑, docs/board/_TEMPLATE.md, docs/RELEASE.md — 반영: ☑
- [문서] 식별 전 보드 SAYO_2X6(12키 2×6)·BT_2X3(6키 2×3 무선, 별도 USB 동글) 준비: 파일 구조 안, 보드 문서 초안과 식별 체크리스트(동글 연결 방식 확인 포함) — 문서 영향: docs/NEW_BOARD.md#9-식별-전-보드, docs/board/BOARD_SAYO_2X6.md, docs/board/BOARD_BT_2X3.md, docs/BOARDS.md#준비-중-보드, README.md#폴더-구조 — 반영: ☑
- [저장소] Sayo 보드 이름 통일: 3X3V_RGB → SAYO_3X3, 2X4V_RGB → SAYO_2X4(보드 파일 sayo_3x3.h·sayo_2x4.h, `-Board sayo_3x3`·`sayo_2x4`, 산출물·그림 파일 이름). 옛 이름을 알리는 장치(부트로더 1.0, 펌웨어 1.7.0)는 `boards.py` 별칭으로 같은 보드(GUI, check_device, hidboot 보드 검사는 `--force` 없이 통과), 설정은 남음 — 문서 영향: README.md, docs/BOARDS.md, docs/BOARD_INFO.md, docs/BOOTLOADER_CH32V203.md, docs/MANUAL.md, docs/TROUBLESHOOTING.md, docs/NEW_BOARD.md, docs/PINMAP.md, docs/board/BOARD_SAYO_3X3V_RGB.md, docs/board/BOARD_SAYO_2X4V_RGB.md, firmware/macro_keyboard/README.md, gui/README.md, tests/hw/ch32v203/test_ch32v203/README.md — 반영: ☑
- [문서] 칩 특성 표(`docs/CHIPS.md`, CH552G·CH32V203C8U6, 자료표 값과 코드 값 구분), 보드 문서 1절은 칩 표 링크만 — 문서 영향: docs/CHIPS.md#칩-특성-표, docs/board/_TEMPLATE.md, README.md#폴더-구조 — 반영: ☑
- [문서] 9키 장치 확인: 옛 이름 부트로더에 새 이름 앱(SAYO_3X3)을 `--force` 없이 HID 장입, check_device·설정 유지·LED 눈 확인 정상 — 문서 영향: docs/board/BOARD_SAYO_3X3V_RGB.md#7-확인-결과, docs/BOOTLOADER_CH32V203.md#하드웨어-시험-2026-11-07-sayo_3x3 — 반영: ☑
- [문서] 멀티에이전트 작업 방식 문서(`docs/WORKFLOW.md`): 역할·소유권, 작업 흐름, 추적 장치, 검사 관문, 승인, 도입 체크리스트 — 문서 영향: docs/WORKFLOW.md, README.md#폴더-구조 — 반영: ☑
- [문서] MK6K1N 실제 장치 1.7.0 확인: 장입·check_device·눈 확인 정상, 설정 유지, 개발 보드 UID 기록 — 문서 영향: docs/board/BOARD_MK6K1N.md#1-개요, docs/board/BOARD_MK6K1N.md#7-확인-결과 — 반영: ☑
- [문서] MK6K1NL 실제 장치 1.7.0 확인(UID 9C-B3-6E-BF): 장입·check_device·눈 확인 정상, 설정 유지 — 문서 영향: docs/board/BOARD_MK6K1NL.md#1-개요, docs/board/BOARD_MK6K1NL.md#7-확인-결과 — 반영: ☑
- [펌웨어] SAYO_3X3·SAYO_2X4·MK6K1NL 전원 인가 점검이 키 둘레를 1번 키부터 시계방향으로 흐르는 빛으로 회전(이웃한 두 칸 교차 페이드, 한 바퀴 1초, 빛은 색 바퀴 색, 색 주기·횟수는 그대로, 9키 가운데 키는 계속 켜짐, MK6K1N은 LED 1개라 그대로; `BOARD_LED_RING_CHASE 0`이면 무지개 회전) — 문서 영향: docs/board/BOARD_SAYO_3X3V_RGB.md, docs/board/BOARD_SAYO_2X4V_RGB.md, docs/board/BOARD_MK6K1NL.md, docs/MANUAL.md, firmware/macro_keyboard/README.md — 반영: ☑

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
