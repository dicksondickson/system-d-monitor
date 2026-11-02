#pragma once
#include "gfx/grid.h"
#include "view.h"

// The two screens of spec.md §7. Each clears the grid and draws the whole
// page from the View. Arduino-free.
namespace pages {

enum class Page : uint8_t { OVERVIEW, GRAPHS };

void overview(Grid& g, const View& v);
void graphs(Grid& g, const View& v);

inline void draw(Page p, Grid& g, const View& v) {
  p == Page::OVERVIEW ? overview(g, v) : graphs(g, v);
}

// Shared chrome (chrome.cpp), spec.md §7:
//   row 1  ┌─┤ SYSTEM-D 7:42:10 PM ├──────────┤ ▂▄▆█ ├─┐   clock, Wi-Fi bars
//   row 2  ├─┤ dgx042 · 192.168.50.192 ├──┤ OK IDLE ├─┤   server, IP, state
//   row 3  ├─┤ TensorFold · deepseek-v4.1-flash-1017 ├┤   engine + model
//   row 24 └─┤ poll 2s · ok 0.4s ago ├──┤ tap: graphs ├─┘
// chrome() draws rows 1–3 and returns the first free row (3, 0-based).
int chrome(Grid& g, const View& v);
void header(Grid& g, const View& v);
void serverLine(Grid& g, const View& v);
void modelRule(Grid& g, const View& v);
void footer(Grid& g, const View& v, const char* hint);

}  // namespace pages
