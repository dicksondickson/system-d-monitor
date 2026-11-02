// Golden text dumps of both pages rendered from demoView() (spec.md §7).
// After an intentional layout change, run `pio test -e native -v`, check the
// printed dump, and paste it here.
#pragma once

static const char* GOLDEN_OVERVIEW = R"GOLDEN(┌─┤ SYSTEM-D 12:47:33  LAN 192.168.50.57 ▂▄▆█ ├┐
├─┤ TensorFold · deepseek-v4.1-… ├─┤ OK BUSY ├─┤
├─┤ Throughput ├───────────────────────────────┤
│ TG １０１．３ tok/s    █▆▄▄▅▆███▆ ▄▄▅▇██     │
│                live ▁▁▁██████████·██████     │
│ PP 1,331/s  avg 1,290 · eng 6,802/s          │
│ PP ▁▁▁▁▁▁▁█▁▁▁▁▁▁▄▁▁▁▁··▁█▁▁▁▁▁▁▂ 6.8K       │
│ run 2/4 · wait 1 · TG avg 95.1 · eng 91.2    │
├─┤ Cache ├────────────────────────────────────┤
│ KV 31.4% █████░░░░░░░░░░░ · 329K/1.0M tok    │
├─┤ Streams ├──────────────────────────────────┤
│  #  state        ctx tokens   TTFT      TG   │
│  0  decoding   31.4%   329K   1.2s  62.7/s   │
│  1  prefill     2.0%    21K  no data         │
│  2  idle        0.0%      0                  │
│  3  idle        0.0%      0                  │
├─┤ Averages ├─────────────────────────────────┤
│ TTFT 1.85s · MTP 48.9% · cache-hit 75.4%     │
│ live TTFT 1.42s · lat 8.16s · n=42           │
│                                              │
│                                              │
│                                              │
│                                              │
└─┤ poll 2s · ok 0.4s ago ├────┤ tap: graphs ├─┘
)GOLDEN";

static const char* GOLDEN_GRAPHS = R"GOLDEN(┌─┤ SYSTEM-D 12:47:33  LAN 192.168.50.57 ▂▄▆█ ├┐
├─┤ TensorFold · deepseek-v4.1-… ├─┤ OK BUSY ├─┤
├─┤ TG live tok/s ├───────────────────┤ 120s ├─┤
│ ⠀⠀⠀⠀⢸⣀⠀⠀⠀⠀⠀⠀⠀⢀⣾⣾⣿⣧⡀⠀⠀⠀⠀⠀⠀⠀⣰⣷⣿⣿  now 101.3    │
│ ⠀⠀⠀⠀⢸⣿⣷⡄⠀⠀⠀⢀⣶⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⣠⣼⣿⣿⣿⣿  avg  73.7    │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣶⣶⣴⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣶⣦⣾⣿⣿⣿⣿⣿⣿  max 102.0    │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿  min   0.0    │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿               │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿               │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿               │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿               │
├─┤ PP live tok/s ├───────────────────┤ 120s ├─┤
│ ⠀⠀⠀⠀⠀⠀⠀⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀  now 1,331    │
│ ⠀⠀⠀⠀⠀⠀⠀⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀  avg 666.6    │
│ ⠀⠀⠀⠀⠀⠀⠀⣿⠀⠀⠀⠀⠀⠀⡀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀  max 6,800    │
│ ⠀⠀⠀⠀⠀⠀⠀⣿⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀  min   0.0    │
│ ⠀⠀⠀⠀⢀⣀⣀⣿⣀⣀⣀⣀⣀⣀⣇⣀⣀⣀⣀⠀⠀⣀⣇⣀⣀⣀⣀⣀⣀⣸               │
├─┤ KV % ├────────────────────────────┤ 120s ├─┤
│ ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀  now 31.4%    │
│ ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀  max 31.4%    │
│ ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣀⣀⣀               │
│ ⠀⠀⠀⠀⢀⣀⣀⣀⣀⣤⣤⣤⣤⣤⣤⣶⣶⣶⣶⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿               │
│                                              │
└─┤ poll 2s · ok 0.4s ago ├──┤ tap: overview ├─┘
)GOLDEN";
