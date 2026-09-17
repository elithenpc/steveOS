#!/usr/bin/env python3
from __future__ import annotations

import struct
from pathlib import Path
from PIL import Image

ROOT = Path("third_party/mint-y-icons/usr/share/icons/Mint-Y")
OUT = Path("build/mint_icons.raw")

ICONS = [
    ("apps/64/browser.png", "WEB"),
    ("apps/64/accessories-calculator.png", "CALC"),
    ("apps/64/accessories-text-editor.png", "NOTE"),
    ("places/64/folder.png", "FILES"),
    ("apps/64/Terminal.png", "TERM"),
    ("apps/64/gnome-system-monitor.png", "TASK"),
    ("apps/64/cinnamon-preferences-color.png", "SET"),
    ("apps/64/calendar.png", "DATE"),
    ("places/64/user-home.png", "HOME"),
    ("places/64/folder-documents.png", "DOCS"),
    ("places/64/folder-download.png", "DOWN"),
    ("places/64/folder-pictures.png", "PICS"),
    ("places/64/gtk-network.png", "NET"),
    ("apps/64/cinnamon-preferences-desktop-display.png", "DISPLAY"),
    ("apps/64/cs-power.png", "POWER"),
    ("apps/64/hwinfo.png", "INFO"),
]

def main() -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    blob = bytearray()
    blob += struct.pack("<II", 0x43494D59, len(ICONS))
    for filename, name in ICONS:
        path = ROOT / filename
        if not path.exists():
            raise SystemExit(f"missing Mint-Y icon: {path}")
        with Image.open(path).convert("RGBA") as im:
            im.thumbnail((48, 48), Image.Resampling.LANCZOS)
            px = im.load()
            w, h = im.size
            name_bytes = name.encode("ascii")[:15]
            name_field = name_bytes + b"\0" * (16 - len(name_bytes))
            blob += struct.pack("<HHI16s", w, h, w * h * 4, name_field)
            for y in range(h):
                for x in range(w):
                    r, g, b, a = px[x, y]
                    blob += bytes((b, g, r, a))
    OUT.write_bytes(blob)
    print(f"Packed {len(ICONS)} Mint-Y icons -> {OUT} ({len(blob)} bytes)")

if __name__ == "__main__":
    main()
