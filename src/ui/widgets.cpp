#include "widgets.h"

#include <math.h>

namespace widgets {

namespace {
constexpr uint32_t H_LINE = 0x2500;  // ─
constexpr uint32_t V_LINE = 0x2502;  // │
constexpr uint32_t T_LEFT = 0x2524;  // ┤
constexpr uint32_t FULL = 0x2588;    // █
constexpr uint32_t SHADE = 0x2591;   // ░
constexpr uint32_t DOT = 0x00B7;     // ·
const uint32_t BLOCKS[9] = {' ', 0x2581, 0x2582, 0x2583, 0x2584, 0x2585, 0x2586, 0x2587, 0x2588};
}  // namespace

namespace {
uint32_t leftEdge(Edge e) { return e == Edge::TOP ? 0x250C : e == Edge::BOTTOM ? 0x2514 : 0x251C; }   // ┌ └ ├
uint32_t rightEdge(Edge e) { return e == Edge::TOP ? 0x2510 : e == Edge::BOTTOM ? 0x2518 : 0x2524; }  // ┐ ┘ ┤
constexpr uint32_t T_RIGHT = 0x251C;  // ├
}  // namespace

int ruleStart(Grid& g, int row, Edge e) {
  g.put(0, row, leftEdge(e), theme::BORDER);
  g.put(1, row, H_LINE, theme::BORDER);
  g.put(2, row, T_LEFT, theme::BORDER);
  g.put(3, row, ' ', theme::BORDER);
  return 4;
}

int ruleTail(const char* right) {
  // " ├" + edge, plus "─┤ right ├─" when there's a right label.
  return 2 + (right ? 1 + utf8Columns(right) + 5 : 0) + 1;
}

void ruleEnd(Grid& g, int row, int col, Edge e, const char* right, uint8_t right_role,
             uint8_t right_attr) {
  const int edge = GRID_COLS - 1;
  if (col > edge - 2) col = edge - 2;  // always leave room to close the title
  g.put(col++, row, ' ', theme::BORDER);
  g.put(col++, row, T_RIGHT, theme::BORDER);
  const int right_start = right ? edge - (utf8Columns(right) + 5) : edge;  // "┤ right ├─"
  for (; col < right_start; col++) g.put(col, row, H_LINE, theme::BORDER);
  if (right && col == right_start) {
    g.put(col++, row, T_LEFT, theme::BORDER);
    g.put(col++, row, ' ', theme::BORDER);
    col = g.text(col, row, right, right_role, right_attr);
    g.put(col++, row, ' ', theme::BORDER);
    g.put(col++, row, T_RIGHT, theme::BORDER);
    g.put(col++, row, H_LINE, theme::BORDER);
  }
  for (; col < edge; col++) g.put(col, row, H_LINE, theme::BORDER);
  g.put(edge, row, rightEdge(e), theme::BORDER);
}

void section(Grid& g, int row, const char* title, const char* right, uint8_t right_role, Edge e) {
  int col = ruleStart(g, row, e);
  col = g.text(col, row, title, theme::TITLE, ATTR_BOLD, GRID_COLS - ruleTail(right));
  ruleEnd(g, row, col, e, right, right_role);
}

void boxRow(Grid& g, int row) {
  g.put(0, row, V_LINE, theme::BORDER);
  g.put(GRID_COLS - 1, row, V_LINE, theme::BORDER);
}

void closeFrame(Grid& g) {
  for (int r = 0; r < GRID_ROWS; r++)
    if (g.at(0, r).ch == ' ') boxRow(g, r);
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
