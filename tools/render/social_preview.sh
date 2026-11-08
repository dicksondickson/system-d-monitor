#!/bin/sh
# Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
# SPDX-License-Identifier: MIT
#
# Refresh the three screens in docs/images/social-preview.png (1280 × 640,
# logo above three screens) from the current renders. Run
# tools/render/screens.py first. The logo half is kept as is; each 480 × 480
# render is scaled to 360 × 360 and placed at y = 250. Needs ffmpeg.
# Upload the result on GitHub: Settings → General → Social preview.
set -e
cd "$(dirname "$0")/../.."
img=docs/images
tmp=$(mktemp -d)
ffmpeg -loglevel error -y -i $img/social-preview.png \
  -i $img/overview.png -i $img/graphs.png -i $img/settings.png \
  -filter_complex "[1]scale=360:360:flags=lanczos[a];[2]scale=360:360:flags=lanczos[b];[3]scale=360:360:flags=lanczos[c];[0][a]overlay=88:250[t1];[t1][b]overlay=460:250[t2];[t2][c]overlay=832:250,format=rgb24" \
  -frames:v 1 -update 1 "$tmp/social-preview.png"
mv "$tmp/social-preview.png" $img/social-preview.png
rmdir "$tmp"
echo "wrote $img/social-preview.png"
