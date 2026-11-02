#include <string.h>

#include "fmt.h"
#include "pages.h"
#include "widgets.h"

namespace pages {

using namespace widgets;
using theme::Role;

namespace {

struct StateLabel {
  const char* text;
  uint8_t role;
};

StateLabel stateLabel(ServerState s) {
  switch (s) {
    case ServerState::IDLE: return {"OK IDLE", theme::GOOD};
    case ServerState::BUSY: return {"OK BUSY", theme::WARN};
    case ServerState::WARMING: return {"OK WARM", theme::WARN};
    case ServerState::RESTART: return {"OK RESTART", theme::WARN};
    case ServerState::STALE: return {"STALE", theme::WARN};
    case ServerState::DOWN: return {"DOWN", theme::BAD};
    case ServerState::NO_WIFI: return {"NO WIFI", theme::BAD};
    default: return {"UNKNOWN", theme::BAD};
  }
}

// Copies at most `cols` codepoints of src into dst, ending with "…" if cut.
void truncate(char* dst, size_t n, const char* src, int cols) {
  if (utf8Columns(src) <= cols) {
    strncpy(dst, src, n - 1);
    dst[n - 1] = '\0';
    return;
  }
  const char* p = src;
  for (int i = 0; i < cols - 1 && utf8Next(p);) i++;
  size_t len = (size_t)(p - src);
  if (len > n - 4) len = n - 4;
  memcpy(dst, src, len);
  strcpy(dst + len, "\xE2\x80\xA6");  // …
}

}  // namespace

void header(Grid& g, const View& v) {
  static const uint32_t BARS[4] = {0x2582, 0x2584, 0x2586, 0x2588};  // ▂▄▆█
  // "SYSTEM-D hh:mm:ss  <server> ▂▄▆█", clipped before the rule's tail.
  const int end = GRID_COLS - ruleTail(nullptr) - 5;  // leave " ▂▄▆█"
  Writer w(g, 0, ruleStart(g, 0, Edge::TOP), end);
  w.put("SYSTEM-D", theme::TITLE, ATTR_BOLD);
  if (v.clock[0]) w.put(" ", theme::BORDER).put(v.clock, theme::VALUE);
  w.put("  ", theme::BORDER).put(v.server[0] ? v.server : fmt::NO_DATA, v.server[0] ? theme::VALUE : theme::DIM);
  w.put(" ", theme::BORDER);
  int col = w.col();
  for (int i = 0; i < 4; i++) g.put(col + i, 0, BARS[i], i < v.wifi_bars ? theme::GOOD : theme::DIM);
  ruleEnd(g, 0, col + 4, Edge::TOP);
}

void serverRule(Grid& g, const View& v) {
  StateLabel st = stateLabel(v.state);
  const char* engine = v.engine[0] ? v.engine : "???";
  const int end = GRID_COLS - ruleTail(st.text);
  Writer w(g, 1, ruleStart(g, 1), end);
  w.put(engine, theme::TITLE, ATTR_BOLD);
  if (v.model[0]) {
    int room = end - w.col() - 3;  // after " · "
    char model[sizeof v.model + 4];
    truncate(model, sizeof model, v.model, room);
    w.put(" \xC2\xB7 ", theme::DIM).put(model, theme::VALUE);
  }
  ruleEnd(g, 1, w.col(), Edge::MID, st.text, st.role, ATTR_BOLD);
}

void footer(Grid& g, const View& v, const char* hint) {
  const int row = GRID_ROWS - 1;
  char pos[16];
  if (v.server_count > 1) {  // which server of how many; tap left/right to switch
    snprintf(pos, sizeof pos, "\xE2\x80\xB9 %u/%u \xE2\x80\xBA", v.server_index + 1u, (unsigned)v.server_count);
    hint = pos;
  }
  Writer w(g, row, ruleStart(g, row, Edge::BOTTOM), GRID_COLS - ruleTail(hint));
  w.put(v.status[0] ? v.status : fmt::NO_DATA, theme::DIM);
  ruleEnd(g, row, w.col(), Edge::BOTTOM, hint, theme::DIM);
}

}  // namespace pages
