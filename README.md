# SYSTEM-D Monitor

![SYSTEM-D](docs/images/system-d-logo.jpeg)

SYSTEM-D is a 4″ touchscreen desk dashboard for your LLM inference servers. It shows live
decode and prefill speed, KV cache, queues, streams, TTFT and speculative-decoding
acceptance for **vLLM** and **TensorFold**, in a terminal-style look, on an
inexpensive ESP32-S3 display board.

Inspired by [btop](https://github.com/aristocratos/btop),
[EngineMetrics](https://github.com/0rand/EngineMetrics) and
[nv-monitor](https://github.com/wentbackward/nv-monitor). The colour themes
are btop's. See [Credits](#credits).

![SYSTEM-D Monitor running on the board: switching pages, opening Settings and changing theme](docs/images/demo.gif)

![Overview](docs/images/overview.png) ![Graphs](docs/images/graphs.png)

- **No server-side setup.** The board polls each engine's own `/metrics`
  endpoint over Wi-Fi every few seconds and works out rates itself. Nothing
  to install on the GPU box, no Prometheus, no cloud.
- **vLLM and TensorFold**, detected automatically, **up to 4 servers**; tap
  left or right to switch between them.
- **42 colour themes** from [btop](https://github.com/aristocratos/btop).
  The default, **SYSTEM-D**, is btop's own Default palette.
- **Honest numbers:** anything not exported, not received yet, or not
  measurable reads `no data`, never a made-up 0.

![Themes](docs/images/themes.png)

<sub>Screenshots are rendered from the firmware's own drawing code with demo data (`tools/render/screens.py`).</sub>

---

## What you need

| | |
|---|---|
| **Board** | **Guition ESP32-S3-4848S040** (also listed as "JCZN" / "ESP32-4848S040" / "4 inch 86 box smart display"): ESP32-S3-WROOM-1 **N16R8** (16 MB flash, 8 MB PSRAM), 480×480 ST7701 IPS panel, GT911 touch. The variants with or without relays and the bottom shell all work. The one used here was bought on **AliExpress** for about **CA$37**: search for "ESP32-S3 4.0 Inch Smart Display Development Board" or "ESP32-4848S040" |
| **Cable** | USB-C **data** cable (a charge-only cable won't show up as a serial port) |
| **Power** | 5 V USB, about **210 mA (≈ 1 W)** measured while running this firmware on Wi-Fi. Any USB port or phone charger will do; Wi-Fi bursts briefly draw more, so avoid weak hubs |
| **Computer** | macOS, Linux or Windows, with Python 3.9+ for PlatformIO |
| **Wi-Fi** | a **2.4 GHz** network (the ESP32-S3 has no 5 GHz radio) on the same LAN as your servers |
| **Servers** | vLLM (V1 metrics; tested with 0.33) or TensorFold (tested with 1.1.8), reachable over plain HTTP with no auth on `/metrics` |

![The board running SYSTEM-D Monitor, powered through a USB meter reading 5.1 V, 0.21 A, 1.07 W](docs/images/photo.jpg)

The board connects through a CH340 USB-serial chip. macOS and Linux have the
driver built in. On Windows, install the
[CH340 driver](https://www.wch-ic.com/downloads/CH341SER_EXE.html) if no COM
port appears.

## Quick start

> **Using an AI coding agent?** Point it at [`SKILL.md`](SKILL.md): it walks
> the agent through the config, build, flash and checks below.

**1. Install PlatformIO Core** (any one of these):

```sh
pipx install platformio     # or: uv tool install platformio   /   pip install platformio
```

Or install the PlatformIO extension for VS Code.

**2. Get the code:**

```sh
git clone https://github.com/dicksondickson/system-d-monitor.git
cd system-d-monitor
```

**3. Configure.** Copy the template. Your copy is gitignored, so your Wi-Fi
password never gets committed.

```sh
cp include/config.example.h include/config.h
```

Then edit `include/config.h`:

```cpp
#define WIFI_SSID   "your-2.4GHz-network"
#define WIFI_PASS   "your-password"
#define TZ_POSIX    "PST8PDT,M3.2.0,M11.1.0"   // your timezone as a POSIX TZ string

#define SERVERS {                                                          \
    {nullptr, "gpu-box.lan", 8000},    /* vLLM */                          \
    {"dgx",   "192.168.1.60", 40000},  /* TensorFold, with a custom label */ \
}
```

- Each entry in `SERVERS` is `{label, host, port}`, and there can be 1–4.
  A `nullptr` label shows the host up to its first dot.
- Use a host name your router's DNS resolves (`gpu-box.lan`) or an IP
  address. The ESP32 has no DNS search domain, so a bare `gpu-box` won't
  resolve.
- **Time zone:** the last line of `/usr/share/zoneinfo/<Area>/<City>` on
  macOS or Linux, or see this
  [list of POSIX TZ strings](https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv).
  For example, `Europe/London` is `GMT0BST,M3.5.0/1,M10.5.0`.

**4. Build and flash.** Plug the board in, then:

```sh
pio run -t upload           # builds, finds the serial port, flashes (~30 s)
pio device monitor          # optional: serial log at 115200
```

If it picks the wrong port, add `--upload-port <port>` (and `-p <port>` for
the monitor). Typical port names:

- macOS: `/dev/cu.usbserial-*`
- Linux: `/dev/ttyUSB0` (add yourself to the `dialout` group)
- Windows: `COM3`

**5. Done.** The board joins your Wi-Fi and starts polling. The serial log
shows every poll:

```
[gpu-box] /metrics    ok, 67420 B, 198 ms, vLLM, 511 lines (19 used)
[gpu-box]   tg 52.6 (eng 61.0) pp last 3354.0 run 1 wait 0 kv 12.0% ttft 2.48s mtp 60.5% rssi -40 dBm
```

Without an `include/config.h`, the firmware still builds (from the template,
with Wi-Fi off) and shows `NO WIFI` with `no include/config.h`.

## Using it

| Touch | Does |
|---|---|
| tap the **left / right third** | previous / next server (the footer shows which, e.g. `1/2`) |
| tap the **centre** | overview ↔ graphs |
| tap the **bottom-left corner** (`≡`) | **Settings**: refresh rate (1, 2, 3, 5, 10 or 30 s), screen brightness (60–100 %) and theme. Tap a theme to apply it; `done` returns |
| **hold 3 s** anywhere | screen off (the board keeps polling in the background) |
| tap while off | screen back on |

Theme, refresh rate, brightness, page and server survive a reboot. `ROTATE_S` in
`config.h` auto-advances between servers.

![Settings](docs/images/settings.png)

## Reading the screen

| Term | Meaning |
|---|---|
| **TG** / **PP** | decode (token generation) / prefill (prompt processing), in tokens per second. Always shown in two different colours |
| **live** | tokens the server produced per real second, summed over every request running at once: throughput as seen from outside. Updates every poll |
| **TG avg / max** | average and peak live TG since the monitor started (or the server restarted), counting only polls where it was generating |
| **PP last** | prefill speed of the most recently finished request (prompt tokens ÷ its prefill seconds). Neither engine reports prefill progress live, so there's no live PP |
| **prefilling Ns** | a request is running, no tokens have been generated yet and its KV cache is filling, so it's in prefill, N seconds so far |
| **run x/max · wait n** | requests running (out of the batch limit; TensorFold only) and queued |
| **KV** | KV-cache use right now. vLLM: the shared pool (`used/pool tok`). TensorFold: the busiest stream's share of its context window. The cache is freed when a request ends, so it reads 0 between requests |
| **KV peak** | the highest KV reading in the recent history (the last 60 polls: 60 s at a 1 s refresh, 2 min at 2 s), so you can still see how full the cache got after a request finishes |
| **Streams** | per-request slots (TensorFold). Columns read `no data` unless the server exports per-stream metrics (`SPEC.md`, Appendix A). When a request finishes, its row stays for **10 s** marked `done`, showing its final values, so you can still read it |
| **TTFT · MTP · cache-hit** | mean time to first token · speculative-decoding acceptance (accepted ÷ drafted tokens) · prompt tokens served from the prefix cache |
| **header** | `SYSTEM-D`, the date (MM-DD-YY) and time, and the board's Wi-Fi signal as braille bars `⣀⣤⣶⣿` (top right); then server name · IP with its state (`CONNECTING` just after boot, `OK IDLE`, `OK BUSY`, `STALE`, `DOWN`, `NO WIFI`, `UNKNOWN`); then engine · model |
| **footer** | `≡` (settings), then `poll 2s · ok 0.4s ago`, or the problem (`timeout · retry 8s`, `dns gpu-box.lan`, `not TensorFold or vLLM`, …) |
| **no data** | not exported, not received yet, or nothing happened in the window |

## Supported servers

The board polls these endpoints. Nothing needs enabling on the server; they
just have to be reachable on the LAN without auth.

| | vLLM | TensorFold |
|---|---|---|
| metrics | `GET /metrics` (`vllm:*`, V1 names) | `GET /metrics` (`tensorfold:*`, vLLM-style names) |
| identity | `GET /version` | `Server: TensorFold` header, `GET /health` |
| model | `GET /v1/models` | `GET /v1/models` |

The engine is identified by its metric prefix. A server that is neither
shows `UNKNOWN · not TensorFold or vLLM`. [`SPEC.md`](SPEC.md) §4–5 lists
the exact metrics read and the formula behind every number. If your server's
output looks different, `tools/capture.sh host:port N name` saves its
responses as a test fixture.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| No serial port appears | Charge-only cable, or (Windows) the CH340 driver is missing |
| Upload fails with "Invalid head of packet" | USB link noise. Upload already runs at 460800; try another cable or USB port |
| Screen stays black but serial says `panel ok` | Backlight; see [`docs/hardware-bringup.md`](docs/hardware-bringup.md) §2.1 |
| `CONNECTING` for more than a few seconds | Normal for the first seconds after boot. After 20 s without joining it turns into `NO WIFI` |
| `NO WIFI` · `check SSID / password` | Wrong SSID or password, or a 5 GHz-only network (`wifi lost · retry` instead means it was connected and dropped) |
| `dns …` in the footer | The host name doesn't resolve on the ESP32. Use `name.lan` or the IP |
| `refused` / `timeout` | Wrong port, a firewall, or the server only listens on localhost (check its `--host`) |
| `UNKNOWN · not TensorFold or vLLM` | Something answers on that port, but it isn't an inference server's `/metrics` |
| Inverted colours, shifted image, flicker | See [`docs/hardware-bringup.md`](docs/hardware-bringup.md): every board quirk we hit, and its fix |

## How it works

```
GPU server(s) ── HTTP /metrics every N s ──▶ one poller task per server (core 0)
                                               streaming Prometheus parser,
                                               engine detection, rate derivation
                                                     │
                                                     ▼
                                                   View ──▶ UI loop (core 1)
                                                            page → 48×20 character grid
                                                            → diff → only the changed cells
                                                              are drawn (Terminus font) into
                                                              the PSRAM framebuffer
```

- **Rates** come from the change in counters between polls. A counter that
  goes down means the server restarted, and rates start again from a new
  baseline.
- **Drawing:** a custom character-grid renderer (not LVGL). A full repaint
  takes ~28 ms; a typical update is under 1 ms.
- **Host tests:** parsing, rate maths and screen layouts are plain C++ with
  no Arduino dependency, so they're tested on your computer.

| Doc | What's in it |
|---|---|
| [`SPEC.md`](SPEC.md) | The full design: data sources, metrics and formulas, screens, states, architecture, decisions |
| [`docs/hardware-bringup.md`](docs/hardware-bringup.md) | The verified board configuration, and every problem hit bringing up this board (black screen, inverted colours, `-O0` builds, flicker, glitches), with fixes |
| [`docs/board-guide.md`](docs/board-guide.md) | General ESP32-4848S040 reference: pinout, toolchains, vendor resources |
| [`THIRD_PARTY.md`](THIRD_PARTY.md) | Fonts, themes and libraries used, with their licences |

## Development

```sh
pio test -e native                     # host tests: parser, derivation, layouts, touch hit-testing
pio run                                # firmware build (no board needed)
tools/render/screens.py                # re-render docs/images/*.png
tools/update_golden.py                 # after an intentional layout change: refresh the test goldens
tools/font/mkfont.py                   # regenerate the font from third_party/terminus/
tools/themes/import_btop.py            # regenerate the themes from third_party/btop/
tools/display-diag/run.sh panel upload # flash a panel + touch test screen
```

| Path | What |
|---|---|
| `include/config.example.h` | Configuration template (copy to `config.h`) |
| `include/board_pins.h` | Pin map |
| `src/hw/` | Panel (ST7701 + RGB bus with bounce buffers), backlight, GT911 touch, screen blit |
| `src/gfx/` | Character grid, font, themes, cell rasteriser |
| `src/ui/` | View model, number formatting, widgets, the overview / graphs / settings pages |
| `src/metrics/` | `/metrics` parser, server info and engine identification, rate derivation |
| `src/net/` | Wi-Fi + SNTP, streaming HTTP GET, per-server poller tasks |
| `test/` | Host tests, and real server captures in `test/fixtures/` |
| `tools/` | Font and theme converters, screenshot renderer, capture script, display diagnostics |
| `third_party/` | Terminus font and btop themes, with their licences |

Board quirks to know before you change anything (details in
[`docs/hardware-bringup.md`](docs/hardware-bringup.md)):

- **Backlight PWM:** must be **1 kHz**. At 20 kHz the screen stays black.
- **Colours:** the panel needs an explicit **`0x20` INVOFF**, or the colours
  are inverted.
- **RGB pins:** R = 11,12,13,14,0 and B = 4,5,6,7,15.
- **Uploads:** use **460800** baud; 921600 corrupts over the CH340.
- **Build flags:** build with **`-O2`**. This PlatformIO platform passes no
  optimisation flag, which means `-O0`.
- **Bounce buffers:** use 2 × 20 lines, and no cache writeback after
  drawing. Otherwise the screen flickers or glitches when Wi-Fi is busy.
- **Start-up order:** start the panel **before** Wi-Fi, and never block
  waiting for the connection.

## Credits

Inspired by [btop](https://github.com/aristocratos/btop),
[EngineMetrics](https://github.com/0rand/EngineMetrics) and
[nv-monitor](https://github.com/wentbackward/nv-monitor). Colour themes from
btop.

Font: [Terminus](https://terminus-font.sourceforge.net/). Libraries and
licences: [`THIRD_PARTY.md`](THIRD_PARTY.md).

## License

[MIT](LICENSE) © 2026 [Dickson Chow](https://github.com/dicksondickson). Bundled third-party components keep
their own licences; see [`THIRD_PARTY.md`](THIRD_PARTY.md).
