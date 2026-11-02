# System-D Monitor

A desk dashboard for TensorFold inference metrics, running on a Guition
ESP32-S3-4848S040 (4″ 480×480 touch panel). The design is in
[`spec.md`](spec.md).

**Status:** M1 done (2026-11-01). Panel, backlight and touch work. The
firmware currently shows the M1 test screen.

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
```

On a healthy boot the log shows `psram 8192 KB`, `panel ok` and
`touch GT911 … addr=0x5D`.

## Layout

| Path | What |
|---|---|
| `spec.md` | Full design: data sources, screens, architecture, milestones |
| `docs/hardware-bringup.md` | Verified board config + the problems found bringing it up (read this if the screen misbehaves) |
| `esp32-4848s040-guide.md` | General reference for the board (corrected where bring-up proved it wrong) |
| `include/board_pins.h` | Pin map |
| `src/hw/` | Panel (ST7701 + RGB bus), backlight (LEDC 1 kHz), GT911 touch |
| `src/main.cpp` | M1 test screen: colour bars, ramps, border, touch dots |
| `tools/display-diag/` | Archived display diagnostic builds from bring-up; `run.sh <round> upload` re-runs one |
| `test/fixtures/` | Captured responses from the TensorFold server, for parser tests |
| `reference/` | Vendor download pack (datasheets, demos, tools). **Not in git** (295 MB, re-downloadable; link in the guide §1) |

## Board gotchas (details in `docs/hardware-bringup.md`)

- Backlight PWM must be **1 kHz**. At 20 kHz the screen stays black.
- The panel needs an explicit **`0x20` INVOFF**, or colours are inverted.
- R/B pins: R = 11,12,13,14,0 and B = 4,5,6,7,15.
- Upload at **460800**. 921600 corrupts over the CH340.
