# Changelog

**v0.1.6 - 2026-11-08**

- Release firmware is silent on serial; `SERIAL_LOG 1` in `config.h` turns the log back on for troubleshooting
- Fix three compiler warnings

**v0.1.5 - 2026-11-08**

- Serial `mem` line shows totals as well as free memory (internal RAM and PSRAM)
- Memory reference for this firmware in the bring-up notes
- How the two cores are used, documented in SPEC
- Updated screenshots and social preview
- SKILL.md: troubleshooting for DOWN, STALE and a stuck shifted image; optional hostname check

**v0.1.4 - 2026-11-08**

- Start Wi-Fi before the display, so boot-time flash access can't leave the image shifted
- Poll only the server on screen
- Down servers retry every 9 s at most
- Server state and Wi-Fi bars in fixed colours (green OK, orange in progress / fair signal, red down / weak signal), also in the Settings theme preview
- No glitch on server switches (page and server saved after 9 s, first poll 150 ms after the redraw)

**v0.1.3 - 2026-11-08**

- Fix the image sometimes staying shifted after boot (40-line bounce buffers)
- Free memory in the serial log

**v0.1.2 - 2026-11-08**

- Set the board's network hostname with `HOSTNAME`
- Read TensorFold's larger `/health`

**v0.1.1 - 2026-11-03**

- Support for the later TensorFold build
- PP counts only computed prompt tokens

**v0.1 - 2026-11-02**

- Initial release
