#pragma once
#include <stdint.h>

#include "gfx/grid.h"
#include "gfx/theme.h"

// The visual language of spec.md §7: btop-style section rules, │ … │ rows,
// bars, block sparklines and braille graphs. Pages only compose these.
// Arduino-free.
namespace widgets {

constexpr int CONTENT_COL = 2;               // first column inside "│ "
constexpr int CONTENT_END = GRID_COLS - 2;   // exclusive; " │" follows

// Section rules form one closed frame with the │ rows (btop style):
//   ┌─┤ title ├──────────┐   top    (Edge::TOP)
//   ├─┤ title ├──┤ right ├─┤   middle (Edge::MID)
//   └─┤ title ├──┤ right ├─┘   bottom (Edge::BOTTOM)
// ruleStart() draws the left junction and "─┤ " and returns the column for
// the title; ruleEnd() closes the title with " ├", fills with "─", adds the
// optional "┤ right ├─" and the right junction.
enum class Edge : uint8_t { TOP, MID, BOTTOM };

int ruleStart(Grid& g, int row, Edge e = Edge::MID);
void ruleEnd(Grid& g, int row, int col, Edge e = Edge::MID, const char* right = nullptr,
             uint8_t right_role = theme::DIM, uint8_t right_attr = ATTR_NONE);
void section(Grid& g, int row, const char* title, const char* right = nullptr,
             uint8_t right_role = theme::DIM, Edge e = Edge::MID);

// Columns a rule needs after its title: " ├" + edge, plus "─┤ right ├─"
// when there is a right label.
int ruleTail(const char* right);

// "│" at both edges of a content row.
void boxRow(Grid& g, int row);

// Gives every row that isn't a rule its │ edges, so the frame is closed even
// across blank rows. Call last.
void closeFrame(Grid& g);

// Writes consecutive coloured segments along a row, clipped at max_col.
class Writer {
 public:
  Writer(Grid& g, int row, int col, int max_col = CONTENT_END)
      : g_(g), row_(row), col_(col), max_(max_col) {}
  Writer& put(const char* utf8, uint8_t role, uint8_t attr = ATTR_NONE) {
    col_ = g_.text(col_, row_, utf8, role, attr, max_);
    return *this;
  }
  Writer& to(int col) {
    col_ = col;
    return *this;
  }
  int col() const { return col_; }

 private:
  Grid& g_;
  int row_, col_, max_;
};

// "█████░░░░░" bar, filled part in `role`, rest dim. NaN → all dim.
void bar(Grid& g, int col, int row, int width, float pct, uint8_t role);

// Block sparkline: `cells` columns × `rows` rows (8 levels per row). Each
// cell is the max of `per` consecutive samples; data is oldest first and
// must hold cells × per samples. NaN samples are gaps (`·`). Returns the
// scale (window max), NaN if the window is all gaps.
float sparkline(Grid& g, int col, int row, int rows, int cells, int per, const float* data,
                uint8_t role);

// Braille area graph: `cols` columns × `rows` rows, 2 samples per column,
// 4 levels per row; data holds cols × 2 samples, oldest first. Values are
// scaled to vmax (clamped). NaN samples leave their dot column empty.
void braille(Grid& g, int col, int row, int rows, int cols, const float* data, float vmax,
             uint8_t role);

// Threshold colour for KV / context fill (spec.md §5).
uint8_t fillRole(float pct);

}  // namespace widgets
