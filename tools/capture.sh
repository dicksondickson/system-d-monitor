#!/usr/bin/env bash
# Record a sequence of server responses as a test fixture (spec.md §10).
#
#   tools/capture.sh <host:port> <count> [name] [interval_s]
#   tools/capture.sh dgx042:40000 30 tensorfold-1.1.8-load
#
# Writes test/fixtures/<name>/ with health.json and v1_models.json (once),
# then NN-metrics.txt every interval (default 2 s), plus times.txt (epoch ms
# per sample, for Δt). Run it while the server is under load to answer the
# open questions in spec.md §12.
set -euo pipefail

target="${1:?usage: capture.sh <host:port> <count> [name] [interval_s]}"
count="${2:?usage: capture.sh <host:port> <count> [name] [interval_s]}"
interval="${4:-2}"
root="$(cd "$(dirname "$0")/.." && pwd)"
name="${3:-capture-$(date +%Y%m%d-%H%M%S)}"
out="$root/test/fixtures/$name"
mkdir -p "$out"

curl -fsS -m 5 "http://$target/health" -o "$out/health.json"
curl -fsS -m 5 "http://$target/v1/models" -o "$out/v1_models.json"
: > "$out/times.txt"
for ((i = 0; i < count; i++)); do
  start=$(python3 -c 'import time; print(time.time())')
  f=$(printf "%s/%02d-metrics.txt" "$out" "$i")
  if curl -fsS -m 5 "http://$target/metrics" -o "$f"; then
    python3 -c 'import time; print(int(time.time()*1000))' >> "$out/times.txt"
    printf "\r%s: %d/%d" "$name" $((i + 1)) "$count"
  else
    echo " sample $i failed" >&2
  fi
  python3 -c "import time; time.sleep(max(0, $interval - (time.time() - $start)))"
done
echo
echo "wrote $out ($(ls "$out" | wc -l | tr -d ' ') files)"
