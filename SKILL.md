---
name: system-d-monitor
description: Configure, build and flash the SYSTEM-D Monitor firmware onto a Guition ESP32-S3-4848S040 display board, then check it works. Use when someone asks to set up, compile, flash, configure or troubleshoot SYSTEM-D Monitor (the ESP32 dashboard for vLLM and TensorFold inference servers).
---

# Set up SYSTEM-D Monitor

SYSTEM-D Monitor is PlatformIO firmware for the **Guition ESP32-S3-4848S040**
(4″ 480×480 touchscreen, ESP32-S3 N16R8, CH340 USB-serial). It polls vLLM or
TensorFold `/metrics` over Wi-Fi and shows the numbers on the screen. The
whole job is: install PlatformIO, write `include/config.h`, build, flash,
and check the screen.

## Ground rules

- **Never commit, print or paste `include/config.h`.** It holds the user's
  Wi-Fi password and is gitignored. Don't echo the password back to the user.
- **Don't change the board settings** in `platformio.ini`, `include/board_pins.h`
  or `src/hw/`. Every value there (pins, panel timings, 1 kHz backlight PWM,
  460800 upload speed, bounce buffers, `-O2`) fixes a real problem on this
  board; `docs/hardware-bringup.md` explains each one.
- Ask the user before anything outside this repo beyond installing
  PlatformIO (drivers, group changes, system packages).

## 1. Ask the user for

| Setting | Notes |
|---|---|
| Wi-Fi name and password | must be a **2.4 GHz** network; the ESP32-S3 has no 5 GHz radio |
| Servers, 1 to 4 | each one: host, port, optional display label. vLLM's default port is 8000 |
| Time zone | only if you can't read it from their computer (step 3) |

Optional, with defaults: refresh `POLL_MS` (2000), auto-rotate between
servers `ROTATE_S` (0 = off), first-boot `BRIGHTNESS_PCT` (100), network
name `HOSTNAME` (`system-d-monitor`; 1–31 letters, digits or hyphens, no
hyphen at either end, or the build fails). Refresh,
brightness and theme can also be changed later on the board's Settings screen.

## 2. Check the servers from this computer

The board needs to reach each server's `/metrics` over plain HTTP, with no
auth:

```sh
curl -s --max-time 5 http://HOST:PORT/metrics | grep -m3 -E '^(vllm|tensorfold):'
```

- Lines starting `vllm:` or `tensorfold:` → good.
- Nothing, or connection refused → wrong port, server not running, or it
  only listens on localhost (its `--host` needs to be `0.0.0.0`).
- **Host names:** the ESP32 has no DNS search domain, so a bare `gpu-box`
  won't resolve on the board even if it does here. Use a name the router's
  DNS knows (`gpu-box.lan`) or the IP address.

## 3. Write the config

```sh
cp include/config.example.h include/config.h
git check-ignore include/config.h   # must print the path: it's ignored
```

Edit only these lines in `include/config.h`:

```cpp
#define WIFI_SSID   "network-name"
#define WIFI_PASS   "password"
#define TZ_POSIX    "PST8PDT,M3.2.0,M11.1.0"

#define SERVERS {                                                  \
    {nullptr, "gpu-box.lan", 8000},    /* label = host up to the first dot */ \
    {"dgx",   "192.168.1.60", 40000},  /* custom label */          \
}
```

- `SERVERS` is `{label, host, port}`, 1–4 entries, with a `\` at the end of
  every line inside the macro.
- `TZ_POSIX` is a POSIX TZ string, not an IANA name. On macOS or Linux,
  `tail -n1 /etc/localtime` prints the computer's own (e.g. `MST7`,
  `GMT0BST,M3.5.0/1,M10.5.0`). For another zone, take the last line of
  `/usr/share/zoneinfo/<Area>/<City>`, or look it up in
  https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv.

## 4. Install PlatformIO Core

Check first: `pio --version`. If it's missing, install it with whichever the
user already has:

```sh
pipx install platformio
uv tool install platformio
pip install platformio
```

If the install or the first build fails on a very new Python, use 3.12
(`uv tool install --python 3.12 platformio`). PlatformIO downloads the
ESP32 toolchain on the first build (several hundred MB, a few minutes).

USB drivers: macOS and Linux have the CH340 driver built in. Windows may
need it (https://www.wch-ic.com/downloads/CH341SER_EXE.html). On Linux the
user must be in the `dialout` group (ask before changing groups).

## 5. Build

```sh
pio run -e board
```

It must end with `[SUCCESS]`. Optionally, the host unit tests (need a C++17
compiler, no board):

```sh
pio test -e native     # test_render, test_metrics and test_fmt must all PASS
```

## 6. Flash

Have the user plug the board in with a USB-C **data** cable, then find the
port and flash:

```sh
pio device list
pio run -e board -t upload --upload-port PORT
```

Port names: macOS `/dev/cu.usbserial-*`, Linux `/dev/ttyUSB0`, Windows
`COM3` etc. No port at all → a charge-only cable, or the Windows driver.
"Port is busy" → close any serial monitor that has it open. "Invalid head of
packet" → USB noise: retry, or try another cable or USB port. Don't raise
`upload_speed`.

## 7. Check it works

Ask the user what the screen shows. A working board:

1. Header shows `CONNECTING` for a few seconds after boot.
2. Then the date and time (`MM-DD-YY h:mm:ss AM`), Wi-Fi bars at the top
   right, the server name · IP, and `OK IDLE` or `OK BUSY`.
3. The second line shows the engine (`vLLM` or `TensorFold`) and the model.

Touch: left / right third = previous / next server, centre = overview ↔
graphs, bottom-left corner `≡` = Settings, hold about 3 s = screen off.

If you need the serial log (115200 baud): `pio device monitor` only runs in
an interactive terminal, so ask the user to run it themselves (in Claude
Code: `! pio device monitor`), and to quit it (Ctrl-C) before you flash
again, because it holds the port.

## Troubleshooting

| On the screen | Fix |
|---|---|
| `NO WIFI` · `check SSID / password` | wrong SSID or password, or a 5 GHz-only network. Fix `config.h`, flash again |
| `NO WIFI` · `wifi lost · retry` | it was connected and dropped: signal or router; it reconnects by itself |
| `dns …` in the footer | the host name doesn't resolve on the ESP32: use `name.lan` or the IP |
| `refused` / `timeout` | wrong port, firewall, or the server listens on localhost only |
| `UNKNOWN · not TensorFold or vLLM` | that port answers, but not with vLLM or TensorFold metrics |
| `no include/config.h` | the build didn't find `include/config.h`: check its path and name |
| black screen, image wrong, flicker | a board difference: read `docs/hardware-bringup.md` before changing anything |
| `no data` in some fields | normal: that server doesn't export that metric, or nothing has happened yet |

## More detail

- `README.md`: hardware, setup and reading the screen, for people
- `SPEC.md`: how every number is derived, the screen layouts, all states
- `docs/hardware-bringup.md`: this board's quirks and their fixes
- `docs/board-guide.md`: general ESP32-4848S040 reference (pinout, variants)
