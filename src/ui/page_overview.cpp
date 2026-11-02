// Overview page, spec.md §7.1.
#include <math.h>
#include <stdio.h>

#include <initializer_list>
#include <string.h>

#include "fmt.h"
#include "pages.h"
#include "widgets.h"

namespace pages {

using namespace widgets;

namespace {

constexpr const char* SEP = " \xC2\xB7 ";  // " · "

uint8_t mtpRole(float pct) {
  if (isnan(pct)) return theme::DIM;
  return pct >= 70 ? theme::GOOD : pct >= 50 ? theme::WARN : theme::BAD;
}

uint8_t valueRole(float v, uint8_t role) { return isnan(v) ? (uint8_t)theme::DIM : role; }

bool allMissing(std::initializer_list<float> vs) {
  for (float v : vs)
    if (!isnan(v)) return false;
  return true;
}

// spec.md §5: when every value in a row is missing, one "no data" replaces
// them all (after an optional leading label). Returns true if it did.
bool noDataRow(Grid& g, int row, std::initializer_list<float> vs, const char* label = nullptr,
               uint8_t label_role = theme::LABEL, uint8_t label_attr = ATTR_NONE) {
  if (!allMissing(vs)) return false;
  Writer w(g, row, CONTENT_COL);
  if (label) w.put(label, label_role, label_attr);
  w.put(fmt::NO_DATA, theme::DIM);
  return true;
}

// Rows 4–5: "TG <hero> tok/s <2-row sparkline>" / "live".
void throughputHero(Grid& g, int row, const View& v) {
  boxRow(g, row);
  boxRow(g, row + 1);
  g.text(CONTENT_COL, row, "TG", theme::DECODE, ATTR_BOLD);
  if (isnan(v.tg_live)) {
    g.text(5, row, fmt::NO_DATA, theme::DIM);
  } else {
    char hero[sizeof(fmt::Str)];
    snprintf(hero, sizeof hero, "%5s", fmt::speed(v.tg_live).s);
    g.big(5, row, hero, theme::DECODE);
  }
  g.text(16, row, "tok/s", theme::DIM);
  g.text(17, row + 1, "live", theme::DIM);
  sparkline(g, 22, row, 2, 20, 3, v.tg_hist, theme::DECODE);
}

void streamRow(Grid& g, int row, const StreamView& s) {
  // Columns (absolute): id 3 · state 6 · ctx ends 22 · tokens ends 29 ·
  // TTFT ends 36 · TG ends 44.
  boxRow(g, row);
  char id[4];
  snprintf(id, sizeof id, "%u", s.id);
  g.text(3, row, id, theme::VALUE);

  const char* state = nullptr;
  uint8_t role = theme::DIM;
  switch (s.state) {
    case StreamState::DECODING: state = "decoding"; role = theme::GOOD; break;
    case StreamState::PREFILL: state = "prefill"; role = theme::WARN; break;
    case StreamState::IDLE: state = "idle"; role = theme::DIM; break;
    case StreamState::NO_DATA: break;
  }
  if (state) g.text(6, row, state, role);

  g.textRight(22, row, fmt::pct(s.ctx_pct), valueRole(s.ctx_pct, fillRole(s.ctx_pct)));
  g.textRight(29, row, fmt::tokens(s.tokens), valueRole(s.tokens, theme::VALUE));

  if (s.state == StreamState::IDLE) return;  // nothing to measure
  const bool ttft = !isnan(s.ttft_s), tg = !isnan(s.tg);
  if (!ttft && !tg) {
    g.text(31, row, fmt::NO_DATA, theme::DIM);  // one "no data" spans both
    return;
  }
  g.textRight(36, row, fmt::durShort(s.ttft_s), valueRole(s.ttft_s, theme::VALUE));
  g.textRight(44, row, fmt::rate(s.tg), valueRole(s.tg, theme::DECODE));
}

}  // namespace

void overview(Grid& g, const View& v) {
  g.clear();
  const int top = chrome(g, v);  // rows 1–3: clock, server + state, model

  // Throughput: section rule, 2-row hero, PP, PP sparkline, run/wait, TG avg/max
  const int thr = top;
  section(g, thr, "Throughput");
  throughputHero(g, thr + 1, v);

  const int pp = thr + 3;
  boxRow(g, pp);
  if (v.pp_per_request) {
    // "PP last 3,354/s · prefilling 12s" (spec.md §7.1)
    Writer w(g, pp, CONTENT_COL);
    w.put("PP ", theme::PREFILL, ATTR_BOLD).put("last ", theme::LABEL);
    w.put(fmt::rate(v.pp_last), valueRole(v.pp_last, theme::PREFILL));
    if (!isnan(v.prefilling_s)) {
      char s[24];
      snprintf(s, sizeof s, "prefilling %.0fs", v.prefilling_s);
      w.put(SEP, theme::DIM).put(s, theme::WARN);
    }
  } else if (!noDataRow(g, pp, {v.pp_live, v.pp_avg, v.pp_eng}, "PP ", theme::PREFILL, ATTR_BOLD))
  Writer(g, pp, CONTENT_COL)
      .put("PP ", theme::PREFILL, ATTR_BOLD)
      .put(fmt::rate(v.pp_live), valueRole(v.pp_live, theme::PREFILL))
      .put("  avg ", theme::LABEL)
      .put(fmt::speed(v.pp_avg), valueRole(v.pp_avg, theme::PREFILL))
      .put(SEP, theme::DIM)
      .put("eng ", theme::LABEL)
      .put(fmt::rate(v.pp_eng), valueRole(v.pp_eng, theme::PREFILL));

  // "run 2/4 · wait 1 · TG avg 95.1 · max 104.2": queue, then live TG since
  // the monitor started / the server restarted (average over generating
  // polls, and peak). Each pair is all-or-nothing, so it collapses to a single
  // "no data". (The PP sparkline row was dropped for line spacing; the PP
  // history is on the graphs page.)
  const int tg_row = pp + 1;
  boxRow(g, tg_row);
  {
    Writer w(g, tg_row, CONTENT_COL);
    if (isnan(v.running)) {
      w.put("run ", theme::LABEL).put(fmt::NO_DATA, theme::DIM);
    } else {
      char run[24];
      if (isnan(v.max_batch)) snprintf(run, sizeof run, "%.0f", v.running);
      else snprintf(run, sizeof run, "%.0f/%.0f", v.running, v.max_batch);
      w.put("run ", theme::LABEL).put(run, theme::VALUE).put(SEP, theme::DIM);
      w.put("wait ", theme::LABEL).put(fmt::count(v.waiting), valueRole(v.waiting, theme::VALUE));
    }
    w.put(SEP, theme::DIM).put("TG ", theme::DECODE, ATTR_BOLD);
    if (isnan(v.tg_avg)) {
      w.put(fmt::NO_DATA, theme::DIM);
    } else {
      w.put("avg ", theme::LABEL).put(fmt::speed(v.tg_avg), theme::DECODE).put(SEP, theme::DIM);
      w.put("max ", theme::LABEL).put(fmt::speed(v.tg_max), valueRole(v.tg_max, theme::DECODE));
    }
  }

  // Cache
  const int cache = tg_row + 1;
  section(g, cache, "Cache");
  boxRow(g, cache + 1);
  {
    Writer w(g, cache + 1, CONTENT_COL);
    w.put("KV ", theme::LABEL).put(fmt::pct(v.kv_pct), fillRole(v.kv_pct)).put(" ", theme::BG);
    bar(g, w.col(), cache + 1, 16, v.kv_pct, fillRole(v.kv_pct));
    w.to(w.col() + 16).put(SEP, theme::DIM);
    if (isnan(v.kv_tokens) || isnan(v.kv_capacity)) {
      w.put(fmt::NO_DATA, theme::DIM);
    } else {
      char tok[2 * sizeof(fmt::Str)];
      snprintf(tok, sizeof tok, "%s/%s", fmt::tokens(v.kv_tokens).s, fmt::tokens(v.kv_capacity).s);
      w.put(tok, theme::VALUE).put(" tok", theme::DIM);
    }
  }

  // Streams: column header, then one row per slot
  const int streams = cache + 2;
  section(g, streams, "Streams");
  const int hdr = streams + 1;
  boxRow(g, hdr);
  g.text(3, hdr, "#", theme::DIM);
  g.text(6, hdr, "state", theme::DIM);
  g.textRight(22, hdr, "ctx", theme::DIM);
  g.textRight(29, hdr, "tokens", theme::DIM);
  g.textRight(36, hdr, "TTFT", theme::DIM);
  g.textRight(44, hdr, "TG", theme::DIM);
  int row = hdr + 1;
  // As many streams as fit above Averages (rule + 2 rows) and the footer.
  const int max_rows = (GRID_ROWS - 1) - 3 - row;
  if (v.n_streams == 0) {
    boxRow(g, row);
    g.text(3, row++, fmt::NO_DATA, theme::DIM);
  }
  for (int i = 0; i < v.n_streams && i < max_rows; i++) streamRow(g, row++, v.streams[i]);

  // Averages
  section(g, row++, "Averages");
  boxRow(g, row);
  if (!noDataRow(g, row, {v.ttft_cum, v.mtp_pct, v.cache_hit_pct}))
  Writer(g, row, CONTENT_COL)
      .put("TTFT ", theme::LABEL)
      .put(fmt::dur(v.ttft_cum), valueRole(v.ttft_cum, theme::VALUE))
      .put(SEP, theme::DIM)
      .put("MTP ", theme::LABEL)
      .put(fmt::pct(v.mtp_pct), mtpRole(v.mtp_pct))
      .put(SEP, theme::DIM)
      .put("cache-hit ", theme::LABEL)
      .put(fmt::pct(v.cache_hit_pct), valueRole(v.cache_hit_pct, theme::VALUE));
  row++;
  boxRow(g, row);
  const bool section_empty =
      allMissing({v.ttft_cum, v.mtp_pct, v.cache_hit_pct, v.ttft_live, v.lat_cum, v.n_requests});
  if (!section_empty && !noDataRow(g, row, {v.ttft_live, v.lat_cum, v.n_requests}))
  Writer(g, row, CONTENT_COL)
      .put("live TTFT ", theme::LABEL)
      .put(fmt::dur(v.ttft_live), valueRole(v.ttft_live, theme::VALUE))
      .put(SEP, theme::DIM)
      .put("lat ", theme::LABEL)
      .put(fmt::dur(v.lat_cum), valueRole(v.lat_cum, theme::VALUE))
      .put(SEP, theme::DIM)
      .put("n=", theme::LABEL)
      .put(fmt::count(v.n_requests), valueRole(v.n_requests, theme::VALUE));
  row++;

  footer(g, v, "tap: graphs");
  closeFrame(g);
}

}  // namespace pages
