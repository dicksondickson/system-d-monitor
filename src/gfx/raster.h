#pragma once
#include <stdint.h>

#include "grid.h"
#include "theme.h"

// Draws one grid cell as 10×20 RGB565 pixels. Arduino-free.
namespace raster {

constexpr int CELL_W = 10;
constexpr int CELL_H = 20;

// dst points at the cell's top-left pixel; stride is in pixels (480 for the
// panel framebuffer).
void cell(const Cell& c, const theme::Theme& t, uint16_t* dst, int stride);

}  // namespace raster
