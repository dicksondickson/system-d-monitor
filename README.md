# System-D Monitor

A desk dashboard for TensorFold inference metrics, running on a Guition
ESP32-S3-4848S040 (4″ 480×480 touch panel). The design is in
[`spec.md`](spec.md).

**Status:** live (2026-11-01). The board polls two servers over Wi-Fi
every 2 s: dgx042 (TensorFold) and dick6000 (vLLM). It shows throughput,
cache, streams and averages. M3 + M4 are done; M5 (every error state, the
boot screen) is in progress. History: [`docs/devlog.md`](docs/devlog.md).

## Using it

| Touch | Does |
|---|---|
| tap the left / right third | previous / next server (footer shows `‹ 1/2 ›`) |
| tap the centre | overview ↔ graphs |
| hold ~1 s | switch theme (ansi ↔ btop) |

Theme, page and server survive a reboot.

## Reading the screen

| Term | Meaning |
|---|---|
| **TG** / **PP** | decode (token generation) / prefill (prompt processing), in tokens per second. Always two different colours |
| **live** | tokens the server produced per real (wall-clock) second, summed over every request running at once: the throughput you see from outside. Updates every poll |
| **eng** | *engine-time* rate: tokens ÷ the seconds the engine actually spent decoding (or prefilling) them, from the server's per-request timers. Roughly single-request speed. Only updates when a request finishes, so it often reads `no data`. With several requests batched, live > eng |
| **avg** | live rate over the last ~30 s (first vs last of 15 polls) |
| **PP last** | prefill speed of the most recently finished request (engine-time). Neither engine reports prefill progress live, so there's no live PP |
| **prefilling Ns** | a request is running, no tokens have been generated yet, and its KV cache is filling: it's in prefill, N seconds so far |
| **run x/max · wait n** | requests running (of the batch limit, TensorFold only) and queued |
| **KV** | KV-cache use. vLLM: the shared pool (`used/pool tok`). TensorFold: the busiest stream's share of its context window |
| **Streams** | per-request slots (TensorFold). Columns read `no data` until TensorFold exports the per-stream metrics in spec Appendix A; vLLM doesn't export them at all |
| **TTFT / lat / n** | mean time to first token / end-to-end latency since the server started, over n requests; `live TTFT` = requests finished in the last poll |
| **MTP** | speculative-decoding acceptance: accepted ÷ drafted tokens |
| **cache-hit** | prompt tokens served from the prefix cache ÷ all prompt tokens (vLLM; TensorFold after Appendix A) |
| **no data** | not exported, not received yet, or nothing happened in the window. Never a fake 0 |
| footer | `poll 2s · ok 0.4s ago`, or the problem: `stale`, `timeout · retry 8s`, `dns …`, `not TensorFold or vLLM`, `wifi lost` |

## Configure

```sh
cp include/config.example.h include/config.h   # gitignored
```

Set `WIFI_SSID` / `WIFI_PASS` (a 2.4 GHz network), `TZ_POSIX` (e.g.
`MST7` for Vancouver), and `SERVERS`: 1–4 `{name, host, port}` entries,
where `name` = `nullptr` uses the host up to the first dot. Use names the
router's DNS knows (`dgx042.lan`). Optional `ROTATE_S` auto-advances
between servers. Without `config.h` the firmware still builds, with Wi-Fi
off.

## Build and flash

PlatformIO Core is installed with uv (keeps it off the system Python):

```sh
uv tool install --python 3.12 platformio
```

Plug the board in with a **data** USB-C cable. It shows up as
`/dev/cu.usbserial-*` (CH340).

```sh
pio run                                              # build
pio run -t upload --upload-port /dev/cu.usbserial-110   # flash (460800 baud)
pio device monitor -p /dev/cu.usbserial-110          # serial log, 115200
pio test -e native                                   # host tests (layout, parsing, derivation)
tools/capture.sh dgx042:40000 30 my-capture          # record /metrics every 2 s as a fixture
```

The serial log prints one line per poll, e.g.
`tg 52.6 (eng nan) pp 0.0 (eng nan) run 1 wait 0 kv 12.0% ttft 2.48s mtp 60.5% rssi -40 dBm`.

After an intentional layout change, run `tools/update_golden.py` and review
the diff of `test/test_render/golden.h`.

On a healthy boot the log shows `psram 8192 KB`, `panel ok` and
`touch GT911 … addr=0x5D`.

## Layout

| Path | What |
|---|---|
| `spec.md` | Full design: data sources, screens, architecture, milestones |
| `docs/devlog.md` | What was built when, decisions, measurements |
| `docs/hardware-bringup.md` | Verified board config + the problems found bringing it up (read this if the screen misbehaves) |
| `esp32-4848s040-guide.md` | General reference for the board (corrected where bring-up proved it wrong) |
| `include/board_pins.h` | Pin map |
| `src/hw/` | Panel (ST7701 + RGB bus), backlight (LEDC 1 kHz), GT911 touch, screen blit |
| `src/gfx/` | 48×24 cell grid, Terminus font, themes, cell rasteriser |
| `src/ui/` | View model, number formatting, widgets, overview + graphs pages, demo data |
| `src/metrics/` | `/metrics` parser, `/health` + `/v1/models`, engine identification, rate derivation |
| `src/net/` | Wi-Fi + SNTP, streaming HTTP GET, poller task |
| `src/main.cpp` | App: live dashboard, tap/hold gestures, NVS-saved theme/page |
| `include/config.example.h` | Config template (copy to `config.h`) |
| `tools/display-diag/` | Archived display diagnostics + the M1 panel test; `run.sh <round\|panel> upload` |
| `tools/font/mkfont.py` | Regenerates `src/gfx/font_ter10x20.h` from `third_party/terminus/` |
| `test/fixtures/` | Captured responses from dgx042 (TensorFold) and dick6000 (vLLM), for parser tests |
| `reference/` | Vendor download pack (datasheets, demos, tools). **Not in git** (295 MB, re-downloadable; link in the guide §1) |

## Board gotchas (details in `docs/hardware-bringup.md`)

- Backlight PWM must be **1 kHz**. At 20 kHz the screen stays black.
- The panel needs an explicit **`0x20` INVOFF**, or colours are inverted.
- R/B pins: R = 11,12,13,14,0 and B = 4,5,6,7,15.
- Upload at **460800**. 921600 corrupts over the CH340.
- Build with **`-O2`**. The platform passes no optimisation flag, so the default is `-O0`.
- **Bounce buffers** on the RGB panel. Without them the screen flickers whenever Wi-Fi is busy.
- Bring the panel up **before** Wi-Fi, and never block on the connection. Black-screen-with-Wi-Fi causes are in bring-up §2.8.
