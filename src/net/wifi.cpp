// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "wifi.h"
#include "app_config.h"
#include "serial_log.h"

#include <WiFi.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/sockets.h>
#include <sys/time.h>
#include <time.h>

namespace net {

namespace {
bool g_enabled = false;
bool g_ever_up = false;
uint32_t g_begin_ms = 0;

// Time: our own SNTP query instead of lwIP's background client, which sends
// whenever its timer fires. This one only runs when the poller calls
// syncTimeIfDue(), in the gap after a poll, so it is never on the air
// together with a metrics request (user request, 2026-11-15).
// Servers and interval: NTP_SERVERS / NTP_SYNC_H in config.h (app_config.h).
constexpr uint32_t NTP_SYNC_MS = NTP_SYNC_H * 60u * 60 * 1000;  // re-sync after a good answer
constexpr uint32_t NTP_RETRY_MS = 60u * 1000;           // after a failure; doubles per failure…
constexpr uint32_t NTP_RETRY_MAX_MS = 60u * 60 * 1000;  // …up to this
constexpr uint32_t NTP_REPLY_MS = 800;                  // wait for the answer

SemaphoreHandle_t g_sync_lock = nullptr;  // held while a query is in progress
bool g_sync_tried = false;                // guarded by g_sync_lock, as are the next three
uint32_t g_sync_at = 0;                   // millis() of the next query
uint8_t g_sync_fails = 0;
uint8_t g_sync_host = 0;

uint32_t be32(const uint8_t* p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// One SNTP request (RFC 4330) to host; sets the system time from the answer.
// Blocks for the DNS lookup plus up to NTP_REPLY_MS.
bool queryNtp(const char* host) {
  IPAddress ip;
  if (!WiFi.hostByName(host, ip) || ip.type() != IPv4) return false;
  const int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return false;
  struct timeval tmo = {0, (suseconds_t)(NTP_REPLY_MS * 1000)};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof tmo);
  struct sockaddr_in to = {};
  to.sin_family = AF_INET;
  to.sin_port = htons(123);
  to.sin_addr.s_addr = (uint32_t)ip;

  uint8_t out[48] = {0x23};  // no leap warning, version 4, mode 3 (client)
  const uint32_t nonce[2] = {esp_random(), esp_random()};
  memcpy(out + 40, nonce, sizeof nonce);  // transmit timestamp: the server echoes it back

  bool ok = false;
  const int64_t sent_us = esp_timer_get_time();
  if (connect(fd, (struct sockaddr*)&to, sizeof to) == 0 && send(fd, out, sizeof out, 0) == sizeof out) {
    uint8_t in[48];
    const int n = recv(fd, in, sizeof in, 0);
    const int64_t trip_us = esp_timer_get_time() - sent_us;
    const uint32_t secs = n == sizeof in ? be32(in + 40) : 0;  // server transmit time, since 1900
    // A server answer (mode 4) from a synchronised clock (stratum 1–15; 0 is
    // "go away") to this very request.
    if (secs && (in[0] & 7) == 4 && in[1] >= 1 && in[1] <= 15 && memcmp(in + 24, nonce, sizeof nonce) == 0) {
      // Top bit clear = after the 2036 rollover of the 32-bit seconds.
      const uint64_t unix_s = (uint64_t)secs + (secs & 0x80000000u ? 0 : 1ULL << 32) - 2208988800ULL;
      const uint64_t us = unix_s * 1000000 + (((uint64_t)be32(in + 44) * 1000000) >> 32) + trip_us / 2;
      struct timeval now = {(time_t)(us / 1000000), (suseconds_t)(us % 1000000)};
      ok = settimeofday(&now, nullptr) == 0;
    }
  }
  close(fd);
  return ok;
}
}

void begin(const char* ssid, const char* pass, const char* tz, const char* hostname) {
  if (!ssid || !ssid[0]) {
    LOGLN("wifi: no SSID configured (include/config.h), staying offline");
    return;
  }
  g_enabled = true;
  WiFi.setHostname(hostname);  // must come before mode(): that's where the core applies it
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // lower latency for 2 s polls; we're on USB power
  WiFi.begin(ssid, pass);
  g_begin_ms = millis();
  g_sync_lock = xSemaphoreCreateMutex();
  setenv("TZ", tz, 1);
  tzset();
  LOGF("wifi: connecting to \"%s\" as %s\n", ssid, hostname);
}

bool up() { return g_enabled && WiFi.status() == WL_CONNECTED; }

bool started() { return !g_enabled || WiFi.STA.started(); }

WifiLink link() {
  if (!g_enabled) return WifiLink::CANT_JOIN;
  if (up()) {
    g_ever_up = true;
    return WifiLink::UP;
  }
  if (g_ever_up) return WifiLink::LOST;
  return millis() - g_begin_ms < JOIN_GRACE_MS ? WifiLink::JOINING : WifiLink::CANT_JOIN;
}

void info(char* ip, size_t ip_len, int8_t* bars) {
  if (!up()) {
    ip[0] = '\0';
    *bars = -1;
    return;
  }
  snprintf(ip, ip_len, "%s", WiFi.localIP().toString().c_str());
  const int rssi = WiFi.RSSI();
  *bars = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -75 ? 2 : rssi >= -85 ? 1 : 0;
}

int rssi() { return up() ? WiFi.RSSI() : 0; }

void syncTimeIfDue() {
  if (!g_sync_lock || !up()) return;
  xSemaphoreTake(g_sync_lock, portMAX_DELAY);
  if (!g_sync_tried || (int32_t)(millis() - g_sync_at) >= 0) {
    const char* host = kNtpServers[g_sync_host];
    [[maybe_unused]] const uint32_t t0 = millis();  // only the log line uses it
    const bool ok = queryNtp(host);
    uint32_t wait = NTP_SYNC_MS;
    if (ok) {
      g_sync_fails = 0;
    } else {
      wait = NTP_RETRY_MS << g_sync_fails;
      if (wait >= NTP_RETRY_MAX_MS) wait = NTP_RETRY_MAX_MS; else g_sync_fails++;
      g_sync_host = (g_sync_host + 1) % kNtpServerCount;  // try the next one
    }
    g_sync_tried = true;
    g_sync_at = millis() + wait;  // wait < 2^31 ms: NTP_SYNC_H is capped at 168
    [[maybe_unused]] char now[24];
    if (SERIAL_LOG) clock(now, sizeof now);
    LOGF("ntp: %s %s, %u ms, next in %u s, clock %s\n", host, ok ? "ok" : "failed",
         (unsigned)(millis() - t0), (unsigned)(wait / 1000), now);
  }
  xSemaphoreGive(g_sync_lock);
}

void waitTimeSync() {
  if (!g_sync_lock) return;
  xSemaphoreTake(g_sync_lock, portMAX_DELAY);
  xSemaphoreGive(g_sync_lock);
}

void clock(char* out, size_t len) {
  out[0] = '\0';
  struct tm t;
  if (!g_enabled || !getLocalTime(&t, 0) || t.tm_year < 2024 - 1900) return;
  char date[12], time[16];
  strftime(date, sizeof date, "%m-%d-%y", &t);     // "11-02-26"
  strftime(time, sizeof time, "%I:%M:%S %p", &t);  // "02:47:33 PM"
  snprintf(out, len, "%s %s", date, time[0] == '0' ? time + 1 : time);  // drop the hour's leading zero
}

}  // namespace net
