// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "ui/view.h"

namespace net {

// Station mode with auto-reconnect; sets the timezone for the header clock.
// hostname is the name sent to the router over DHCP. An empty SSID leaves
// Wi-Fi off.
void begin(const char* ssid, const char* pass, const char* tz, const char* hostname);

bool up();

// True once the Wi-Fi driver has started (radio calibrated, config saved),
// or when Wi-Fi is off. Joining the network comes later.
bool started();

// Link state for the header: tells "still connecting after boot" apart from
// "can't join" and "lost". JOIN_GRACE_MS: how long the first connection may
// take before it counts as failed.
constexpr uint32_t JOIN_GRACE_MS = 20000;
WifiLink link();

// Header info: IP ("" when down) and signal bars 0–4 (-1 when down);
// RSSI ≥ −55/−67/−75/−85 dBm → 4/3/2/1 bars (SPEC.md §7.1).
void info(char* ip, size_t ip_len, int8_t* bars);

// Raw signal strength in dBm (0 when down).
int rssi();

// Time sync, driven by the poller so it never overlaps a metrics request.
// syncTimeIfDue(): call between two polls. Does nothing unless a sync is due
// (right after boot, then every NTP_SYNC_H hours; after a failure 1 min,
// doubling up to 1 h, moving on to the next of NTP_SERVERS); then it sends
// one SNTP query and blocks until the answer (DNS lookup
// + up to 0.8 s). waitTimeSync(): call before a poll; blocks while another
// task's query is still in progress.
void syncTimeIfDue();
void waitTimeSync();

// Date and 12-hour time, "11-02-26 2:47:33 PM" (MM-DD-YY, user request), or
// "" until the first time sync.
void clock(char* out, size_t len);

}  // namespace net
