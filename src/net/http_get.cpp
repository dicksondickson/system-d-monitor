#include "http_get.h"

#include <WiFi.h>
#include <strings.h>

namespace http {

namespace {

constexpr uint32_t MAX_TOTAL_MS = 5000;  // hard cap per request

// Reads one header line (without CRLF) into buf. Returns false on timeout,
// disconnect or an over-long line.
bool readLine(WiFiClient& c, char* buf, size_t len, uint32_t deadline) {
  size_t n = 0;
  while ((int32_t)(deadline - millis()) > 0) {
    int ch = c.read();
    if (ch < 0) {
      if (!c.connected()) return false;
      delay(1);
      continue;
    }
    if (ch == '\n') {
      if (n && buf[n - 1] == '\r') n--;
      buf[n] = '\0';
      return true;
    }
    if (n + 1 >= len) return false;
    buf[n++] = (char)ch;
  }
  return false;
}

}  // namespace

Result get(const char* host, uint16_t port, const char* path, Sink sink, void* ctx,
           uint32_t connect_ms, uint32_t read_ms) {
  Result r;
  const uint32_t t0 = millis();
  IPAddress ip;
  if (!WiFi.hostByName(host, ip)) {
    r.err = Err::DNS;
    r.ms = millis() - t0;
    return r;
  }
  snprintf(r.ip, sizeof r.ip, "%s", ip.toString().c_str());
  WiFiClient c;
  if (!c.connect(ip, port, connect_ms)) {
    r.err = Err::CONNECT;
    r.ms = millis() - t0;
    return r;
  }
  c.setNoDelay(true);
  c.printf("GET %s HTTP/1.0\r\nHost: %s:%u\r\nUser-Agent: system-d-monitor\r\n"
           "Accept: */*\r\nConnection: close\r\n\r\n",
           path, host, (unsigned)port);

  const uint32_t hard = t0 + MAX_TOTAL_MS;
  char line[256];
  // Status line: "HTTP/1.x 200 OK"
  if (!readLine(c, line, sizeof line, min(hard, millis() + read_ms))) {
    r.err = c.connected() ? Err::TIMEOUT : Err::BAD_RESPONSE;
    r.ms = millis() - t0;
    return r;
  }
  if (strncmp(line, "HTTP/", 5) != 0 || !strchr(line, ' ')) {
    r.err = Err::BAD_RESPONSE;
    r.ms = millis() - t0;
    return r;
  }
  r.status = atoi(strchr(line, ' ') + 1);
  // Headers until the blank line
  long content_length = -1;
  for (;;) {
    if (!readLine(c, line, sizeof line, min(hard, millis() + read_ms))) {
      r.err = Err::TIMEOUT;
      r.ms = millis() - t0;
      return r;
    }
    if (!line[0]) break;
    if (strncasecmp(line, "Server:", 7) == 0) {
      const char* v = line + 7;
      while (*v == ' ') v++;
      snprintf(r.server, sizeof r.server, "%.*s", (int)sizeof r.server - 1, v);
    } else if (strncasecmp(line, "Content-Length:", 15) == 0) {
      content_length = atol(line + 15);
    }
  }
  if (r.status != 200) {
    r.err = Err::STATUS;
    r.ms = millis() - t0;
    return r;
  }
  // Body, streamed
  char buf[512];
  uint32_t idle_deadline = millis() + read_ms;
  while ((int32_t)(hard - millis()) > 0) {
    if (content_length >= 0 && (long)r.bytes >= content_length) break;
    int avail = c.available();
    if (avail > 0) {
      int n = c.read((uint8_t*)buf, min((size_t)avail, sizeof buf));
      if (n > 0) {
        sink(buf, (size_t)n, ctx);
        r.bytes += n;
        idle_deadline = millis() + read_ms;
      }
      continue;
    }
    if (!c.connected()) break;  // server closed: body complete (HTTP/1.0)
    if ((int32_t)(idle_deadline - millis()) <= 0) {
      r.err = Err::TIMEOUT;
      break;
    }
    delay(1);
  }
  if (r.err == Err::OK && (int32_t)(hard - millis()) <= 0) r.err = Err::TIMEOUT;
  c.stop();
  r.ms = millis() - t0;
  return r;
}

void describe(const Result& r, const char* host, char* out, size_t len) {
  switch (r.err) {
    case Err::OK: snprintf(out, len, "ok"); break;
    case Err::DNS: snprintf(out, len, "dns %s", host); break;
    case Err::CONNECT: snprintf(out, len, "refused"); break;
    case Err::TIMEOUT: snprintf(out, len, "timeout"); break;
    case Err::STATUS: snprintf(out, len, "http %d", r.status); break;
    case Err::BAD_RESPONSE: snprintf(out, len, "bad response"); break;
  }
}

}  // namespace http
