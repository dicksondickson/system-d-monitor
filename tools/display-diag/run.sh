#!/usr/bin/env bash
# Build (and optionally flash) an archived display diagnostic round.
#   tools/display-diag/run.sh <1-6> [upload] [port]
# Copies the round into src/diag/ for the build and removes it afterwards.
set -euo pipefail

round="${1:?usage: run.sh <1-6> [upload] [port]}"
action="${2:-build}"
port="${3:-/dev/cu.usbserial-110}"

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
src=( "$here"/round"$round"_*.cpp )
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
