#!/usr/bin/env python3
"""Render the README screenshots into docs/images/ (no hardware needed).

  tools/render/screens.py

Builds tools/render/render_screen.cpp against src/gfx + src/ui with the host
C++ compiler, renders the demo data, and writes PNGs with only the Python
standard library. Needs a C++17 compiler (clang or gcc).
"""
import os, struct, subprocess, sys, tempfile, zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "docs", "images")

SHOTS = [  # (file, page, theme)
    ("overview.png", "overview", "elementarish"),
    ("graphs.png", "graphs", "elementarish"),
    ("settings.png", "settings", "elementarish"),
]
GALLERY = ("themes.png", "overview", ["System-D", "nord", "gruvbox_dark", "tokyo-night", "dracula", "solarized_light"])


def read_ppm(path):
    data = open(path, "rb").read()
    head, _, rest = data.partition(b"\n255\n")
    w, h = map(int, head.split()[1:3])
    return w, h, rest


def write_png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    os.makedirs(OUT, exist_ok=True)
    tmp = tempfile.mkdtemp()
    exe = os.path.join(tmp, "render_screen")
    srcs = [os.path.join(ROOT, "tools", "render", "render_screen.cpp")]
    for d in ("gfx", "ui"):
        srcs += [os.path.join(ROOT, "src", d, f) for f in sorted(os.listdir(os.path.join(ROOT, "src", d)))
                 if f.endswith(".cpp")]
    cxx = os.environ.get("CXX", "c++")
    subprocess.run([cxx, "-std=c++17", "-O2", "-I" + os.path.join(ROOT, "src"), "-I" + os.path.join(ROOT, "include"),
                    *srcs, "-o", exe], check=True)

    def render(page, theme):
        ppm = os.path.join(tmp, f"{page}-{theme}.ppm")
        subprocess.run([exe, page, theme, ppm], check=True)
        return read_ppm(ppm)

    for name, page, theme in SHOTS:
        w, h, rgb = render(page, theme)
        write_png(os.path.join(OUT, name), w, h, rgb)
        print("wrote", os.path.relpath(os.path.join(OUT, name), ROOT))

    name, page, themes = GALLERY
    tiles = [render(page, t) for t in themes]
    w, h = tiles[0][0], tiles[0][1]
    gap = 8
    cols = 2 if len(tiles) <= 4 else 3
    rows = (len(tiles) + cols - 1) // cols
    W, H = cols * w + (cols - 1) * gap, rows * h + (rows - 1) * gap
    sheet = bytearray(b"\x20" * (W * H * 3))
    for k, (_, _, rgb) in enumerate(tiles):
        r, c = divmod(k, cols)
        in_row = min(cols, len(tiles) - r * cols)  # a short last row is centred
        ox = (W - (in_row * w + (in_row - 1) * gap)) // 2 + c * (w + gap)
        oy = r * (h + gap)
        for y in range(h):
            sheet[((oy + y) * W + ox) * 3:((oy + y) * W + ox + w) * 3] = rgb[y * w * 3:(y + 1) * w * 3]
    write_png(os.path.join(OUT, name), W, H, bytes(sheet))
    print("wrote", os.path.relpath(os.path.join(OUT, name), ROOT), "(" + ", ".join(themes) + ")")


if __name__ == "__main__":
    sys.exit(main())
