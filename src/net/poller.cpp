// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

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

constexpr uint32_t INFO_EVERY_MS = 30000;
constexpr uint32_t BACKOFF_MS[] = {2000, 4000, 9000};  // SPEC.md §8; capped at 9 s (user, 2026-11-08)
constexpr int BACKOFF_N = sizeof BACKOFF_MS / sizeof BACKOFF_MS[0];

struct Timing {
  std::atomic<uint32_t> poll_ms;
  uint32_t connect_ms, read_ms;
} g_timing;

// Collects a small JSON body. TensorFold's /health grew to 2.7 KB on the
// 2026-11 build (per-stream details), so 8 KB leaves headroom.
struct Buffer {
  char data[8192];
  size_t len = 0;
  bool truncated = false;
};
void toBuffer(const char* d, size_t n, void* ctx) {
  Buffer* b = (Buffer*)ctx;
  size_t room = sizeof b->data - 1 - b->len;
  if (n > room) {
    n = room;
    b->truncated = true;
  }
  memcpy(b->data + b->len, d, n);
  b->len += n;
}
void toParser(const char* d, size_t n, void* ctx) { ((PromParser*)ctx)->feed(d, n); }

// Everything for one server. Allocated once at start-up.
struct Slot {
  int index;
  ServerCfg cfg;
  char label[24];          // for the serial log
  SemaphoreHandle_t lock;
  Deriver deriver;         // guarded by lock
  uint32_t retry_at = 0;   // guarded by lock
  char ip[16] = "";        // guarded by lock: last resolved address
  ServerInfo info;         // task only
  Sample sample;           // task only
  Buffer json;             // task only
  View log_view;           // task only (serial value line)
  TaskHandle_t handle = nullptr;
  std::atomic<uint32_t> poll_after{0};  // millis(): first poll after a switch waits for this
};

Slot* g_slots[4];
int g_count = 0;
std::atomic<uint32_t> g_generation[4];
std::atomic<int> g_active{0};  // only this server is polled (the one on screen)
// On a switch the UI redraws the whole screen at once; the new server's
// first poll waits this long so the two PSRAM bursts don't overlap
// (bring-up §2.9; user request, 2026-11-08).
constexpr uint32_t SWITCH_POLL_DELAY_MS = 150;

// Sleeps up to ms; setActive() wakes it early.
void nap(uint32_t ms) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms)); }

bool fetchJson(Slot& s, const char* path, bool (*parse)(const char*, size_t, ServerInfo&)) {
  s.json.len = 0;
  s.json.truncated = false;
  http::Result r = http::get(s.cfg.host, s.cfg.port, path, toBuffer, &s.json, g_timing.connect_ms,
                             g_timing.read_ms);
  bool ok = r.err == http::Err::OK && parse(s.json.data, s.json.len, s.info);
  Serial.printf("[%s] %-11s %s, %u B, %u ms%s\n", s.label, path, ok ? "ok" : "failed",
                (unsigned)r.bytes, (unsigned)r.ms,
                s.json.truncated ? " (truncated: over the JSON buffer)" : "");
  return ok;
}

// One line of derived values per poll, for checking the screen against the
// server (and the Wi-Fi bars against the real signal).
void logValues(Slot& s) {
  View& v = s.log_view;
  xSemaphoreTake(s.lock, portMAX_DELAY);
  s.deriver.fill(v, millis(), g_timing.poll_ms, WifiLink::UP, 0);
  xSemaphoreGive(s.lock);
  Serial.printf("[%s]   tg %.1f (eng %.1f) pp last %.1f%s run %.0f wait %.0f kv %.1f%% "
                "ttft %.2fs mtp %.1f%% rssi %d dBm\n",
                s.label, v.tg_live, v.tg_eng, v.pp_last, isnan(v.prefilling_s) ? "" : " PREFILLING",
                v.running, v.waiting, v.kv_pct, v.ttft_cum, v.mtp_pct, net::rssi());
}

void task(void* arg) {
  Slot& s = *(Slot*)arg;
  std::atomic<uint32_t>& gen = g_generation[s.index];
  bool need_models = true, need_info = true;
  uint32_t last_info = 0;
  Engine engine = Engine::UNKNOWN;

  for (;;) {
    if (g_active.load() != s.index) {  // off screen: don't poll (setActive() wakes us)
      nap(1000);
      continue;
    }
    const int32_t hold = (int32_t)(s.poll_after.load() - millis());
    if (hold > 0) {  // just switched to: let the redraw finish first
      nap(hold);
      continue;
    }
    const uint32_t start = millis();
    if (!net::up()) {
      nap(500);
      gen++;
      continue;
    }

    // Engine facts: TensorFold has /health, vLLM has /version (its /health
    // is an empty 200). Until the engine is known, try both.
    if (need_info || start - last_info >= INFO_EVERY_MS) {
      bool ok = false;
      if (engine != Engine::VLLM) ok = fetchJson(s, "/health", parseHealth);
      if (!ok && engine != Engine::TENSORFOLD) ok = fetchJson(s, "/version", parseVersion);
      need_info = !ok;
      last_info = start;
    }
    if (need_models) need_models = !fetchJson(s, "/v1/models", parseModels);

    s.sample = Sample();
    PromParser parser(s.sample);
    http::Result r = http::get(s.cfg.host, s.cfg.port, "/metrics", toParser, &parser,
                               g_timing.connect_ms, g_timing.read_ms);
    parser.finish();
    s.sample.t_ms = millis();
    if (r.ip[0]) {
      xSemaphoreTake(s.lock, portMAX_DELAY);
      memcpy(s.ip, r.ip, sizeof s.ip);
      xSemaphoreGive(s.lock);
    }

    uint32_t wait = g_timing.poll_ms;
    if (r.err == http::Err::OK) {
      Identity id = identify(s.sample, r.server, s.info);
      if (id.engine != engine && id.engine != Engine::UNKNOWN) {
        engine = id.engine;  // re-read the engine's own info endpoint
        need_info = true;
      }
      xSemaphoreTake(s.lock, portMAX_DELAY);
      bool restart = s.deriver.onSample(s.sample, id, s.info);
      xSemaphoreGive(s.lock);
      if (restart) need_models = need_info = true;
      Serial.printf("[%s] /metrics    ok, %u B, %u ms, %s%s, %u lines (%u used)%s\n", s.label,
                    (unsigned)r.bytes, (unsigned)r.ms, engineName(id.engine),
                    id.confirmed ? "" : " (unconfirmed)", (unsigned)parser.lines(),
                    (unsigned)parser.recognised(), restart ? ", RESTART" : "");
      logValues(s);
    } else {
      char why[32];
      http::describe(r, s.cfg.host, why, sizeof why);
      xSemaphoreTake(s.lock, portMAX_DELAY);
      s.deriver.onFailure(s.sample.t_ms, why);
      int fails = s.deriver.consecutiveFailures();
      xSemaphoreGive(s.lock);
      if (fails >= 3) {
        int i = fails - 3;
        wait = BACKOFF_MS[i < BACKOFF_N ? i : BACKOFF_N - 1];
        need_models = need_info = true;  // re-read everything when it comes back
      }
      Serial.printf("[%s] /metrics    FAILED: %s (%u ms), retry in %u ms\n", s.label, why,
                    (unsigned)r.ms, (unsigned)wait);
    }
    xSemaphoreTake(s.lock, portMAX_DELAY);
    s.retry_at = start + wait;
    xSemaphoreGive(s.lock);
    gen++;

    const uint32_t elapsed = millis() - start;
    if (elapsed < wait) nap(wait - elapsed);
  }
}

}  // namespace

void begin(const ServerCfg* servers, int count, int active, uint32_t poll_ms, uint32_t connect_ms,
           uint32_t read_ms) {
  g_active = active;
  g_timing.poll_ms = poll_ms;
  g_timing.connect_ms = connect_ms;
  g_timing.read_ms = read_ms;
  g_count = count < 4 ? count : 4;
  for (int i = 0; i < g_count; i++) {
    Slot* s = new Slot();
    s->index = i;
    s->cfg = servers[i];
    if (s->cfg.name) {
      snprintf(s->label, sizeof s->label, "%s", s->cfg.name);
    } else {
      const char* dot = strchr(s->cfg.host, '.');
      snprintf(s->label, sizeof s->label, "%.*s", dot ? (int)(dot - s->cfg.host) : 23, s->cfg.host);
    }
    s->lock = xSemaphoreCreateMutex();
    g_slots[i] = s;
    char name[16];
    snprintf(name, sizeof name, "poll%d", i);
    xTaskCreatePinnedToCore(task, name, 10240, s, 1, &s->handle, 0);  // parser holds a 2 KB line
  }
}

int count() { return g_count; }

void setActive(int index) {
  if (index < 0 || index >= g_count || index == g_active.load()) return;
  Slot& s = *g_slots[index];
  // Before the caller redraws: keep the history, but show no rates that
  // span the pause, and "connecting…" until the first poll is in.
  xSemaphoreTake(s.lock, portMAX_DELAY);
  s.deriver.onResume();
  xSemaphoreGive(s.lock);
  s.poll_after = millis() + SWITCH_POLL_DELAY_MS;
  g_active = index;
  if (s.handle) xTaskNotifyGive(s.handle);  // polls after SWITCH_POLL_DELAY_MS
  Serial.printf("[%s] polling (others paused)\n", s.label);
}

void setPollMs(uint32_t ms) { g_timing.poll_ms = ms; }
uint32_t pollMs() { return g_timing.poll_ms; }

void snapshot(int index, View& v, WifiLink link) {
  if (index < 0 || index >= g_count) return;
  Slot& s = *g_slots[index];
  const uint32_t now = millis();
  xSemaphoreTake(s.lock, portMAX_DELAY);
  const uint32_t retry = (int32_t)(s.retry_at - now) > 0 ? s.retry_at - now : 0;
  s.deriver.fill(v, now, g_timing.poll_ms, link, retry);
  memcpy(v.server_ip, s.ip, sizeof v.server_ip);
  xSemaphoreGive(s.lock);
}

uint32_t generation(int index) {
  return (index >= 0 && index < g_count) ? g_generation[index].load() : 0;
}

}  // namespace poller
