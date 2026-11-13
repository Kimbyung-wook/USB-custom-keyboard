# BT 동글 펌웨어 (CH582F)

SAYO_2B3 USB 동글(CH582F)용. 일반 BLE HID 키보드(HID over GATT, 서비스 0x1812)에 중앙기기로 붙어 받은 입력을 USB HID로 넘김. 아직 **장입 전, 하드웨어 미확인**.

## 구조

| 파일 | 내용 |
|---|---|
| `src/main.c` | 시작 순서, 32 kHz 클록 선택, BLE 라이브러리 초기화, 10 ms 틱(버튼·LED·상태기계), USB 송신 |
| `src/ble_host.c` | 검색·연결·본딩, HID 서비스 탐색, 알림 구독, LED 출력 보고서 |
| `src/store.c` | 데이터 플래시: 본딩 장치 캐시(0x7000, 1 KB), 빠른 재연결 횟수(0x7800). BLE 스택 본딩 기록은 0x7E00 |
| `src/usb_hid.c` | USB HID 장치(USB1·USB2 둘 다 대기, PC가 설정한 쪽 사용). 1209:C55F |
| `src/board.h` | LED·버튼 핀. 동글에 둘 다 없음(-1, 확정) |
| `src/core/` | 칩과 무관한 논리(PC 시험 대상) |
| `ch58x.mk` | 공용 빌드 규칙(핀 시험도 사용) |
| `test/` | core PC 시험 |

`src/core/`:

- `hid_desc`: BLE Report Map 해석(Desktop·Keyboard·LED·Button·Consumer 페이지)
- `hid_conv`: BLE 입력 보고서 → USB 보고서. 부트 키보드 8 B(6키 넘으면 ErrorRollOver), 소비자 키·시스템 키·마우스는 보고서 ID 사용. 바뀔 때만 보냄
- `hogp`: 광고 데이터 판정(HID, 짝짓기 모드), 탐색 결과(핸들) 정리
- `link`: 상태기계(짝짓기·재연결·준비, 버튼·재꽂기 동작, LED 모양)
- `rq`: 보고서 큐(32칸, USB가 바쁠 때 보관)

## 동작

동글에 버튼·LED 없음 → 조작은 재꽂기만, 상태 표시 없음.

| 상태 | 들어가는 조건 |
|---|---|
| 짝짓기 | 본딩 없음(시간 제한 없음), 3번 빠르게 재꽂기(60초) |
| 재연결 | 본딩 있음 |
| 준비 | 암호화·구독 끝 |

- 빠르게 재꽂기: 꽂은 뒤 3초 안에 뽑기를 반복
- 본딩 지우기: 5번 빠르게 재꽂기 → 짝짓기
- 짝짓기 60초가 지나면 옛 본딩으로 재연결(새 장치를 못 찾았을 때)
- 짝짓기: 1초씩 검색해 HID 광고 + 검색 가능 플래그 + 연결 가능 장치 중 신호가 가장 센 것에 연결. 실패한 장치는 이번 짝짓기 동안 뺌
- 새 장치가 준비되면 옛 본딩을 지우고 새것만 남김(1대만)
- 암호키: 키보드가 요구하면 `000000` 입력 뒤 Enter
- 재연결: 본딩 주소로 바로 연결 요청(100% 스캔), 시간 제한 없음. 끊기면 즉시 다시 요청
- 캐시가 맞으면 재연결 때 탐색 없이 알림만 다시 켬. 핸들 오류 나면 다시 탐색
- Report Map을 못 쓰면 부트 프로토콜(부트 키보드 특성)로 대체
- 끊기면 눌린 키를 모두 뗌 보고서를 보냄

## 제한

- 주소가 바뀌는(RPA) 키보드는 재연결 안 될 수 있음(본딩 주소로만 연결)
- HID 서비스는 첫 번째만 사용. 보고서 8개, Report Map 512 B까지
- USB 원격 깨우기 없음(PC 절전 중 키로 깨우기 안 됨)
- 상태 표시 없음(LED 없음). 짝짓기 여부는 키보드 쪽 표시나 입력으로 확인
- 앱에서 들어가는 부트로더 없음 → 다시 장입하려면 ROM 부트로더(PB22) 필요

## 빌드

- 도구: xPack `riscv-none-elf-gcc` 15.2, `mingw32-make`, WCH CH583EVT SDK(`C:/Users/user/tools/sdk_ch583/EVT/EXAM`). 환경 변수 `MK_CH58X_SDK`, `MK_RISCV_BIN`, `MK_WCHISP`가 먼저
- `mingw32-make` → `build/dongle.bin`
- LED·버튼이 있는 다른 기판이면: `mingw32-make DEFS="-DBOARD_LED_PIN=PIN_PB(4) -DBOARD_BTN_PIN=PIN_PB(22)"` (예시, `src/board.h`). 버튼 3초 = 짝짓기, 10초 = 본딩 지우기, 짝짓기 중 짧게 = 취소. LED: 짝짓기 빠른 깜빡임, 재연결 1초마다 짧게, 준비 켜짐
- Makefile 고친 뒤 `rm -rf build`
- 크기(2026-11-13): 플래시 169,424 B / 448 KB(BLE 라이브러리 대부분), 정적 RAM 약 13 KB / 32 KB
- PC 시험: 루트의 `uv run pytest`에 포함(`tests/gui/test_dongle_host.py`, gcc 없으면 건너뜀). 단독 실행 `python test/run.py` (MinGW gcc, `MK_HOST_GCC`로 바꿈) → 93개 통과

## 장입

- **첫 장입은 공장 펌웨어와 키보드 짝짓기를 영구히 지움** → 사용자 별도 승인 뒤에만
- 순서: 핀 시험([tests/hw/ch582_dongle](../../tests/hw/ch582_dongle/README.md), USB 포트·X32K 확인) → 이 펌웨어
- ROM 부트로더 진입(추정, 키보드 CH573과 같은 방식): **PB22를 GND에 댄 채 동글을 USB에 꽂음** → `4348:55E0`. 통신 없으면 6~15초 뒤 앱으로 돌아감
  - PB22 위치는 기판에서 찾음(칩 핀 배치는 데이터시트). 안 되면 `DOWNLOAD_CFG`가 PB11일 수 있음
- 확인(읽기만): `wchisp info` → 칩 CH582, `CFG_ROM_READ`(읽기 보호), UID 기록
- 장입: `mingw32-make flash` (= `wchisp flash build/dongle.bin`)
- 드라이버: WinUSB 설치됨. 시스템 드라이버는 바꾸지 않음

## 하드웨어 확인 순서

1. 핀 시험 장입 → 입력된 `USB<n>`, `X32K`(크리스털 유무) 기록
2. 이 펌웨어 장입 → 장치 관리자에 `BT Dongle`(1209:C55F) 키보드로 잡힘
3. BLE 키보드(예: SAYO_2B3 + BLE HID 펌웨어, 또는 시판 BLE 키보드)를 짝짓기 모드로 → 입력 확인
4. Caps Lock → 키보드 LED 반영(키보드가 지원하면)
5. 키보드 전원 끄고 켜기 → 재연결 시간 확인, 키 누른 채 끊기 → 키 떼짐 확인
6. 동글 재꽂기 → 탐색 없이 재연결 확인
7. 재꽂기 3번(짝짓기), 5번(본딩 지움) 확인
