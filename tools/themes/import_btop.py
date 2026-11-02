#!/usr/bin/env python3
# Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
# SPDX-License-Identifier: MIT
"""Convert btop's themes into src/gfx/themes_btop.h.

  tools/themes/import_btop.py      # reads third_party/btop/, writes src/gfx/themes_btop.h

btop themes are `theme[key]="#rrggbb"` files; "#gg" is a grey shorthand and
an empty value means "default". Like btop, every key a theme doesn't set
comes from the built-in Default theme (third_party/btop/default_theme.txt).
btop is Apache-2.0: third_party/btop/LICENSE.

Role mapping (SPEC.md §6):
  BG main_bg · VALUE main_fg · TITLE title · LABEL graph_text · DIM inactive_fg
  (DIM and LABEL lifted towards main_fg to their MIN_CONTRAST against BG if fainter)
  BORDER/BOX2/BOX3/BOX4 cpu_box/mem_box/net_box/proc_box (one per section box)
  GOOD/WARN/BAD cpu_start/cpu_mid/cpu_end (btop's low → high gradient)
  DECODE proc_misc · PREFILL temp_mid (falls back when too close to DECODE)
  BRAND fixed red #ff3b30 (not from btop)

btop's built-in Default palette is listed first and named "SYSTEM-D"; it is
the firmware's default theme.
"""
import glob, os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SRC = os.path.join(ROOT, "third_party", "btop")
OUT = os.path.join(ROOT, "src", "gfx", "themes_btop.h")

ROLES = ["BG", "BORDER", "TITLE", "LABEL", "VALUE", "DIM", "GOOD", "WARN", "BAD",
         "DECODE", "PREFILL", "BOX2", "BOX3", "BOX4", "BRAND"]  # order = enum theme::Role
BRAND_RED = (0xFF, 0x3B, 0x30)
DEFAULT_THEME_NAME = "SYSTEM-D"  # btop's built-in Default palette, renamed
# btop's inactive_fg is for disabled items and is often barely visible, and
# many themes' graph_text is nearly as faint; here they are secondary text
# (DIM: units, "live", "idle", "poll 2s"; LABEL: "run", "wait", "TTFT"), so
# each is lifted towards main_fg until it reaches this contrast ratio against
# the background (WCAG formula). Was 2.75 for DIM only; raised to 3.5 and
# applied to LABEL, then DIM to 4.5, because it was still too faint on the
# panel (user).
MIN_CONTRAST = {"DIM": 4.5, "LABEL": 3.5}
KEYS = {"BG": "main_bg", "BORDER": "cpu_box", "TITLE": "title", "LABEL": "graph_text",
        "VALUE": "main_fg", "DIM": "inactive_fg", "GOOD": "cpu_start", "WARN": "cpu_mid",
        "BAD": "cpu_end", "DECODE": "proc_misc", "PREFILL": "temp_mid", "BOX2": "mem_box",
        "BOX3": "net_box", "BOX4": "proc_box"}
PREFILL_FALLBACKS = ["temp_end", "upload_end", "hi_fg", "download_end", "cpu_end"]


def parse(text):
    return {k: v.strip() for k, v in re.findall(r'"?(\w+)"?\s*[\]\,]\s*[=,]?\s*"([^"]*)"', text)}


def rgb(v):
    v = v.strip().lstrip("#")
    if len(v) == 2:  # grey shorthand
        return (int(v, 16),) * 3
    if len(v) == 6:
        return tuple(int(v[i:i + 2], 16) for i in (0, 2, 4))
    return None


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def luminance(c):
    def lin(x):
        x /= 255
        return x / 12.92 if x <= 0.03928 else ((x + 0.055) / 1.055) ** 2.4
    r, g, b = map(lin, c)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(a, b):
    hi, lo = sorted((luminance(a), luminance(b)), reverse=True)
    return (hi + 0.05) / (lo + 0.05)


def lift(c, towards, bg, target):
    """Mix c towards `towards` just enough to reach `target` contrast on bg."""
    if contrast(c, bg) >= target:
        return c
    mix = lambda t: tuple(round(x + (y - x) * t) for x, y in zip(c, towards))
    lo, hi = 0.0, 1.0
    for _ in range(20):
        mid = (lo + hi) / 2
        lo, hi = (lo, mid) if contrast(mix(mid), bg) >= target else (mid, hi)
    return mix(hi)


def resolve(theme, default):
    def get(key):
        c = rgb(theme.get(key, "")) or rgb(default[key])
        return c
    out = {r: get(KEYS[r]) for r in ROLES if r in KEYS}
    out["BRAND"] = BRAND_RED
    if rgb(theme.get("main_bg", "")) is None:
        out["BG"] = (0, 0, 0)  # btop: empty main_bg = terminal background
    for role, target in MIN_CONTRAST.items():
        out[role] = lift(out[role], out["VALUE"], out["BG"], target)
    if dist(out["PREFILL"], out["DECODE"]) < 80:
        # Pick the fallback most distinct from both DECODE and the background.
        cands = [out["PREFILL"]] + [get(k) for k in PREFILL_FALLBACKS]
        out["PREFILL"] = max(cands, key=lambda c: min(dist(c, out["DECODE"]), dist(c, out["BG"])))
    return out


def rgb565(c):
    r, g, b = c
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def main():
    default = parse(open(os.path.join(SRC, "default_theme.txt")).read())
    default = {k: v for k, v in default.items() if k}
    # btop's built-in Default palette, shown here as "SYSTEM-D" (the default theme).
    themes = [(DEFAULT_THEME_NAME, resolve({}, default))]
    for f in sorted(glob.glob(os.path.join(SRC, "themes", "*.theme")), key=lambda p: os.path.basename(p).lower()):
        t = dict(re.findall(r'theme\[(\w+)\]\s*=\s*"([^"]*)"', open(f).read()))
        themes.append((os.path.basename(f)[:-6], resolve(t, default)))

    lines = [
        "#pragma once",
        "// Generated by tools/themes/import_btop.py from btop's themes",
        "// (https://github.com/aristocratos/btop, Apache-2.0: third_party/btop/LICENSE).",
        "// Do not edit by hand.",
        '#include "theme.h"',
        "",
        "namespace theme {",
        f"constexpr int BTOP_COUNT = {len(themes)};",
        "const Theme BTOP_THEMES[BTOP_COUNT] = {",
    ]
    for name, roles in themes:
        vals = ", ".join(f"0x{rgb565(roles[r]):04X}" for r in ROLES)
        lines.append(f'    {{"{name}", {{{vals}}}}},')
    lines += ["};", "}  // namespace theme", ""]
    open(OUT, "w").write("\n".join(lines))
    print(f"wrote {os.path.relpath(OUT, ROOT)}: {len(themes)} themes")
    if "-v" in sys.argv:
        for name, r in themes:
            print(f"  {name:24} decode #{'%02x%02x%02x' % r['DECODE']}  prefill #{'%02x%02x%02x' % r['PREFILL']}  "
                  f"d={dist(r['DECODE'], r['PREFILL']):.0f}  bg #{'%02x%02x%02x' % r['BG']}")


if __name__ == "__main__":
    main()
