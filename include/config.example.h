#pragma once
// System-D Monitor configuration (spec.md §10).
//
// Copy this file to include/config.h and fill it in. config.h is gitignored,
// so your Wi-Fi password never ends up in the repo. Without a config.h the
// firmware still builds (from this file) and shows NO WIFI.

#define WIFI_SSID   ""          // 2.4 GHz network (the ESP32-S3 has no 5 GHz)
#define WIFI_PASS   ""
#define TZ_POSIX    "PST8PDT,M3.2.0,M11.1.0"   // header clock; POSIX TZ string

#define SERVER_HOST "dgx042.lan"  // bare "dgx042" won't resolve: no DNS search domain
#define SERVER_PORT 40000
// #define SERVER_NAME "dgx042"   // header label; default: SERVER_HOST up to the first dot

#define POLL_MS     2000          // /metrics every 2 s
#define CONNECT_MS  800
#define READ_MS     1500
#define BRIGHTNESS  200           // 0–255 backlight PWM
