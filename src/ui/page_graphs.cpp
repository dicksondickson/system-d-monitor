// Graphs page, spec.md §7.2.
#include <math.h>
#include <stdio.h>

#include "fmt.h"
#include "pages.h"
#include "widgets.h"

namespace pages {

using namespace widgets;

namespace {

struct Stats {
  float now = NAN, avg = NAN, max = NAN, min = NAN;
};

Stats stats(const float* d) {
  Stats s;
  s.now = d[RING_LEN - 1];
  float sum = 0;
  int n = 0;
  for (int i = 0; i < RING_LEN; i++) {
    if (isnan(d[i])) continue;
    sum += d[i];
    n++;
    if (isnan(s.max) || d[i] > s.max) s.max = d[i];
    if (isnan(s.min) || d[i] < s.min) s.min = d[i];
  }
  if (n) s.avg = sum / n;
  return s;
}

using Formatter = fmt::Str (*)(float);

// "now 100.9" — label, then the value right-aligned in 5 columns.
void statLine(Grid& g, int row, const char* label, float v, Formatter f, uint8_t role) {
  char buf[32];
  fmt::Str s = f(v);
  snprintf(buf, sizeof buf, "%5s", s.s);
  Writer(g, row, 34).put(label, theme::LABEL).put(" ", theme::BG).put(buf, isnan(v) ? (uint8_t)theme::DIM : role);
}

struct GraphSpec {
  const char* title;
  const float* data;
  int rows;
  float floor;      // minimum y-scale, so idle noise doesn't fill the box
  bool fixed_100;   // KV: always 0–100 %
  Formatter fmt;
  uint8_t role;
  bool full_stats;  // now/avg/max/min, else now/max
};

int graph(Grid& g, int row, const GraphSpec& s) {
  section(g, row++, s.title, "120s");
  Stats st = stats(s.data);
  float vmax = s.fixed_100 ? 100.0f : fmaxf(isnan(st.max) ? 0.0f : st.max, s.floor);
  for (int r = 0; r < s.rows; r++) boxRow(g, row + r);
  if (isnan(st.max)) {  // empty window: one centred "no data", no stats
    g.text((GRID_COLS - 7) / 2, row + (s.rows - 1) / 2, fmt::NO_DATA, theme::DIM);
    return row + s.rows;
  }
  braille(g, CONTENT_COL, row, s.rows, 30, s.data, vmax, s.role);
  statLine(g, row, "now", st.now, s.fmt, s.role);
  if (s.full_stats) {
    statLine(g, row + 1, "avg", st.avg, s.fmt, s.role);
    statLine(g, row + 2, "max", st.max, s.fmt, s.role);
    statLine(g, row + 3, "min", st.min, s.fmt, s.role);
  } else {
    statLine(g, row + 1, "max", st.max, s.fmt, s.role);
  }
  return row + s.rows;
}

}  // namespace

void graphs(Grid& g, const View& v) {
  g.clear();
  header(g, v);
  serverRule(g, v);
  int row = 2;
  row = graph(g, row, {"TG live tok/s", v.tg_hist, 8, 10, false, fmt::speed, theme::DECODE, true});
  row = graph(g, row, {"PP live tok/s", v.pp_hist, 5, 100, false, fmt::speed, theme::PREFILL, true});
  row = graph(g, row, {"KV %", v.kv_hist, 4, 0, true, fmt::pct, theme::GOOD, false});
  footer(g, v, "tap: overview");
  closeFrame(g);
}

}  // namespace pages
