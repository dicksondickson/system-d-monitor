// Settings screen (spec.md §7.4): refresh rate and theme picker. Opened by
// tapping the ≡ notch in the bottom-left corner of the dashboard.
//
//   ╭─┐Settings┌─────────────────────────────────╮
//   │ Refresh   ‹          2 s          ›         │   tap left / right half
//   │ TG 101.3 · PP 3,354/s · KV ████░░ · OK IDLE │   live preview
//   ╰─────────────────────────────────────────────╯
//   ╭─┐Themes┌───────────────────────────┐1/2┌─╮
//   │ ▶ EngineMetrics        everforest-dark-hard  │   13 rows × 2 columns
//   │ …                                            │
//   │ ‹ prev           done            next ›      │
//   ╰─┘≡└─┘tap a theme to apply└───────────────────╯
#include <stdio.h>
#include <string.h>

#include "fmt.h"
#include "pages.h"
#include "widgets.h"

namespace pages {

using namespace widgets;

namespace {
constexpr int LIST_TOP = 5;     // first theme row
constexpr int LIST_ROWS = 13;
constexpr int NAV_ROW = 18;
constexpr int COL_W = 22;       // per theme column
constexpr int REFRESH_PREV_COL = 14;  // ‹, left half (x 140)
constexpr int REFRESH_NEXT_COL = 33;  // ›, right half (x 330)
const char* SEP = " \xC2\xB7 ";

// "▶name": the marker sits in the gap before the name, so names get 21 columns.
void themeCell(Grid& g, int row, int col, int index, bool current) {
  const theme::Theme& t = theme::get(index);
  char name[32];
  const int room = COL_W - 1;
  if ((int)strlen(t.name) > room) snprintf(name, sizeof name, "%.*s\xE2\x80\xA6", room - 1, t.name);
  else snprintf(name, sizeof name, "%s", t.name);
  if (current) g.put(col, row, 0x25B6, theme::TITLE);  // ▶
  g.text(col + 1, row, name, current ? theme::TITLE : theme::VALUE, current ? ATTR_BOLD : ATTR_NONE);
}
}  // namespace

const uint32_t POLL_OPTIONS_MS[POLL_OPTION_COUNT] = {1000, 2000, 3000, 5000, 10000, 30000};

int themePages() { return (theme::count() + THEMES_PER_PAGE - 1) / THEMES_PER_PAGE; }

void settings(Grid& g, const Settings& s) {
  g.clear();
  const uint8_t box = theme::BORDER, list_box = theme::BOX2;

  boxTop(g, 0, box, "Settings");
  boxRow(g, 1, box);
  {
    // ‹ sits in the left half and › in the right half, matching the tap
    // zones (settingsHit): left half = slower, right half = faster.
    char poll[16];
    snprintf(poll, sizeof poll, "%u s", (unsigned)(s.poll_ms / 1000));
    g.text(CONTENT_COL, 1, "Refresh", theme::LABEL);
    g.put(REFRESH_PREV_COL, 1, 0x2039, theme::TITLE, ATTR_BOLD);  // ‹
    g.text(GRID_COLS / 2 - utf8Columns(poll) / 2, 1, poll, theme::VALUE, ATTR_BOLD);
    g.put(REFRESH_NEXT_COL, 1, 0x203A, theme::TITLE, ATTR_BOLD);  // ›
  }
  // Live preview of the current theme's roles.
  boxRow(g, 2, box);
  {
    // "TG 101.3 · PP 3,354 · KV ██░░ · OK BUSY DOWN" (exactly 44 columns)
    Writer w(g, 2, CONTENT_COL);
    w.put("TG ", theme::DECODE, ATTR_BOLD).put("101.3", theme::DECODE).put(SEP, theme::DIM);
    w.put("PP ", theme::PREFILL, ATTR_BOLD).put("3,354", theme::PREFILL).put(SEP, theme::DIM);
    w.put("KV ", theme::LABEL);
    bar(g, w.col(), 2, 4, 50, theme::GOOD);
    w.to(w.col() + 4).put(SEP, theme::DIM).put("OK", theme::GOOD, ATTR_BOLD).put(" ", theme::BG);
    w.put("BUSY", theme::WARN, ATTR_BOLD).put(" ", theme::BG).put("DOWN", theme::BAD, ATTR_BOLD);
  }
  boxBottom(g, 3, box);

  char pages_label[24];
  snprintf(pages_label, sizeof pages_label, "%d/%d", s.list_page + 1, themePages());
  boxTop(g, 4, list_box, "Themes", pages_label);
  const int first = s.list_page * THEMES_PER_PAGE;
  for (int r = 0; r < LIST_ROWS; r++) {
    boxRow(g, LIST_TOP + r, list_box);
    for (int c = 0; c < 2; c++) {
      const int i = first + c * LIST_ROWS + r;
      if (i < theme::count()) themeCell(g, LIST_TOP + r, CONTENT_COL + c * COL_W, i, i == s.theme);
    }
  }
  boxRow(g, NAV_ROW, list_box);
  const bool has_prev = s.list_page > 0, has_next = s.list_page + 1 < themePages();
  g.text(CONTENT_COL, NAV_ROW, "\xE2\x80\xB9 prev", has_prev ? theme::TITLE : theme::DIM, ATTR_BOLD);
  g.text(21, NAV_ROW, "done", theme::TITLE, ATTR_BOLD);
  g.textRight(CONTENT_END, NAV_ROW, "next \xE2\x80\xBA", has_next ? theme::TITLE : theme::DIM, ATTR_BOLD);

  int col = edgeStart(g, GRID_ROWS - 1, Edge::BOTTOM, list_box);
  col = notch(g, GRID_ROWS - 1, col, Edge::BOTTOM, list_box, "\xE2\x89\xA1", theme::TITLE, ATTR_BOLD);
  g.put(col++, GRID_ROWS - 1, 0x2500, list_box);
  col = notch(g, GRID_ROWS - 1, col, Edge::BOTTOM, list_box, "tap a theme to apply", theme::DIM);
  edgeEnd(g, GRID_ROWS - 1, col, Edge::BOTTOM, list_box);
}

SettingsHit settingsHit(int x, int y, const Settings& s) {
  const int row = y / CELL_PX_H, w = GRID_PX_W;
  // The whole Settings box (rows 1–3, 72 px) is the refresh control, so it's
  // easy to hit: left half = ‹, right half = ›.
  if (row <= 3) return {x < w / 2 ? SettingsHit::POLL_PREV : SettingsHit::POLL_NEXT, 0};
  if (row >= LIST_TOP && row < LIST_TOP + LIST_ROWS) {
    const int i = s.list_page * THEMES_PER_PAGE + (x >= w / 2 ? LIST_ROWS : 0) + (row - LIST_TOP);
    if (i < theme::count()) return {SettingsHit::THEME, i};
    return {SettingsHit::NONE, 0};
  }
  // Nav: the nav row and the bottom edge below it (48 px), so a tap that
  // lands a little low still counts. The ≡ corner closes Settings, as it
  // opened them.
  if (row == NAV_ROW || row == GRID_ROWS - 1) {
    if (row == GRID_ROWS - 1 && x < w / 4) return {SettingsHit::DONE, 0};
    if (x < w / 3) return {SettingsHit::PAGE_PREV, 0};
    if (x >= w * 2 / 3) return {SettingsHit::PAGE_NEXT, 0};
    return {SettingsHit::DONE, 0};
  }
  return {SettingsHit::NONE, 0};
}

}  // namespace pages
