#!/usr/bin/env python3
"""Place desktop.c forward declarations before every generated helper use."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = root / "kernel" / "desktop.c"
text = path.read_text(encoding="utf-8")

# Remove any copies first. This makes the fixer safe to run repeatedly and
# corrects older generated files where the declarations landed too late.
text = text.replace("static void mark_dirty(void);\n", "")
text = text.replace("static void save_settings(void);\n", "")

marker = "static void terminal_add(const char*s);\n"
if marker not in text:
    raise RuntimeError("desktop helper declaration anchor missing")

decls = marker + "static void mark_dirty(void);\nstatic void save_settings(void);\n"
text = text.replace(marker, decls, 1)
path.write_text(text, encoding="utf-8")
