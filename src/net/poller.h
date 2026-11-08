// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

#include "app_config.h"
#include "ui/view.h"

// One background task per server (core 0, SPEC.md §10 "Tasks"): /metrics
// every poll_ms; /health + /v1/models on connect, every 30 s and after a
// restart (vLLM: /version instead of /health); exponential backoff while a
// server is down. Only the active server (the one on screen) is polled; the
// others' tasks sleep and keep their history (Deriver::onResume()).
namespace poller {

void begin(const ServerCfg* servers, int count, int active, uint32_t poll_ms, uint32_t connect_ms,
           uint32_t read_ms);

int count();

// Poll only server `index` from now on (the one on screen); it polls at once.
void setActive(int index);

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
