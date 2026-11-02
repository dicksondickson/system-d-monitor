// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

#include "screen.h"

#include <string.h>

#include "board_pins.h"
#include "gfx/raster.h"

namespace screen {

namespace {
constexpr int BAND_PIXELS = raster::CELL_H * SCREEN_W;  // one cell row: 480×24
static_assert(GRID_ROWS * raster::CELL_H == SCREEN_H, "grid must fill the panel");

uint16_t* g_fb = nullptr;
Grid g_shown;
bool g_full = true;
// Dirty cells of a cell row are rasterised here (internal SRAM), then the
// span from the first to the last dirty column is copied line by line into
// the PSRAM framebuffer, which keeps PSRAM writes few and sequential.
uint16_t g_band[BAND_PIXELS];
}  // namespace

void begin(Arduino_RGB_Display* gfx) {
  g_fb = gfx->getFramebuffer();
  g_full = true;
}

void invalidate() { g_full = true; }

int present(const Grid& next, const theme::Theme& t) {
  if (!g_fb) return 0;
  int painted = 0;
  for (int r = 0; r < GRID_ROWS; r++) {
    int first = -1, last = -1;
    for (int c = 0; c < GRID_COLS; c++) {
      if (!g_full && next.at(c, r) == g_shown.at(c, r)) continue;
      // Every cell in [first, last] is copied, so render the clean cells in
      // between too; they're already on screen, this just fills the buffer.
      for (int k = last < 0 ? c : last + 1; k <= c; k++)
        raster::cell(next.at(k, r), t, g_band + k * raster::CELL_W, SCREEN_W);
      if (first < 0) first = c;
      last = c;
      painted++;
    }
    if (first < 0) continue;

    uint16_t* band = g_fb + r * BAND_PIXELS;
    const int x0 = first * raster::CELL_W;
    const size_t bytes = (last - first + 1) * raster::CELL_W * sizeof(uint16_t);
    for (int y = 0; y < raster::CELL_H; y++)
      memcpy(band + y * SCREEN_W + x0, g_band + y * SCREEN_W + x0, bytes);
    // No cache writeback: with bounce buffers (panel.cpp) the driver copies
    // the framebuffer through the CPU cache, so it sees these writes. The
    // writeback was an extra 19 KB PSRAM burst per band that competed with
    // the bounce-buffer refills and made the image glitch (bring-up §2.9).
  }
  g_shown = next;
  g_full = false;
  return painted;
}

}  // namespace screen
