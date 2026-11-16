# 칩 특성

- 이 프로젝트가 쓰는 칩의 특성을 한 곳에 모은 표. 보드 문서 1절(개요)은 칩 이름과 이 표 링크만 둠
- 보드별 값(키·LED 핀, 진입 키)은 [BOARDS.md](BOARDS.md)와 보드 문서

## 칩 특성 표

- 값의 출처 세 가지
  - **자료표 값(손으로 씀)**: 칩 자체의 사양. 근거는 자료표 이름·버전·표 (PDF는 저장소 밖 `MacroKeyboard/reference/`)
  - **코드 값**: 우리 펌웨어·GUI가 쓰는 배치·한도(`gui/core/chips.py`, `firmware/macro_keyboard/src/hal/ch32v203/boot_layout.h`, `firmware/bootloader_ch573/boot_layout.h`, `firmware/macro_keyboard/src/config.h`). 나중에 생성 구역(`chips`)으로 옮길 수 있음. 아래 표에서는 **(코드)**로 표시
  - **SDK 값**: WCH EVT SDK의 링커 파일·드라이버(CH573 SDK, CH582는 CH583 SDK). 자료표가 없는 CH573F·CH582F에 씀. 아래 표에서는 **(SDK)**로 표시
- CH573F(SAYO_2B3 키보드)·CH582F(SAYO_2B3 동글): 저장소 밖 자료표를 아직 다 보지 못함. 실제 장치·SDK·코드·자료표 일부로 확인한 값만 확정, 나머지는 "확인 필요"

| 항목 | CH552G | CH32V203C8U6 | CH573F (SAYO_2B3) | CH582F (SAYO_2B3 동글) |
|---|---|---|---|---|
| 코어 | E8051 (MCS51 호환) | QingKe V4B (32비트 RISC-V) | 32비트 RISC-V (`rv32imac`, **SDK**). 코어 세대(V3 계열)는 확인 필요 | 32비트 RISC-V (`rv32imac`, **SDK**). 코어 세대(V4 계열)는 확인 필요 |
| 최대 클럭 / 우리 설정 | 24 MHz / 24 MHz 내부 클럭 | 144 MHz / 48 MHz 내부 HSI(`SYSCLK_FREQ_48MHz_HSI`, USBFS 48 MHz용) | 최대 확인 필요 / 60 MHz PLL(`CLK_SOURCE_PLL_60MHz`, **코드**) | 최대 확인 필요 / 60 MHz PLL(**코드**) |
| 저속 클록 (BLE 시간 기준) | – | – | 내부 LSI(32 kHz RC). 32 kHz 크리스털 핀 PA10·PA11이 키 5·6이라 크리스털 없음(확인, 2026-11-13) | 32 kHz 크리스털을 켤 때 재서 판단: 100 ms에 2,900~3,700 클럭이면 크리스털, 아니면 내부 LSI(**코드**). SDK는 central에 크리스털을 권함. 이 동글에 크리스털이 있는지는 확인 필요 |
| 코드 Flash 전체 | 16 KB | 무대기 영역 64 KB + 비무대기 영역(224 KB − 64 KB). 우리 배치는 앞 64 KB만 씀 | 448 KB (**SDK**). 지우기 4 KB 블록, 쓰기 256 B 단위(**코드**) | 448 KB (**SDK**) |
| 부트로더 | ROM 부트로더 2 KB (16 KB 중 BootLoader 영역, 우리 장치는 2.5.0) | ROM 부트로더 28 KB(시스템 영역, BOOT0=1로 진입) + **자체 HID 부트로더 8 KB**(0x08000000~, **코드**) | ROM 부트로더(BTVER 02.90, USB `4348:55E0`, 다운로드 설정 핀 PB22(기본)/PB11, 확인) + **자체 HID 부트로더 8 KB**(0x0000~, USB `1209:C55E`, **코드**, [BOOTLOADER_CH573.md](BOOTLOADER_CH573.md)) | ROM 부트로더(USB `4348:55E0`, 추정). 진입은 PB22를 GND에 댄 채 USB 꽂기로 추정, 동글 기판에 그 패드가 있는지 확인 필요. 자체 부트로더 없음 |
| ROM 부트로더 진입·시간 제한 | 키 D(P1.5)를 누른 채 꽂기 | BOOT0=1(브리지) | PB22를 GND에 댄 채 SW1 켜기. 통신이 없으면 약 6~15초 뒤 앱으로 돌아감(관찰, 정확한 값 확인 필요). wchisp는 WinUSB 드라이버 필요 | 확인 필요 |
| 앱에 쓸 수 있는 양 | 14,336 B (**코드**, 16 KB − 2 KB) | 52,992 B (0x08002000~0x0800EEFF, **코드**). 그 뒤 앱 정보 256 B(0x0800EF00) | 446,464 B (0x3000~0x6FFFF, **코드**). 앞에 앱 정보 4 KB 블록(0x2000) | 448 KB 전체(부트로더 없음, 0x0000~) |
| RAM | iRAM 256 B + xRAM 1 KB | SRAM 20 KB | SRAM 18 KB(0x20003800~, **SDK**) | SRAM 32 KB(0x20000000~, **SDK**) |
| 설정 저장 | DataFlash 128 B (바이트 단위 재기록) | 플래시 끝 4,096 B (0x0800F000~, 업데이트해도 유지, **코드**) | Data-Flash(EEPROM) 32 KB 따로 있음. 설정은 0~2,047(지우기·쓰기 256 B 단위), 슬롯 상태 0x7000, BLE 짝 정보 0x7C00~0x8000(본딩 4개, **코드**). 앱 업데이트해도 유지 | Data-Flash 32 KB. 연결 장치 기록 0x7000(1 KB), 재꽂기 횟수 0x7800, BLE 짝 정보 0x7E00~(**코드**) |
| 문자열 합계 최대 | 102자 | 4,070~4,072자 (키 수에 따라, [BOARDS.md](BOARDS.md#구성)) | 설정 2 KB 기준, 확인 필요 | – (설정 없음) |
| 패키지·핀 수 | SOP16, 16핀 | QFN48, 48핀 (+ 바닥 패드 VSS) | QFN28 ([BOARD_SAYO_2X3B_RGB 3절](board/BOARD_SAYO_2X3B_RGB.md#3-칩-핀맵)) | QFN28, CH573F와 같은 핀 배치(PB22 = 17번, PB11 = 12번, 자료표 CH583DS1) |
| GPIO 수 | 칩 전체 최대 17(TSSOP20 기준). SOP16에 나온 GPIO는 12개 + RST(USB D+/D− 포함, [MK6K1N 3절](board/BOARD_MK6K1N.md#3-칩-핀맵)) | 37 | 확인 필요 | 확인 필요 |
| USB | 장치 전용 풀스피드 1개 (P3.6/P3.7). D+ 1.5 kΩ 풀업 칩 안에 있음(`bUC_DEV_PU_EN`) | USBD(PA11/PA12, 장치 전용) + USBFS(PB6/PB7, 호스트/장치). 우리 펌웨어는 USBFS, 풀업은 칩 안(`USBFS_UC_DEV_PU_EN`) | USB 1개(PB10 UD−/PB11 UD+), 풀스피드 장치로 동작 확인 | USB 2개: USB(PB10/PB11), USB2(PB12/PB13) (**SDK**). 동글이 어느 포트를 쓰는지는 핀 시험으로 확인 필요(`tests/hw/ch582_dongle`) |
| 무선 | 없음 | 없음 | BLE 내장(**SDK** BLE 라이브러리). 이 기판은 패치 안테나. 우리 펌웨어 1단계는 BLE 안 씀 | BLE 내장(**SDK**). 동글 펌웨어는 central(HOGP 호스트), 송신 +4 dBm(**코드**) |
| 공급 전압 | VCC 5 V 또는 3.3 V(2.8 V까지). 칩 안 5 V → 3.3 V 레귤레이터(V33 핀). 플래시 쓰기는 5 V 필요 | VDD 2.4~3.6 V (정격 3.3 V) | VDD33 2.3~3.6 V, 절대 최대 4.2 V(자료표, [BOARD_SAYO_2X3B_RGB 5절](board/BOARD_SAYO_2X3B_RGB.md#전원과-배터리)). 칩 전원은 내부 VBAT 채널로 잼(배터리 → 다이오드 → VDD, LDO 없음) | USB 5 V에서 기판 레귤레이터로 받음(추정). 칩 범위 확인 필요 |
| 고유 ID | 있음 (HELLO 32~35) | 96비트 (HELLO는 앞부분) | 8바이트 UID(확인). 앱 HELLO 32~35는 앞 4바이트, 자체 부트로더 HELLO 21~28은 8바이트 | 있음(8바이트 추정). 동글 UID는 `wchisp info` 전이라 모름 |
| 칩 ID (HELLO 18, **코드**) | `0x52` | `0x19` | `0x73` (자체 부트로더 HELLO 45도 같은 값) | – (설정 통신 없음) |
| USB VID:PID (우리 펌웨어, **코드**) | 앱 `1209:C55D` | 앱 `1209:C55D`, 자체 부트로더 `1209:C55E` | 앱 `1209:C55D`, 자체 부트로더 `1209:C55E` (CH32V203과 같은 값) | 동글 `1209:C55F` (공장 펌웨어는 `8089:0008`) |
| 근거 | 자료표 CH552 V1F, 1장 개요 표·2장 특성·7장 전원 | 자료표 CH32V203 V2.6, 특성·표 2-1(제품 자원)·표 3-1-1(핀 정의) | CH573 EVT SDK(`Link.ld`, 드라이버), WCHISPTool·wchisp, 시험 펌웨어 `tests/hw/ch573/test_ch573` (2026-11-13), 자료표(전압) | CH583 EVT SDK(`Link.ld`), 자료표 CH583DS1(핀 배치만 확인), 동글 펌웨어 `firmware/dongle_ch582`, 칩 표기 사용자 관찰(2026-11-13) |

- CH32V203 보조 펌웨어: JieLi USB_KEY 어댑터 [firmware/jl_usbkey_ch32v203](../firmware/jl_usbkey_ch32v203/README.md)(JUZH_3B4용, **보류**: 공장 벤더 HID `03 EF EF`로 UBOOT 진입이 돼 불필요). `1209:C560` "JieLi USB_KEY adapter", 우리 부트로더 없이 칩 전체 이미지(0x08000000~, 3,428 B), 자체 부트로더의 `usb_hid.c`를 PID·제품 이름만 바꿔 씀

## JieLi AC6321A (JUZH_3B4, 준비 중)

- 칩: JieLi AC6321A, 계열 AC632N, 내부 코드 **BD19**, 아키텍처 q32s(JL clang pi32), BLE 전용, QFN32 4×4, 24 MHz 크리스털
- 이 프로젝트 보드: JUZH_3B4(칩 각인 `BP3D136-21A4`, [보드 문서](board/BOARD_KEY12_JUZHEN_V01.md))
- 확정 근거(2026-11-15, 자세히 [칩 식별](board/BOARD_KEY12_JUZHEN_V01.md#칩-식별-jieli-32핀))
  - UBOOT SCSI INQUIRY "BD19 UBOOT1.00"
  - 실크 점 = 1번 핀. RF 8·OSC 9/10(PCB 패턴), USB0 14/15(도통)
  - 바닥 패드 VSS(= GND) 도통, 23번 PB8 비접지
  - AC632N 계열에서 QFN32 4×4는 AC6321A뿐(SDK 자료표 8종 대조)
- GPIO: PA0~PA9, PB0~PB9, USB1DP·USB1DM(29·28, GPIO로 바꿀 수 있음), PP0(LDOIN, 4번)
- ADC: PA1·PA3·PA5·PA7·PA8·PA9, PB1·PB4·PB6, USB1 DP·DM. 내부 VBAT·LDO5V 1/4
- 주의 핀: 고전압 내성 핀(PB0·PA0·PB7·PB5·PB3, P00), PA9(기본 풀업, 길게 낮추면 리셋), PB3(MCLR). 전체 표는 [보드 문서 3절](board/BOARD_KEY12_JUZHEN_V01.md#3-칩-핀맵)·[ac6321a_pins.h](../tests/hw/ac6321a/test_ac6321a/ac6321a_pins.h)
- 부트로더: 칩 마스크 ROM의 **UBOOT1.00**(USB 저장장치 `4C4A:3442`). 지울 수 없음 → 벽돌 위험 낮음. 자체 부트로더는 만들지 않고 이것을 씀
  - 진입: 공장 펌웨어 = 벤더 HID `03 EF EF`. 우리 펌웨어 = BOOT 명령(또는 `03 EF EF`) → SDK `go_mask_usb_updata()`(ROM `nvram_set_boot_state` 0x106f40)
- UBOOT1.00 명령(PM 시험)
  - 동작 확인: `FB06` RAM 쓰기(+CRC16), `FD07` RAM 읽기, `FB08` 점프
  - 플래시 명령(`FC0B` 등)은 ROM이 거부 → RAM 로더 필요. `bd19loader.bin`(SDK `cpu/bd19/tools`, 공개 것과 같은 파일)을 0x2000에 올리고, RAM 0x7A00에 설정 블록(`UIMMTAG-` + `SPI=2_3_0_0`, MengLi 인코딩)을 쓴 뒤 `FC14`부터 보내면 동작(PM 확인, 2026-11-15). `FC16`은 응답 없음
  - 플래시: SPI NOR, ID `CD 71 13`, 512 KiB(추정). 공장 펌웨어 백업 완료(두 번 읽어 일치). `FC0B` 응답 ID = 바이트 3..5
  - 로더 명령(PM 실측): `FB00` 지우기(64 KiB), `FB04` 쓰기(4 KiB씩), `FD05` 읽기, `FC0E` CRC = crc16(다항 0x1021, 초기 0), `FC0C` 리셋. 시험 이미지 쓰기·검증 성공(2026-11-15). 실행 뒤 USB·BLE 안 잡힘 → 보드 보류
  - ROM 진입 조건(`rom.lst`, PM): USB 키(D+/D− `0x16EF`), UART 키(`0x68AF`), 앱 헤더 CRC 실패 때 USB 폴백뿐. 앱이 USB를 못 열면 하드웨어 없이는 UBOOT로 못 돌아감 → 플래시에 쓰기 전에 RAM 실행 시험
  - Windows SPTI(SCSI 직접 명령)는 관리자 권한 필요
- 개발 환경: SDK fw-AC63_BT_SDK(`cpu/bd19`, `apps/hid`, 대상 `ac632n_hid`), JL 툴체인 2.5.3(`C:\JL`, clang pi32), Code::Blocks
  - **빌드 뒤 스크립트(`RUN_POST_SCRIPT`)는 반드시 끔**: `download.bat` → `isd_download`가 꽂힌 칩에 자동으로 씀
- 자료: SDK 폴더 doc/datasheet/AC632N/AC6321A(Datasheet V1.3, BLE 참고 원리도 V2.0, 마우스 표준 원리도 V1.0)

## 우리 펌웨어의 사용량

- 근거: [firmware/macro_keyboard/README.md](../firmware/macro_keyboard/README.md#크기) "크기" (빌드 시각에 따라 ±4 B), 동글은 [firmware/dongle_ch582/README.md](../firmware/dongle_ch582/README.md)

| 칩 | 코드(1.7.0) | 한도 대비 | RAM |
|---|---|---|---|
| CH552G | 9,629 B (LED 1개 보드), 10,006 B (키마다 LED 보드) | 67~70% / 14,336 B | 1.6.0 기준 전역 변수 약 360 B + 스택 약 120 B (1.7.0 값은 확인 필요) |
| CH32V203C8U6 | 앱 12,292~12,304 B | 약 23% / 52,992 B | 확인 필요 |
| CH573F (1단계, USB 유선) | 앱 12,460 B, 자체 부트로더 5,204 B | 약 3% / 446,464 B | 확인 필요 |
| CH582F (동글) | 169,424 B (BLE 라이브러리 포함) | 약 37% / 448 KB | 정적 약 13 KB / 32 KB |

**이력**

- 2026-11-08: 처음 만듦 (자료표 CH552 V1F, CH32V203 V2.6)
- 2026-11-13: CH573F(SAYO_2B3)·CH582F(동글) 열 추가, 추정 값에서 시작
- 2026-11-13: CH573F·CH582F를 SDK·코드·실제 장치 값으로 바꿈("준비 중"·"추정" 열 이름 삭제). 저속 클록·ROM 진입·VID:PID 행 추가
- 2026-11-15: JieLi AC6321A(JUZH_3B4) 절 추가: 확정 근거, GPIO·ADC·주의 핀, ROM UBOOT1.00(진입·동작 명령·로더 안 됨), 개발 환경
- 2026-11-15: AC6321A RAM 로더 동작 확인(설정 블록 0x7A00 + FC14), 플래시 SPI NOR `CD 71 13`·512 KiB(추정), 공장 펌웨어 백업 완료
- 2026-11-15: AC6321A 플래시 쓰기 성공(로더 명령 FB00·FB04·FD05·FC0E·FC0C, CRC 규칙), FC0B ID 위치
- 2026-11-15: AC6321A ROM 진입 조건(USB 키·UART 키·헤더 CRC 폴백), 시험 장입 뒤 USB 안 잡혀 보드 보류
