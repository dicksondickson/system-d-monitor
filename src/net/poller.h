#pragma once
#include <stdint.h>

#include "app_config.h"
#include "ui/view.h"

// One background task per server (core 0, spec.md §10 "Tasks"): /metrics
// every poll_ms; /health + /v1/models on connect, every 30 s and after a
// restart (vLLM: /version instead of /health); exponential backoff while a
// server is down. Servers poll independently, so a dead one never delays
// another.
namespace poller {

void begin(const ServerCfg* servers, int count, uint32_t poll_ms, uint32_t connect_ms,
           uint32_t read_ms);

int count();

// Change the poll interval at runtime (Settings); takes effect on each
// task's next cycle.
void setPollMs(uint32_t ms);
uint32_t pollMs();

// Fills v (except the header fields, which the caller sets) from server
// `index`'s latest data. Safe to call from the UI task.
void snapshot(int index, View& v, WifiLink link);

// Incremented after every poll attempt of server `index` (and while Wi-Fi
// is down); redraw when the shown server's generation changes.
uint32_t generation(int index);

}  // namespace poller
