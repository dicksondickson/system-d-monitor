#!/usr/bin/env bash
# Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
# SPDX-License-Identifier: MIT
# Build (and optionally flash) an archived display diagnostic round.
#   tools/display-diag/run.sh <1-6|panel> [upload] [port]
# "panel" is the M1 panel test (colour bars, ramps, border, touch dots).
# Copies the round into src/diag/ for the build and removes it afterwards.
set -euo pipefail

round="${1:?usage: run.sh <1-6|panel> [upload] [port]}"
action="${2:-build}"
port="${3:-/dev/cu.usbserial-110}"

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
if [[ "$round" == panel ]]; then src=( "$here/panel_test.cpp" ); else src=( "$here"/round"$round"_*.cpp ); fi
[[ -f "${src[0]}" ]] || { echo "no round $round in $here" >&2; exit 1; }

pio="$(command -v pio || echo "$HOME/.local/bin/pio")"
diag="$root/src/diag"
mkdir -p "$diag"
trap 'rm -rf "$diag"' EXIT
cp "${src[0]}" "$diag/"
[[ "$round" == 1 ]] && cp "$here/st7701_hsd040bpn1.h" "$diag/"

cd "$root"
if [[ "$action" == upload ]]; then
  "$pio" run -e diag -t upload --upload-port "$port"
else
  "$pio" run -e diag
fi
