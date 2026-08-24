"""OES-IMPORT: generate GUI smoke .feature files for imported 1C objects.

For each catalog in a spec JSON, emit a scenario that opens its OBJECT form and asserts every form
field (Code, Description + each imported attribute) exists, then opens its LIST form. One .feature
per spec, one scenario per object, sharing a Контекст that launches Enterprise once (the runner
reconnects to the already-open client between scenarios).

Usage:
  python tools/gen_smoke_features.py <spec.json> <base_dir> <out.feature>
"""

from __future__ import annotations

import json
import os
import sys


def esc(name: str) -> str:
    return name.replace('"', "'")


def gen(spec_path: str, base_dir: str, out_path: str) -> int:
    with open(spec_path, encoding="utf-8") as f:
        spec = json.load(f)
    catalogs = spec.get("catalogs", [])

    L = ["# language: ru",
         f"# Автогенерация: дымовые GUI-тесты импортированных справочников ({os.path.basename(spec_path)})",
         "",
         "Функционал: Дымовое тестирование импортированных справочников",
         "",
         "  Контекст:",
         f'    Дано Я запускаю предприятие на базе "{base_dir}"',
         "    И Я закрываю все открытые окна",
         ""]

    n_obj = n_field = 0
    for c in catalogs:
        name = c.get("name", "")
        if not name:
            continue
        attrs = [a.get("name", "") for a in c.get("attributes", []) if a.get("name")]
        n_obj += 1

        L.append(f"  Сценарий: {name} — форма объекта")
        L.append(f'    Когда Я открываю форму объекта справочника "{esc(name)}"')
        L.append('    Тогда Поле "Code" существует')
        L.append('    И Поле "Description" существует')
        n_field += 2
        for a in attrs:
            L.append(f'    И Поле "{esc(a)}" существует')
            n_field += 1
        L.append("    И Я закрываю все открытые окна")
        L.append("")

        L.append(f"  Сценарий: {name} — форма списка")
        L.append(f'    Когда Я открываю форму списка справочника "{esc(name)}"')
        L.append("    И Я закрываю все открытые окна")
        L.append("")

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(L))
    print(f"wrote {out_path}: {n_obj} объектов, {n_field} проверок полей")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("usage: gen_smoke_features.py <spec.json> <base_dir> <out.feature>")
        raise SystemExit(2)
    raise SystemExit(gen(sys.argv[1], sys.argv[2], sys.argv[3]))
