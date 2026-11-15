# jl_usbkey_bluepill: JieLi USB_KEY 송신기 (Blue Pill)

> ⚠️ **초안**(2026-11-15). 빌드만 함(`build/jl_usbkey_bluepill.bin` 1,028 B), 장입·실물 시험 안 함. 장입은 사용자가 "ㄱㄱ"라고 한 뒤에만

- **목적**: 앱이 USB 전에 멈춘 JUZH_3B4(JieLi AC6321A, BD19)를 ROM USB 장입 모드(UBOOT1.00 디스크)로 넣음. 납땜 없이 선을 패드에 잠깐 대는 것만으로
- [jl_usbkey_ch32v203](../jl_usbkey_ch32v203/README.md)의 ENTER(클럭 선 자동)를 옮김. 아날로그 스위치·전원 스위치·USB 명령 없음. 리셋 뒤 10초 동안 키만 보냄
- 출처: `kagaimiq/jielie` isp/usb/usb-key.md, `kagaimiq/jl-uboot-tool` how-to-enter-uboot.md
- ROM 동작(`rom.lst`): 켤 때 D+/D−로 키 `0x16EF`를 받으면 UBOOT에 들어가고 약 1.3초 동안 PC의 SOF를 기다림 → **JUZH USB 선은 PC에 꽂혀 있어야 함**

## 동작

- 클럭: HSE 8 MHz × 9 = 72 MHz(실패하면 HSI/2 × 16 = 64 MHz). 시간은 SysTick(HCLK, 32비트로 늘림)
- 리셋 직후 `SEND_MS`(10초) 동안 반복:
  - 키 `0x16EF` 16비트, MSB 먼저. 한 선 클럭·다른 선 데이터, 클럭 Low 동안 데이터 바꾸고 오름에서 읽힘. 반주기 10 µs(약 50 kHz)
  - 키 보낼 때 PA0·PA1 push-pull(저항 없이 직접 구동, PC 쪽 15 kΩ 풀다운을 이김)
  - 키 뒤 간격 3 ms(ch32v203의 응답 대기 창과 같음). 이때 선은 놓음(내부 풀업 입력, `GAP_RELEASE 1`)
  - 키 8개마다 클럭 선을 D+ ↔ D−로 바꿈(D+ 클럭 먼저)
- 응답 감지: 간격 중 두 선이 함께 Low 300 µs 이상. **결과는 LED만**, 송신은 안 멈춤(`ACK_STOPS 0`)
  - PC·칩의 풀다운이 내부 풀업(약 40 kΩ)보다 세서 거의 늘 "응답"으로 읽힐 수 있음 → 참고만
- 끝나면 PA0·PA1 floating 입력(PC에 돌려줌)
- LED PC13(Low = 켬): 송신 중 켬 → 끝난 뒤 천천히 깜빡임(0.5초 켬·0.5초 끔). 응답을 감지했으면 2초마다 두 번 깜빡임
- 다시 하려면 Blue Pill 리셋 버튼

### 설정 (`config.h`, `pins.h`)

| 상수 | 기본 | 뜻 |
|---|---|---|
| `USB_KEY` | `0x16EF` | 보낼 키 |
| `HALF_US` | 10 | 반주기 µs |
| `SEND_MS` | 10000 | 송신 시간 ms |
| `AUTO_SWITCH` | 8 | 클럭 선 바꾸는 키 수 |
| `FIRST_CLK` | 1 | 처음 클럭 선(1 D+, 2 D−) |
| `ACK_WAIT_US` / `ACK_MIN_US` | 3000 / 300 | 키 뒤 간격, 응답 판정 Low 길이 |
| `GAP_RELEASE` | 1 | 1: 간격에 선 놓음(감지 가능, 칩과 충돌 없음). 0: High로 구동(감지 없음, 칩이 응답하면 충돌) |
| `ACK_STOPS` | 0 | 1: 응답 감지하면 멈춤(ch32v203 방식) |
| `PIN_DP` / `PIN_DM` / `PIN_LED` | PA0 / PA1 / PC13 | 핀 |

## 배선

- 그림 자리: Blue Pill ↔ JUZH 패드 배선(그림은 사용자가 나중에 추가)

| Blue Pill | JUZH_3B4 | 비고 |
|---|---|---|
| PA0 | D+ (칩 15번) | 저항 없이 직접 |
| PA1 | D− (칩 14번) | 저항 없이 직접 |
| GND | GND | 꼭 공통 |

- Blue Pill 전원은 자기 USB(또는 USB2UART 3.3 V). JUZH 쪽 3.3 V·5 V와 잇지 않음
- JUZH USB 선은 PC에 그대로 꽂음(PC가 SOF를 보내야 함). 가능하면 다른 장치 없는 포트

## 복구 순서

1. JUZH 전원 끔(USB 선 뽑기. 배터리가 있으면 전원 스위치도 끔)
2. Blue Pill GND를 JUZH GND에, PA0·PA1 선을 D+·D− 패드에 댐
3. Blue Pill 리셋 → PC13 LED 켜짐(10초 송신 시작)
4. 10초 안에 JUZH USB를 PC에 꽂음(또는 전원 켬). 선은 그동안 계속 댐
5. LED가 깜빡이기 시작하면 선을 뗌
6. PC에 "BD19 UBOOT1.00" 디스크가 보이면 성공 → PM이 공장 백업을 다시 씀
- 안 되면: 3부터 다시. `FIRST_CLK`·`HALF_US`·`GAP_RELEASE`를 바꿔 볼 수 있음(다시 빌드·장입)

## 빌드

- 도구: xPack `arm-none-eabi-gcc` 15.2.1-1.1(`C:\Users\user\tools\gcc\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin`), `mingw32-make`. 환경 변수 `MK_ARM_BIN`이 있으면 그 폴더를 씀
- 벤더 HAL 없음: `startup.c`(벡터 표·.data/.bss), `stm32f103c8.ld`(플래시 64 KiB·RAM 20 KiB), `main.c`(레지스터 직접)

```bash
cd firmware/jl_usbkey_bluepill
mingw32-make
```

- 결과: `build/jl_usbkey_bluepill.bin`(0x08000000부터), `.elf`, `.hex`, `.map`
- `Makefile`을 고친 뒤에는 `rm -rf build`

## 장입 방법 (USB2UART, ROM 시리얼 부트로더)

- 상태: **장입 안 함**. 사용자가 "ㄱㄱ"라고 한 뒤 PM이 함
- 배선(USB2UART는 3.3 V 신호):

| USB2UART | Blue Pill |
|---|---|
| TX | PA10 (USART1 RX) |
| RX | PA9 (USART1 TX) |
| GND | GND |
| 3.3 V | 3.3 V (Blue Pill USB로 전원을 주면 잇지 않음) |

1. BOOT0 점퍼 = 1, BOOT1 = 0 → 리셋(ROM 부트로더)
2. 장치 관리자에서 COM 번호 확인
3. 쓰기(쓰기 전 확인 묻음, 칩 전체 지우기 → 쓰기 → 검증):

```bash
powershell -ExecutionPolicy Bypass -File firmware/jl_usbkey_bluepill/flash_uart.ps1 -Port COM5
```

4. BOOT0 점퍼 = 0 → 리셋 → LED 켜짐(10초) 뒤 깜빡이면 동작
- 도구: `uv run --no-project --with stm32loader==0.7.1 stm32loader`(처음 실행 때 PyPI에서 받음)
- 읽기 보호가 걸린 Blue Pill이면 쓰기가 실패할 수 있음(미확인)

## 위험·미확인

- 실물 미시험(Blue Pill·JUZH 모두)
- PC가 D+/D−에 보내는 신호(리셋·SOF)와 키가 섞일 수 있음. 키 보내는 동안 push-pull로 PC 쪽과 맞섬(PC 풀다운 15 kΩ는 문제없음, PC가 구동하는 순간은 짧은 충돌)
- 키 뒤 간격에 선을 놓으면 PC 풀다운 때문에 Low로 보임. ROM이 간격 중 High를 기대하는지 모름(`GAP_RELEASE 0`으로 바꿔 시험 가능)
- 어느 선이 클럭인지 자료가 다름 → 둘 다 번갈아 씀
- ROM이 키를 보는 시간 창, BD19(AC6321A)에서 이 키 형식이 맞는지 미확인
- 응답 감지는 오검출 가능(LED만 참고)
- 짝퉁 Blue Pill(CKS32·GD32 등)은 클럭·부트로더가 다를 수 있음
