# 출시 순서

- 새 버전(펌웨어·GUI 같은 번호)을 낼 때 위에서부터 차례로. 한 단계라도 실패하면 거기서 멈추고 고침
- 변경 기록: [CHANGELOG.md](../CHANGELOG.md) (항목 양식은 그 파일 맨 위)
- 평소: 코드를 바꾸면 그때마다 CHANGELOG `## 미출시`에 한 줄 추가(`반영: ☐`), 문서를 고치면 `☑`

## 체크리스트

| # | 할 일 | 확인 |
|---|---|---|
| 1 | CHANGELOG `## 미출시` 항목의 "문서 영향" 문서를 모두 고침 | 모든 항목이 `반영: ☑` |
| 2 | 버전 올림: `firmware/macro_keyboard/src/config.h`(`FW_MAJOR`/`FW_MINOR`/`FW_PATCH`), `gui/core/version.py`, `pyproject.toml` | 세 곳 같은 번호 (`test_consistency.py`가 검사) |
| 3 | `uv run python tools\gen_docs.py` | 생성 구역(`gen:이름` 표시)이 최신, 문서 검사 통과 |
| 4 | 보드 4개 빌드: `tools\build_fw.ps1 -Board <보드>` (mk6k1n, mk6k1nl, sayo_3x3, sayo_2x4) | 4개 모두 성공, 크기 확인 |
| 5 | `uv run pytest` | 전부 통과 (GUI + host + 교차 검사) |
| 6 | 바뀐 보드의 실제 장치 확인 (아래 표) | 보드 문서 7절 "확인 결과"에 날짜와 결과 |
| 7 | CHANGELOG `## 미출시` → `## <버전> (<YYYY-MM-DD>)`, 각 항목의 "문서 영향"·"반영" 칸 지움. 각 문서 "이력"에 같은 날짜로 한 줄 | 미출시 절은 `- 없음` |
| 8 | 커밋 (사용자가 "Commit"이라고 한 뒤, push 안 함) | 다른 세션의 변경과 섞이지 않게 파일을 나눠서 |

- 장입은 사용자가 "ㄱㄱ"라고 한 뒤에만 (6단계 전)

## 6단계: 실제 장치 확인

- 바뀐 보드마다. 점검 도구: `uv run python tools\check_device.py` (HELLO 보드 비교, 설정 읽기), 저장 유지는 `--write` → 다시 꽂기 → `--verify` → `--restore <백업>`

| # | 항목 | 확인 방법 |
|---|---|---|
| 1 | 장입 | GUI 업데이트 창 또는 `tools\flash.ps1`, 버전·빌드 날짜가 연결 막대에 표시 |
| 2 | HELLO | `check_device` 통과 (보드 이름, 키/노브/행, 칩) |
| 3 | 설정 유지 | 장입 전 설정이 그대로 |
| 4 | 키·노브 | 메모장에서 키마다 입력 |
| 5 | LED | 꽂을 때 색 바퀴, 상태 색, 누르는 동안 초록 |
| 6 | 잠금 | 다시 꽂으면 잠김 → 패턴으로 풀림 |
| 7 | 업데이트 모드 | 진입 키로 들어감(보라) |

- 새 보드를 처음 확인할 때는 [NEW_BOARD.md](NEW_BOARD.md) 6절의 표 전체
