#!/usr/bin/env python3
"""Ensure desktop.c has forward declarations for helpers used before definition."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
path = root / "kernel" / "desktop.c"
text = path.read_text(encoding="utf-8")

marker = "static void terminal_add(const char*s);\n"
decls = marker + "static void mark_dirty(void);\nstatic void save_settings(void);\n"

if "static void mark_dirty(void);" not in text or "static void save_settings(void);" not in text:
    if marker not in text:
        raise RuntimeError("desktop helper declaration anchor missing")
    text = text.replace(marker, decls, 1)
    path.write_text(text, encoding="utf-8")
