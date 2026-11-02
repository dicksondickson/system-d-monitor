#include "poller.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>

#include "http_get.h"
#include "metrics/derive.h"
#include "metrics/prom.h"
#include "metrics/server_info.h"
#include "wifi.h"

namespace poller {

namespace {

constexpr uint32_t HEALTH_EVERY_MS = 30000;
constexpr uint32_t BACKOFF_MS[] = {2000, 4000, 8000, 16000, 30000};  // spec.md §8

struct Config {
  const char* host;
  uint16_t port;
  uint32_t poll_ms, connect_ms, read_ms;
} g_cfg;

SemaphoreHandle_t g_lock;
Deriver g_deriver;              // guarded by g_lock
ServerInfo g_info;              // poller task only
uint32_t g_retry_at = 0;        // guarded by g_lock
std::atomic<uint32_t> g_generation{0};

// Collects a small JSON body.
struct Buffer {
  char data[2048];
  size_t len = 0;
};
void toBuffer(const char* d, size_t n, void* ctx) {
  Buffer* b = (Buffer*)ctx;
  size_t room = sizeof b->data - 1 - b->len;
  if (n > room) n = room;
  memcpy(b->data + b->len, d, n);
  b->len += n;
}
void toParser(const char* d, size_t n, void* ctx) { ((PromParser*)ctx)->feed(d, n); }

bool fetchJson(const char* path, bool (*parse)(const char*, size_t, ServerInfo&)) {
  static Buffer buf;
  buf.len = 0;
  http::Result r = http::get(g_cfg.host, g_cfg.port, path, toBuffer, &buf, g_cfg.connect_ms,
                             g_cfg.read_ms);
  bool ok = r.err == http::Err::OK && parse(buf.data, buf.len, g_info);
  Serial.printf("poll %-11s %s, %u B, %u ms\n", path, ok ? "ok" : "failed", (unsigned)r.bytes,
                (unsigned)r.ms);
  return ok;
}

// One line of derived values per poll, for checking the screen against
// the server (and the Wi-Fi bars against the real signal).
void logValues() {
  static View v;
  xSemaphoreTake(g_lock, portMAX_DELAY);
  g_deriver.fill(v, millis(), g_cfg.poll_ms, true, 0);
  xSemaphoreGive(g_lock);
  Serial.printf("  tg %.1f (eng %.1f) pp %.1f (eng %.1f) run %.0f wait %.0f kv %.1f%% "
                "ttft %.2fs mtp %.1f%% rssi %d dBm\n",
                v.tg_live, v.tg_eng, v.pp_live, v.pp_eng, v.running, v.waiting, v.kv_pct,
                v.ttft_cum, v.mtp_pct, net::rssi());
}

void task(void*) {
  static Sample sample;
  bool need_models = true;
  uint32_t last_health = 0;
  bool had_health = false;

  for (;;) {
    const uint32_t start = millis();
    if (!net::up()) {
      vTaskDelay(pdMS_TO_TICKS(500));
      g_generation++;
      continue;
    }

    if (!had_health || start - last_health >= HEALTH_EVERY_MS) {
      had_health = fetchJson("/health", parseHealth) || had_health;
      last_health = start;
    }
    if (need_models) need_models = !fetchJson("/v1/models", parseModels);

    sample = Sample();
    PromParser parser(sample);
    http::Result r = http::get(g_cfg.host, g_cfg.port, "/metrics", toParser, &parser,
                               g_cfg.connect_ms, g_cfg.read_ms);
    parser.finish();
    sample.t_ms = millis();

    uint32_t wait = g_cfg.poll_ms;
    if (r.err == http::Err::OK) {
      Identity id = identify(sample, r.server, g_info);
      xSemaphoreTake(g_lock, portMAX_DELAY);
      bool restart = g_deriver.onSample(sample, id, g_info);
      xSemaphoreGive(g_lock);
      if (restart) {
        need_models = true;
        had_health = false;
      }
      Serial.printf("poll /metrics    ok, %u B, %u ms, %s%s, %u lines (%u used)%s\n",
                    (unsigned)r.bytes, (unsigned)r.ms, engineName(id.engine),
                    id.confirmed ? "" : " (unconfirmed)", (unsigned)parser.lines(),
                    (unsigned)parser.recognised(), restart ? ", RESTART" : "");
      logValues();
    } else {
      char why[32];
      http::describe(r, g_cfg.host, why, sizeof why);
      xSemaphoreTake(g_lock, portMAX_DELAY);
      g_deriver.onFailure(sample.t_ms, why);
      int fails = g_deriver.consecutiveFailures();
      xSemaphoreGive(g_lock);
      if (fails >= 3) {
        int i = fails - 3;
        wait = BACKOFF_MS[i < 5 ? i : 4];
        need_models = true;  // re-read everything when it comes back
        had_health = false;
      }
      Serial.printf("poll /metrics    FAILED: %s (%u ms), retry in %u ms\n", why, (unsigned)r.ms,
                    (unsigned)wait);
    }
    xSemaphoreTake(g_lock, portMAX_DELAY);
    g_retry_at = start + wait;
    xSemaphoreGive(g_lock);
    g_generation++;

    const uint32_t elapsed = millis() - start;
    if (elapsed < wait) vTaskDelay(pdMS_TO_TICKS(wait - elapsed));
  }
}

}  // namespace

void begin(const char* host, uint16_t port, uint32_t poll_ms, uint32_t connect_ms,
           uint32_t read_ms) {
  g_cfg = {host, port, poll_ms, connect_ms, read_ms};
  g_lock = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(task, "poller", 8192, nullptr, 1, nullptr, 0);
}

void snapshot(View& v, bool wifi_up) {
  const uint32_t now = millis();
  xSemaphoreTake(g_lock, portMAX_DELAY);
  const uint32_t retry = (int32_t)(g_retry_at - now) > 0 ? g_retry_at - now : 0;
  g_deriver.fill(v, now, g_cfg.poll_ms, wifi_up, retry);
  xSemaphoreGive(g_lock);
}

uint32_t generation() { return g_generation.load(); }

}  // namespace poller
