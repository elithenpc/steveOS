from pathlib import Path
from PIL import Image

src = Path("blehhh.png")
out = Path("build/boot.raw")
out.parent.mkdir(parents=True, exist_ok=True)

img = Image.open(src).convert("RGBA")
w, h = img.size

# Store a tiny header followed by BGRA pixels.
data = bytearray()
data += w.to_bytes(4, "little")
data += h.to_bytes(4, "little")

for r, g, b, a in img.getdata():
    data += bytes((b, g, r, a))

out.write_bytes(data)
print(f"Converted {src} ({w}x{h}) -> {out} ({len(data):,} bytes)")
