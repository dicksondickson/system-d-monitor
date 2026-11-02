# Hardware bring-up log — Guition ESP32-S3-4848S040

What it took to get this board's screen, backlight and touch working
(milestone M1, 2026-11-01), and what to check if it ever goes wrong again.
The final, verified configuration is in **§1**. Everything after that is how
it was found.

---

## 1. Verified working configuration

Confirmed on the panel 2026-11-01: colour bars match their labels, the
gradients are smooth, the 1 px border shows on all four edges, and the touch
dot lands under the finger.

| Item | Setting | Where |
|---|---|---|
| Board / chip | ESP32-S3 rev 0.2, 16 MB flash, 8 MB embedded PSRAM (AP_3v3) | — |
| Toolchain | PlatformIO Core 6.2.0 (`uv tool install --python 3.12 platformio`), platform `espressif32@8.0.0` (Arduino core 3.3.x), Arduino_GFX `1.6.9` | `platformio.ini` |
| PSRAM | `memory_type = qio_opi`, `psram_type = opi`, `-DBOARD_HAS_PSRAM` | `platformio.ini` |
| Serial | CH340 at `/dev/cu.usbserial-110`, `-DARDUINO_USB_CDC_ON_BOOT=0`, monitor 115200 | `platformio.ini` |
| **Upload speed** | **460800**. 921600 gives "Invalid head of packet" on this link | `platformio.ini` |
| **Optimisation** | **`-O2` in `build_flags`**. The platform passes none, so the default is `-O0` (§2.5) | `platformio.ini` |
| RGB pins | R0–R4 = 11,12,13,14,0 · G0–G5 = 8,20,3,46,9,10 · B0–B4 = 4,5,6,7,15 | `include/board_pins.h` |
| Control pins | DE 18, VSYNC 17, HSYNC 16, PCLK 21; init SPI CS 39 / SCK 48 / MOSI 47 (`Arduino_SWSPI`) | `include/board_pins.h` |
| Timing | PCLK 12 MHz; HSYNC pol 1 / fp 10 / pw 8 / bp 50; VSYNC pol 1 / fp 10 / pw 8 / bp 20 | `src/hw/panel.cpp` |
| **Bounce buffers** | 2 × 20 lines (`BOUNCE_PX = 480 × 20`, 38 KB internal SRAM); needed once Wi-Fi is on (§2.7), 20 lines with two servers (§2.9). **No cache writeback** after blits | `src/hw/panel.cpp`, `src/hw/screen.cpp` |
| **Init sequence** | Arduino_GFX `st7701_type9_init_operations` **+ `0x20` INVOFF**, spliced in before the colour-mode command and re-sent 50 ms after `begin()` | `src/hw/panel.cpp` |
| Rotation | 0 (native) | `src/hw/panel.cpp` |
| **Backlight** | GPIO38, LEDC **1 kHz**, 8-bit, attached after `panel::begin()` | `src/hw/backlight.cpp` |
| Touch | GT911 at I²C **0x5D**, SDA 19 / SCL 45, 400 kHz, polled ~30 Hz, product id "911", 480×480; raw coordinates match the display at rotation 0 with no transform | `src/hw/touch_gt911.cpp` |

Boot log of a healthy board:

```
system-d monitor — M1 bring-up
chip  ESP32-S3 rev 2, 2 cores, 240 MHz
flash 16 MB
psram 8192 KB total, 8188 KB free
panel ok 480x480, psram free after framebuffer 7737 KB
touch GT911 product="911" addr=0x5D max=480x480
ready — touch the screen
```

---

## 2. Problems found and their fixes

### 2.1 Screen completely black: backlight PWM at 20 kHz

- **Symptom:** the firmware boots and the serial log says `panel ok`, but
  the screen shows nothing at all.
- **Cause:** the LEDC channel on GPIO38 never starts at 20 kHz.
  `ledcAttach()` returns ok, but `ledcRead()` and `ledcReadFreq()` read
  back **0**, so the backlight stays off. At 1 kHz and 200 Hz the readbacks
  are correct and the screen lights. The vendor demo uses
  `digitalWrite(38, HIGH)`, which also works.
- **Fix:** `PWM_HZ = 1000` in `src/hw/backlight.cpp`.
- **Note:** switching back to `digitalWrite` after a failed 20 kHz attach
  gave mixed results. In round 3 it stayed dark (that round also did
  `gpio_reset_pin` + an unguarded `ledcDetach`); in round 4 it relit. The
  firmware attaches LEDC once at 1 kHz and never switches modes.

### 2.2 Colours inverted: the panel needs an explicit INVOFF

- **Symptom:** red shows as yellow, green as magenta, blue as cyan, white as
  black. That's every colour inverted, with red and blue apparently swapped
  as well.
- **Cause:** Arduino_GFX's type9 sequence never sends an inversion command,
  and this glass displays inverted unless it receives `0x20` (INVOFF).
  `0x21` (INVON, which type9 has commented out "by manufacture") made no
  visible difference, and nor did MADCTL `0x36 = 0x08` (BGR).
- **Fix:** add `0x20` (`src/hw/panel.cpp`): spliced into the type9
  sequence before `WRITE_C8_D8, 0x3A, 0x60`, and sent again 50 ms after
  `gfx->begin()`. If a future Arduino_GFX changes type9 so the anchor isn't
  found exactly once, the splice is skipped, a warning is printed, and the
  re-send still applies the fix.
- **Note:** the apparent red/blue swap was a side effect of the inversion,
  not a wiring problem. Red ↔ yellow and blue ↔ cyan look like a swap.
  Once INVOFF was in, the original pin order was correct.

### 2.3 R/B pin order: the guide's snippet was wrong

- The display snippet in `docs/board-guide.md` §3 passed R0–R4 =
  4,5,6,7,15 and B0–B4 = 11,12,13,14,0. Swapping the pins to that order
  (with INVOFF in) made **red show as blue and blue as red**. The correct
  order is R = 11–14,0 and B = 4–7,15, as in the guide's own pin table, the
  vendor demo and Arduino_GFX's `ESP32_4848S040_86BOX_GUITION` config.
  The guide's snippet has been corrected.

### 2.4 Serial corruption at 921600 baud

- **Symptom:** `esptool` fails with `Invalid head of packet (0x50):
  Possible serial noise or corruption`.
- **Fix:** `upload_speed = 460800`. Flashing at 460800 takes ~6 s for the
  ~450 KB app.

### 2.5 Build is unoptimised by default (`-O0`)

- **Symptom (M2):** everything ran ~4× slower than it should. Rasterising
  a 10×20 cell took ~37 µs (≈45 cycles per pixel), a full-page repaint took
  62 ms, and even `Grid::clear()` was slow.
- **Cause:** with `platformio/espressif32@8.0.0`, the Arduino build passes
  **no `-O` flag at all**. The core's `flags/cpp_flags` has none, so GCC
  defaults to `-O0`. Check with `pio run -v` and look for `-O` on a
  compile line.
- **Fix:** `-O2` in `build_flags` (`platformio.ini`). The full repaint went
  from 62 → 28 ms and a 1-cell update from 1.2 → 0.3 ms.
- **Related:** flash and PSRAM share one bus, and the LCD DMA keeps PSRAM
  busy, so code or data fetched from flash stalls on cache misses. The font
  tables live in internal DRAM and the raster code in IRAM (`FAST_DATA` /
  `FAST_CODE` in `src/gfx/platform.h`).

### 2.6 Smaller notes

- `-DCORE_DEBUG_LEVEL=…` in `build_flags` triggers a redefinition warning
  on every file. The platform already defines it, so don't add it.
- The GT911 answered at 0x5D. The driver also probes 0x14, the other
  address the chip can strap to when INT isn't driven at reset.
- A stray touch (`x=470 y=0`) was logged once at boot. It's harmless, but
  if it causes false taps later, drop the first reading after `begin()`.

### 2.7 Flicker every few seconds once Wi-Fi is on: bounce buffers

- **Symptom (M3):** with live polling over Wi-Fi, the screen flickered
  every few seconds, roughly once per 2 s poll. The M2 demo build (no
  Wi-Fi, a redraw every second) never flickered.
- **Cause:** without bounce buffers the LCD's DMA streams the frame
  straight from PSRAM. Wi-Fi traffic and the post-poll framebuffer writes
  (copy + cache writeback) compete for the same PSRAM/MSPI bandwidth, the
  DMA underruns, and the panel shows a glitch.
- **Fix:** pass `bounce_buffer_size_px = 480 × 10` to
  `Arduino_ESP32RGBPanel`. The DMA then reads two 10-line buffers in
  internal SRAM, which the CPU refills from PSRAM in an interrupt. That
  costs ~19 KB of internal RAM. The user confirmed the flicker is gone.

### 2.8 Screen goes black when Wi-Fi comes up (another session)

- **Report:** in an earlier, separate development session (not this
  codebase), the screen worked on its own but went black as soon as Wi-Fi
  was brought up. That code wasn't available, so the cause wasn't
  confirmed. These are the usual causes, most likely first, with how to
  tell them apart from the serial log:

| # | Cause | Serial log shows | How this firmware avoids it |
|---|---|---|---|
| 1 | **Blocking wait for the connection**: `WiFi.begin()` then `while (WiFi.status() != WL_CONNECTED) delay(…)` before the first frame or the backlight. If Wi-Fi never connects (wrong password, 5 GHz SSID, typo), the screen stays black forever. | stops after "connecting", no further output | `WiFi.begin()` is non-blocking, and all polling runs in the poller task (`src/net/poller.cpp`). The UI loop never waits on the network, and a missing link shows as `NO WIFI` |
| 2 | **Internal RAM exhausted.** Wi-Fi takes a large share of internal (DMA-capable) RAM. If it starts first, the display's bounce buffers, DMA descriptors or LVGL draw buffers can fail to allocate, `begin()` returns false, and nothing is drawn | a panel/LCD init failure, or `esp_lcd` allocation errors | `panel::begin()`, which allocates the bounce buffers, runs **before** `net::begin()` in `setup()`; failure prints `FATAL panel begin failed` |
| 3 | **Brownout reboot.** Wi-Fi transmit bursts add a few hundred mA on top of the panel (~260 mA). A weak USB port or thin cable sags, the brownout detector resets the board, and it loops before drawing | `Brownout detector was triggered`, repeated boot banners | not handled in code: use a good data cable and a port or supply that can deliver ≥ 500 mA |
| 4 | **PSRAM bandwidth.** Without bounce buffers the LCD DMA reads PSRAM directly, and Wi-Fi traffic starves it. Here that showed as flicker (§2.7); worse cases can make the panel lose sync and go dark | normal boot, polls running | bounce buffers (§2.7) |

- **Also rule out the backlight:** if that build drove GPIO38 with LEDC
  PWM at 20 kHz, the screen is black regardless of Wi-Fi (§2.1).
- **Init order this firmware uses** (`src/main.cpp`, `setup()`):
  1. `panel::begin()`: ST7701 init, RGB bus, framebuffer and bounce
     buffers;
  2. `net::begin()`: Wi-Fi starts, non-blocking, plus SNTP;
  3. `poller::begin()`: the polling task on core 0;
  4. first `redraw()`;
  5. `backlight::begin()` + `set()` (1 kHz).

### 2.9 Random glitches with two servers: image wraps, stray pixels

- **Symptom:** after adding the second server (dick6000, vLLM, 67 KB
  `/metrics` every 2 s), the image now and then wrapped vertically (top text
  showing at the bottom) and stray pixels flashed, at random. The serial log
  showed no resets or errors.
- **Cause:** with bounce buffers the CPU must refill each buffer from the
  PSRAM framebuffer before the LCD needs it. Two servers' Wi-Fi traffic,
  plus the PSRAM write bursts from each redraw, sometimes made a refill
  late. The panel then shows stale lines or slips by some rows.
  `CONFIG_LCD_RGB_RESTART_IN_VSYNC` is enabled in this core's sdkconfig, so
  the driver re-syncs at the next frame: glitches were brief, not
  permanent.
- **Fix** (confirmed clean by the user):
  1. **No `Cache_WriteBack_Addr` after blits.** With bounce buffers the
     driver reads the framebuffer through the CPU cache, so the writeback
     wasn't needed. Each one was an extra 19 KB PSRAM burst per dirty band.
  2. **Bounce buffers 10 → 20 lines** (2 × 19.2 KB): twice the slack per
     refill.
  3. **Redraw only when the server on screen updates** (per-server
     generation counter), not on every poll of any server.
- If it ever comes back: try 40-line buffers (must divide 480 lines), or
  lower PCLK (12 → 10 MHz, ~35 Hz refresh). The NVS writes on tap/switch
  briefly disable the flash cache, and the RGB ISR isn't IRAM-safe in this
  build (`CONFIG_LCD_RGB_ISR_IRAM_SAFE` off), so a glitch exactly on a tap
  would point there.

---

## 3. How it was found (diagnostic rounds)

Each round was a separate diagnostic build whose screen output the user
reported back. They're archived in
[`tools/display-diag/`](../tools/display-diag/README.md) and can be re-run
with `tools/display-diag/run.sh <round> upload`. The readings were sometimes ambiguous: in rounds 2 and 6 the
answer conflicted with later rounds. The table records what was concluded,
and §1 lists only what was verified at the end.

| Round | Test | Reported | Concluded |
|---|---|---|---|
| M1 v1 | Full test screen, LEDC 20 kHz | black | — |
| 1 | `digitalWrite` backlight blink; cycle init type9 / vendor HSD040BPN1 listing / type1 | blinks; all three show a picture, colours "inverted" | backlight hardware OK; the init variant isn't the problem |
| 2 | INVON with MADCTL 00/08 × software R/B swap; then LEDC 20 kHz | "B (INVON + BGR) correct", "PWM lit" | *later contradicted* |
| M1 v2 | INVON + BGR fixups after `begin()`, LEDC 20 kHz | black | — |
| 3 | Bisect M1 setup: digital → LEDC 20 kHz → pin reset + LEDC → digital + touch → LEDC | only step 1 visible | LEDC at 20 kHz kills the backlight |
| 4 | Digital / LEDC at 20 kHz, 1 kHz, 200 Hz, alternating | 3 (20 kHz) blank, rest lit; colours inverted | **20 kHz fails, 1 kHz works**; serial: 20 kHz duty/freq read back 0 |
| M1 v3 | INVON + BGR spliced into init, LEDC 1 kHz | lit, touch OK, colours inverted | backlight fixed |
| 5 | Re-send INVON + BGR at 0.1 → 8 s | never correct | INVON/BGR have no effect |
| 6 | INVOFF ± software R/B swap; control: INVON + ~swapRB | "B (INVOFF + swap) correct" | INVOFF works; swap reading *later contradicted* |
| M1 v4 | INVOFF + pins swapped to R = 4–7,15 | red ↔ blue swapped | pin swap wrong |
| **M1 v5** | **INVOFF + original pins R = 11–14,0** | **bars, ramps, border, touch all correct** | **final** |

The vendor's own init listing (`reference/…/1_2_4.0_LvglWidgets/ST7701S_HSD3.95IPS(HSD040BPN1)480x480_V1.0 -RGB 验证OK20230531.txt`)
also produces a picture with the same inversion, so it's a valid
alternative to type9 if ever needed.

---

## 4. If the screen breaks again

1. **Check the serial log first** (`pio device monitor`). The boot log
   (§1) should show `psram 8192 KB` and `panel ok`. `FATAL psram not found`
   means the `qio_opi` setting was lost. `panel begin failed` means the
   framebuffer allocation or RGB bus failed.
2. **Black screen with `panel ok`:** suspect the backlight. Test with
   `pinMode(38, OUTPUT); digitalWrite(38, HIGH);`, and check the LEDC
   frequency (§2.1).
3. **Wrong colours:** an inversion problem gives red → yellow,
   white → black. A pin problem gives red ↔ blue only, with white still
   white. Fix inversion with `0x20` (§2.2) and pins in `board_pins.h`
   (§2.3).
4. **Flicker that comes and goes with network activity:** check that
   bounce buffers are still enabled (§2.7).
   **Random wrap-around / stray pixels:** see §2.9 (bounce-buffer size, no
   writeback).
   **Black only once Wi-Fi is on:** go through §2.8.
5. **Shifted or cropped image / missing edge of the border:** tune the
   porch values in `src/hw/panel.cpp` (guide §3).
6. **Touch dead:** the boot log should show `GT911 … addr=0x5D`. INT/RST
   aren't wired, so a wedged GT911 needs a power cycle (unplug USB).
