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

// Shared chrome (chrome.cpp): row 1 header, row 2 server rule, row 24 footer.
void header(Grid& g, const View& v);
void serverRule(Grid& g, const View& v);
void footer(Grid& g, const View& v, const char* hint);

}  // namespace pages
