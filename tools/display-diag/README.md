# Display diagnostics (archived)

The throwaway firmware builds used to bring up the panel on 2026-11-01. Each
one put a test pattern on screen, and the user reported what they saw. The
conclusions are in [`docs/hardware-bringup.md`](../../docs/hardware-bringup.md)
§3. This folder keeps the exact code so any round can be re-run.

## Running a round

```sh
tools/display-diag/run.sh 4            # build round 4
tools/display-diag/run.sh 4 upload     # build + flash (default port /dev/cu.usbserial-110)
tools/display-diag/run.sh 4 upload /dev/cu.usbserial-XXXX
```

The script copies the round into `src/diag/` (gitignored), builds
`env:diag` (`platformio.ini`), and deletes `src/diag/` again. Flash the
normal firmware afterwards with `pio run -t upload`.

## Rounds

| File | Tests | Result (2026-11-01) |
|---|---|---|
| `round1_init_sequences.cpp` | backlight blink via `digitalWrite`; init sequences type9 / vendor HSD040BPN1 / type1 | backlight OK; all three sequences give a picture; colours inverted |
| `round2_invon_madctl.cpp` | INVON with MADCTL 00/08 × software R/B swap; LEDC 20 kHz | reported "B correct, PWM lit", later contradicted |
| `round3_bisect_setup.cpp` | M1's setup steps one by one (LEDC modes, touch init) | only step 1 visible → LEDC 20 kHz kills the backlight |
| `round4_backlight_pwm.cpp` | LEDC at 20 kHz / 1 kHz / 200 Hz between `digitalWrite` steps | 20 kHz dark (duty/freq read back 0); 1 kHz and 200 Hz fine |
| `round5_fixup_timing.cpp` | re-send INVON + MADCTL=BGR at 0.1 → 8 s | never correct → those commands do nothing visible |
| `round6_invoff.cpp` | INVOFF ± software R/B swap; INVON + software-fix control | INVOFF confirmed as the fix (the swap reading was wrong) |

## Conditions then vs now

Each round calls `panel::begin()`, which has changed since. Re-running a
round today runs on top of the **final** fixes, so rounds that were
inverted at the time will now show correct colours.

| Round | `panel::begin()` at the time | Backlight driver at the time |
|---|---|---|
| 1–2 | type9 only, no fixups | round's own `digitalWrite` / LEDC 20 kHz |
| 3–4 | type9 + INVON + MADCTL=BGR sent right after `begin()` | round's own code |
| 5–6 | INVON + MADCTL=BGR spliced into type9 + re-sent 50 ms later | `backlight.cpp` at 1 kHz |
| now | **INVOFF** spliced into type9 + re-sent 50 ms later | `backlight.cpp` at 1 kHz |

All rounds ran with the final RGB pin order (R = 11–14,0, B = 4–7,15).

## Other files

| File | What |
|---|---|
| `vendor_st7701s_hsd040bpn1.txt` | The vendor's ST7701S init listing for the HSD040BPN1 glass, copied from `reference/…/1_2_4.0_LvglWidgets/` (GB18030 text) |
| `vendor_init_to_ops.py` | Converts that listing to an Arduino_GFX `batchOperation` array |
| `st7701_hsd040bpn1.h` | Its output, used by round 1. Regenerate with `vendor_init_to_ops.py vendor_st7701s_hsd040bpn1.txt st7701_hsd040bpn1_init_operations > st7701_hsd040bpn1.h` |
| `run.sh` | Build/flash helper above |

## Note on how this archive was made

These builds were deleted from `src/diag/` after bring-up, then restored
from the session that wrote them. The code is unchanged except for the
header comments: each now records its result, and round 1 includes
`st7701_hsd040bpn1.h` from its own folder rather than from `src/hw/`.
