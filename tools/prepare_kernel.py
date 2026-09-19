#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
version = (root / "VERSION").read_text(encoding="utf-8").strip()
if not re.fullmatch(r"\d+\.\d+\.\d+", version):
    raise SystemExit(f"Invalid VERSION: {version!r}")

path = root / "src" / "kernel.c"
text = path.read_text(encoding="utf-8")
updated, count = re.subn(r'#define STEVEOS_VERSION "[^"]+"', f'#define STEVEOS_VERSION "{version}"', text, count=1)
if count != 1:
    raise SystemExit("STEVEOS_VERSION definition not found")
path.write_text(updated, encoding="utf-8")
