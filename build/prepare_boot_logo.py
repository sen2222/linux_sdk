#!/usr/bin/env python3
"""Generate the bootloader/kernel logos from rootfs/share/res/power_on_logo.jpg."""

from io import BytesIO
from pathlib import Path

try:
    from PIL import Image, ImageOps
except ImportError:
    raise SystemExit("Boot logo conversion requires Pillow (python3-pil).")


sdk_root = Path(__file__).resolve().parent.parent
source = sdk_root / "system/rootfs/rootfs/share/res/power_on_logo.jpg"
with Image.open(source) as image:
    image = ImageOps.exif_transpose(image).convert("RGB")
    if image.size != (800, 480):
        raise SystemExit("power_on_logo.jpg must be 800x480.")
    # Linux CLUT224 accepts at most 224 colours; U-Boot reserves 16 entries.
    indexed = image.quantize(colors=224, method=0)  # Median cut, including older Pillow.

bmp = BytesIO()
indexed.save(bmp, format="BMP")
palette = indexed.getpalette()
colours = [f"{palette[i]} {palette[i + 1]} {palette[i + 2]}".encode("ascii")
           for i in range(0, len(palette), 3)]
pixels = indexed.tobytes()
ppm = b"P3\n# Generated from rootfs/share/res/power_on_logo.jpg\n800 480\n255\n"
ppm += b"\n".join(b" ".join(colours[p] for p in pixels[i:i + 4])
                  for i in range(0, len(pixels), 4)) + b"\n"


def publish(path, content):
    # Keep timestamps when unchanged, so repeated builds do not rebuild logos.
    if path.exists() and path.read_bytes() == content:
        return
    temporary = path.with_suffix(path.suffix + ".tmp")
    try:
        temporary.write_bytes(content)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"Boot logo: {path.relative_to(sdk_root)}")


publish(sdk_root / "system/uboot/tools/logos/mx6ull_sen_emmc.bmp", bmp.getvalue())
publish(sdk_root / "system/kernel/drivers/video/logo/logo_linux_clut224.ppm", ppm)
