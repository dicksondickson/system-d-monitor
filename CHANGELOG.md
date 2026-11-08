# Changelog

- v0.1.4 - 2026-11-08 - Start Wi-Fi before the display, so boot-time flash access can't leave the image shifted; poll only the server on screen; down servers retry every 9 s at most; server state and Wi-Fi bars in fixed colours (green OK, orange in progress / fair signal, red down / weak signal)
- v0.1.3 - 2026-11-08 - Fix the image sometimes staying shifted after boot (40-line bounce buffers); free memory in the serial log
- v0.1.2 - 2026-11-08 - Set the board's network hostname with `HOSTNAME`; read TensorFold's larger `/health`
- v0.1.1 - 2026-11-03 - Support for the later TensorFold build; PP counts only computed prompt tokens
- v0.1 - 2026-11-02 - Initial release
