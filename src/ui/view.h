#pragma once
#include <math.h>
#include <stdint.h>

// Everything the pages draw, already derived (spec.md §5). The poller fills
// one of these per poll; pages only read it. NaN = no data. Arduino-free.

constexpr int RING_LEN = 60;  // 120 s at 2 s per sample
constexpr int MAX_STREAMS = 8;

enum class ServerState : uint8_t { IDLE, BUSY, WARMING, RESTART, STALE, DOWN, NO_WIFI, UNKNOWN };

enum class StreamState : uint8_t { NO_DATA, IDLE, PREFILL, DECODING };

struct StreamView {
  uint8_t id = 0;
  StreamState state = StreamState::NO_DATA;
  float ctx_pct = NAN;
  float tokens = NAN;
  float ttft_s = NAN;
  float tg = NAN;  // tok/s
};

struct View {
  // Header (row 1)
  char clock[9] = "";     // HH:MM:SS, empty until NTP
  char server[24] = "";   // server name, e.g. "dgx042"
  char ip[16] = "";       // the monitor's own IP (serial log only)
  int8_t wifi_bars = -1;  // 0–4, -1 = no link

  // Server rule (row 2)
  char engine[12] = "";   // "TensorFold", "vLLM", "???"
  char model[48] = "";
  ServerState state = ServerState::UNKNOWN;

  // Throughput
  float tg_live = NAN, tg_avg = NAN, tg_eng = NAN;
  float pp_live = NAN, pp_avg = NAN, pp_eng = NAN;
  // Neither engine counts prompt tokens while prefilling (TensorFold: at
  // request end; vLLM: when the prefill completes), so a live prefill rate
  // isn't measurable (spec.md §5). In this mode the PP row shows the last
  // finished request's prefill speed and "prefilling Ns" while one runs, and
  // pp_hist holds per-request prefill speeds at completion.
  bool pp_per_request = false;
  float pp_last = NAN;        // last finished request: prompt ÷ prefill time
  float prefilling_s = NAN;   // seconds the current prefill has run, NaN = none
  float running = NAN, max_batch = NAN, waiting = NAN;

  // Cache. kv_capacity: tokens behind 100 % (vLLM: KV pool size; TensorFold:
  // the context window, as its KV ratio is per stream). ctx_window sizes the
  // per-stream token counts.
  float kv_pct = NAN, kv_tokens = NAN, kv_capacity = NAN, ctx_window = NAN;

  // Streams
  StreamView streams[MAX_STREAMS];
  uint8_t n_streams = 0;

  // Averages
  float ttft_cum = NAN, ttft_live = NAN, lat_cum = NAN, n_requests = NAN;
  float mtp_pct = NAN, mtp_accepted = NAN, mtp_drafted = NAN;
  float cache_hit_pct = NAN;

  // 120 s rings, oldest first; NaN = gap (missed poll / restart).
  float tg_hist[RING_LEN];
  float pp_hist[RING_LEN];
  float kv_hist[RING_LEN];

  // Footer rule (row 24): poll status or error reason, and which server of
  // how many is shown ("‹ 1/2 ›" when there's more than one).
  char status[40] = "";
  uint8_t server_index = 0;
  uint8_t server_count = 1;

  View() {
    for (int i = 0; i < RING_LEN; i++) tg_hist[i] = pp_hist[i] = kv_hist[i] = NAN;
  }
};
