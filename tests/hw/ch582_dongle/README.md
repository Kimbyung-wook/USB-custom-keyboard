# CH582F 동글 핀 시험

SAYO_2B3 USB 동글(CH582F)의 사용 USB 포트와 32 kHz 크리스털 유무를 확인하는 시험 펌웨어(동글에 LED·버튼은 없음으로 확정, 핀 구동·버튼 감지는 확인용으로 남김). USB 키보드로 결과를 입력함 → 메모장을 열어 두고 꽂음. 아직 **장입 전**.

## 빌드

- `mingw32-make` → `build/pin_test.bin` (7,780 B). 규칙·도구 경로는 [firmware/dongle_ch582/ch58x.mk](../../../firmware/dongle_ch582/ch58x.mk)
- 소스: `pin_test.c` + 동글의 `src/usb_hid.c`(USB1·USB2 둘 다 대기), `src/core/rq.c`

## 장입

- **첫 장입은 동글 공장 펌웨어와 키보드 짝짓기를 영구히 지움** → 사용자 별도 승인 뒤에만
- ROM 부트로더(추정): PB22를 GND에 댄 채 USB에 꽂음 → `4348:55E0`, 6~15초 안에 장입
- `wchisp info`로 칩 CH582, 읽기 보호, UID 확인(읽기만) → UID는 PM에 알림
- `mingw32-make flash`

## 입력 읽기

| 입력 | 뜻 |
|---|---|
| `CH582 PIN TEST USB<n> RST<n> MAC xx:..` | 사용 중 USB 포트(1 = PB10/PB11, 2 = PB12/PB13), 리셋 원인, MAC |
| `RC32K <n> X32K <n> OK\|NONE` | 100 ms 동안 RTC 틱. X32K가 2,000 넘으면 OK(크리스털 있으면 약 3,277). 본 펌웨어는 2,900~3,700만 크리스털로 봄 |
| `PU LOW: ...` | 풀업인데 낮음: 바깥에서 GND로 당김(눌린 버튼, LED 음극 쪽 등) |
| `PD HIGH: ...` | 풀다운인데 높음: 바깥에서 전원으로 당김 |
| `PA04 H` … `L` | 핀 하나를 1초 높음(H), 1초 낮음(L). H에서 켜지면 높음 = 켜짐 LED, L에서 켜지면 낮음 = 켜짐 |
| `*` (핀 이름 뒤) | 시작 때 PU LOW였던 핀 |
| `LOOP` | 한 바퀴 끝 |
| `BTN PB22` / `BTN PB22 up` | 그 핀이 눌림/뗌 = 버튼 |

- 보기 어려우면 PC 키보드 Scroll Lock 켜기 → 지금 핀만 반복
- 건드리지 않는 핀: 쓰는 USB 포트 두 핀, PB23(RST일 수 있음)
- 결과는 핀·레벨·LED 색을 적어 PM에 알림

## 결과

- 아직 없음
