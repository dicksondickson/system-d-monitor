# System-D Monitor

A 4.0″ desk dashboard (Guition **ESP32-S3-4848S040**, 480×480 touch) that
shows live **TensorFold** inference metrics — decode/prefill tok/s, context
fill, per-stream state, request queue, MTP acceptance, TTFT. The screen is a
btop-style character TUI using the metric vocabulary of
[EngineMetrics](https://github.com/0rand/EngineMetrics) (layout in §7).

No broker, no exporter, no cloud: the ESP32 polls the engine's own HTTP
endpoints every 2 s and works out rates on the device from the change
between polls (delta-sampling).

**Status:** spec only, no firmware yet. Decisions recorded 2026-11-01 are
in §12.

---

## 1. Scope

**v1 — one server, TensorFold**
- Monitors exactly one server: `dgx042`, TensorFold 1.1.8.
- Two pages: **overview** (the sketch layout, §7.1) and **graphs**
  (120 s braille charts).
- Honest numbers: anything unavailable reads **`no data`** (§5), never a
  fake 0. Restarts and outages show as gaps, not spikes.
- Survives Wi-Fi drops, server restarts and server downtime without a
  reboot. Runs for weeks.

**Inspired by EngineMetrics, not a copy.** Borrow its metric vocabulary
(TG/PP live vs engine, window avg, honest gaps, sparklines with `·`
gaps, bars) and its default colours. The layout is our own (§7): btop-style
section rules, a 2× hero decode number and a per-stream table. Only the
metrics that matter at a glance on a 4″ screen are shown. Not carried over:
totals, engine-time totals, multi-prefill, kept prompts, spill tier,
latency percentiles, model card, process/VRAM memory.

**Later (not v1):** vLLM support, multiple servers (left/right tap
navigation, auto-rotate, page dots, an all-servers summary page), web or SD
config, OTA, p50/p99 from histogram buckets, auto-dim on idle. §10 keeps
the code shaped so these can be added without rewrites.

Never: auth/HTTPS (the server is plain HTTP on the LAN), battery operation.

---

## 2. Hardware

- Guition/JCZN ESP32-4848S040, relay-less "no bottom shell" variant
  (ESP32-S3-WROOM-1 **N16R8**: 16 MB QIO flash, 8 MB **OPI** PSRAM)
- 480×480 ST7701 RGB panel, GT911 capacitive touch (polled, INT/RST not wired)
- USB-C power + flashing through CH340, serial @ 115200
- **Mounting: native orientation (rotation 0).** No display or touch
  transforms.

### Pin map used by the firmware

> Verified on the panel 2026-11-01 (bring-up log: `docs/hardware-bringup.md`).
> Red is GPIO 11–14,0 and blue is 4–7,15, matching the vendor demo and
> Arduino_GFX's 86BOX config. The display snippet in
> `esp32-4848s040-guide.md` §3 originally had them swapped (now corrected).

| Function | GPIO |
|---|---|
| RGB R0–R4 | 11, 12, 13, 14, 0 |
| RGB G0–G5 | 8, 20, 3, 46, 9, 10 |
| RGB B0–B4 | 4, 5, 6, 7, 15 |
| DE / VSYNC / HSYNC / PCLK | 18 / 17 / 16 / 21 |
| ST7701 init SPI: CS / SCK / MOSI | 39 / 48 / 47 |
| Backlight PWM (LEDC) | 38 |
| GT911 SDA / SCL (I²C @ 0x5D) | 19 / 45 |

Timing starting point: PCLK 12–14 MHz, HSYNC pol 1 / fp 10 / pw 8 / bp 50,
VSYNC pol 1 / fp 10 / pw 8 / bp 20. ST7701 needs Arduino_GFX's "type 9" init
sequence **plus an explicit `0x20` INVOFF** (found by the display diag,
2026-11-01). Without it colours come out inverted. `0x21` and MADCTL's BGR
bit have no visible effect. The firmware sends INVOFF inside the sequence
and again after the RGB bus starts.
Unused: SD (41/42), I²S (1/2/40).

> **Backlight PWM at 1 kHz, not 20 kHz.** At 20 kHz the LEDC channel on
> GPIO38 never starts (duty and frequency read back 0) and the screen looks
> completely black. 1 kHz and 200 Hz work.

---

## 3. Software stack

| Layer | Choice |
|---|---|
| Build | PlatformIO, Arduino framework |
| Display | Arduino_GFX `Arduino_RGB_Display`, full 480×480 RGB565 framebuffer in PSRAM (460,800 B) |
| UI | **Custom character grid** (§6). No LVGL. |
| Touch | Minimal GT911 polling driver (or TAMC_GT911), ~30 Hz |
| Network | `WiFi` + `WiFiClient`, hand-written HTTP/1.0 GET with a streaming line parser |
| JSON | ArduinoJson with a filter document (`/health`, `/v1/models`, both < 1 KB) |
| Time | SNTP + POSIX TZ string (header clock only) |
| Persistence | `Preferences` (NVS): theme, last page |

Versions: pin the exact Arduino_GFX release, the matching ESP32 Arduino core,
and the platform in `platformio.ini` during M1. Don't float them.

Board settings: `memory_type = qio_opi`, `flash_mode = qio`,
`psram_type = opi`, 16 MB flash, a partition table with a large app slot,
`-DBOARD_HAS_PSRAM`, `-DARDUINO_USB_CDC_ON_BOOT=0` (note **-D**, not the
guide's `-U`).

---

## 4. Data source

### 4.1 Server (captured 2026-11-01)

`http://dgx042:40000/` → **TensorFold 1.1.8**, CUDA backend
(`Server: TensorFold`), model `deepseek-v4.1-flash-1017`, 1 M context,
`max_batch_size` 4. The responses, all served with `Content-Length` and not
chunked, are saved in `test/fixtures/tensorfold-1.1.8-idle/`.

| Endpoint | Size | Use |
|---|---|---|
| `GET /metrics` | ~14 KB (116 `_bucket` lines) | **All counters and gauges.** Every poll. |
| `GET /health` | 353 B | `status`, `warming`, `max_batch_size`. On connect, then every 30 s. |
| `GET /v1/models` | 334 B | `data[0].id` (model), `context_length`. On connect and after a restart. |

> **Hostname:** the ESP32's DNS resolver has no search domain, so bare
> `dgx042` won't resolve. Use `dgx042.lan` (this Mac resolves it to
> 192.168.50.192) or the IP, preferably with a DHCP reservation.

This TensorFold's `/health` is **not** the schema EngineMetrics reads (no
`busy`, `streams{}`, pool or token counters). `/metrics` is the source of
truth.

### 4.2 Metrics read

Every name is prefixed `tensorfold:`. The prefix is kept as a parameter
because vLLM uses the same suffixes under `vllm:`, which makes vLLM a later
addition (a prefix plus a few quirks) rather than a rewrite.

| Suffix | Type | Meaning (from `HELP`) | Combine series |
|---|---|---|---|
| `num_requests_running` | gauge | requests in prefill or decode | sum |
| `num_requests_waiting` | gauge | queued requests | sum |
| `prompt_tokens_total` | counter | prompt tokens | sum |
| `generation_tokens_total` | counter | generated tokens, finished requests | sum |
| `generation_tokens_running` | gauge | generated tokens held by live streams | sum |
| `request_prefill_time_seconds_sum` | hist sum | prefill seconds, finished requests | sum |
| `request_decode_time_seconds_sum` | hist sum | decode seconds, finished requests | sum |
| `time_to_first_token_seconds_{sum,count}` | hist | TTFT | sum |
| `e2e_request_latency_seconds_{sum,count}` | hist | request latency | sum |
| `spec_decode_num_draft_tokens_total` | counter | MTP drafted | sum |
| `spec_decode_num_accepted_tokens_total` | counter | MTP accepted | sum |
| `kv_cache_usage_perc{stream=…}` | gauge 0–1 | a stream's cache occupancy (vLLM's name) | **kept per stream** (id from the label, up to 8) |
| `kv_cache_usage_ratio{pool=…}` | gauge 0–1 | tokens in a stream cache ÷ its context window | **max** over pools ⚠ §12 |
| `prompt_tokens_cached_total` | counter | prompt tokens served from cache — **App. A** | sum |
| `stream_state{stream=…,state=…}` | gauge (1) | current state of a stream slot — **App. A** | per stream |
| `stream_generation_tokens{stream=…}` | gauge | tokens generated by the slot's current request — **App. A** | per stream |
| `stream_ttft_seconds{stream=…}` | gauge | TTFT of the slot's current request — **App. A** | per stream |
| `stream_request_start_seconds{stream=…}` | gauge | Unix start time of the slot's current request — **App. A** | per stream |

Rows marked **App. A** don't exist in TF 1.1.8. They are the additions
specified in Appendix A, and they read `no data` until TensorFold emits them.

**Parsing rules:**
- Read one line at a time from the socket into a 1 KB buffer. Skip any
  overlong line up to the next `\n`.
- Skip blank lines, `#…`, `*_bucket` and `*_created`.
- The format is `name[{labels}] value [timestamp]`. Parse the value with
  `strtod`; treat `NaN`/`+Inf` as absent.
- Labels are ignored, except `stream=` on `kv_cache_usage_perc` and
  `stream_*` (selects the stream slot, 0–7) and `state=` on `stream_state`.
  Unknown names are ignored.
- The request is `GET … HTTP/1.0` with `Connection: close`.

### 4.3 Engine identification: TensorFold or vLLM

The monitor never assumes the engine. It identifies the engine on connect,
after every DOWN → up transition, and after a restart. It shows the result
on the server rule (`TensorFold` / `vLLM`) and on the boot screen with the
version.

| Step | TensorFold | vLLM |
|---|---|---|
| 1. `/metrics` names (deciding) | known names with prefix `tensorfold:` | known names with prefix `vllm:` |
| 2. confirm | `Server: TensorFold` response header, or `/health` JSON with `version` | `GET /version` → `{"version": …}` |
| version shown | `/health.version` (`1.1.8`) | `/version.version` |

- The engine is the prefix with the most recognised names in one
  `/metrics` scan. A tie, or no recognised names → **UNKNOWN**.
- If step 2 disagrees with step 1, the engine is still taken from step 1.
  A dim `unconfirmed` is added on the footer rule.
- **UNKNOWN** (anything that isn't TensorFold or vLLM — wrong port, another
  server, an HTML page): row 2 reads `??? · host:port ─── UNKNOWN ┤`, the
  footer rule reads `not TensorFold or vLLM`, every value is `no data`, and
  the server is re-checked every 30 s. Nothing from a non-engine response
  is ever shown as a metric.
- If the engine changes between polls (e.g. something else took the port),
  treat it as a restart and identify again.
- **vLLM in v1:** identified and labelled, but its metric mapping is
  "Later" (§1), so its values read `no data` until that lands. The shared
  suffix table (§4.2) keeps that addition small.

---

## 5. Derived metrics

Each successful poll produces a `Sample`: `t_ms` (from `millis()` at
response end) plus the values above. `prev` is the previous good sample and
`win` is the last 15 good samples (≈ 30 s).

| Shown as | Formula |
|---|---|
| **TG live** (hero) | Δ(`generation_tokens_total` + `generation_tokens_running`) / Δt, so in-flight tokens count ⚠ §12 |
| TG eng | Δ`generation_tokens_total` / Δ`request_decode_time_seconds_sum` |
| **PP live** | Δ`prompt_tokens_total` / Δt ⚠ §12 |
| PP eng | Δ`prompt_tokens_total` / Δ`request_prefill_time_seconds_sum` |
| `avg` | first-vs-last over `win` |
| run / wait | `num_requests_running` / `max_batch_size` (`/health`) · `num_requests_waiting` |
| **KV** | max over pools of `kv_cache_usage_ratio` × 100 |
| KV tokens | KV ratio × `context_length` (`/v1/models`) / `context_length` |
| **Stream n** ctx / tokens | `kv_cache_usage_perc{stream=n}` × 100 / × `context_length`; state `used` if > 0, else `free` |
| **TTFT / lat** | sum / count (cumulative) of TTFT / e2e latency, `n` = TTFT count |
| live TTFT | Δsum / Δcount; `no data` when nothing finished in the window |
| **cache-hit** | `prompt_tokens_cached_total` / `prompt_tokens_total` (cumulative), App. A |
| Stream state | the `state` label of `stream_state{stream=n}` = 1, App. A |
| Stream TTFT | `stream_ttft_seconds{stream=n}`, App. A |
| Stream TG | Δ`stream_generation_tokens{stream=n}` / Δt while `stream_request_start_seconds` is unchanged; `no data` on the first poll of a new request |
| **MTP** | accepted / drafted (cumulative), with both counts |
| Busy | running > 0 or waiting > 0 |
| Wi-Fi bars | `WiFi.RSSI()`, thresholds in §7.1 |

**Rules**
- No `prev`, or Δt ≤ 0 → `no data`.
- A wall rate with zero token progress is a real **0.0** (idle), not
  `no data`. An engine-time rate with Δdenominator = 0 is `no data`. Both
  engine rates count
  finished requests only, so they update in steps as requests complete.
- **Restart:** if `prompt_tokens_total`, `generation_tokens_total` or any
  `_count` goes **down**, clear `win` and `prev`, push a gap into the rings,
  show `OK RESTART` on the server rule for 30 s, and re-fetch `/v1/models`
  and `/health`.
- **Rings:** 60 slots (120 s at 2 s) each for TG live, PP live and KV.
  A missed poll pushes a gap (NaN), so time spacing is preserved.

**Colours** (EngineMetrics thresholds):
- TG live: ≥ 30 good, > 0 warn, otherwise dim.
- KV and per-stream ctx: < 70 good, < 90 warn, otherwise bad.
- MTP: ≥ 70 good, ≥ 50 warn, otherwise bad; dim if nothing was ever drafted.

**Number format** (as in the sketch):
- Speeds: `100.9` below 1,000; `1,331` from 1,000 to 99,999 (thousands
  separator); `120K` and `1.20M` above that. The hero always uses one
  decimal below 1,000.
- Durations: `312ms`, `8.16s`.
- Counts: `9.2K`.

**`no data`** (dim) replaces any value that is unavailable: the metric isn't
exported, nothing has been received yet, or there was no progress in the
window. Placement:
- **One cell:** `eng no data`.
- **Several adjacent cells in a row:** one `no data` spans them (stream
  row 1 in §7.1). This keeps narrow columns readable.
- **Whole section** (e.g. no `stream_*` series at all): the section shows a
  single `│ no data │` row.
- **Hero:** `no data` at 1×, dim, centred vertically in the 2-row hero area.
- **Graphs:** gaps stay as `·` / a baseline dot. A graph with no samples
  shows `no data` centred.

Not `no data`: a real idle 0.0, and blank TTFT/TG on `idle` stream rows
(there's no request to measure).

---

## 6. Renderer

- **Grid:** 48 × 24 cells of 10 × 20 px = exactly 480 × 480.
  Each cell = `{uint16 glyph, uint8 fg, uint8 bg}` (palette index).
  There are two grids, front and back. A frame renders into the back grid,
  then only cells that differ are blitted into the framebuffer.
- **Font:** Terminus 10×20 (`ter-u20n`, SIL OFL), subset to ASCII plus `· …`,
  converted to a C array by `tools/mkfont.py`. Bold = a second Terminus
  weight (`ter-u20b`) for section titles and the server state.
- **Hero text:** `ter-u20b` glyphs pixel-doubled to 20 × 40, each covering a
  2 × 2 block of cells. The grid marks these cells as one "big glyph" so the
  diff-blit redraws them together. Used only for TG live.
- **Procedural glyphs** (drawn as rects/dots, no font needed): box drawing
  `─│┤├┌┐└┘`, blocks `▁▂▃▄▅▆▇█░`, and braille U+2800–28FF.
- **Sparklines** (overview), scaled to the window max, gap = `·`:
  - PP: 30 block cells, each = max of 2 samples → 120 s at 8 levels.
  - TG (next to the hero): 20 cells × 2 rows, each cell = max of 3 samples
    → 120 s at 16 levels.
- **Graphs** (graphs page): braille, one cell = 2 samples × 4 levels, so
  30 columns = 60 samples = 120 s. A gap is one dim baseline dot.
- Redraw at most 10 Hz: after each poll, on touch, and once a second for the
  clock. The poller never touches the grid.
- Tearing risk: the RGB panel scans continuously. If cell updates visibly
  tear, switch to double framebuffers with a swap on VSYNC (`num_fbs = 2`,
  another 450 KB PSRAM).

### Themes (hold 0.7 s to cycle, saved in NVS)

| Role | `ansi` (EngineMetrics, default) | `btop` (alternative) |
|---|---|---|
| bg | `#000000` | `#0b0d10` |
| border, title | cyan `#00cdcd` (bold) | muted green `#3d7b46` / `#eeeeee` |
| label | yellow `#cdcd00` | `#8a8f98` |
| value | white `#e5e5e5` | `#d0d0d0` |
| dim | `#7f7f7f` | `#4a4f57` |
| good / warn / bad | `#00cd00` / `#cdcd00` / `#cd0000` | `#77ca9b` / `#cbc06c` / `#dc4c4c` |
| **decode** (TG hero, TG spark/graph) | bright green `#00ff5f` | `#77ca9b` |
| **prefill** (PP numbers, spark/graph) | magenta `#cd00cd` | `#4897d4` |
| graph fill (KV) | value colour | vertical gradient good → warn → bad |

Tune the colours on the real panel, because RGB565 shifts them.

---

## 7. Screens (48 × 24)

The layout follows the 2026-11-01 sketch:
- btop-style section rules `─┤ Title ───`, with status on the right of a
  rule (`OK BUSY ┤`);
- `│ … │` rows;
- a 2× "hero" decode number;
- a per-stream table (nv-monitor style).

Values are illustrative, and assume TensorFold already exports Appendix A.

### 7.1 Overview

```
─┤ SYSTEM-D 12:47:33  LAN 192.168.50.57 ▂▄▆ ────
─┤ TensorFold · deepseek-v4.1-flash… ─ OK BUSY ┤
─┤ Throughput ──────────────────────────────────
│ TG １００．９ tok/s    █▇▅▃▁▂▃▅▇█ ▇▆▅▂▂▂     │
│                live ▁▁▁██████████·██████     │
│ PP 1,331/s  avg 1,290  · eng 6,802/s         │
│ PP ▁▁▁▁▁▁▁█▁▁▁▁▁▁▄▁▁▁▁▁▁▁█▁▁▁▁▁▁▁ 6.8K       │
│ run 2/4 · wait 1 · TG avg 95.1 · eng 91.2    │
─┤ Cache ───────────────────────────────────────
│ KV 31.4% █████░░░░░░░░░░░ · 329K/1.0M tok    │
─┤ Streams ─────────────────────────────────────
│  #  state        ctx tokens   TTFT      TG   │
│  0  decoding   31.4%   329K   1.2s  62.7/s   │
│  1  prefill     2.0%    21K  no data         │
│  2  idle        0.0%      0                  │
│  3  idle        0.0%      0                  │
─┤ Averages ────────────────────────────────────
│ TTFT 1.85s · MTP 48.9% · cache-hit 75.4%     │
│ live TTFT 1.42s · lat 8.16s · n=42           │




─┤ poll 2s · ok 0.4s ago ───────── tap: graphs ┤
```

(The hero `１００．９` is written in fullwidth digits to show its width. On the
device it is drawn at 2× and spans rows 4–5, 2 columns per character.)

| Rows | Section | Content |
|---|---|---|
| 1 | header | `SYSTEM-D`, clock `HH:MM:SS`, `LAN <ip>` and Wi-Fi signal bars `▂▄▆█` (RSSI ≥ −55 / −67 / −75 / −85 dBm → 4/3/2/1 bars, none = no link) |
| 2 | server | **detected engine, spelled out** (`TensorFold` or `vLLM`, §4.3) · model id, state on the right (§8). The model id is cut with `…` to fit; the boot screen shows it in full |
| 3–8 | Throughput | **hero: TG live** at 2× in the decode colour + 2-row, 20-cell sparkline (3 samples per cell = 120 s, 16 levels); PP live + avg + engine rate in the prefill colour; PP 30-cell sparkline; `run x/max · wait n` + TG avg and engine rate |
| 9–10 | Cache | KV bar + used / window tokens |
| 11–16 | Streams | header + one row per stream slot, up to `max_batch_size` (4 now; spare rows 20–23 allow up to 8): id, state, ctx %, tokens, TTFT, TG |
| 17–19 | Averages | cumulative TTFT · MTP acceptance · cache-hit; then live TTFT · e2e latency · `n` |
| 20–23 | spare | stream overflow; otherwise blank |
| 24 | footer rule | poll status (or error reason, §8) · touch hint |

**Decode ≠ prefill:** the decode (TG) and prefill (PP) numbers and graphs
always use two distinct colours (`decode`, `prefill` in §6), so the two
rates can't be confused at a glance.

**Stream rows:**
- `decoding` shows everything. TG is the stream's own Δtokens/Δt.
- `prefill` has no first token yet, so TTFT and TG read `no data`.
- `idle` shows ctx and tokens; TTFT and TG stay blank, because there's no
  request to measure.
- Before TensorFold exports Appendix A (today's 1.1.8), each row shows id,
  ctx % and tokens, then `no data`.

### 7.2 Graphs

```
─┤ SYSTEM-D 12:47:33  LAN 192.168.50.57 ▂▄▆ ────
─┤ TensorFold · deepseek-v4.1-flash… ─ OK BUSY ┤
─┤ TG live tok/s ──────────────────────── 120s ┤
│ ⠀⠀⠀⠀⢸⣿⣦⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣄⣴⠀⠀⣤⡀⠀⠀⠀⠀⠀⠀⠀  now 100.9    │
│ ⠀⠀⠀⠀⢸⣿⣿⣦⡀⠀⠀⠀⠀⠀⠀⢀⣾⣿⣿⠀⠀⣿⣿⣦⡀⠀⠀⠀⠀⠀  avg  95.1    │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣦⡀⠀⠀⢀⢠⣾⣿⣿⣿⠀⠀⣿⣿⣿⣧⡀⠀⠀⠀⢀  max 104.2    │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣧⣤⣶⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣷⣾⣤⣾  min   0.0    │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿               │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿               │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿               │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿               │
─┤ PP live tok/s ──────────────────────── 120s ┤
│ ⠀⠀⠀⠀⠀⠀⠀⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀  now 1,331    │
│ ⠀⠀⠀⠀⠀⠀⠀⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀  avg 1,290    │
│ ⠀⠀⠀⠀⠀⠀⠀⣿⠀⠀⠀⠀⠀⠀⡀⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀  max 6,800    │
│ ⠀⠀⠀⠀⠀⠀⠀⣿⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀  min     0    │
│ ⠀⠀⠀⠀⢀⣀⣀⣿⣀⣀⣀⣀⣀⣀⣇⣀⣀⣀⣀⣀⣀⣀⣇⣀⣀⣀⣀⣀⣀⣀               │
─┤ KV % ───────────────────────────────── 120s ┤
│ ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢀⣀⣠⣤⣶⣶⣿⣿⣿  now 11.6%    │
│ ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣀⣠⣤⣴⣶⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿  max 44.0%    │
│ ⠀⠀⠀⠀⠀⠀⠀⠀⣀⣠⣤⣴⣶⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿               │
│ ⠀⠀⠀⠀⢰⣶⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿               │

─┤ poll 2s · ok 0.4s ago ─────── tap: overview ┤
```

(Monospace terminals render braille at varying widths, so the mockup may
look ragged. On the device every glyph is one 10×20 cell.)

- Rows 1–2 and the footer rule are the same as the overview.
- TG live gets 8 rows (32 levels, decode colour), PP live 5 (prefill
  colour), KV 4. Right column: `now / avg / max / min` for the window.
- The y-scale is the window max, with a floor (TG 10, PP 100) so idle noise
  doesn't fill the box. KV is fixed at 0–100 %.
- The column gap in TG is a missed poll or a restart.
- A graph with no samples in the window shows `no data` centred in its box.

### 7.3 Boot

Full-screen log lines in the label/value colours, in this order:

1. `system-d monitor v0.1`
2. `wifi  <ssid> … ok 192.168.50.x`
3. `ntp   ok 14:05`
4. `srv   dgx042.lan:40000 … TensorFold 1.1.8 ok`
5. `model deepseek-v4.1-flash-1017`

Then the overview opens. A failure stays on screen with its reason; an
unrecognised server shows `srv … not TensorFold or vLLM`. The overview
opens anyway after 10 s, in the matching state (§8).

---

## 8. States and errors

The state sits on the right of the server rule (row 2). The detail goes on
the footer rule (row 24).

| State | Condition | Row 2 | Row 24 | Body |
|---|---|---|---|---|
| IDLE | ok, running = waiting = 0 | `OK IDLE` good | `poll 2s · ok 0.4s ago` | normal |
| BUSY | ok, running or waiting > 0 | `OK BUSY` warn | same | normal |
| WARMING | `/health.warming == true` | `OK WARM` warn | same | normal |
| RESTART | counter went down (§5), for 30 s | `OK RESTART` warn | `restarted 12s ago` | windows reset |
| STALE | last good sample > 3 × poll ago | `STALE` warn | `stale 9s · <reason>` | values dim |
| DOWN | 3 consecutive failures | `DOWN` bad | `<reason> · retry 8s` | last values dim |
| NO WIFI | `WiFi.status() != WL_CONNECTED` | `NO WIFI` bad | `wifi lost · retry` | values dim; header shows no bars |
| UNKNOWN | responds, but isn't TensorFold or vLLM (§4.3) | `??? · host:port` … `UNKNOWN` bad | `not TensorFold or vLLM` | all `no data` |

Reasons are short: `dns dgx042.lan`, `refused`, `timeout`, `http 503`,
`no metrics`.
While DOWN, the server is retried with backoff 2 → 4 → 8 → 16 → 30 s. It
returns to the 2 s cadence on the first success, and that sample becomes
the new baseline (so it can trigger restart detection).

---

## 9. Touch (native orientation)

| Gesture | Action |
|---|---|
| tap anywhere | overview ↔ graphs |
| hold ≥ 0.7 s | cycle theme (ansi → btop → ansi), saved |

A tap is a press and release within 500 ms, with movement < 24 px.
GT911 is polled at ~30 Hz. Check touch coordinates against the display axes
in M1. They don't matter for whole-screen taps yet, but they will once
left/right zones arrive with multi-server support.

---

## 10. Firmware architecture

### Config — `include/config.h` (gitignored; commit `config.example.h`)

```cpp
#pragma once
#define WIFI_SSID   "…"
#define WIFI_PASS   "…"
#define TZ_POSIX    "PST8PDT,M3.2.0,M11.1.0"   // header clock

#define SERVER_NAME "dgx042"
#define SERVER_HOST "dgx042.lan"                // or "192.168.50.192"
#define SERVER_PORT 40000

constexpr uint32_t POLL_MS    = 2000;
constexpr uint8_t  WINDOW     = 15;     // samples for avg / live
constexpr uint8_t  BRIGHTNESS = 200;    // 0–255 backlight PWM
constexpr uint16_t CONNECT_MS = 800;
constexpr uint16_t READ_MS    = 1500;
```

### Tasks

| Task | Core | Job |
|---|---|---|
| `poller` | 0 (with Wi-Fi) | Every `POLL_MS`: GET `/metrics` (stream-parse), plus `/health` / `/v1/models` when due. Build a `Sample`, run `derive`, push rings, publish a `View` snapshot under a mutex. |
| `ui` (Arduino `loop`) | 1 | Poll touch, copy the `View` under the mutex, render the page into the back grid, diff-blit, drive the backlight. |

The UI only reads published snapshots. The poller never draws.

All state belongs to a single `Server` struct (config, `win[15]`, three
60-float rings, last error, backoff). That's about 4 KB, statically
allocated. Multi-server later means an array of these, with no other
structural change. The grids are 2 × 48 × 24 × 4 B ≈ 9 KB. There's no heap
allocation in steady state, so a 24 h soak should show flat free heap.

### Layout

```
platformio.ini            env:board + env:native (host tests)
include/config.example.h
src/main.cpp
src/hw/       panel.cpp  touch_gt911.cpp  backlight.cpp
src/gfx/      grid.cpp   glyphs.cpp (procedural)  font_ter10x20.h  theme.cpp
src/net/      wifi.cpp   http_get.cpp  poller.cpp
src/metrics/  prom.cpp (line parser + suffix table)  tf_health.cpp
              sample.h   derive.cpp   ring.h   fmt.cpp
src/ui/       widgets.cpp (section rule, row, hero, bar, sparkline, table, braille graph)
              page_overview.cpp  page_graphs.cpp  boot.cpp
test/fixtures/tensorfold-1.1.8-idle/  metrics.txt health.json v1_models.json
test/test_prom/  test_derive/  test_render/   (PlatformIO native, Unity)
tools/mkfont.py  tools/capture.sh
```

- `ui/widgets` holds the §7 visual language: `─┤ title ───` rules with an
  optional right-hand status, `│ … │` rows, the 2× hero, bars, 1- and 2-row
  sparklines, the stream table and braille graphs. Pages only compose
  widgets.
- `metrics/`, `gfx/grid` and `ui/` compile without Arduino headers, so they
  can be tested on the host.
- `test_render` renders each page to a 48×24 text dump and compares it with
  a golden file, so layout regressions show up without hardware.
- `tools/capture.sh host:port N` curls `/metrics`, `/health` and
  `/v1/models` N times, 2 s apart, into a new fixture directory. That gives
  the derive tests real sequences.

---

## 11. Milestones

| # | Deliverable | Done when |
|---|---|---|
| M0 | Hardware sanity | Factory demo shows UI, touch responds (data cable) |
| M1 ✅ 2026-11-01 | Skeleton | `platformio.ini` pinned; colour bars show **R/G/B in the right places**; backlight PWM works; touch dot follows the finger; serial log at 115200 |
| M2 | Renderer | Font + procedural glyphs + widgets; static §7.1 / §7.2 on the panel in both themes; full-page diff-blit ≤ 10 ms |
| M3 | Data in | Wi-Fi + SNTP; streaming HTTP; parser passes native tests on the idle fixture + a load capture; engine identification passes tests for TensorFold (real fixture), vLLM (synthetic `/metrics` + `/version`) and UNKNOWN (an HTML page, an empty body); serial prints parsed samples from dgx042 |
| M3a | TensorFold exports (in the TensorFold repo) | Appendix A metrics live on dgx042; a load capture with ≥ 2 concurrent requests saved as `test/fixtures/tensorfold-<ver>-load/` |
| M4 | Derive | Rates, window, rings, restart detection; native tests on captured sequences, including a server restart |
| M5 | Live UI | Both pages fed by real data; tap/hold work; every §8 state reachable (pull Wi-Fi, stop the server) |
| M6 | Hardening | NVS persistence; backoff; 24 h soak with flat heap and no stuck states |

---

## 12. Decisions & open questions

**Decided (2026-11-01)**
- v1 monitors one TensorFold server (dgx042). vLLM and multi-server come
  later.
- Overview layout follows the sketch: header with clock / LAN IP / signal,
  server rule with state, Throughput with a 2× TG hero, Cache, Streams
  table, Averages. EngineMetrics supplies the metric vocabulary and
  colours, not the layout.
- Decode and prefill always use distinct colours.
- Unavailable values read `no data` (rules in §5), never `—` or a hidden
  column.
- The monitor confirms that the server is TensorFold or vLLM (§4.3),
  spells out which one on screen, and treats anything else as UNKNOWN.
- TensorFold's exports will be extended (Appendix A) to fill the Streams
  table and cache-hit.
- Config: compile-time `config.h`.
- Renderer: custom char grid on Arduino_GFX, no LVGL.
- Density: 48 × 24 cells, 10 × 20 font.
- Orientation: native (rotation 0).

**Open: verify with a capture under load (`tools/capture.sh`, M3)**
1. Is `generation_tokens_total + generation_tokens_running` continuous when
   a request finishes? Does it double-count or dip?
2. Does `prompt_tokens_total` increment at prefill end, or at request end?
   PP live depends on it.
3. With several requests in flight, how many `kv_cache_usage_perc{stream=…}`
   series appear? Does a finished stream keep a non-zero value (cached
   prompt), which would make `used` mean "holds cache" rather than "busy"?
4. Do `kv_cache_usage_ratio{pool=…}` and `kv_cache_usage_perc{stream=…}`
   differ under load? `HELP` says they share streams and ratios, but the
   labels differ. If they're identical, KV = max stream and the pool series
   is ignored.
5. Does `dgx042.lan` resolve from the ESP32 (router DNS), or should the
   config use the IP?

**Open: TensorFold side**
6. Implement Appendix A in TensorFold (M3a). While you're in there, item 4
   (`kv_cache_usage_ratio{pool}` vs `kv_cache_usage_perc{stream}`) can be
   settled from the source rather than by capture.

---

## Appendix A. TensorFold export contract

These additions go to TensorFold's `/metrics`. Names follow the existing
vLLM-style naming under `tensorfold:`. Each one needs `# HELP` / `# TYPE`
lines like the rest.

### A.1 Per-stream (fills the Streams table)

Emit these for **every** stream slot `0 … max_batch_size − 1` on every
scrape, idle slots included, so the table has stable rows. Use the same
`stream="n"` label values as `kv_cache_usage_perc`.

| Metric | Type | Value | When the slot is idle |
|---|---|---|---|
| `tensorfold:stream_state{stream="n",state="…"}` | gauge | `1`, one line per slot carrying its **current** state: `idle`, `prefilling`, `decoding` (`paused` if TensorFold has it) | `state="idle"` |
| `tensorfold:stream_generation_tokens{stream="n"}` | gauge | tokens generated so far by the current request | `0` |
| `tensorfold:stream_ttft_seconds{stream="n"}` | gauge | TTFT of the current request; `NaN` until its first token | `NaN` |
| `tensorfold:stream_request_start_seconds{stream="n"}` | gauge | Unix time (s, float) when the current request entered the slot | `0` |

The device derives per-stream TG as Δ`stream_generation_tokens` / Δt for as
long as `stream_request_start_seconds` stays the same. A changed start time
means a new request, so that poll's TG reads `no data`. The device never
needs per-stream counters that survive across requests.

Example (one prefilling slot, one decoding, two idle):

```
# HELP tensorfold:stream_state Current state of a stream slot (value is always 1).
# TYPE tensorfold:stream_state gauge
tensorfold:stream_state{stream="0",state="decoding"} 1
tensorfold:stream_state{stream="1",state="prefilling"} 1
tensorfold:stream_state{stream="2",state="idle"} 1
tensorfold:stream_state{stream="3",state="idle"} 1
# HELP tensorfold:stream_generation_tokens Tokens generated by the slot's current request.
# TYPE tensorfold:stream_generation_tokens gauge
tensorfold:stream_generation_tokens{stream="0"} 1874
tensorfold:stream_generation_tokens{stream="1"} 0
tensorfold:stream_generation_tokens{stream="2"} 0
tensorfold:stream_generation_tokens{stream="3"} 0
# HELP tensorfold:stream_ttft_seconds Time to first token of the slot's current request; NaN before the first token.
# TYPE tensorfold:stream_ttft_seconds gauge
tensorfold:stream_ttft_seconds{stream="0"} 1.214
tensorfold:stream_ttft_seconds{stream="1"} NaN
tensorfold:stream_ttft_seconds{stream="2"} NaN
tensorfold:stream_ttft_seconds{stream="3"} NaN
# HELP tensorfold:stream_request_start_seconds Unix time the slot's current request started; 0 when idle.
# TYPE tensorfold:stream_request_start_seconds gauge
tensorfold:stream_request_start_seconds{stream="0"} 1793630811.42
tensorfold:stream_request_start_seconds{stream="1"} 1793630839.07
tensorfold:stream_request_start_seconds{stream="2"} 0
tensorfold:stream_request_start_seconds{stream="3"} 0
```

### A.2 Cache-hit (fills Averages)

| Metric | Type | Value |
|---|---|---|
| `tensorfold:prompt_tokens_cached_total` | counter | prompt tokens served from the prefix/KV cache instead of being computed. Same name and meaning as vLLM's `vllm:prompt_tokens_cached_total` |

`cache-hit = prompt_tokens_cached_total / prompt_tokens_total` (cumulative).
It reads `no data` until this counter exists.

### A.3 Budget and compatibility

- Adds 4 × slots + 1 lines: 17 for 4 slots, under 1.5 KB with HELP/TYPE.
- Purely additive. Existing names are unchanged, so EngineMetrics and
  anything else scraping TensorFold keep working.
- The monitor reads `version` from `/health`. Bump it with this change so
  the boot screen shows which build is running.
