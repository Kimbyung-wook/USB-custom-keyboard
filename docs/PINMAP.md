# 키보드별 핀 배치 (안내)

- 보드별 문서로 나눔 (2026-11-07)
  - 요약 표: [BOARDS.md](BOARDS.md)
  - 보드마다 2절 버튼 배치와 매핑, 3절 칩 핀맵(그림 + 표)
  - CH552G 핀, 노브 신호: [BOARD_MK6K1NL.md](board/BOARD_MK6K1NL.md)
  - [BOARD_MK6K1NL.md](board/BOARD_MK6K1NL.md), [BOARD_SAYO_3X3_RGB.md](board/BOARD_SAYO_3X3_RGB.md), [BOARD_SAYO_2X4_RGB.md](board/BOARD_SAYO_2X4_RGB.md), [BOARD_SAYO_2X6_RGB.md](board/BOARD_SAYO_2X6_RGB.md)
  - CH32V203C8U6 48핀 칩 핀맵: [SAYO_3X3 3절](board/BOARD_SAYO_3X3_RGB.md#3-칩-핀맵), [SAYO_2X4 3절](board/BOARD_SAYO_2X4_RGB.md#3-칩-핀맵)
  - 준비 중 CH573F QFN28 칩 핀맵: [SAYO_2B3 3절](board/BOARD_SAYO_2X3B_RGB.md#3-칩-핀맵)
  - 준비 중 CH582F 동글(QFN28, CH573F와 같은 핀 배치): [BOARD_DONGLE_CH582.md 3절](board/BOARD_DONGLE_CH582.md#3-칩-핀맵)
  - 문서 양식·그림 다시 그리기: [board/_TEMPLATE.md](board/_TEMPLATE.md)
- 칩 자료표(PDF)는 저장소 밖 상위 폴더 `MacroKeyboard/reference/`: `CH552.pdf`, `CH32V203.pdf`(V2.6, 핀 정의는 표 3-1-1). CH573F·CH582F는 자료표 없음(값의 근거는 [CHIPS.md](CHIPS.md))
- 새 키보드를 받으면: [BOARDS.md](BOARDS.md) "새 보드 추가"
- 그림 스크립트 `tools/draw_pinmap.py`가 그리는 그림은 `docs/images/`, 쓰는 곳은 보드 문서와 BOARDS.md

**이력**

- 2026-11-07: 보드별 문서로 나눔
- 2026-11-13: SAYO_2X6, 준비 중 SAYO_2B3(CH573F)·동글(CH582F) 링크
