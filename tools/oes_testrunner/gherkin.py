"""OES-TEST: minimal Russian-Gherkin parser for the OES test runner (docs/test-automation.md).

Supports the core Vanessa-style constructs:
    # comment / language header
    Функционал: <name>            (also: Feature:)
    Контекст:                     (also: Предыстория / Background) — steps run before EVERY scenario
    Сценарий: <name>              (also: Scenario:)
    Дано / Когда / Тогда / И / Также / *  step lines  (also Given/When/Then/And)
Doc-strings, data tables and Scenario Outline (Структура сценария / Примеры) are NOT parsed yet —
they are a documented follow-up. Keeps the MVP small and dependency-free.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

_FEATURE = ("функционал", "feature", "функция", "функциональность")
_SCENARIO = ("сценарий", "scenario")
_BACKGROUND = ("контекст", "предыстория", "background")
_STEP = ("дано", "когда", "тогда", "и", "также", "затем", "*",
         "given", "when", "then", "and", "but")


@dataclass
class Step:
    keyword: str
    text: str
    line: int


@dataclass
class Scenario:
    name: str
    steps: list[Step] = field(default_factory=list)
    line: int = 0


@dataclass
class Feature:
    name: str
    scenarios: list[Scenario] = field(default_factory=list)
    background: list[Step] = field(default_factory=list)


def _split_keyword(line: str):
    """Return (keyword, rest) if the line starts with a step keyword, else (None, line)."""
    stripped = line.strip()
    low = stripped.lower()
    for kw in _STEP:
        if kw == "*":
            if stripped.startswith("*"):
                return "*", stripped[1:].strip()
            continue
        # keyword must be a whole word followed by space
        if low == kw or low.startswith(kw + " "):
            return stripped[:len(kw)], stripped[len(kw):].strip()
    return None, stripped


def parse_feature(text: str) -> Feature:
    feature = Feature(name="")
    current: Scenario | None = None
    in_background = False        # steps go into feature.background, run before EVERY scenario

    for i, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith("//"):
            continue
        if line.startswith("@"):        # tags — ignored for now
            continue

        low = line.lower()

        # Feature: / Функционал:
        if any(low.startswith(k + ":") for k in _FEATURE):
            feature.name = line.split(":", 1)[1].strip()
            continue

        # Background / Контекст / Предыстория (with or without a trailing name)
        if any(low == k or low.startswith(k + ":") for k in _BACKGROUND):
            in_background = True
            current = None
            continue

        # Scenario: / Сценарий:
        if any(low.startswith(k + ":") for k in _SCENARIO):
            in_background = False
            current = Scenario(name=line.split(":", 1)[1].strip(), line=i)
            feature.scenarios.append(current)
            continue

        kw, rest = _split_keyword(line)
        if kw is None:
            continue
        if in_background:
            feature.background.append(Step(keyword=kw, text=rest, line=i))
        elif current is not None:
            current.steps.append(Step(keyword=kw, text=rest, line=i))

    return feature


if __name__ == "__main__":
    import sys
    f = parse_feature(open(sys.argv[1], encoding="utf-8").read())
    print("Feature:", f.name)
    if f.background:
        print("  Background:")
        for st in f.background:
            print(f"    {st.keyword} {st.text}")
    for sc in f.scenarios:
        print("  Scenario:", sc.name)
        for st in sc.steps:
            print(f"    {st.keyword} {st.text}")
