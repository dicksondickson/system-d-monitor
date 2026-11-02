# System-D Monitor

A 4.0″ desk dashboard (Guition **ESP32-S3-4848S040**, 480×480 touch) that
shows live **vLLM** and **TensorFold** inference metrics (decode/prefill
tok/s, KV/context fill, per-stream state, request queue, MTP acceptance,
TTFT) for up to 4 servers. The screen is a
btop-style character TUI using the metric vocabulary of
[EngineMetrics](https://github.com/0rand/EngineMetrics) (layout in §7).

No broker, no exporter, no cloud: the ESP32 polls the engine's own HTTP
endpoints every 2 s and works out rates on the device from the change
between polls (delta-sampling).

**Status:** working (M1–M4 done, M5 in progress). This is the design
document; for setup see [`README.md`](README.md). It was written for the
author's two servers (`dgx042`: TensorFold, `dick6000`: vLLM), which appear
as worked examples throughout. Decisions are in §12 and history is in
[`docs/devlog.md`](docs/devlog.md).

---

## 1. Scope

**v1: TensorFold and vLLM, up to 4 servers**
- Started with one server (`dgx042`, TensorFold 1.1.8). Since 2026-11-01
  it monitors two: `dgx042` (TensorFold) and `dick6000` (vLLM 0.33). Tap
  left or right to switch between them.
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

**Later:** an all-servers summary page, web or SD config, OTA, p50/p99
from histogram buckets, auto-dim on idle. (vLLM support and multiple
servers with left/right navigation and optional auto-rotate were "later"
items; both landed 2026-11-01.) §10 keeps
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
> `docs/board-guide.md` §3 originally had them swapped (now corrected).

| Function | GPIO |
|---|---|
| RGB R0–R4 | 11, 12, 13, 14, 0 |
| RGB G0–G5 | 8, 20, 3, 46, 9, 10 |
| RGB B0–B4 | 4, 5, 6, 7, 15 |
| DE / VSYNC / HSYNC / PCLK | 18 / 17 / 16 / 21 |
| ST7701 init SPI: CS / SCK / MOSI | 39 / 48 / 47 |
| Backlight PWM (LEDC) | 38 |
| GT911 SDA / SCL (I²C @ 0x5D) | 19 / 45 |

**Bounce buffers** (2 × 20 lines, 38 KB of internal SRAM, `BOUNCE_PX` in
`src/hw/panel.cpp`): without them the LCD DMA reads PSRAM directly and the
screen flickers every poll once Wi-Fi is active (bring-up log §2.7). With
two servers, 10-line buffers weren't enough (§2.9).
With bounce buffers the framebuffer is read through the CPU cache, so
`screen::present()` does **no** cache writeback.

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
- **vLLM (since 2026-11-01, `dick6000`):** `/health` is an empty 200, so
  the poller falls back to **`GET /version`** (`{"version": "0.33.0rc1…"}`).
  That confirms the engine and gives the version. The model and
  `max_model_len` (262,144) come from `/v1/models`. `/metrics` is ~67 KB
  and ~511 lines; its longest line, `cache_config_info` at ~1.1 KB, is why
  the parser's line buffer is 2 KB. The extra vLLM names read are
  `request_prompt_tokens_sum`, `request_generation_tokens_sum`
  (per-request token sums) and the `kv_cache_size_tokens` label on
  `cache_config_info` (KV pool size, 355,241 tokens on dick6000). Captured
  in `test/fixtures/vllm-0.33.0rc1-idle/`.

---

## 5. Derived metrics

Each successful poll produces a `Sample`: `t_ms` (from `millis()` at
response end) plus the values above. `prev` is the previous good sample and
`win` is the last 15 good samples (≈ 30 s).

| Shown as | Formula |
|---|---|
| **TG live** (hero) | Δ(`generation_tokens_total` + `generation_tokens_running`) / Δt, so in-flight tokens count. Verified under load (§12 Q1) |
| TG eng ("engine-time"; **serial log only**, removed from the screen 2026-11-01 because it was confusing) | Δ finished-request tokens / Δ`request_decode_time_seconds_sum`: tokens per second of the engine's own decode time for the requests that finished this poll, i.e. roughly single-request speed. Finished-request tokens = `request_generation_tokens_sum` (vLLM) or `generation_tokens_total` (TensorFold, which only counts finished requests). vLLM's `generation_tokens_total` moves live, so pairing it with the finish-time decode sum would mix timings |
| PP live | Δ`prompt_tokens_total` / Δt. **Not shown on either engine:** TensorFold counts prompt tokens at request end (§12 Q2), vLLM when the prefill completes, so it reads 0 during a prefill and then spikes |
| PP eng | Δ finished-request prompt tokens (`request_prompt_tokens_sum` on vLLM, else `prompt_tokens_total`) / Δ`request_prefill_time_seconds_sum`: prefill speed of the request(s) that finished in this poll |
| **PP last** | the most recent non-`no data` PP eng, kept until the next request finishes |
| **prefilling Ns** | shown while `num_requests_running` > 0, live TG = 0 and KV > 0; N = seconds since the first poll that matched. On vLLM a prefill alongside other decoding requests isn't flagged (TG > 0) |
| **TG avg** | mean of live TG over the polls where it was > 0, **since the monitor started or the server last restarted**. Idle polls are excluded, or the number would mostly measure idle time (user request, 2026-11-01; it was a 30 s window before) |
| **TG max** | highest live TG over the same span |
| PP `avg` | first-vs-last over `win` (not shown in per-request PP mode) |
| run / wait | `num_requests_running` / `max_batch_size` (`/health`) · `num_requests_waiting` |
| **KV** | TensorFold: max over pools of `kv_cache_usage_ratio`; vLLM: `kv_cache_usage_perc` (one shared pool); × 100 |
| KV tokens | KV ratio × capacity / capacity. Capacity = `kv_cache_size_tokens` (vLLM pool) or `context_length` (TensorFold, whose ratio is per stream) |
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
- **Rings:** 60 slots (120 s at 2 s) each for TG live, PP and KV.
  A missed poll pushes a gap (NaN), so time spacing is preserved. The PP
  ring holds PP live on vLLM. On TensorFold it holds **PP eng at the poll a
  request finished, and 0 otherwise**: one point per finished request.

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

Built in M2 (2026-11-01); code in `src/gfx/`, `src/ui/`, `src/hw/screen.cpp`.

- **Grid:** 48 × 20 cells of 10 × 24 px = exactly 480 × 480. (Until
  2026-11-01 it was 48 × 24 cells of 10 × 20 px; the user found the lines
  too tight.) Each 20 px Terminus glyph sits centred in its 24 px cell, with
  4 px of leading. The padding rows repeat the glyph's first and last row:
  blank for text, but a continuous stroke for box drawing and blocks, so
  frame lines and bars still join. The 2× hero is 40 px tall, centred in its
  48 px two-cell block.
  Each cell = `{uint16 codepoint, uint8 fg, uint8 bg, uint8 attr}`, with
  colours stored as theme roles. Pages draw the whole screen into a `Grid`.
  `screen::present()` diffs it against what's shown and repaints only the
  changed cells.
- **Font:** Terminus 10×20 (`ter-u20n` regular, `ter-u20b` bold for titles,
  the server state and the hero; SIL OFL, `third_party/terminus/`).
  `tools/font/mkfont.py` converts it into `src/gfx/font_ter10x20.h`: ASCII,
  `· — … ● ○`, box drawing `─│┌┐└┘├┤┬┴┼`, blocks `▁▂▃▄▅▆▇█░` and the full
  braille range U+2800–28FF. That's 376 regular + 97 bold glyphs, ~19 KB.
  Terminus's box and block glyphs tile exactly in the 10×20 glyph box, so nothing
  is drawn procedurally except the hero.
- **Hero text:** `ter-u20b` glyphs pixel-doubled to 20 × 40, each covering a
  2 × 2 block of cells (`ATTR_BIG` + quadrant). Used only for TG live.
- **Sparklines** (overview), scaled to the window max, gap = dim `·`:
  - PP: 30 block cells, each = max of 2 samples → 120 s at 8 levels.
  - TG (next to the hero): 20 cells × 2 rows, each cell = max of 3 samples
    → 120 s at 16 levels.
- **Graphs** (graphs page): braille, one cell = 2 samples × 4 levels, so
  30 columns = 60 samples = 120 s. A gap leaves its dot column empty.
- **Blit:** the dirty cells of each cell row are rasterised into a 19 KB
  band buffer in internal SRAM. The span from the first to the last dirty
  column is then copied to the PSRAM framebuffer, and that band is written
  ~~back out of the CPU cache~~ (dropped 2026-11-01: with bounce buffers the
  driver reads the framebuffer through the cache, so no writeback is needed,
  and its PSRAM bursts caused glitches; bring-up §2.9).
- **Speed on the board** (`-O2`, font tables in DRAM and raster code in
  IRAM, `gfx/platform.h`):

  | Operation | Time |
  |---|---|
  | Full-page repaint (theme or page switch) | ~28 ms |
  | Clock tick (1 cell) | ~0.3 ms |
  | Laying out a page into the grid | ~1.5 ms |

  The full repaint is bound by PSRAM bandwidth (~10 ms copy + ~8.5 ms cache
  writeback), which the LCD DMA shares, so the original ≤ 10 ms target isn't
  reachable this way.
- Redraw on change only: after each poll, on touch, and once a second for
  the clock. The poller never touches the grid.
- Tearing: not visible in testing. If it shows up, switch to double
  framebuffers with a swap on VSYNC (`num_fbs = 2`, another 450 KB PSRAM).

### Themes (chosen in Settings, saved in NVS by name)

43 themes: **EngineMetrics** (the original curses palette, theme 0), then
btop's built-in **Default** and its **41 bundled themes** (nord, gruvbox,
dracula, tokyo-night, solarized, …). The btop themes are vendored in
`third_party/btop/` (Apache-2.0, commit in `SOURCE.txt`) and converted into
`src/gfx/themes_btop.h` by `tools/themes/import_btop.py`. As in btop, any key
a theme doesn't set comes from Default; `"#gg"` is grey shorthand and an
empty `main_bg` means black.

| Role | btop key | Used for |
|---|---|---|
| BG | `main_bg` | background |
| VALUE / TITLE / LABEL / DIM | `main_fg` / `title` / `graph_text` / `inactive_fg` | text |
| BORDER, BOX2, BOX3, BOX4 | `cpu_box`, `mem_box`, `net_box`, `proc_box` | the header / Throughput (and TG graph) / Streams (and PP graph) / Averages (and KV graph) boxes, one colour each as in btop |
| GOOD / WARN / BAD | `cpu_start` / `cpu_mid` / `cpu_end` | thresholds and states (btop's low → high gradient) |
| DECODE | `proc_misc` | TG numbers and graphs |
| BRAND | — (fixed `#ff3b30`) | `SYSTEM-D` in the header: the same red in every theme (user request) |
| PREFILL | `temp_mid`, or the fallback (`temp_end`, `upload_end`, `hi_fg`, …) most distinct from DECODE and the background | PP numbers and graphs |

Tune the colours on the real panel, because RGB565 shifts them.

---

## 7. Screens (48 × 20)

The layout follows the 2026-11-01 sketch, drawn **the way btop draws**
(user's choice, 2026-11-01; it replaced a single frame with `├─┤ Title ├`
junctions, which the user didn't like):
- every section is its **own box** with rounded corners `╭ ╮ ╰ ╯`;
- titles and labels sit in **notches** on the border: `╭─┐Throughput┌──╮`
  on top, `╰─┘poll 2s · ok 0.4s ago└──╯` on the bottom (btop's
  `title_left/right` and `title_left/right_down` symbols);
- each box has its own colour (`cpu_box`, `mem_box`, `net_box`, `proc_box`);
- the last box's bottom edge is the footer, with the **`≡` settings notch**
  in the bottom-left corner;
- a 2× "hero" decode number and a per-stream table (nv-monitor style).

The mockups below are the real renders of the demo data: the golden files
in `test/test_render/golden.h`, checked by `pio test -e native`.

Values are illustrative, and assume TensorFold already exports Appendix A.

### 7.1 Overview

```
╭─┐SYSTEM-D┌─┐12:47:33 PM┌──────────────┐▂▄▆█┌─╮
│ dgx042 · 192.168.50.192              OK BUSY │
│ TensorFold · deepseek-v4.1-flash-1017        │
╰──────────────────────────────────────────────╯
╭─┐Throughput┌─────────────────────────────────╮
│ TG １０１．３ tok/s    █▆▄▄▅▆███▆ ▄▄▅▇██     │
│                live ▁▁▁██████████·██████     │
│ PP last 3,354/s · prefilling 12s             │
│ run 2/4 · wait 1 · TG avg 95.1 · max 104.2   │
│ KV 31.4% █████░░░░░░░░░░░ · 329K/1.0M tok    │
╰──────────────────────────────────────────────╯
╭─┐Streams┌────────ctx─tokens───TTFT──────TG───╮
│  0  decoding   31.4%   329K   1.2s  62.7/s   │
│  1  prefill     2.0%    21K  no data         │
│  2  idle        0.0%      0                  │
│  3  idle        0.0%      0                  │
╰──────────────────────────────────────────────╯
╭─┐Averages┌───────────────────────────────────╮
│ TTFT 1.85s · MTP 48.9% · cache-hit 75.4%     │
╰─┘≡└─┘poll 2s · ok 0.4s ago└────┘tap: graphs└─╯
```

(The hero `１０１．３` is written in fullwidth digits to show its width. On the
device it is drawn at 2× and spans rows 4–5, 2 columns per character.)

| Rows | Box | Content |
|---|---|---|
| 1–4 | header (BORDER) | top edge: `┐SYSTEM-D┌─┐7:42:10 PM┌` (two notches; the border between them is the divider) and the Wi-Fi bars `┐▂▄▆█┌` in the top-right notch (RSSI ≥ −55/−67/−75/−85 dBm → 4/3/2/1 lit); row 2: **server name · IP** (the resolved address), state right-aligned (§8); row 3: **engine · model**; bottom edge |
| 5–11 | Throughput (BOX2) | hero TG live at 2× + 2-row 120 s sparkline; `PP last 3,354/s · prefilling 12s`; `run x/max · wait n · TG avg … · max …` (since start, §5); `KV 31.4% ████░░ · 329K/1.0M tok` (Cache folded in) |
| 12–17 | Streams (BOX3) | column labels `ctx tokens TTFT TG` in the top edge over their columns; 4 stream rows (dgx042's batch size) |
| 18–20 | Averages (BOX4) | `TTFT · MTP · cache-hit`; bottom edge = footer: `┘≡└` settings notch, `┘poll 2s · ok 0.4s ago└` (or the problem, §8), `┘tap: graphs└` or `┘1/2└` |

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
╭─┐SYSTEM-D┌─┐12:47:33 PM┌──────────────┐▂▄▆█┌─╮
│ dgx042 · 192.168.50.192              OK BUSY │
│ TensorFold · deepseek-v4.1-flash-1017        │
╰──────────────────────────────────────────────╯
╭─┐TG live tok/s┌───────────────────────┐120s┌─╮
│ ⠀⠀⠀⠀⢸⣤⡀⠀⠀⠀⠀⠀⠀⣠⣾⣾⣿⣧⣄⠀⠀⠀⠀⠀⠀⢀⣴⣷⣿⣿ now  101.3    │
│ ⠀⠀⠀⠀⢸⣿⣿⣧⣄⣀⣀⣴⣿⣿⣿⣿⣿⣿⣿⠀⠀⣀⣀⣠⣾⣿⣿⣿⣿⣿ avg   73.7    │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿ max  102.0    │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿ min    0.0    │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿               │
│ ⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿               │
╰──────────────────────────────────────────────╯
╭─┐PP per request tok/s┌────────────────┐120s┌─╮
│ ⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀ last 3,354    │
│ ⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀⡇⠀⠀⠀⠀⠀⠀⠀ avg  4,385    │
╰──────────────────────────────────────────────╯
╭─┐KV %┌────────────────────────────────┐120s┌─╮
│ ⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀ now  31.4%    │
│ ⠀⠀⠀⠀⢀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣤⠀⠀⣤⣤⣤⣤⣤⣤⣤⣤⣴ max  31.4%    │
╰─┘≡└─┘poll 2s · ok 0.4s ago└──┘tap: overview└─╯
```

(Monospace terminals render braille at varying widths, so the mockup may
look ragged. On the device every glyph is one 10×24 cell.)

- Rows 1–2 and the footer rule are the same as the overview.
- Boxes: TG live 6 rows (BOX2), PP 2 (BOX3), KV 2 (BOX4, its bottom edge
  is the footer). Stats are shown for as many lines as the graph has rows
  (`now/avg/max/min` for TG, `last/avg` for PP, `now/max` for KV).
- The span in each box's right notch is 60 samples × the refresh rate:
  `60s` at 1 s, `120s` at 2 s, `5m` at 5 s, `30m` at 30 s.
- Braille is stretched over the full 24 px cell instead of padded, so the
  dot rows stack without gaps. Right column: `now / avg / max / min` for the window.
- The PP graph is titled **PP per request tok/s**: one bar
  per finished request. Its stats cover finished requests only, and
  `last` replaces `now`.
- The y-scale is the window max, with a floor (TG 10, PP 100) so idle noise
  doesn't fill the box. KV is fixed at 0–100 %.
- The column gap in TG is a missed poll or a restart.
- A graph with no samples in the window shows `no data` centred in its box.

### 7.3 Settings

Opened by tapping the **bottom-left corner** (`≡`, row 20, x < 120); the
same corner or **done** goes back.

```
╭─┐Settings┌───────────────────────────────────╮
│ Refresh   ‹  2 s  ›   tap ‹ or ›             │
│ TG 101.3 · PP 3,354 · KV ██░░ · OK BUSY DOWN │
╰──────────────────────────────────────────────╯
╭─┐Themes┌───────────────────────────────┐2/2┌─╮
│  kanagawa-wave         tokyo-storm           │
│  kyli0x                tomorrow-night        │
│  matcha-dark-sea       twilight              │
│  monokai               whiteout              │
│  night-owl                                   │
│ ▶nord                                        │
│  onedark                                     │
│  orange                                      │
│  paper                                       │
│  phoenix-night                               │
│  solarized_dark                              │
│  solarized_light                             │
│  tokyo-night                                 │
│ ‹ prev             done               next › │
╰─┘≡└─┘tap a theme to apply└───────────────────╯
```

| Row | Tap | Does |
|---|---|---|
| 1–4 (the whole Settings box, 72 px) | left / right half | refresh rate down / up: **1, 2, 3, 5, 10, 30 s** (default `POLL_MS`). `‹` is drawn in the left half (col 15) and `›` in the right half (col 34), matching the zones. Saved in NVS (`poll_ms`); the poller tasks pick it up on their next cycle |
| 3 | — | live preview of the current theme's roles |
| 6–18 | a name | apply that theme immediately (screen repaints in it), saved in NVS by **name** (`theme`) so adding themes never shifts the choice. Two columns × 13 rows = 26 per page; `▶` marks the current one |
| 19–20 (nav row + bottom edge, 48 px) | left / centre / right third | previous page / done / next page; the `≡` corner (row 20, x < 120) also closes Settings |

### 7.4 Boot

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
| tap left third (x < 160) | previous server (with > 1 server) |
| tap right third (x ≥ 320) | next server (with > 1 server) |
| tap centre (anywhere with 1 server) | overview ↔ graphs |
| tap bottom-left corner (`≡`, row 20, x < 120) | Settings (§7.3) |
| hold ≥ 0.7 s | nothing (it used to cycle the theme; with 43 themes that moved to Settings) |
| `ROTATE_S > 0` | auto-advance to the next server every N s; any touch pauses it for 60 s |

A tap is a press and release within 500 ms, with movement < 24 px.
GT911 is polled at ~30 Hz. Touch coordinates match the display at rotation
0 with no transform (verified in M1). Theme (by name), refresh rate, page
and the current server are saved in NVS. The footer's right label shows `‹ 1/2 ›` (current/total)
(`┘1/2└`) instead of `tap: graphs` when more than one server is configured. It
has no `‹ ›`: it isn't a button, and a `‹` drawn in the right third would point
the wrong way. Every tap is logged on serial (`tap x=… y=… (col, row)` plus the
Settings action) for checking touch targets.

---

## 10. Firmware architecture

### Config — `include/config.h` (gitignored; template `include/config.example.h`)

```cpp
#pragma once
// System-D Monitor configuration (spec.md §10).
//
// Copy this file to include/config.h and fill it in. config.h is gitignored,
// so your Wi-Fi password never ends up in the repo. Without a config.h the
// firmware still builds (from this file) and shows NO WIFI.

#define WIFI_SSID   ""          // 2.4 GHz network (the ESP32-S3 has no 5 GHz)
#define WIFI_PASS   ""
#define TZ_POSIX    "PST8PDT,M3.2.0,M11.1.0"   // header clock; POSIX TZ string

// Servers to monitor, 1–4: { name, host, port }. name nullptr = host up to
// the first dot. Use host names the router's DNS knows ("dgx042.lan"): the
// ESP32 has no DNS search domain, so a bare "dgx042" won't resolve.
#define SERVERS {                                \
    {nullptr, "dgx042.lan", 40000},              \
}
#define ROTATE_S    0             // auto-advance to the next server every N s (0 = off)

#define POLL_MS     2000          // /metrics every 2 s
#define CONNECT_MS  800
#define READ_MS     1500
#define BRIGHTNESS  200           // 0–255 backlight PWM
```

`src/app_config.h` turns `SERVERS` into `kServers[]` (1–4 entries,
checked at compile time). An older single-server config with
`SERVER_HOST`/`SERVER_PORT` still works. It uses `config.h` if it exists. Otherwise it builds from
the example with Wi-Fi off, the footer reads `no include/config.h`, and the
compiler prints a `#warning`.

### Tasks

| Task | Core | Job |
|---|---|---|
| `poller` | 0 (with Wi-Fi) | Every `POLL_MS`: GET `/metrics` (stream-parse), plus `/health` / `/v1/models` when due. Build a `Sample`, run `derive`, push rings, publish a `View` snapshot under a mutex. |
| `ui` (Arduino `loop`) | 1 | Poll touch, copy the `View` under the mutex, render the page into the back grid, diff-blit, drive the backlight. |

The UI only reads published snapshots. The poller never draws.

All state belongs to a single `Server` struct (config, `win[15]`, three
60-float rings, last error, backoff). That's about 4 KB, statically
allocated. Multi-server later means an array of these, with no other
structural change. The grids are 2 × 48 × 20 × 5 B ≈ 10 KB. There's no heap
allocation in steady state, so a 24 h soak should show flat free heap.

### Layout

```
platformio.ini            env:board, env:diag (archived diagnostics), env:native (host tests)
include/board_pins.h      pin map
include/config.example.h  (M3) Wi-Fi + server; copy to config.h (gitignored)
src/main.cpp
src/hw/       panel.cpp (ST7701 + RGB bus)  backlight.cpp  touch_gt911.cpp
              screen.cpp (diff + band blit to the PSRAM framebuffer)
src/gfx/      grid.cpp  raster.cpp  font.cpp  font_ter10x20.h (generated)
              theme.cpp  platform.h (FAST_DATA / FAST_CODE)
src/net/      (M3) wifi.cpp  http_get.cpp  poller.cpp
src/metrics/  (M3–M4) prom.cpp  tf_health.cpp  sample.h  derive.cpp  ring.h
src/ui/       view.h  fmt.cpp  widgets.cpp  chrome.cpp  page_overview.cpp
              page_graphs.cpp  demo_view.cpp
test/fixtures/tensorfold-1.1.8-idle/  metrics.txt health.json v1_models.json
test/test_render/ (golden page dumps)  test/test_fmt/   (PlatformIO native, Unity)
tools/font/mkfont.py  tools/update_golden.py  tools/display-diag/  tools/capture.sh (M3)
third_party/terminus/  ter-u20n.bdf ter-u20b.bdf OFL.TXT
```

- `ui/widgets` holds the §7 visual language: the closed `├─┤ title ├──┤`
  frame with an optional right-hand label, `│ … │` rows, the 2× hero, bars, 1- and 2-row
  sparklines, the stream table and braille graphs. Pages only compose
  widgets.
- `metrics/`, `gfx/grid` and `ui/` compile without Arduino headers, so they
  can be tested on the host.
- `test_render` renders each page to a 48×20 text dump and compares it with
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
| M2 ✅ 2026-11-01 | Renderer | Font + widgets; static §7.1 / §7.2 on the panel in both themes; tap/hold gestures; host golden tests. Full-page repaint ~28 ms (PSRAM-bound, §6), incremental < 1 ms |
| M3 ✅ 2026-11-01 | Data in | Wi-Fi + SNTP; streaming HTTP; parser passes native tests on the idle fixture; engine identification passes tests for TensorFold (real fixture), vLLM (synthetic `/metrics`) and UNKNOWN (an HTML page, an empty body, another exporter); serial prints parsed samples from dgx042. *Differences:* the load behaviour was checked from the live serial log, not a saved load fixture; the vLLM `/version` confirmation is deferred with vLLM support |
| M3a | TensorFold exports (in the TensorFold repo) | Appendix A metrics live on dgx042; a load capture with ≥ 2 concurrent requests saved as `test/fixtures/tensorfold-<ver>-load/` |
| M4 ✅ 2026-11-01 | Derive | Rates, window, rings, restart detection, prefill detection; native tests on sequences built from the real capture (rates, idle, restart, stale/down/no-wifi, not-an-engine, prefill), plus the live load test |
| M5 (in progress) | Live UI | Both pages fed by real data; tap/hold work; every §8 state reachable (pull Wi-Fi, stop the server) |
| M6 | Hardening | NVS persistence; backoff; 24 h soak with flat heap and no stuck states |

---

## 12. Decisions & open questions

**Decided (2026-11-01)**
- v1 started with one TensorFold server (dgx042); vLLM (dick6000) and
  multiple servers were added the same day.
- Overview layout follows the sketch: a header with clock / server name /
  signal, a server rule with state, Throughput with a 2× TG hero, Cache,
  Streams table and Averages, closed into one btop-style frame (§7).
  EngineMetrics supplies the metric vocabulary and colours, not the
  layout.
- Decode and prefill always use distinct colours.
- Unavailable values read `no data` (rules in §5), never `—` or a hidden
  column.
- The monitor confirms that the server is TensorFold or vLLM (§4.3),
  spells out which one on screen, and treats anything else as UNKNOWN.
- TensorFold's exports will be extended (Appendix A) to fill the Streams
  table and cache-hit. Deferred for now: those values read `no data`.
- Config: compile-time `config.h`.
- Renderer: custom char grid on Arduino_GFX, no LVGL.
- Density: 48 × 20 cells of 10 × 24 px (Terminus 10 × 20 + 4 px leading; was 48 × 24 until the user asked for more line spacing).
- Orientation: native (rotation 0).
- **Header shows the server's name** (`dgx042`), not the monitor's IP.
- **PP on both engines:** no live prefill rate. The PP row shows `PP last
  <rate>` plus `prefilling Ns` while a prefill runs, and the PP
  sparkline/graph plots per-request prefill speed (§5, §7). The user chose
  this for TensorFold, over keeping Δprompt/Δt and over a per-request-only
  display. It was extended to vLLM when dick6000 was added, because vLLM
  also counts prompt tokens only when a prefill completes.
- **Throughput row 9 is `TG avg · max`**, both since start (§5). The
  engine-time `eng` rate is off the screen (user: "eng is kind of
  confusing"); it's still in the serial log.
- **Clock is 12-hour** (`2:47:33 PM`).
- **btop boxes + btop themes + Settings (2026-11-01):** separate rounded
  boxes with notched titles instead of `├─┤` dividers; all 42 btop themes
  plus EngineMetrics; a Settings screen (bottom-left `≡`) for theme and
  refresh rate.
- **Header (user, 2026-11-01):** Wi-Fi bars pinned top-right; server
  name + IP on their own line with the state right-justified; engine +
  model on their own line, so the full model name fits.
- **Multiple servers (2026-11-01):** up to 4 in `SERVERS`, one poller task
  each, left/right thirds to switch, `‹ n/N ›` in the footer.

**Answered by the 2026-11-01 load test** (live serial log, single requests
of up to a 127K-token prompt)
1. **In-flight tokens: yes, continuous.** While decoding, TG live read a
   steady 50–61 tok/s with no double count. The poll where a request
   finishes reads lower (34–45 tok/s), one dip per request. That's
   acceptable for now.
2. **`prompt_tokens_total` increments when the request finishes**, not at
   prefill end. During a ~36 s prefill it stayed flat, then jumped by
   ~127K in one poll, so Δprompt/Δt read 0 and then 63K tok/s. That led to
   the TensorFold PP decision above. PP eng from the same poll gives the
   real prefill speed: 3,354 tok/s.
3. **Partly answered.** KV drops to 0.0% the poll a request finishes, so a
   stream with non-zero KV is serving a request, not just holding cache.
   Not yet seen: several requests in flight at once.
5. **`dgx042.lan` resolves** from the ESP32 via the router's DNS. Polls
   take 16–50 ms on Wi-Fi (114 ms worst seen) at −31 to −46 dBm.

**Still open**
3. With several requests in flight, how many `kv_cache_usage_perc{stream=…}`
   series appear?
4. Do `kv_cache_usage_ratio{pool=…}` and `kv_cache_usage_perc{stream=…}`
   differ under load? The monitor currently shows KV = max of the pool
   series. Capture with `tools/capture.sh dgx042:40000 30 <name>` during
   concurrent requests.
6. Implement Appendix A in TensorFold (M3a), including A.4. Item 4 can be
   settled from the TensorFold source.

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

### A.4 Live prefill progress (optional, makes PP live possible)

| Metric | Type | Value |
|---|---|---|
| `tensorfold:prompt_tokens_prefilled_total` | counter | prompt tokens prefilled so far, **incremented as prefill chunks complete** (not at request end) |

With it, the monitor can show a true live PP rate (Δ/Δt), as on vLLM,
instead of `prefilling Ns` (§12, load test Q2). Until then, PP on TensorFold
is per request.
