#include "wifi.h"

#include <WiFi.h>
#include <esp_sntp.h>
#include <time.h>

namespace net {

namespace {
bool g_enabled = false;
bool g_ever_up = false;
uint32_t g_begin_ms = 0;
}

void begin(const char* ssid, const char* pass, const char* tz) {
  if (!ssid || !ssid[0]) {
    Serial.println("wifi: no SSID configured (include/config.h), staying offline");
    return;
  }
  g_enabled = true;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // lower latency for 2 s polls; we're on USB power
  WiFi.begin(ssid, pass);
  g_begin_ms = millis();
  configTzTime(tz, "pool.ntp.org", "time.cloudflare.com");
  Serial.printf("wifi: connecting to \"%s\"\n", ssid);
}

bool up() { return g_enabled && WiFi.status() == WL_CONNECTED; }

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

void clock(char* out, size_t len) {
  out[0] = '\0';
  struct tm t;
  if (!g_enabled || !getLocalTime(&t, 0) || t.tm_year < 2024 - 1900) return;
  char buf[16];
  strftime(buf, sizeof buf, "%I:%M:%S %p", &t);  // "02:47:33 PM"
  snprintf(out, len, "%s", buf[0] == '0' ? buf + 1 : buf);  // drop the leading zero
}

}  // namespace net
