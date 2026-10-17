"""Render our rect-only SVG source to a PNG and a multi-resolution Windows ICO.

Run with Python + Pillow. No dependency is needed when building the app itself:
the generated ICO is included in the project.
"""
from io import BytesIO
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SVG = ET.parse(ROOT / "assets/vdesktop.svg").getroot()
NS = {"s": "http://www.w3.org/2000/svg"}


def rgb(value):
    return tuple(int(value[i:i + 2], 16) for i in (1, 3, 5))


def render(size):
    scale = size * 4 / 256
    dim = size * 4
    result = Image.new("RGBA", (dim, dim))
    for rect in SVG.findall("s:rect", NS):
        x, y, w, h = (float(rect.get(key)) * scale for key in ("x", "y", "width", "height"))
        radius = float(rect.get("rx", "0")) * scale
        bounds = (x, y, x + w, y + h)
        mask = Image.new("L", result.size)
        ImageDraw.Draw(mask).rounded_rectangle(bounds, radius=radius, fill=255)
        fill = rect.get("fill")
        if fill.startswith("url"):
            gradient = SVG.find("s:defs/s:linearGradient", NS)
            stops = [rgb(s.get("stop-color")) for s in gradient]
            layer = Image.new("RGBA", result.size)
            draw = ImageDraw.Draw(layer)
            for row in range(dim):
                t = max(0, min(1, (row - y) / h))
                color = tuple(round(a + (b - a) * t) for a, b in zip(*stops))
                draw.line((0, row, dim, row), fill=(*color, 255))
        else:
            layer = Image.new("RGBA", result.size, (*rgb(fill), 255))
        result.paste(layer, (0, 0), mask)
        if rect.get("stroke"):
            stroke = float(rect.get("stroke-width")) * scale
            # SVG strokes straddle the shape boundary; Pillow strokes are inset.
            outline = (x - stroke / 2, y - stroke / 2, x + w + stroke / 2, y + h + stroke / 2)
            ImageDraw.Draw(result).rounded_rectangle(outline, radius=radius + stroke / 2,
                outline=(*rgb(rect.get("stroke")), 255), width=max(1, round(stroke)))
    return result.resize((size, size), Image.Resampling.LANCZOS)


sizes = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
frames = []
for size in sizes:
    stream = BytesIO()
    render(size).save(stream, format="PNG")
    frames.append(stream.getvalue())
offset = 6 + 16 * len(sizes)
directory = bytearray(struct.pack("<HHH", 0, 1, len(sizes)))
for size, frame in zip(sizes, frames):
    directory.extend(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(frame), offset))
    offset += len(frame)
(ROOT / "assets/vdesktop.ico").write_bytes(directory + b"".join(frames))
render(256).save(ROOT / "assets/vdesktop.png")
print("Created vdesktop.ico:", ", ".join(map(str, sizes)))
