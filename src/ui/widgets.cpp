#include "widgets.h"

#include <math.h>

namespace widgets {

namespace {
constexpr uint32_t H_LINE = 0x2500;  // ─
constexpr uint32_t V_LINE = 0x2502;  // │
constexpr uint32_t FULL = 0x2588;    // █
constexpr uint32_t SHADE = 0x2591;   // ░
constexpr uint32_t DOT = 0x00B7;     // ·
const uint32_t BLOCKS[9] = {' ', 0x2581, 0x2582, 0x2583, 0x2584, 0x2585, 0x2586, 0x2587, 0x2588};
}  // namespace

namespace {
constexpr uint32_t ROUND_TL = 0x256D, ROUND_TR = 0x256E, ROUND_BR = 0x256F, ROUND_BL = 0x2570;  // ╭╮╯╰
constexpr uint32_t NOTCH_TOP_L = 0x2510, NOTCH_TOP_R = 0x250C;   // ┐ ┌  (btop title_left/right)
constexpr uint32_t NOTCH_BOT_L = 0x2518, NOTCH_BOT_R = 0x2514;   // ┘ └  (btop title_left/right_down)
}  // namespace

int edgeStart(Grid& g, int row, Edge e, uint8_t box) {
  g.put(0, row, e == Edge::TOP ? ROUND_TL : ROUND_BL, box);
  g.put(1, row, H_LINE, box);
  return 2;
}

int notch(Grid& g, int row, int col, Edge e, uint8_t box, const char* text, uint8_t role,
          uint8_t attr) {
  g.put(col++, row, e == Edge::TOP ? NOTCH_TOP_L : NOTCH_BOT_L, box);
  col = g.text(col, row, text, role, attr, GRID_COLS - 2);
  g.put(col++, row, e == Edge::TOP ? NOTCH_TOP_R : NOTCH_BOT_R, box);
  return col;
}

int edgeTail(const char* right) { return 1 + (right ? utf8Columns(right) + 3 : 0) + 1; }

int notchCol(const char* right) { return GRID_COLS - 1 - (utf8Columns(right) + 3) + 1; }

void edgeEnd(Grid& g, int row, int col, Edge e, uint8_t box, const char* right, uint8_t right_role,
             uint8_t right_attr) {
  const int edge = GRID_COLS - 1;
  const int right_start = right ? edge - (utf8Columns(right) + 3) : edge;  // "┐right┌─"
  for (; col < right_start; col++) g.put(col, row, H_LINE, box);
  if (right && col == right_start) {
    col = notch(g, row, col, e, box, right, right_role, right_attr);
    g.put(col++, row, H_LINE, box);
  }
  for (; col < edge; col++) g.put(col, row, H_LINE, box);
  g.put(edge, row, e == Edge::TOP ? ROUND_TR : ROUND_BR, box);
}

void boxTop(Grid& g, int row, uint8_t box, const char* title, const char* right, uint8_t right_role) {
  int col = notch(g, row, edgeStart(g, row, Edge::TOP, box), Edge::TOP, box, title, theme::TITLE, ATTR_BOLD);
  edgeEnd(g, row, col, Edge::TOP, box, right, right_role);
}

void boxBottom(Grid& g, int row, uint8_t box) { edgeEnd(g, row, edgeStart(g, row, Edge::BOTTOM, box), Edge::BOTTOM, box); }

void boxRow(Grid& g, int row, uint8_t box) {
  g.put(0, row, V_LINE, box);
  g.put(GRID_COLS - 1, row, V_LINE, box);
}

void bar(Grid& g, int col, int row, int width, float pct, uint8_t role) {
  int filled = isnan(pct) ? 0 : (int)lroundf(fminf(fmaxf(pct, 0.0f), 100.0f) / 100.0f * width);
  for (int i = 0; i < width; i++) {
    if (i < filled) g.put(col + i, row, FULL, role);
    else g.put(col + i, row, SHADE, theme::DIM);
  }
}

float sparkline(Grid& g, int col, int row, int rows, int cells, int per, const float* data,
                uint8_t role) {
  float vmax = NAN;
  for (int i = 0; i < cells * per; i++)
    if (!isnan(data[i]) && (isnan(vmax) || data[i] > vmax)) vmax = data[i];
  const float scale = (isnan(vmax) || vmax <= 0) ? 1.0f : vmax;
  const int levels = rows * 8;

  for (int c = 0; c < cells; c++) {
    float m = NAN;
    for (int k = 0; k < per; k++) {
      float v = data[c * per + k];
      if (!isnan(v) && (isnan(m) || v > m)) m = v;
    }
    int h = isnan(m) ? -1 : (int)lroundf(fmaxf(m, 0.0f) / scale * levels);
    for (int r = 0; r < rows; r++) {
      const bool bottom = r == rows - 1;
      const int base = (rows - 1 - r) * 8;
      if (h < 0) {
        g.put(col + c, row + r, bottom ? DOT : ' ', theme::DIM);
        continue;
      }
      int f = h - base;
      f = f < 0 ? 0 : f > 8 ? 8 : f;
      if (f > 0) g.put(col + c, row + r, BLOCKS[f], role);
      else g.put(col + c, row + r, bottom ? BLOCKS[1] : ' ', role);
    }
  }
  return vmax;
}

void braille(Grid& g, int col, int row, int rows, int cols, const float* data, float vmax,
             uint8_t role) {
  static const uint8_t LEFT[4] = {0x01, 0x02, 0x04, 0x40};   // dots 1,2,3,7 (top→bottom)
  static const uint8_t RIGHT[4] = {0x08, 0x10, 0x20, 0x80};  // dots 4,5,6,8
  const int levels = rows * 4;
  const float scale = (isnan(vmax) || vmax <= 0) ? 1.0f : vmax;

  int h[2 * 64];
  for (int i = 0; i < cols * 2; i++) {
    float v = data[i];
    if (isnan(v)) {
      h[i] = -1;
      continue;
    }
    int lv = (int)lroundf(fminf(fmaxf(v, 0.0f) / scale, 1.0f) * levels);
    h[i] = (v > 0 && lv == 0) ? 1 : lv;
  }
  for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; c++) {
      uint8_t bits = 0;
      for (int side = 0; side < 2; side++) {
        int v = h[c * 2 + side];
        if (v < 0) continue;
        const uint8_t* dots = side ? RIGHT : LEFT;
        for (int d = 0; d < 4; d++) {
          int level = (rows - r - 1) * 4 + (3 - d) + 1;
          if (v >= level) bits |= dots[d];
        }
      }
      g.put(col + c, row + r, 0x2800 + bits, role);
    }
  }
}

uint8_t fillRole(float pct) {
  if (isnan(pct)) return theme::DIM;
  return pct < 70 ? theme::GOOD : pct < 90 ? theme::WARN : theme::BAD;
}

}  // namespace widgets
