// Host renderer for screenshots: draws a page with the firmware's own grid,
// font and rasteriser and writes a 480×480 binary PPM.
//   render_screen <overview|graphs|settings> <theme name> <out.ppm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "gfx/grid.h"
#include "gfx/raster.h"
#include "gfx/theme.h"
#include "ui/demo_view.h"
#include "ui/pages.h"

int main(int argc, char** argv) {
  if (argc != 4) {
    fprintf(stderr, "usage: %s <overview|graphs|settings> <theme> <out.ppm>\n", argv[0]);
    return 2;
  }
  static uint16_t fb[480 * 480];
  Grid g;
  const int t = theme::find(argv[2]);
  if (!strcmp(argv[1], "settings")) pages::settings(g, {2000, t, t / pages::THEMES_PER_PAGE});
  else pages::draw(!strcmp(argv[1], "graphs") ? pages::Page::GRAPHS : pages::Page::OVERVIEW, g, demoView());
  for (int r = 0; r < GRID_ROWS; r++)
    for (int c = 0; c < GRID_COLS; c++)
      raster::cell(g.at(c, r), theme::get(t), fb + r * raster::CELL_H * 480 + c * raster::CELL_W, 480);
  FILE* f = fopen(argv[3], "wb");
  if (!f) return 1;
  fprintf(f, "P6\n480 480\n255\n");
  for (int i = 0; i < 480 * 480; i++) {
    const uint16_t p = fb[i];
    const unsigned char rgb[3] = {(unsigned char)((p >> 11) << 3), (unsigned char)(((p >> 5) & 63) << 2),
                                  (unsigned char)((p & 31) << 3)};
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
  return 0;
}
