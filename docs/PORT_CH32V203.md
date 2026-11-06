# CH32V203 이식 계획

- 두 보드는 **별개 작업**: 지금은 **9키(3X3V_RGB)** 기준, 8키(2X4V_RGB)는 9키 완료 후 진행
- 제품: SayoDevice 계열 (8키 = Keyb T8B7). 키마다 3색 한 칩 RGB LED

## 9키 (3X3V_RGB): 상태 (2026-11-06)

- 펌웨어 **구현·빌드 완료** (11 KB, 경고 0). 호스트 테스트, GUI 테스트 209개 통과
- **하드웨어에 아직 장입 안 함**
- 빌드: `cd firmware/macro_keyboard; mingw32-make -f Makefile.ch32v203` (PATH에 xPack gcc). 결과 `build/ch32v203/macro_keyboard-3X3V_RGB.bin`
- 정한 동작
  - 1줄 LED는 파랑만 사용 (PA11/PA12가 USB 핀)
  - 잠금(패턴 암호)·자동 잠금 있음 (2026-11-06 추가). 노브가 없어 **아무 키 2초**로 패턴 입력 시작
    - 패턴 단계는 키만 (A~I)
    - 암호가 있으면 키는 **뗄 때** 입력 (2초 누르면 입력 없이 패턴 입력 시작). 암호가 없으면 누를 때 입력
    - 구현: `src/core/input.c`. 호스트 테스트 통과, 하드웨어 확인 전
  - BOOT 명령은 재시작만 함 (부트로더 진입은 BOOT 브리지로만)
  - LED는 줄 단위 같은 색, 색별 밝기 보정 R 12 / G 3 / B 8 (16단계 중)

### 구현된 것

| 영역 | 내용 |
|---|---|
| 기능 코드 | 노브 0개 허용(`BOARD_KNOBS 0`), 키 상태 16비트(`hal_sw_t`), 저장 후 `hal_store_flush` |
| HAL | `src/hal/ch32v203/`: TIM2 시간, 키 9개, TIM3 소프트웨어 PWM LED, 플래시 끝 4 KB(256 B 페이지 16개, 0x0800F000~) 저장(RAM 사본), 바뀐 페이지만 지우고 씀, 칩 UID, 재시작 |
| USB | EVT USBFS 드라이버 수정본(`src/hal/ch32v203/usb/`): 키보드 + Vendor HID(64바이트) |
| GUI | 보드 목록(연결 안 됨이면 선택, 연결되면 자동 전환), `3X3V_RGB` 보드·칩 `0x19` 추가 |

### 할 것

1. 장입 ("ㄱㄱ" 후, BOOT 브리지 연결 상태)
2. 아래 "확인할 것"을 차례로 시험하고 문제를 고침
3. 결과를 문서에 반영: `BOARD_INFO.md` 칩 ID, `PINMAP.md` 핀 그림(그림은 사용자 추가)
4. (선택) `tools/build_fw.ps1`에 CH32V203 빌드 추가

### 확인할 것 (장입 후, 순서대로)

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

- PB6이 시작부터 낮게 읽힘 (용도 미확인)
- LED 애노드 배선: 공통 전원이면 줄 단위 제어만 가능 (키별 제어 불가)
- PA11/PA12를 USB와 LED로 같이 쓸 수 있는지 (지금은 포기)

### 키보드 없이 할 것 (준비 작업)

| # | 작업 | 내용 | 끝난 기준 |
|---|---|---|---|
| A ✅ | host_test 9키 구성 | `run.py`에 `3X3V_RGB` 시험 16개(키 9개, 동시 입력, 떨림, 저장, 잠금 거부). 잠금 없는 보드는 펌웨어가 패턴 암호를 거부(`BAD_ARG`), GUI도 저장 막음, HELLO에 자동 잠금 비트 안 보냄 | 완료 (2026-11-06) |
| B ✅ | 빌드 스크립트 | `tools/build_fw.ps1 -Board 3x3v_rgb` → `Makefile.ch32v203` 호출(빌드 날짜 포함, 매번 새로 빌드) | 완료: `build/ch32v203/macro_keyboard-3X3V_RGB.bin` 11 KB |
| C ✅ | 장입 스크립트 | `tools/flash.ps1 -Board 3x3v_rgb`: 브리지 안내 → `wchisp info` → `yes` 입력 시 `wchisp flash` | 완료 (실제 장입은 미시험) |
| D ✅ | HAL 코드 점검 | 아래 "HAL 점검 결과" | 완료 (2026-11-06) |
| E ✅ | 확인용 도구 | `tools/check_device.py`: HELLO·보드·칩·설정 읽기. `--write`(백업 후 시험 문자열) → 재연결 → `--verify` → `--restore <백업>`, `--long N` | 완료, 시험 7개 |
| F ✅ | GUI 업데이트 창 | CH32V203이면 "이 창으로 장입 불가" + 브리지·`flash.ps1` 안내, 업데이트 모드 단추 끔 | 완료, GUI 209개 통과 |
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
| 이미지가 설정 페이지 침범 | 링커 스크립트는 64 KB 전체 허용 | Makefile에서 `.bin`이 61440바이트(64 KB − `STORE_SIZE` 4 KB) 넘으면 실패 (지금 11 KB) |
| 플래시 접근 클럭 | `FLASH_Access_Clock_Cfg` | SDK 예제는 96 MHz에서도 안 씀 → 48 MHz는 불필요 |
| 응답 대기 중 USB 버스 리셋 | 요청 처리 중 리셋 | 드라이버가 `Vendor_Req`·EP2 상태 초기화 → 위험 낮음 |
| 칩 ID | wchisp는 칩 `0x31`, 계열 `0x19`로 표시할 가능성 | 장입 때 `wchisp probe` 값으로 확정 (확인할 것 8번) |
- 하드웨어가 필요한 일: 장입, "확인할 것" 1~9, 참고 키보드 LED 확인

## 8키 (2X4V_RGB): 보류

- 9키 완료 후 진행
- 확인된 것·시험할 것: [test_ch32v203 README](../tests/hw/ch32v203/test_ch32v203/README.md)의 "2X4V_RGB (8키)"
- 요약: 핀맵 확인(test_ch32v203) → `src/boards/2x4v_rgb.h` → `BOARD=2X4V_RGB` 빌드 → GUI 보드 추가

## 장입 절차

- BOOT 브리지 연결 → USB 연결 → `wchisp flash` → 브리지 제거 → USB 재연결
- GUI의 업데이트 창은 이 보드에서는 쓸 수 없음(부트로더 진입을 펌웨어가 못 함)

## 메모리와 저장 용량 (2026-11-06)

- 칩: CH32V203C8U6, 코드 플래시 64 KB, RAM 20 KB (EVT `Link.ld`, 스택 2 KB)
- 설정 블롭: `BLOB_CAPACITY` 4096 (`config.h`, `Makefile.ch32v203`의 `STORE_SIZE`와 같아야 함)
  - 플래시 0x0800F000~0x0800FFFF, 펌웨어 이미지는 60 KB까지
  - RAM에 사본 3개(storage blob·stage, HAL shadow) = 12 KB
- 빌드 결과: text 11060, bss 14876 → RAM 약 5 KB 남음. RAM이 한계라 4 KB가 실용 최대
- 문자열 합계 최대: 9키 4096 − 8 − 9×2 = **4070자**, 8키 4096 − 8 − 8×2 = **4072자**
  - 패턴 암호·자동 잠금을 쓰면 그만큼(슬롯 길이 2바이트씩 + 암호 2~8 + 설정 1바이트) 줄어듦
- 블롭 위치·길이는 `blob_pos_t`(용량 > 255면 16비트). CH552는 8비트 그대로라 코드 크기 같음
- HELLO 7~8바이트가 4096을 알려 GUI가 자동으로 맞춤. 프로토콜 변경 없음
- 시험: host_test `3X3V_RGB` 4096바이트 블롭 4070자 입력, 4097바이트 거부, 300자 슬롯
