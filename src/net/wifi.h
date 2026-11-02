#pragma once
#include <stddef.h>
#include <stdint.h>

#include "ui/view.h"

namespace net {

// Station mode with auto-reconnect, plus SNTP for the header clock.
// An empty SSID leaves Wi-Fi off.
void begin(const char* ssid, const char* pass, const char* tz);

bool up();

// Link state for the header: tells "still connecting after boot" apart from
// "can't join" and "lost". JOIN_GRACE_MS: how long the first connection may
// take before it counts as failed.
constexpr uint32_t JOIN_GRACE_MS = 20000;
WifiLink link();

// Header info: IP ("" when down) and signal bars 0–4 (-1 when down);
// RSSI ≥ −55/−67/−75/−85 dBm → 4/3/2/1 bars (spec.md §7.1).
void info(char* ip, size_t ip_len, int8_t* bars);

// Raw signal strength in dBm (0 when down).
int rssi();

// Date and 12-hour time, "11-02-26 2:47:33 PM" (MM-DD-YY, user request), or
// "" until SNTP has set the time.
void clock(char* out, size_t len);

}  // namespace net
