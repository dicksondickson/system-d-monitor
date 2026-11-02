#include "derive.h"

#include <stdio.h>
#include <string.h>

namespace {

// Generated tokens including in-flight ones. TensorFold's
// generation_tokens_total only counts finished requests, so add the live
// streams' gauge (spec.md §5, open question 1).
float genLive(const Sample& s, Engine e) {
  if (e == Engine::TENSORFOLD) {
    float running = s.has(F_GEN_RUNNING) ? s.get(F_GEN_RUNNING) : 0.0f;
    return s.get(F_GEN_TOKENS) + running;
  }
  return s.get(F_GEN_TOKENS);
}

// Δa/Δb, or NaN when b didn't advance (or either is missing).
float ratio(float da, float db) { return (db > 0 && da >= 0) ? da / db : NAN; }

float kvPct(const Sample& s) {
  if (s.has(F_KV_POOL)) return s.get(F_KV_POOL) * 100.0f;
  float m = NAN;
  for (const StreamSample& st : s.streams)
    if (st.present && !isnan(st.kv) && (isnan(m) || st.kv > m)) m = st.kv;
  return isnan(m) ? NAN : m * 100.0f;
}

// Tokens of finished requests, to pair with the prefill/decode time sums
// (which also only move when a request finishes). vLLM's prompt/generation
// totals move as the engine works, so use its per-request histogram sums.
float reqPrompt(const Sample& s) {
  return s.has(F_REQ_PROMPT) ? s.get(F_REQ_PROMPT) : s.get(F_PROMPT_TOKENS);
}
float reqGen(const Sample& s) { return s.has(F_REQ_GEN) ? s.get(F_REQ_GEN) : s.get(F_GEN_TOKENS); }

bool wentDown(const Sample& cur, const Sample& prev, Field f) {
  return cur.has(f) && prev.has(f) && cur.get(f) < prev.get(f);
}

StreamState toState(StreamPhase p) {
  switch (p) {
    case StreamPhase::IDLE: return StreamState::IDLE;
    case StreamPhase::PREFILLING: return StreamState::PREFILL;
    case StreamPhase::DECODING: return StreamState::DECODING;
    default: return StreamState::NO_DATA;
  }
}

}  // namespace

const Sample& Deriver::at(int i) const {
  return win_[(head_ - n_ + i + WINDOW) % WINDOW];
}

void Deriver::pushRings(float tg, float pp, float kv) {
  if (!rings_init_) {
    for (int i = 0; i < RING_LEN; i++) tg_ring_[i] = pp_ring_[i] = kv_ring_[i] = NAN;
    rings_init_ = true;
  }
  tg_ring_[ring_head_] = tg;
  pp_ring_[ring_head_] = pp;
  kv_ring_[ring_head_] = kv;
  ring_head_ = (ring_head_ + 1) % RING_LEN;
}

bool Deriver::onSample(const Sample& s, const Identity& id, const ServerInfo& info) {
  id_ = id;
  info_ = info;
  ever_ok_ = true;
  last_ok_ms_ = s.t_ms;
  failures_ = 0;
  reason_[0] = '\0';

  bool restart = false;
  if (n_ > 0) {
    const Sample& prev = at(n_ - 1);
    restart = wentDown(s, prev, F_PROMPT_TOKENS) || wentDown(s, prev, F_GEN_TOKENS) ||
              wentDown(s, prev, F_TTFT_COUNT) || wentDown(s, prev, F_E2E_COUNT);
  }
  if (restart) {  // new baseline: rates restart from scratch, gap in the rings
    n_ = 0;
    restarted_ = true;
    restart_ms_ = s.t_ms;
  }

  win_[head_] = s;
  head_ = (head_ + 1) % WINDOW;
  if (n_ < WINDOW) n_++;

  float tg = NAN, pp = NAN;
  const bool per_request = id.engine != Engine::UNKNOWN;
  if (n_ >= 2) {
    const Sample& prev = at(n_ - 2);
    const float dt = (s.t_ms - prev.t_ms) / 1000.0f;
    if (dt > 0) {
      tg = ratio(genLive(s, id.engine) - genLive(prev, id.engine), dt);
      pp = ratio(s.get(F_PROMPT_TOKENS) - prev.get(F_PROMPT_TOKENS), dt);
    }
    const float pp_eng = ratio(reqPrompt(s) - reqPrompt(prev),
                               s.get(F_PREFILL_S) - prev.get(F_PREFILL_S));
    if (!isnan(pp_eng)) last_pp_eng_ = pp_eng;
    if (per_request) pp = isnan(pp_eng) ? (isnan(pp) ? NAN : 0.0f) : pp_eng;
  }
  // Prefilling: a request is running, nothing has been generated since the
  // last poll, and its KV cache is in use (spec.md §5).
  const bool prefilling = per_request && s.get(F_RUNNING) > 0 && tg == 0 && kvPct(s) > 0;
  if (prefilling && !prefilling_) prefill_since_ms_ = s.t_ms;
  prefilling_ = prefilling;
  pushRings(tg, pp, kvPct(s));
  return restart;
}

void Deriver::onFailure(uint32_t t_ms, const char* reason) {
  (void)t_ms;
  failures_++;
  strncpy(reason_, reason ? reason : "error", sizeof reason_ - 1);
  reason_[sizeof reason_ - 1] = '\0';
  pushRings(NAN, NAN, NAN);
}

void Deriver::fill(View& out, uint32_t now_ms, uint32_t poll_ms, bool wifi_ok,
                   uint32_t retry_ms) const {
  // Start from a fresh View so nothing from the previous poll lingers; the
  // header belongs to the caller.
  View v;
  memcpy(v.clock, out.clock, sizeof v.clock);
  memcpy(v.server, out.server, sizeof v.server);
  memcpy(v.ip, out.ip, sizeof v.ip);
  v.wifi_bars = out.wifi_bars;
  v.server_index = out.server_index;
  v.server_count = out.server_count;
  float slot_tg[SAMPLE_STREAMS];
  for (float& x : slot_tg) x = NAN;

  // Identity and model
  snprintf(v.engine, sizeof v.engine, "%s", engineName(id_.engine));
  const char* model = info_.model[0] ? info_.model : (n_ ? at(n_ - 1).model_name : "");
  snprintf(v.model, sizeof v.model, "%s", model);

  // Rings, oldest first
  if (rings_init_) {
    for (int i = 0; i < RING_LEN; i++) {
      int k = (ring_head_ + i) % RING_LEN;
      v.tg_hist[i] = tg_ring_[k];
      v.pp_hist[i] = pp_ring_[k];
      v.kv_hist[i] = kv_ring_[k];
    }
  }

  if (n_ > 0) {
    const Sample& cur = at(n_ - 1);
    const Engine e = id_.engine;

    if (n_ >= 2) {
      const Sample& prev = at(n_ - 2);
      const float dt = (cur.t_ms - prev.t_ms) / 1000.0f;
      if (dt > 0) {
        v.tg_live = ratio(genLive(cur, e) - genLive(prev, e), dt);
        v.pp_live = ratio(cur.get(F_PROMPT_TOKENS) - prev.get(F_PROMPT_TOKENS), dt);
      }
      v.tg_eng = ratio(reqGen(cur) - reqGen(prev), cur.get(F_DECODE_S) - prev.get(F_DECODE_S));
      v.pp_eng = ratio(reqPrompt(cur) - reqPrompt(prev),
                       cur.get(F_PREFILL_S) - prev.get(F_PREFILL_S));
      v.ttft_live = ratio(cur.get(F_TTFT_SUM) - prev.get(F_TTFT_SUM),
                          cur.get(F_TTFT_COUNT) - prev.get(F_TTFT_COUNT));

      const Sample& first = at(0);
      const float span = (cur.t_ms - first.t_ms) / 1000.0f;
      if (span > 0) {
        v.tg_avg = ratio(genLive(cur, e) - genLive(first, e), span);
        v.pp_avg = ratio(cur.get(F_PROMPT_TOKENS) - first.get(F_PROMPT_TOKENS), span);
      }

      // Per-stream TG while the same request keeps the slot.
      for (int i = 0; i < SAMPLE_STREAMS; i++) {
        const StreamSample &a = cur.streams[i], &b = prev.streams[i];
        if (a.phase == StreamPhase::DECODING && b.present && a.start_s == b.start_s && dt > 0)
          slot_tg[i] = ratio(a.gen_tokens - b.gen_tokens, dt);
      }
    }

    v.pp_per_request = e != Engine::UNKNOWN;
    v.pp_last = last_pp_eng_;
    if (prefilling_) v.prefilling_s = (now_ms - prefill_since_ms_) / 1000.0f;

    v.running = cur.get(F_RUNNING);
    v.waiting = cur.get(F_WAITING);
    v.max_batch = info_.max_batch;

    v.ctx_window = info_.context_length;
    v.kv_pct = kvPct(cur);
    v.kv_capacity = cur.has(F_KV_POOL_TOKENS) ? cur.get(F_KV_POOL_TOKENS) : v.ctx_window;
    v.kv_tokens = v.kv_pct / 100.0f * v.kv_capacity;

    // Streams: one row per slot the server reports, in slot order.
    StreamView rows[MAX_STREAMS];
    int n = 0;
    for (int i = 0; i < SAMPLE_STREAMS && n < MAX_STREAMS; i++) {
      const StreamSample& st = cur.streams[i];
      if (!st.present) continue;
      StreamView r;
      r.id = (uint8_t)i;
      r.state = toState(st.phase);
      r.ctx_pct = isnan(st.kv) ? NAN : st.kv * 100.0f;
      r.tokens = st.kv * v.ctx_window;
      r.ttft_s = st.ttft_s;
      r.tg = slot_tg[i];
      rows[n++] = r;
    }
    for (int i = 0; i < MAX_STREAMS; i++) v.streams[i] = i < n ? rows[i] : StreamView{};
    v.n_streams = (uint8_t)n;

    const float ttft_n = cur.get(F_TTFT_COUNT), e2e_n = cur.get(F_E2E_COUNT);
    v.ttft_cum = ttft_n > 0 ? cur.get(F_TTFT_SUM) / ttft_n : NAN;
    v.lat_cum = e2e_n > 0 ? cur.get(F_E2E_SUM) / e2e_n : NAN;
    v.n_requests = ttft_n;
    v.mtp_accepted = cur.get(F_ACCEPTED_TOKENS);
    v.mtp_drafted = cur.get(F_DRAFT_TOKENS);
    v.mtp_pct = v.mtp_drafted > 0 ? v.mtp_accepted / v.mtp_drafted * 100.0f : NAN;
    const float prompt = cur.get(F_PROMPT_TOKENS);
    v.cache_hit_pct = (cur.has(F_CACHED_TOKENS) && prompt > 0)
                          ? cur.get(F_CACHED_TOKENS) / prompt * 100.0f : NAN;
  }

  // State and footer (spec.md §8)
  const float ago = (now_ms - last_ok_ms_) / 1000.0f;
  const unsigned retry_s = (retry_ms + 999) / 1000;
  const char* dot = " \xC2\xB7 ";
  if (!wifi_ok) {
    v.state = ServerState::NO_WIFI;
    snprintf(v.status, sizeof v.status, "wifi lost%sretry", dot);
  } else if (!ever_ok_ && failures_ < 3) {
    v.state = ServerState::UNKNOWN;
    snprintf(v.status, sizeof v.status, "connecting\xE2\x80\xA6");
  } else if (failures_ >= 3) {
    v.prefilling_s = NAN;
    v.state = ServerState::DOWN;
    snprintf(v.status, sizeof v.status, "%.20s%sretry %us", reason_, dot, retry_s);
  } else if (id_.engine == Engine::UNKNOWN) {
    v.state = ServerState::UNKNOWN;
    snprintf(v.status, sizeof v.status, "not TensorFold or vLLM");
  } else if (now_ms - last_ok_ms_ > 3 * poll_ms) {
    v.state = ServerState::STALE;
    char reason[sizeof reason_ + 4] = "";
    if (reason_[0]) snprintf(reason, sizeof reason, "%s%s", dot, reason_);
    snprintf(v.status, sizeof v.status, "stale %.0fs%.*s", ago, (int)(sizeof v.status - 12), reason);
  } else if (restarted_ && now_ms - restart_ms_ < RESTART_SHOW_MS) {
    v.state = ServerState::RESTART;
    snprintf(v.status, sizeof v.status, "restarted %us ago", (unsigned)((now_ms - restart_ms_) / 1000));
  } else {
    const bool busy = v.running > 0 || v.waiting > 0;
    v.state = info_.warming ? ServerState::WARMING : busy ? ServerState::BUSY : ServerState::IDLE;
    snprintf(v.status, sizeof v.status, "poll %us%sok %.1fs ago", (unsigned)(poll_ms / 1000), dot, ago);
  }
  out = v;
}
