# ESP32-4848S040 (Guition / JCZN) — Complete Reference Guide

> **Board:** ESP32-S3 Arduino LVGL WiFi & Bluetooth Development Board, 4.0" 480×480,
> Capacitive Touch, **no bottom shell** (relay-less version)
> **Module:** ESP32-S3-WROOM-1 **N16R8** (16 MB Flash / 8 MB PSRAM)

---

## 1. Hardware Specifications

| Component | Detail |
|---|---|
| MCU | ESP32-S3-WROOM-1 (N16R8): dual-core @ 240 MHz, 512 KB SRAM, 384 KB ROM |
| Wireless | Wi-Fi 2.4 GHz + **Bluetooth LE only** (S3 has no Bluetooth Classic) |
| Flash | 16 MB (Quad SPI) |
| PSRAM | 8 MB (**Octal SPI** — must be configured as OPI or firmware finds nothing) |
| Display | 4.0" IPS TFT, 480×480, 16-bit RGB565 (65K colors) |
| Display driver | ST7701 (config over 3-wire SPI, pixel data over 16-bit RGB parallel bus) |
| Touch | Goodix GT911 capacitive, I²C addr 0x5D, 5-point multitouch |
| Backlight | PWM-controllable, GPIO38 |
| Storage | MicroSD/TF card slot (SPI mode) |
| Audio | NS4168 I²S DAC + ~2.5 W class-D amp, speaker connector |
| Battery | LiPo connector via IP5306 power-management chip (see gotchas) |
| USB | USB-C through **CH340** USB-serial chip (not native USB) |
| Power | 5 V, ~260 mA typical (vendor); **210 mA (≈ 1 W) measured** running System-D Monitor on Wi-Fi |
| Active area | 71.8 × 70.2 mm; module ~86.5 × 86.5 × 37.8 mm (with back shell) |
| Operating temp | −20 … 70 °C (storage −30 … 80 °C) |

**Your variant ("without bottom shell")** has no relays and no mains PSU — just the
front display board with an exposed 8-pin connector (P1/H1). Power strictly via
USB-C. Safer to hack on, and the relay GPIOs (40, 2, 1) are free for other uses.

**Vendor resources**
- Datasheet PDF: https://www.guition.com/icms/upload/fb081940d6fc11f09850077a33e1404f/FTPData/UEditor/file/2026121/1768961092477/ESP32-4848S040%20Specifications-EN.pdf
- Demo/examples package: `http://pan.jczn1688.com/pd/1/ESP32 module/4.0inch_ESP32-4848S040.zip`
- Ships with a factory demo preloaded — plug in USB and confirm the UI + touch work
  **before** flashing anything (sanity check).

---

## 2. Complete GPIO Map

### RGB data bus (pixel data)
| Signal | GPIOs |
|---|---|
| R0–R4 (R = DB13,14,15,16,17) | 11, 12, 13, 14, 0 |
| G0–G5 (G = DB6,7,8,9,10,11) | 8, 20, 3, 46, 9, 10 |
| B0–B4 (B = DB1,2,3,4,5) | 4, 5, 6, 7, 15 |

### Control signals
| Signal | GPIO |
|---|---|
| DE (data enable) | 18 |
| VSYNC | 17 |
| HSYNC | 16 |
| PCLK | 21 |
| LCD_CS (3-wire init SPI) | 39 |
| SPI SCK (display init + SD) | 48 |
| SPI MOSI/SDA (display init + SD) | 47 |

### Touch (I²C, GT911 @ 0x5D)
| Signal | GPIO |
|---|---|
| TP_SDA | 19 |
| TP_SCL | 45 (strapping pin — use `ignore_strapping_warning` in ESPHome) |
| INT / RST | **not wired** → GT911 is polled; software reset via I²C |

### Backlight
| Signal | GPIO |
|---|---|
| BL (PWM) | 38 |

### SD card (SPI mode)
| SD pin | Function | GPIO |
|---|---|---|
| 2 | CS | 42 |
| 3 | MOSI | 47 (shared with display init SPI) |
| 5 | SCK | 48 (shared) |
| 7 | MISO | 41 |

### Audio (I²S)
| Signal | GPIO |
|---|---|
| BCLK / L3 | 1 |
| LRCLK / L2 | 2 |
| DIN / L1 | 40 |

### Misc
| Item | GPIO / note |
|---|---|
| UART0 TX / RX | 43 / 44 → CH340 → USB-C |
| GPIO0 | BOOT strapping pin, also DB17(R) |
| GPIO3 | JTAG/strapping pin, also DB8(G) |
| GPIO35/36/37 | present on header — check your board revision |
| SW1 | hidden button beside USB-C (wired to IP5306) |
| RST/BOOT buttons | hidden inside case; CH340 RTS/DTR handle them for flashing |

**⚠️ Strapping pins:** GPIO0 and GPIO3 double as RGB data lines. Safe — they're
outputs only and not driven until after boot — but don't plan other uses for them.

---

## 3. Display Fundamentals (read before coding)

- The ST7701 panel has **no GRAM** — the ESP32-S3 streams every frame over the
  RGB parallel bus continuously.
- The framebuffer (~450 KB = 480×480×2) **must live in PSRAM** → OPI PSRAM is
  mandatory, or the sketch dies at init.
- ST7701 needs its **exact init sequence** ("type 9" in Arduino_GFX terms) over
  the 3-wire SPI before the RGB bus starts. Wrong init = black screen. This board
  is literally the reason the type-9 sequence exists in Arduino_GFX.
- Working timing values: PCLK 12–14 MHz, HSYNC pw 8 / fp 10 / bp 20–50,
  VSYNC pw 8 / fp 10 / bp 10–20. If you see horizontal shift or tearing, tweak
  the back-porch values.

### Reference display init (Arduino_GFX)

> Corrected 2026-11-01 after testing on this board (`docs/hardware-bringup.md`).
> The earlier version had R and B swapped. It also needs an explicit `0x20`
> INVOFF after the type9 sequence, or colours come out inverted.

```cpp
// 9-bit mode SPI for ST7701 config
bus = new Arduino_ESP32SPI(
    GFX_NOT_DEFINED /* DC */, 39 /* CS */, 48 /* SCK */, 47 /* MOSI */,
    GFX_NOT_DEFINED /* MISO */);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0 /* R4 */,
    8 /* G0 */, 20 /* G1 */, 3 /* G2 */, 46 /* G3 */, 9 /* G4 */, 10 /* G5 */,
    4 /* B0 */, 5 /* B1 */, 6 /* B2 */, 7 /* B3 */, 15 /* B4 */,
    1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */,
    50 /* hsync_back_porch */,
    1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */,
    20 /* vsync_back_porch */);

gfx = new Arduino_RGB_Display(
    480, 480, rgbpanel, 0 /* rotation */, true /* auto_flush */,
    bus, GFX_NOT_DEFINED /* RST */,
    st7701_type9_init_operations, sizeof(st7701_type9_init_operations));
// then send INVOFF (0x20) on command page 0 — see src/hw/panel.cpp
```

### LVGL framebuffer pattern (the universal trick)
```cpp
// Entire frame buffer in PSRAM
frame_buffer = (lv_color_t *)heap_caps_malloc(
    sizeof(lv_color_t) * 480 * 480, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
if (frame_buffer == NULL) { /* PSRAM not enabled → fix build config */ }
```

---

## 4. Development Paths

### A. Arduino IDE
Board: **ESP32S3 Dev Module**, with:
- PSRAM: **OPI PSRAM** ← the #1 critical setting
- Flash Mode: QIO 80 MHz
- Flash Size: 16MB (128Mbit)
- Partition Scheme: Huge App or 16MB custom
- USB CDC On Boot: **Disabled** (USB goes through CH340)
- Upload Mode: UART0 / Hardware CDC
- Upload Speed: 921600
- Core Debug Level: None

Libraries: **GFX Library for Arduino** (moononournation/Arduino_GFX), **LVGL**
(8.4 or 9.x — see gotchas), **TAMC_GT911** for touch.
Requires ESP32 Arduino core 3.x (2.x won't find `lv_conf.h` in the sketch folder).

### B. PlatformIO (recommended)
Key `platformio.ini` settings (from working skeletons):
```ini
[env:Guition_ESP32_4848S040]
platform = espressif32
framework = arduino
board = esp32-s3-devkitm-1
board_build.arduino.memory_type = qio_opi
board_build.flash_mode = qio
board_build.psram_type = opi
board_upload.flash_size = 16MB
board_upload.maximum_size = 16777216
monitor_speed = 115200
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=0   ; CH340 on UART, not native USB (was -U, a typo)
    -DBOARD_HAS_PSRAM
    -DLV_CONF_INCLUDE_SIMPLE
lib_deps =
    https://github.com/moononournation/Arduino_GFX
    lvgl/lvgl@^8.4.0              ; or ^9.x with SPIRAM_MALLOC_ALWAYSINTERNAL=0
    https://github.com/tamctec/gt911-arduino
```

**Starter repos (copy one of these):**
| Repo | What it gives you |
|---|---|
| `NorthernMan54/ESP32-4848S040` | Full skeleton: platformio.ini, GT911 lib bundled, huge_app.csv, pin docs |
| `ha5dzs/Guition-ESP32-4848S040-platformio` | Skeleton + a written log of every pitfall he hit (SD, SPI, LVGL 8 vs 9) |
| `aquaElectronics/esp32-4848s040-st7701` | Verified ST7701 timings + init for Arduino_GFX |
| `sand1812/ESP32-4848S040` | LVGL 9.2 example |
| `abcbbck-png/ESP32-4848S040-First-Start` | **ESP-IDF** project with official `esp_lcd_st7701` + `esp_lcd_touch_gt911` components, LVGL 9, PWM backlight, UART console for live timing tuning |

### C. ESPHome (if you use Home Assistant — most popular route)
Full working config on devices.esphome.io (search "Guition ESP32-S3-4848S040").
Key elements: `st7701s` display platform, `gt911` touchscreen, LEDC backlight on
GPIO38, PSRAM octal 80 MHz, ESP-IDF framework.

**Production-ready firmwares to copy:**
- `PrayerfulDrop/ESPHome-Guition-ESP32-S3-4848S040` — massive feature set: weather +
  5-day forecast, calendar, HVAC arc dial, lights/fans/covers (6 slots each), media
  player, vacuum, alarm panel w/ PIN, flip-clock screensaver, 6 themes, 9 languages,
  notification banners, remote screenshot endpoint (`/screenshot`)
- `albert-canfield/HA-panel-esphome` — wall-switch replacement (relay versions),
  thermostat, RGBW light control, auto-dimming backlight
- `cruftbox/tessera` — standalone PlatformIO/LVGL 8.4 HA wall controller, WebSocket
  API, setup wizard, multi-panel manager, 3×3 tile grid

ESPHome notes: enable `ignore_strapping_warning` on GPIO45; dim backlight during
OTA (`on_begin`) to avoid screen corruption; rotation 270 for USB-down mounting.

### D. LCD Wizard (no-code GUI designer)
lcdwizard.com has a dedicated preset **"Guition ESP32-4848S040"** and generates a
complete Arduino/PlatformIO LVGL project from a drag-and-drop designer.

---

## 5. Known Gotchas (learned the hard way by others)

1. **OPI PSRAM not set** → framebuffer alloc fails / boot loop / garbage screen.
   First thing to set, in any toolchain.
2. **LVGL version**: 8.4 "just works." LVGL 9 had garbage-screen issues when the
   whole framebuffer was pushed to PSRAM — fix: set
   `SPIRAM_MALLOC_ALWAYSINTERNAL=0` (or low). Others run LVGL 9.2/9.3 fine.
3. **Charge-only USB cable** = board never detected. Use a data cable.
4. **No native USB**: USB-C is CH340 serial only — no USB-CDC, no JTAG over USB.
   Serial monitor at 115200.
5. **SD card sharing SPI** with the display's MOSI-only config bus is finicky.
   Working approach: software SPI for the display, hardware SPI for SD
   (SCK=48, MOSI=47, MISO=41, CS=42). Format cards FAT32 (≤32 GB recommended).
6. **No RTC** — time via NTP over Wi-Fi (or an SD config file). No battery-backed clock.
7. **IP5306 battery chip** auto-shuts off after ~32 s if load < ~45 mA — fine for
   an always-on display, fatal for deep-sleep projects.
8. **Audio vs relays conflict** (relay versions): I²S shares GPIO1/2 with relays
   2/3, and enabling the amp path needs resistor rework (R21–R27). Your relay-less
   unit may already be routed — check the resistors near the NS4168 chip.
9. **Screen rotation** is a pain point on RGB panels. USB-down = rotation 270 +
   touch `mirror_x: true` + `swap_xy: true`; may introduce lag in some configs.
10. **RGB panel artifacts** (occasional flicker/ghosting when widgets overlap):
    inherent to streaming an RGB panel with partial LVGL buffers; mitigations
    involve full-frame direct render mode.
11. **GT911 INT/RST not wired** → touch is polled, reset done in software; a
    wedged GT911 may need a power cycle.
12. **Home Assistant IP-bans** panels that retry with a bad token — validate
    tokens from the PC before flashing.

*Found on this board during bring-up (2026-11-01, details in `docs/hardware-bringup.md`):*

13. **Backlight PWM at 20 kHz never starts** (LEDC duty/frequency read back
    0, screen stays black). Use 1 kHz (200 Hz also works) or plain
    `digitalWrite(38, HIGH)`.
14. **Colours inverted** (red → yellow, white → black) with Arduino_GFX's
    type9 sequence: send `0x20` INVOFF. `0x21` and MADCTL BGR do nothing
    visible.
15. **921600 baud uploads corrupt** over the CH340 ("Invalid head of
    packet"); use 460800.

---

## 6. Pin Header Reference (vendor table, condensed)

| Pin | GPIO | Notes | Connected to |
|---|---|---|---|
| IO0 | 0 | BOOT | DB17 (R4) |
| IO1 | 1 | | I2S-BCLK / L3 |
| IO2 | 2 | | I2S-LRCLK / L2 |
| IO3 | 3 | JTAG | DB8 (G2) |
| IO4–IO7 | 4–7 | | DB1–DB4 (B0–B3) |
| IO8 | 8 | | DB6 (G0) |
| IO9 | 9 | | DB10 (G4) |
| IO10 | 10 | SPICS0 | DB11 (G5) |
| IO11 | 11 | SPID | DB13 (R0) |
| IO12 | 12 | SPICLK | DB14 (R1) |
| IO13 | 13 | SPIQ | DB15 (R2) |
| IO14 | 14 | | DB16 (R3) |
| IO15 | 15 | | DB5 (B4) |
| IO16 | 16 | | HSYNC |
| IO17 | 17 | U1TXD | VSYNC |
| IO18 | 18 | U1RXD | DE |
| IO19 | 19 | USB_D− | TP_SDA |
| IO20 | 20 | USB_D+ | DB7 (G1) |
| IO21 | 21 | | PCLK |
| IO35–IO37 | 35–37 | | free (verify on board) |
| IO38 | 38 | | BL_C (backlight) |
| IO39 | 39 | | LCD_CS |
| IO40 | 40 | | I2S-DIN / L1 |
| IO41 | 41 | | TF(D1) — SD MISO |
| IO42 | 42 | | TF(D3) — SD CS |
| IO43 | 43 | U0TXD | TX |
| IO44 | 44 | U0RXD | RX |
| IO45 | 45 | VSPI | TP_SCL |
| IO46 | 46 | log | DB9 (G3) |
| IO47 | 47 | SPICLK_P | TF(SDA) / display MOSI |
| IO48 | 48 | SPICLK_N | TF(SCK) / display SCK |

---

## 7. Project Ideas (things people actually built)

- **Home Assistant wall panel / thermostat** — the dominant use case; relay models
  replace wall switches outright. 900+ post community thread on the HA forum:
  "GUITION 4\" 480x480 ESP32-S3-4848S040 Smart Display with LVGL"
- **Weather station / smart clock** — NTP time, flip-clock screensaver, forecast
- **Marine NMEA2000 multi-function display** — `HugoJunkers/Marine-MFD-ESP32-4848S040`;
  hardware mod: remove PSU/relays, add 12V→5V DC-DC + CAN transceiver on GPIO1/2,
  autopilot + instrument pages over the boat's NMEA bus
- **HomeDing IoT wall panel** — homeding.github.io board page w/ `env.json` config
- **WiFi setup UI + SD-card config**, photo frame from SD, kitchen timer,
  Bluetooth LE gadget panel, dash gauge, custom HMI for any project
- **3D-printed desk stands**: MakerWorld "Guition ESP32-S3 4848S040 Desk Stand";
  Thingiverse thing:7373058

---

## 8. Suggested Getting-Started Path

1. **Verify hardware**: plug in USB (data cable!), confirm factory demo + touch work.
2. **Pick a stack**:
   - Home Assistant user → ESPHome (PrayerfulDrop or devices.esphome.io config)
   - Want to learn embedded GUI → PlatformIO + Arduino_GFX + LVGL, start from
     NorthernMan54 or ha5dzs skeleton
   - Want GUI design tool → LCD Wizard preset
3. **Set OPI PSRAM + 16 MB flash + QIO before anything else.**
4. Flash a color-squares + touch-test sketch; confirm touch coordinates match.
5. Tune RGB timing only if you see shift/tearing.
6. Build your UI on top (or configure widgets/entities if on ESPHome).

---

## 9. Key Links Index

| Resource | URL |
|---|---|
| Vendor datasheet (EN, PDF) | guition.com — ESP32-4848S040 Specifications-EN.pdf |
| Vendor demo package | pan.jczn1688.com/pd/1/ESP32 module/4.0inch_ESP32-4848S040.zip |
| ESPHome device page | devices.esphome.io/devices/guition-esp32-s3-4848s040/ |
| HA community thread | community.home-assistant.io/t/729271 |
| HomeDing board page | homeding.github.io/boards/esp32s3/panel-4848S040.htm |
| Skeleton (recommended) | github.com/NorthernMan54/ESP32-4848S040 |
| Skeleton + pitfall log | github.com/ha5dzs/Guition-ESP32-4848S040-platformio |
| Verified ST7701 timings | github.com/aquaElectronics/esp32-4848s040-st7701 |
| ESP-IDF starter | github.com/abcbbck-png/ESP32-4848S040-First-Start |
| LVGL 9 example | github.com/sand1812/ESP32-4848S040 |
| Full ESPHome dashboard | github.com/PrayerfulDrop/ESPHome-Guition-ESP32-S3-4848S040 |
| HA wall panel (relay) | github.com/albert-canfield/HA-panel-esphome |
| HA wall controller (PIO) | github.com/cruftbox/tessera |
| Marine MFD | github.com/HugoJunkers/Marine-MFD-ESP32-4848S040 |
| GUI designer preset | lcdwizard.com |
| Arduino_GFX | github.com/moononournation/Arduino_GFX |
| GT911 touch lib | github.com/tamctec/gt911-arduino |
