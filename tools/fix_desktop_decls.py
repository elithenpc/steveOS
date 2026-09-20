#!/usr/bin/env python3
"""Place desktop.c forward declarations before every generated helper use."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = root / "kernel" / "desktop.c"
text = path.read_text(encoding="utf-8")

# Generated desktop.c changes over time, so never depend on a generated
# declaration anchor being above the first helper. Put the declarations at
# the beginning of the translation unit, before any function can use them.
for decl in ("static void mark_dirty(void);\n", "static void save_settings(void);\n"):
    text = text.replace(decl, "")

prefix = "static void mark_dirty(void);\nstatic void save_settings(void);\n"
text = prefix + text
path.write_text(text, encoding="utf-8")
