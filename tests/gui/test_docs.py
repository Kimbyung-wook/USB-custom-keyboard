"""The Markdown docs agree with the repository.

- generated regions are up to date (tools/gen_docs.py --check)
- relative links and #anchors resolve
- repository paths written in backticks exist
- the docs that list boards name every board of gui/core/boards.py
- CHANGELOG.md "미출시" items have the agreed form (when the file exists)

Problems known when a check was added sit in KNOWN; each must still be
there (remove it from KNOWN once the doc is fixed), so the list only shrinks.
"""
from __future__ import annotations

import re
import sys
import warnings
from pathlib import Path

import pytest

from gui.core import boards

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import gen_docs  # noqa: E402

DOCS = gen_docs.md_files()
# History: describes the repository as it was, not checked for paths and links
HISTORY = ("docs/old/", "CHANGELOG.md")
# Paths that exist only after a step (gitignored): tools/build_gui.ps1 writes it
GENERATED_PATHS = {"gui/_build_info.py"}


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def body(path: Path) -> str:
    """The doc without fenced code blocks (their text is not links or prose)."""
    return re.sub(r"^(```|~~~).*?^\1", "", path.read_text(encoding="utf-8"), flags=re.S | re.M)


def current(path: Path) -> bool:
    return not rel(path).startswith(HISTORY)


# ---- generated regions --------------------------------------------------------

def test_generated_regions_up_to_date():
    out: list[str] = []
    count, stale = gen_docs.process(write=False, out=out.append)
    if count == 0:
        warnings.warn("no gen:<name> regions in the docs yet (tools/gen_docs.py has nothing to check)")
    assert not stale, "run: uv run python tools/gen_docs.py\n" + "\n".join(out)


def test_every_region_name_is_known():
    for path in DOCS:
        for m in gen_docs.REGION.finditer(path.read_text(encoding="utf-8")):
            gen_docs.generator(m.group(2))     # KeyError for an unknown name


# ---- links and anchors --------------------------------------------------------

def slug(heading: str) -> str:
    """GitHub's anchor of a heading: lower case, punctuation dropped, spaces -> -."""
    s = re.sub(r"<[^>]+>|`", "", heading).strip().lower()
    s = re.sub(r"[^\w\- ]", "", s)
    return s.replace(" ", "-")


def anchors(path: Path) -> set[str]:
    out: set[str] = set()
    seen: dict[str, int] = {}
    for m in re.finditer(r"^#{1,6}\s+(.+?)\s*#*\s*$", body(path), re.M):
        s = slug(m.group(1))
        n = seen.get(s, 0)
        seen[s] = n + 1
        out.add(s if n == 0 else f"{s}-{n}")
    out.update(re.findall(r'<a\s+(?:id|name)="([^"]+)"', path.read_text(encoding="utf-8")))
    return out


LINK = re.compile(r"(?<!!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)|!\[[^\]]*\]\(([^)\s]+)\)")


def link_problems() -> list[str]:
    out = []
    for path in DOCS:
        if not current(path):
            continue
        for m in LINK.finditer(body(path)):
            target = m.group(1) or m.group(2)
            if re.match(r"[a-z]+:", target):            # http:, mailto:, ...
                continue
            file_part, _, anchor = target.partition("#")
            dest = (path.parent / file_part).resolve() if file_part else path
            if not dest.exists():
                out.append(f"{rel(path)}: link {target} (no such file)")
            elif anchor and dest.suffix == ".md" and anchor not in anchors(dest):
                out.append(f"{rel(path)}: link {target} (no such heading)")
    return out


# ---- repository paths in backticks -------------------------------------------

PATH = re.compile(r"`((?:tools|firmware|docs|gui|tests)[/\\][^`\s]*)")


def path_exists(p: str) -> bool:
    p = p.replace("\\", "/").rstrip(".,:;)")
    p = re.sub(r":\d+$", "", p)                          # file:line
    if p in GENERATED_PATHS or re.search(r"(^|/)(build|dist)(/|$)", p):
        return True                                       # build output, not in the repo
    if "<" in p or "*" in p or "{" in p:                  # placeholder or pattern
        pattern = re.sub(r"<[^>]*>|\{[^}]*\}", "*", p)
        return any(ROOT.glob(pattern))
    return (ROOT / p).exists()


def path_problems() -> list[str]:
    out = []
    for path in DOCS:
        if not current(path):
            continue
        for p in sorted(set(PATH.findall(path.read_text(encoding="utf-8")))):
            if not path_exists(p):
                out.append(f"{rel(path)}: path {p}")
    return out


# ---- board lists ------------------------------------------------------------

BOARD_DOCS = ("README.md", "docs/BOARDS.md", "docs/BOARD_INFO.md")


def board_problems() -> list[str]:
    out = []
    for name in BOARD_DOCS:
        src = (ROOT / name).read_text(encoding="utf-8")
        if "<!-- gen:boards -->" in src:
            continue                                      # generated, checked above
        out += [f"{name}: board {b.name} not named" for b in boards.BOARDS if b.name not in src]
    return out


# ---- CHANGELOG --------------------------------------------------------------

# The form CHANGELOG.md itself sets out ("양식 규칙")
AREAS = "펌웨어|부트로더|GUI|도구|테스트|문서|저장소"
UNRELEASED_ITEM = re.compile(
    rf"^- \[({AREAS})\] (.+?) — 문서 영향: (없음|[^ ,]+(?:, [^ ,]+)*) — 반영: (☐|☑)$")
RELEASED_ITEM = re.compile(rf"^- \[({AREAS})\] (.+)$")
VERSION_HEAD = re.compile(r"^\d+\.\d+(\.\d+)? \(\d{4}-\d{2}-\d{2}\)$")


def doc_ref_problem(ref: str) -> str:
    """"docs/MANUAL.md#anchor" -> "" when the file (and heading) exist."""
    file_part, _, anchor = ref.partition("#")
    dest = ROOT / file_part
    if not dest.exists():
        return "no such file"
    if anchor and anchor not in anchors(dest):
        return "no such heading"
    return ""


def changelog_problems() -> list[str]:
    path = ROOT / "CHANGELOG.md"
    if not path.exists():
        return []
    sections = re.split(r"^## (.+?)\s*$", body(path), flags=re.M)[1:]
    heads, texts = sections[0::2], sections[1::2]
    if not heads or heads[0] != "미출시":
        return ["CHANGELOG.md: the first '##' section must be '## 미출시'"]
    out = []
    for head, txt in zip(heads, texts):
        items = [line for line in txt.splitlines() if line.startswith("- ")]
        stray = [line for line in txt.splitlines()
                 if line.strip() and not line.startswith(("- ", "<!--", "### "))]
        out += [f"CHANGELOG.md: '{head}': not an item: {line[:60]}" for line in stray]
        if head == "미출시":
            for line in items:
                if line == "- 없음":
                    continue
                m = UNRELEASED_ITEM.match(line)
                if not m:
                    out.append(f"CHANGELOG.md: 미출시 item form: {line[:70]}")
                    continue
                if " — " in m.group(2):
                    out.append(f"CHANGELOG.md: ' — ' inside the change text: {line[:70]}")
                if m.group(3) != "없음":
                    for ref in m.group(3).split(", "):
                        problem = doc_ref_problem(ref)
                        if problem:
                            out.append(f"CHANGELOG.md: 문서 영향 {ref} ({problem})")
        else:
            if not VERSION_HEAD.match(head):
                out.append(f"CHANGELOG.md: version heading form: '## {head}'")
            out += [f"CHANGELOG.md: '{head}' item form: {line[:70]}" for line in items
                    if not RELEASED_ITEM.match(line) or " — 문서 영향:" in line]
    return out


# ---- the checks, with the problems known when they were added ----------------

KNOWN: set[str] = set()


@pytest.mark.parametrize("find", [link_problems, path_problems, board_problems, changelog_problems],
                         ids=["links", "paths", "boards", "changelog"])
def test_docs(find):
    found = set(find())
    new = sorted(found - KNOWN)
    assert not new, "\n".join(new)


def test_known_problems_still_there():
    """A fixed problem leaves KNOWN, so the list never hides a new one."""
    found = set(link_problems() + path_problems() + board_problems() + changelog_problems())
    assert not sorted(KNOWN - found), "fixed, remove from KNOWN:\n" + "\n".join(sorted(KNOWN - found))
