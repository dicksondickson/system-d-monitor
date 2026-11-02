#pragma once
#include <stdint.h>

#include <string>

#include "theme.h"

// The 48×24 character grid (spec.md §6). Pages draw into a Grid; the screen
// diffs it against what's on the panel and repaints only changed cells.
// Arduino-free so pages can be rendered and checked on the host.

constexpr int GRID_COLS = 48;
constexpr int GRID_ROWS = 24;

enum Attr : uint8_t {
  ATTR_NONE = 0,
  ATTR_BOLD = 1 << 0,
  // 2× "hero" glyphs span a 2×2 block of cells; each cell holds the same
  // codepoint and its quadrant (0 = top-left, 1 = top-right, 2 = bottom-left,
  // 3 = bottom-right) in bits 2–3.
  ATTR_BIG = 1 << 1,
};
constexpr uint8_t bigQuadrant(uint8_t attr) { return (attr >> 2) & 3; }
constexpr uint8_t bigAttr(uint8_t quadrant) { return ATTR_BIG | (uint8_t)(quadrant << 2); }

struct Cell {
  uint16_t ch = ' ';
  uint8_t fg = theme::VALUE;
  uint8_t bg = theme::BG;
  uint8_t attr = ATTR_NONE;

  bool operator==(const Cell& o) const {
    return ch == o.ch && fg == o.fg && bg == o.bg && attr == o.attr;
  }
  bool operator!=(const Cell& o) const { return !(*this == o); }
};

class Grid {
 public:
  Grid() { clear(); }

  void clear();
  const Cell& at(int col, int row) const { return cells_[row][col]; }

  void put(int col, int row, uint32_t ch, uint8_t fg, uint8_t attr = ATTR_NONE);

  // UTF-8 text from (col,row), clipped at max_col (exclusive, default: grid
  // edge). Returns the column after the last character written.
  int text(int col, int row, const char* utf8, uint8_t fg, uint8_t attr = ATTR_NONE,
           int max_col = GRID_COLS);

  // Text right-aligned so it ends just before end_col. Returns its start column.
  int textRight(int end_col, int row, const char* utf8, uint8_t fg, uint8_t attr = ATTR_NONE);

  // 2× text: each character covers 2 columns × 2 rows. Returns the column
  // after the last character.
  int big(int col, int row, const char* ascii, uint8_t fg);

  void fill(int col, int row, int count, uint32_t ch, uint8_t fg);

  // Text dump for tests and golden files: one line per row. A 2× character
  // prints as its fullwidth form (U+FF01–FF5E) in its top-left cell, nothing
  // in its top-right cell, and spaces in its bottom cells, so every line is
  // 48 display columns wide (as in the spec.md §7 mockups).
  std::string dump() const;

 private:
  Cell cells_[GRID_ROWS][GRID_COLS];
};

// UTF-8 helpers (exposed for tests and width calculations).
uint32_t utf8Next(const char*& s);       // 0 at end of string
int utf8Columns(const char* s);          // codepoints = grid columns
