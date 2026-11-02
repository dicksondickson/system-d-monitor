#!/usr/bin/env python3
"""Refresh test/test_render/golden.h from the current render.

Run after an intentional layout change, then review `git diff` of golden.h:
  tools/update_golden.py
"""
import os, re, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PIO = os.environ.get("PIO", os.path.expanduser("~/.local/bin/pio"))
GOLDEN = os.path.join(ROOT, "test", "test_render", "golden.h")

out = subprocess.run([PIO, "test", "-e", "native", "-f", "test_render", "-v"], cwd=ROOT,
                     capture_output=True, text=True).stdout
pages = {}
for name in ("overview", "graphs"):
    # The dump runs until the Unity result line that follows it.
    m = re.search(rf"--- actual {name} ---\n(.*?\n)test/test_render/", out, re.S)
    if m:
        pages[name] = m.group(1)
if not pages:
    sys.exit("golden files already match (or the test didn't run)")

src = open(GOLDEN).read()
for name, dump in pages.items():
    var = "GOLDEN_" + name.upper()
    src = re.sub(rf'(static const char\* {var} = R"GOLDEN\().*?(\)GOLDEN";)',
                 lambda m: m.group(1) + dump + m.group(2), src, flags=re.S)
open(GOLDEN, "w").write(src)
print("updated:", ", ".join(pages))
