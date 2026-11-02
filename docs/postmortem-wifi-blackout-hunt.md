# Post-mortem — the Wi-Fi blackout hunt (failed attempt, 2026-11-01)

Work stopped on user request at the end of this session. This document is
the complete record of that attempt: what was tried, what each result
ruled out, every failure mode encountered (board, firmware, and process),
and what remains open. The repo now carries a **different, working
lineage** (`b062fae..9a3fe9f`, see [devlog](devlog.md) — M1–M4 live on
panel). Everything below documents the *earlier* attempt lineage that
never got a stable screen with Wi-Fi, plus one follow-on pivot that also
failed. Its own git history (`890e9d0`, `8b4a52f`, `c08fd83`, `ec74779`,
`382dfef`, ~25 commits) was superseded by the working repo reset; the
record lives here.

---

## 1. The problem as it presented to this session

A working-looking build (character-TUI dashboard, Arduino_GFX RGB565 at
480×480, PSRAM framebuffer) drew correctly with Wi-Fi **off**. The moment
the station associated with an AP the screen went permanently black:

* serial stayed alive and sane (framebuffer readback kept counting
  non-black pixels — the panel data path kept being fed),
* Wi-Fi stayed connected and the board answered pings in ~7 ms,
* the panel showed no backlight glow whatsoever, ever, afterwards,
* a reboot restored the picture until the next association.

That signature — alive CPU + alive framebuffer + dead panel with no glow —
was the one thing every test kept reproducing and nothing explained.

## 2. What was tried, in order, with results

All builds were flashed to the same board (CH340, `/dev/cu.usbserial-110`),
and judged by serial logs plus a human "can you see it?" oracle.

| # | Change | Result / what it ruled out |
|---|---|---|
| 1 | Start Wi-Fi on core 0 instead of core 1 | black — not a core-collision story |
| 2 | Start Wi-Fi *before* display init | black — not an internal-RAM-first allocation failure |
| 3 | Backlight blink on GPIO38 after blanking | "no glow at all" per user — later shown to be an untrustworthy observation (§4.2, §5-B1) |
| 4 | Panel `end()` + full re-init at runtime | reinit returned OK, still black — not a wedged-DMA-permanent state |
| 5 | 11 MHz PCLK + 60-line bounce buffers | black — not a marginal-bandwidth-at-12MHz story |
| 6 | Rebuild `esp_lcd_panel_rgb.c` with `RESTART_IN_VSYNC=n` (the esp-idf#16893 prebuilt-vs-source discrepancy) | black |
| 7 | Same object + `c0fb05b` EOF-resync cherry-pick (the esp-idf#19070 driver fix, not in release/v5.5) | black — upstream's two driver-logic fixes are not the answer for this unit |
| 8 | pioarduino **HybridCompile**: full IDF-lib rebuild with `SPIRAM_FETCH_INSTRUCTIONS`, `SPIRAM_RODATA`, `DATA_CACHE_LINE_64B`, `RESTART_IN_VSYNC` unset (Espressif's documented mitigation set, esp-idf#12342) | built, verified in `qio_opi` sdkconfig, patched object re-applied, flashed — Wi-Fi solid, frames fed, **still black** |
| 9 | Pivot to the community-proven stack: **ESPHome 2026.10.1 / IDF 5.5.6 source build** with the stock RemoteWebView client (a project written for this exact board, streaming Wi-Fi→JPEG→panel) | Wi-Fi up, WebSocket connected, tiles decoded (24 ms full frame, 1–4 ms diffs) and drawn, board pinging — **black** |
| 10 | Same, with the vendor's own ST7701 init table (`st7701_type9`) and this board's proven porch timings (hbp 50 / vbp 20) replacing ESPHome's built-in sequence | black — init-table mismatch exonerated |

After 10, the plan of record was `num_fbs=0` with a custom user-refilled
bounce driver. It was never built: the user pivoted to cleanup, then
ended the attempt.

## 3. Key external findings (verified, reusable)

* **Nobody had ever combined display + Wi-Fi on this board in the
  Arduino/prebuilt world.** Vendor demo folder: 12 demos, display-only or
  Wi-Fi-only, never both. Same for every third-party "working setup"
  found. The only configurations with real display-over-Wi-Fi traffic are
  ESP-IDF/ESPHome source builds (RemoteWebView, VahaC HA dashboard).
* Upstream family: [esp-idf#19070](https://github.com/espressif/esp-idf/issues/19070)
  (EOF resync fix, master-only), [esp-idf#16893](https://github.com/espressif/esp-idf/issues/16893)
  (arduino prebuilds flipped `RESTART_IN_VSYNC=y` vs IDF default),
  [arduino-esp32#9348](https://github.com/espressif/arduino-esp32/issues/9348)
  (WONTFIX), [esp-idf#12342](https://github.com/espressif/esp-idf/issues/12342)
  ("MEM_SPI bandwidth limitation"; recommends the XIP set),
  [esp-idf#15013](https://github.com/espressif/esp-idf/issues/15013).
  We reproduced the settings of all of them — none fixed the unit.
* RemoteWebView server/client protocol and docker deployment are
  straightforward and worked first try (kept in `../rwv/` compose file).

## 4. Failure-mode catalogue

### 4.1 Board/firmware failure modes *this* session confirmed or exercised

1. **Backlight is not a bare GPIO-LED**: PWM drive quirks matter — the
   successor lineage proved 20 kHz LEDC silently fails to start on GPIO38
   (bring-up §2.1); 1 kHz or plain `digitalWrite` work.
2. **The glass needs explicit INVOFF (0x20)** — type9 never sends it;
   without it all colours invert and white is black (bring-up §2.2). A
   black or "wrong-colour" screen on this panel is often *not* a signal
   problem.
3. **Prebuilt arduino core ≥ IDF 5.4 ships `LCD_RGB_RESTART_IN_VSYNC=y`**,
   different from IDF defaults — changes blanking-vs-flicker behaviour
   (#16893). Real, but not this unit's root cause (test 6/8/9).
4. **PSRAM/flash bus contention under radio load** is real on this design
   (successor lineage fixed *flicker* with bounce buffers, §2.7/§2.9).
   Bandwidth-only explanations, though, failed to explain full black
   (tests 5, 8, 9).
5. **USB-powered brownout / rail sag under combined panel (~260 mA) +
   Wi-Fi TX bursts** — the successor lineage's cause-table §2.8 #3. It
   matches *every* observation this session made and could not explain:
   dead pixels + alive CPU + alive radio + *no backlight glow* (the LED
   rail sags below dropout while the 3.3 V regulator keeps the MCU
   running). **This session never measured power** — no ammeter, no
   different cable, no powered hub, no second supply. `power_save_mode:
   none` (added for latency in the ESPHome configs) removes modem sleep
   and *raises* average radio current — likely made it worse. This is the
   leading untested explanation for tests 9–10, and arguably for the
   original saga. Lesson: put a USB power meter inline before spending a
   weekend in sdkconfig.

### 4.2 Process/tooling failure modes (how the hunt lost time)

1. **Framebuffer readback is a false oracle.** "nonblack=2502 constant"
   looked like a live display; it only proves the CPU-side buffer. Every
   "green" serial report in the blackout tests was worthless — the
   panel/rail was the dead element.
2. **Exit codes swallowed by pipes**: `compile | tail` reported success
   for failing builds; two "flash + test" cycles actually re-flashed the
   *previous* binary and the results were interpreted as if the new
   config had run (web_server and gpio-backlight probes were ghosts).
   Gate on artifacts (`$?` before pipe, new binary timestamp), not logs.
3. **pioarduino HybridCompile landmines**: framework reinstall wipes
   `tool-scons`; its runtime `scons-local-4.11.1` tree is missing
   `Tool/FortranCommon.py` + `Scanner/Fortran.py` (link step dies) —
   healed via a `sitecustomize.py` shim (kept:
   `../tools/libesp_lcd_fix/scons_fortrancommon_selfheal.py`); uv caches
   outside the sandbox; rebuilt archives use `.o` member names where
   prebuilts used `.obj` (a blind `ar r` *adds a duplicate* definition —
   caught at link, fixed in `apply.sh`).
4. **Serial console assumptions**: ESPHome's UART command console
   answered `help` with nothing; the "backlight cycle" probe probably
   never ran, yet its (mis)result informed the next two hypotheses. Any
   probe that cannot self-confirm should not be allowed to steer.
5. **ESPHome config traps hit** (2026.10): `platformio_options` nests
   under `esphome:`; init-sequence delays must be strings `delay 120ms`
   (bare `[120]` becomes command 0x78); `!include` of a sequence file
   mis-nests (inline the lists); `monochromatic` light needs a float
   output (gpio output fails validation); `web_server version: 3` has
   different REST paths than legacy (`/light/x/status` 404s).
6. **Component-manager git cache** in `~/Library/Caches/Espressif` needs
   full network access or cmake dies mid-configure.

## 5. State at stop time

* Board currently flashed with ESPHome RemoteWebView stock + type9 init
  (black, per §2.9–2.10 — power hypothesis untested).
* Working lineage in this repo untouched by this session's cleanup; the
  cleanup (~29 GB) removed only caches/toolchains: `~/.cache/uv`,
  `~/Library/Caches/{esphome,Espressif}`, docker image
  `strangev/remote-webview-server`, project `.platformio`/venvs/scratch.
  User's open-webui containers and Docker Desktop were left running.
* Surviving side-artifacts, on disk but gitignored by the current repo
  (they were tracked in the replaced history): `esphome/` configs
  (phase-1 test-card + RemoteWebView client for this board), `rwv/`
  (docker compose + TUI-style dashboard page draft),
  `tools/libesp_lcd_fix/` (patched esp_lcd object, apply script, scons
  self-heal, README), `sdkconfig.rgbsafe.defaults`. These are only
  relevant if anyone revisits the prebuilt/HybridCompile path; the
  working lineage does not need any of them.
* The attempt-lineage's firmware source (board.cpp/ui_term/SELFTEST
  harness) exists only in the replaced git history.

## 6. If this is ever resumed

1. **Measure power first** — USB inline meter, known-good short cable,
   ≥500 mA supply, panel full-white + Wi-Fi iperf load, watch the 5 V
   rail (bring-up §2.8 #3).
2. Then follow [hardware-bringup.md](hardware-bringup.md) §4 — the
   successor firmware already runs Wi-Fi + panel + touch stably; most
   failure modes in §4.1 above are already engineered around.
3. Do **not** re-enter the sdkconfig rabbit hole without step 1.

## 7. Sources consulted (deduplicated)

esp-idf issues: 19070, 16893, 12342, 15013 · arduino-esp32 9348 ·
RemoteWebViewClient (vantreeseba fork of strange-v) + RemoteWebViewServer
· VahaC ESP32-4848S040-EspHome-LVGL · devices.esphome.io guition page ·
aquaelectronics working-ArduinoGFX thread (display-only) ·
dmatking/esp32-video-stream · atomic14 TinyTV · HomeDing panel board page
· vendor folder (1-Demo 12 demos audited, IO pin distribution, burn
files) · openHASP 791 · ESPHome 16328 · LVGL forum horizontal-offset
thread · esp32.com 135705.
