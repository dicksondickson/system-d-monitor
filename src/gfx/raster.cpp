#include "raster.h"

#include "font.h"
#include "platform.h"

namespace raster {

FAST_CODE void cell(const Cell& c, const theme::Theme& t, uint16_t* dst, int stride) {
  const uint16_t fg = t.rgb565[c.fg];
  const uint16_t bg = t.rgb565[c.bg];

  if (c.attr & ATTR_BIG) {
    // Pixel-double the bold glyph: this quadrant shows a 5×10 region of it.
    const uint16_t* g = font::rows(c.ch, true);
    const uint8_t q = bigQuadrant(c.attr);
    const int sx = (q & 1) * (CELL_W / 2), sy = (q >> 1) * (CELL_H / 2);
    for (int y = 0; y < CELL_H; y++) {
      const uint16_t bits = g[sy + y / 2];
      uint16_t* p = dst + y * stride;
      for (int x = 0; x < CELL_W; x++) p[x] = (bits >> (15 - (sx + x / 2))) & 1 ? fg : bg;
    }
    return;
  }

  const uint16_t* g = font::rows(c.ch, c.attr & ATTR_BOLD);
  for (int y = 0; y < CELL_H; y++) {
    const uint16_t bits = g[y];
    uint16_t* p = dst + y * stride;
    for (int x = 0; x < CELL_W; x++) p[x] = (bits >> (15 - x)) & 1 ? fg : bg;
  }
}

}  // namespace raster
