# jl_usbkey_ch32v203: JieLi USB_KEY 어댑터 (CH32V203)

> ⏸ **보류**(2026-11-15): 공장 펌웨어에서 벤더 HID `03 EF EF`로 UBOOT 진입이 확인돼 처음 설치에 필요 없음. 사용자: 어댑터·동글 부품 구입 불가. 앱이 USB 전에 멈추는 복구용으로만 남김. 납땜 없이 쓰는 Blue Pill판: [jl_usbkey_bluepill](../jl_usbkey_bluepill/README.md)([보드 문서 5절](../../docs/board/BOARD_KEY12_JUZHEN_V01.md#5-부팅장입))
>
> ⚠️ **초안**(2026-11-15). 빌드만 함(`build/jl_usbkey.bin` 3,428 B), 장입·실물 시험 안 함. 장입은 사용자가 "ㄱㄱ"라고 한 뒤에만

- **목적**: JieLi 칩(JUZH_3B4, AC6321A)을 ROM USB 다운로드 모드(**UBOOT**)로 넣음. 처음 설치·복구용(CH32V203의 BOOT 브리지 `-Full`에 해당). JieLi USB 업데이터 동글이 하는 일을 대신함
- 장입 절차 전체: [BOARD_KEY12_JUZHEN_V01.md 장입 방향](../../docs/board/BOARD_KEY12_JUZHEN_V01.md#장입-방향-사용자-결정-2026-11-15)
- 출처: `kagaimiq/jielie` 저장소의 isp/usb/usb-key.md, `kagaimiq/jl-uboot-tool` 저장소의 how-to-enter-uboot.md

## 동작

1. 키보드의 D+·D−를 USB 2:1 스위치로 어댑터 쪽에 돌림
2. 키보드 전원(VBUS)을 끄고 켬
3. D+·D−에 16비트 키 `0x16EF`를 반복(한 선 클럭·한 선 데이터, MSB 먼저, 클럭 오름에서 읽음, 약 50 kHz)
4. 칩이 두 선을 1~2 ms 함께 낮추면 응답. 판정: 키 하나 뒤 선을 놓고 3 ms 안에 두 선이 300 µs 넘게 함께 Low
5. 선을 놓고 스위치를 PC 쪽으로 돌림 → PC에 "UBOOT" 저장장치가 나타남

- 어느 선이 클럭인지 자료 두 곳이 다름 → ENTER 기본(자동)은 키 8개마다 D+ 클럭·D− 클럭을 번갈아 씀
- 응답이 없어도 끝나면 선을 PC로 돌림(키보드는 그냥 부팅)
- 켤 때: 키보드 전원 켬, 선은 PC 쪽(보통 USB 선처럼 동작)

## 핀 (`pins.h`, 바꿀 수 있음)

| 핀 | 쓰임 | 비고 |
|---|---|---|
| PA0 | 키보드 D+(스위치 어댑터 쪽) | 열린 드레인, 외부 4.7 kΩ 이하 풀업(3.3 V) |
| PA1 | 키보드 D−(스위치 어댑터 쪽) | 열린 드레인, 외부 4.7 kΩ 이하 풀업(3.3 V) |
| PA2 | USB 2:1 스위치 선택 | High = 어댑터 쪽(`SEL_TO_KEY 1`) |
| PA3 | 키보드 전원 스위치 EN | High = 켬(`PWR_ON 1`) |
| PB6·PB7 | 어댑터 자신의 USB(PC와 통신) | |
| PA13·PA14 | SWD | 그대로 둠 |

- 풀업이 필요한 이유: 칩의 USB 내부 풀다운(약 15 kΩ)을 이겨야 함. CH32V203 내부 풀업으로는 부족

## 배선

- 그림 자리: 어댑터 배선(그림은 사용자가 나중에 추가)

```
PC ──USB── 어댑터 보드(CH32V203, 자체 USB PB6/PB7)
             PA0 ──┬── 4.7k ── 3.3V
             PA1 ──┼── 4.7k ── 3.3V
                   │
             [USB 2:1 스위치: CH442E·TS3USB221·FSUSB42 등]
             PA2 ── SEL        공통 D+/D− ── 키보드 USB D+/D−
                               포트 1 D+/D− ── PA0/PA1
                               포트 2 D+/D− ── PC 두 번째 USB
             PA3 ── 전원 스위치 EN(부하 스위치 SY6280·TPS2041, 또는 2N7002 + AO3401)
                               5V(PC 두 번째 USB VBUS) → 키보드 VBUS
GND 모두 공통
```

- 부품: CH32V203C8 개발 보드 1(키·LED가 안 물린 보드 권장. SAYO 보드는 핀이 키·LED에 쓰임), USB 2:1 아날로그 스위치 모듈 1, USB 부하 스위치 1(또는 MOSFET 2), 4.7 kΩ 2, USB 선 2(하나는 잘라 분기)
- 키보드 쪽 PC 연결은 따로 둔 포트 또는 USB 2.0 허브 권장(UBOOT의 SOF 감지가 다른 USB 신호에 약함)
- **배터리 주의**: 키보드에 배터리가 있으면 VBUS를 꺼도 칩이 안 꺼짐
  - 배터리를 빼거나 키보드 전원 스위치로 끔(기판 확인 필요)
  - 그때는 ENTER 끄는 시간 0으로 보내고, 제한 시간 안에 사용자가 키보드를 켬

## 명령

- 벤더 HID `1209:C560`, 제품 문자열 "JieLi USB_KEY adapter", 64바이트 보고서
- 머리 형식은 [PROTOCOL.md](../../docs/PROTOCOL.md)와 같음: 요청 [0] 명령, [1] 순번. 응답 [0] = 명령 | 0x80, [1] 순번, [2] 상태(0 OK, 1 BAD_CMD, 2 BAD_ARG)
- 16비트 값은 리틀 엔디언

| 명령 | 이름 | 요청 | 응답 |
|---|---|---|---|
| `0x01` | HELLO | – | [3..4] 버전(0.1), [5..20] `JL_USBKEY`, [21..32] 칩 UID 12바이트, [33] 전원(1 켬), [34] 경로(1 어댑터 쪽) |
| `0x20` | POWER | [2] 0 끔 / 1 켬 | – |
| `0x21` | ROUTE | [2] 0 PC 쪽 / 1 어댑터 쪽(선 놓음) | – |
| `0x22` | ENTER | [2..3] 끄는 시간 ms(0 = 전원 안 건드림, 사용자가 켬), [4..5] 제한 ms(0 = 3000), [6] 반주기 µs(0 = 10 → 50 kHz, 최대 100), [7] 클럭 선(0 자동, 1 D+, 2 D−) | [3] 응답 받음, [4] 쓴 클럭 선(1 D+, 2 D−), [5..6] 보낸 키 수, [7..8] 켠 뒤 응답까지 ms |
| `0x23` | LINES | – | [3] D+, [4] D− 지금 값 |

- ENTER는 끝날 때까지 응답하지 않음(최대 제한 시간 + 끄는 시간)
- GUI 쪽(설계, 아직 없음): `gui/core/`의 `jl_usbkey.py`(find·hello·power·route·enter·lines)

## 빌드

- 도구: CH32V203 본 펌웨어와 같음(xPack RISC-V GCC, WCH EVT SDK, `MK_EVT_SDK`·`MK_RISCV_BIN`)

```bash
cd firmware/jl_usbkey_ch32v203
mingw32-make
```

- 결과: `build/jl_usbkey.bin`, 0x08000000부터 칩 전체 이미지(우리 부트로더 없음)
- USB 코드는 자체 부트로더의 `../bootloader_ch32v203/usb_hid.c`를 그대로 씀(`-DUSB_PID=0xC560`, `-DUSB_PRODUCT`). 부트로더 바이너리는 바뀌지 않음
- `Makefile`을 고친 뒤에는 `rm -rf build`

## 장입 방법

- 상태: **장입 안 함**. 사용자가 "ㄱㄱ"라고 한 뒤에만
- wchisp + BOOT 브리지(처음 설치와 같은 방법, 자체 부트로더 없음): BOOT0 브리지 + 전원 → ROM 부트로더 → wchisp로 `build/jl_usbkey.bin`
- 어느 CH32V203 보드에 넣을지는 사용자 결정

## 할 일·미확인

- 클럭 선(자동으로 둘 다 시도), ROM이 키를 보는 시간 창, 응답 뒤 PC 연결까지 허용 시간
- 배터리가 있는 키보드의 전원 끄기 방법(기판 확인)
- 실물 시험(부품 구입·조립 뒤)
