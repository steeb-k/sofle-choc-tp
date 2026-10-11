#!/usr/bin/env python3
"""Render the left-half OLED artwork and emit keymaps/sofle_choc_tp/oled_name.h.

The left OLED runs at OLED_ROTATION_270: a logical 32 px wide by 128 px tall
canvas, stored as 8-pixel vertical byte columns (index = x + (y / 8) * 32),
which is what oled_write_raw_P takes. Two things are generated:

- the name ('kznjk') drawn horizontally in a bold font, rotated to read from the
  top of the screen downwards and top-aligned (ink starts on the first pixel
  row), OLED_NAME_LINES text lines tall;
- one 32x16 label per host OS (macOS, linux, win, ios, ?): a white rounded box
  with the word knocked out in the OLED's own 6x8 font (glcdfont.c from the
  vendor tree), OLED_OS_LINES tall, indexed by OLED_OS_* in the header.

Usage: make_oled_name.py [--text kznjk] [--lines 9] [--font path] [--size N]
                         [--reuse]   # keep the name pixels already in the header
Writes the header and layout/oled_preview.png (the whole screen as keymap.c
draws it). Needs Pillow (pip install pillow, e.g. into .venv).
"""
import argparse
import platform
import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
WIDTH = 32
OS_LABELS = [("OLED_OS_MAC", "macOS"), ("OLED_OS_LINUX", "linux"), ("OLED_OS_WIN", "win"), ("OLED_OS_IOS", "ios"), ("OLED_OS_UNKNOWN", "?")]
OS_LINES = 2
# Rows as keymap.c lays them out (used for the preview only).
OS_ROW, LAYER_ROW = 10, 13
DEFAULT_FONT = "/System/Library/Fonts/Supplemental/Arial Bold.ttf" if platform.system() == "Darwin" else "/usr/share/fonts/liberation/LiberationSans-Bold.ttf"


def render_name(text: str, lines: int, font_path: str, size: int, rotate: int) -> Image.Image:
    height = lines * 8
    font = ImageFont.truetype(font_path, size)
    canvas = Image.new("1", (height * 2, WIDTH * 3), 0)
    ImageDraw.Draw(canvas).text((8, 8), text, font=font, fill=1)
    bbox = canvas.getbbox()
    if bbox is None:
        raise SystemExit("font rendered nothing")
    ink = canvas.crop(bbox)
    if ink.width > height or ink.height > WIDTH:
        raise SystemExit(f"'{text}' renders {ink.width}x{ink.height}, exceeds {height}x{WIDTH}; lower --size")
    strip = Image.new("1", (height, WIDTH), 0)
    strip.paste(ink, (0, (WIDTH - ink.height) // 2))  # ink at the start of the strip = top of the screen after rotation
    return strip.rotate(rotate, expand=True)  # top_align() in main() puts the ink on row 0 either way


def top_align(img: Image.Image, lines: int) -> Image.Image:
    bbox = img.getbbox()
    out = Image.new("1", (WIDTH, lines * 8), 0)
    if bbox:
        out.paste(img.crop((0, bbox[1], WIDTH, bbox[3])), (0, 0))
    return out


def unpack(data: bytes, lines: int) -> Image.Image:
    img = Image.new("1", (WIDTH, lines * 8), 0)
    px = img.load()
    for page in range(lines):
        for x in range(WIDTH):
            b = data[page * WIDTH + x]
            for bit in range(8):
                if b >> bit & 1:
                    px[x, page * 8 + bit] = 1
    return img


def pack(img: Image.Image) -> bytes:
    w, h = img.size
    assert w == WIDTH and h % 8 == 0
    px = img.load()
    out = bytearray()
    for page in range(h // 8):
        for x in range(w):
            b = 0
            for bit in range(8):
                if px[x, page * 8 + bit]:
                    b |= 1 << bit
            out.append(b)
    return bytes(out)


def load_qmk_font(path: Path) -> list:
    src = path.read_text()
    src = src[src.index("font[]"):]
    return [int(h, 16) for h in re.findall(r"0x([0-9A-Fa-f]{2})", src)]


def draw_text(img: Image.Image, font: list, x0: int, y0: int, s: str, ink: int) -> None:
    px = img.load()
    for i, c in enumerate(s):
        glyph = font[ord(c) * 6:ord(c) * 6 + 6]
        for cx in range(6):
            for bit in range(8):
                if glyph[cx] >> bit & 1:
                    px[x0 + i * 6 + cx, y0 + bit] = ink


def render_os_label(font: list, word: str) -> Image.Image:
    img = Image.new("1", (WIDTH, OS_LINES * 8), 0)
    ImageDraw.Draw(img).rounded_rectangle((0, 0, WIDTH - 1, OS_LINES * 8 - 1), radius=2, fill=1)
    draw_text(img, font, (WIDTH - 6 * len(word)) // 2, 4, word, 0)
    return img


def c_rows(data: bytes) -> str:
    return "\n".join("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + WIDTH]) + "," for i in range(0, len(data), WIDTH))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--text", default="kznjk")
    ap.add_argument("--lines", type=int, default=9, help="name height in 8 px text lines")
    ap.add_argument("--font", default=DEFAULT_FONT)
    ap.add_argument("--size", type=int, default=30)
    ap.add_argument("--rotate", type=int, default=-90, help="-90 reads downward with letter tops to the right; 90 the other way")
    ap.add_argument("--reuse", action="store_true", help="keep the name pixels from the existing header (no font needed), top-aligned")
    ap.add_argument("--qmk-font", default=str(ROOT / "vendor/vial-qmk/drivers/oled/glcdfont.c"))
    ap.add_argument("--out", default=str(ROOT / "keymaps/sofle_choc_tp/oled_name.h"))
    ap.add_argument("--preview", default=str(ROOT / "layout/oled_preview.png"))
    args = ap.parse_args()

    if args.reuse:
        src = Path(args.out).read_text()
        m = re.search(r"OLED_NAME_LINES (\d+)", src)
        body = src[src.index("oled_name_bitmap"):]
        body = body[:body.index("};")]
        old = bytes(int(h, 16) for h in re.findall(r"0x([0-9A-Fa-f]{2})", body))
        name = top_align(unpack(old, int(m.group(1))), args.lines)
    else:
        name = top_align(render_name(args.text, args.lines, args.font, args.size, args.rotate), args.lines)
    qmk_font = load_qmk_font(Path(args.qmk_font))
    labels = [(ident, word, render_os_label(qmk_font, word)) for ident, word in OS_LABELS]

    header = [
        "// Generated by tools/make_oled_name.py -- do not edit by hand.",
        f"// '{args.text}' as a {WIDTH}x{args.lines * 8} bitmap for the OLED_ROTATION_270 left display,",
        "// packed as vertical byte columns (oled_write_raw_P format), ink top-aligned,",
        f"// plus one {WIDTH}x{OS_LINES * 8} host-OS label (white rounded box, word knocked out in the",
        "// OLED's 6x8 font) per OLED_OS_* index.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"#define OLED_NAME_LINES {args.lines}",
        f"#define OLED_OS_LINES {OS_LINES}",
    ]
    header += [f"#define {ident} {i}  // \"{word}\"" for i, (ident, word, _) in enumerate(labels)]
    header += [
        "",
        f"static const char PROGMEM oled_name_bitmap[{WIDTH} * OLED_NAME_LINES] = {{",
        c_rows(pack(name)),
        "};",
        "",
        f"static const char PROGMEM oled_os_bitmap[{len(labels)}][{WIDTH} * OLED_OS_LINES] = {{",
    ]
    for ident, word, img in labels:
        header += [f"    [{ident}] = {{  // {word}", c_rows(pack(img)), "    },"]
    header += ["};", ""]
    Path(args.out).write_text("\n".join(header))

    # Preview: the whole 32x128 screen as oled_task_user draws it (macOS host, base layer).
    screen = Image.new("1", (WIDTH, 128), 0)
    screen.paste(name, (0, 0))
    screen.paste(labels[0][2], (0, OS_ROW * 8))
    draw_text(screen, qmk_font, 0, LAYER_ROW * 8, "Layer", 1)
    draw_text(screen, qmk_font, 0, (LAYER_ROW + 1) * 8, "kbd", 1)
    screen.resize((WIDTH * 4, 128 * 4), Image.NEAREST).save(args.preview)
    print(f"wrote {args.out} and {args.preview}")


if __name__ == "__main__":
    main()
