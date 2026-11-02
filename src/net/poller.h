#pragma once
#include <stdint.h>

#include "ui/view.h"

// Background task (core 0) that polls the server (spec.md §10 "Tasks"):
// /metrics every POLL_MS, /health on connect + every 30 s, /v1/models on
// connect and after a restart; exponential backoff while it's down.
namespace poller {

void begin(const char* host, uint16_t port, uint32_t poll_ms, uint32_t connect_ms,
           uint32_t read_ms);

// Fills v (except the header fields, which the caller sets) from the latest
// data. Safe to call from the UI task.
void snapshot(View& v, bool wifi_up);

// Incremented after every poll attempt; the UI redraws when it changes.
uint32_t generation();

}  // namespace poller
