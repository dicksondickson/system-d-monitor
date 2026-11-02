// Renders both pages from the demo view and compares the text dump with the
// golden files (spec.md §7 layout). On mismatch the actual dump is printed.
#include <unity.h>

#include <cstdio>
#include <sstream>
#include <string>

#include "golden.h"
#include "gfx/grid.h"
#include "ui/demo_view.h"
#include "gfx/theme.h"
#include "ui/pages.h"

static std::string render(pages::Page p, const View& v = demoView()) {
  Grid g;
  pages::draw(p, g, v);
  return g.dump();
}

// Display width of a UTF-8 line: fullwidth forms (the 2× hero) count as 2.
static int displayCols(const std::string& line) {
  int cols = 0;
  const char* s = line.c_str();
  while (uint32_t c = utf8Next(s)) cols += (c >= 0xFF01 && c <= 0xFF5E) || c == 0x3000 ? 2 : 1;
  return cols;
}

static void expectPage(const char* golden, const std::string& actual, const char* name) {
  if (actual != golden) printf("\n--- actual %s ---\n%s", name, actual.c_str());
  TEST_ASSERT_EQUAL_STRING_MESSAGE(golden, actual.c_str(), name);
}

void setUp() {}
void tearDown() {}

static void test_overview_matches_golden() {
  expectPage(GOLDEN_OVERVIEW, render(pages::Page::OVERVIEW), "overview");
}

static void test_graphs_matches_golden() {
  expectPage(GOLDEN_GRAPHS, render(pages::Page::GRAPHS), "graphs");
}

static void test_every_row_is_48_columns() {
  for (auto p : {pages::Page::OVERVIEW, pages::Page::GRAPHS}) {
    std::istringstream in(render(p));
    std::string line;
    int rows = 0;
    while (std::getline(in, line)) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(GRID_COLS, displayCols(line), line.c_str());
      rows++;
    }
    TEST_ASSERT_EQUAL_INT(GRID_ROWS, rows);
  }
}

static void test_empty_view_says_no_data() {
  View v;  // nothing received yet
  std::string ov = render(pages::Page::OVERVIEW, v);
  TEST_ASSERT_TRUE(ov.find("no data") != std::string::npos);
  TEST_ASSERT_TRUE(ov.find("UNKNOWN") != std::string::npos);
  TEST_ASSERT_TRUE(ov.find("nan") == std::string::npos);  // never a raw NaN
  std::string gp = render(pages::Page::GRAPHS, v);
  TEST_ASSERT_TRUE(gp.find("no data") != std::string::npos);
  TEST_ASSERT_TRUE(gp.find("nan") == std::string::npos);
}

static void test_settings_screen() {
  Grid g;
  const int nord = theme::find("nord");
  pages::Settings s{2000, nord, nord / pages::THEMES_PER_PAGE};
  pages::settings(g, s);
  std::string d = g.dump();
  printf("\n--- settings ---\n%s", d.c_str());
  std::istringstream in(d);
  std::string line;
  int rows = 0;
  while (std::getline(in, line)) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(GRID_COLS, displayCols(line), line.c_str());
    rows++;
  }
  TEST_ASSERT_EQUAL_INT(GRID_ROWS, rows);
  TEST_ASSERT_TRUE(d.find("2 s") != std::string::npos);
  // ‹ in the left half, › in the right half of the refresh row
  TEST_ASSERT_EQUAL_UINT16(0x2039, g.at(14, 1).ch);
  TEST_ASSERT_EQUAL_UINT16(0x203A, g.at(33, 1).ch);
  TEST_ASSERT_TRUE(d.find("\xE2\x96\xB6nord") != std::string::npos);  // ▶ marks the current theme
}

static void test_brand_is_red_in_every_theme() {
  Grid g;
  pages::overview(g, demoView());
  TEST_ASSERT_EQUAL_UINT8(theme::BRAND, g.at(3, 0).fg);  // the "S" of SYSTEM-D
  for (int i = 0; i < theme::count(); i++)
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(theme::BRAND_RED, theme::get(i).rgb565[theme::BRAND], theme::get(i).name);
}

static void test_themes_and_hits() {
  TEST_ASSERT_EQUAL_INT(42, theme::count());  // btop Default + 41 bundled
  TEST_ASSERT_EQUAL_STRING("System-D", theme::get(0).name);  // btop's Default, renamed
  TEST_ASSERT_TRUE(theme::find("tokyo-night") > 0);
  // Unknown names (first boot, the removed EngineMetrics theme, the old
  // "Default" name) → System-D.
  TEST_ASSERT_EQUAL_INT(0, theme::find("no-such-theme"));
  TEST_ASSERT_EQUAL_INT(0, theme::find("EngineMetrics"));
  TEST_ASSERT_EQUAL_INT(0, theme::find("Default"));
  TEST_ASSERT_EQUAL_INT(2, pages::themePages());

  pages::Settings s{2000, 0, 0};
  using H = pages::SettingsHit;
  // The visible ‹ and › must fall in their own zones (this was wrong: both
  // sat in the left half, so tapping › lowered the rate).
  TEST_ASSERT_EQUAL(H::POLL_PREV, pages::settingsHit(14 * 10 + 5, 30, s).kind);  // on ‹
  TEST_ASSERT_EQUAL(H::POLL_NEXT, pages::settingsHit(33 * 10 + 5, 30, s).kind);  // on ›
  TEST_ASSERT_EQUAL(H::POLL_PREV, pages::settingsHit(50, 5, s).kind);    // anywhere in the box, left
  TEST_ASSERT_EQUAL(H::POLL_NEXT, pages::settingsHit(400, 70, s).kind);  // … right, bottom edge
  H h = pages::settingsHit(30, 5 * 24 + 5, s);                            // first list row, left column
  TEST_ASSERT_EQUAL(H::THEME, h.kind);
  TEST_ASSERT_EQUAL_INT(0, h.theme);
  TEST_ASSERT_EQUAL_INT(13 + 2, pages::settingsHit(300, 7 * 24, s).theme);  // right column, 3rd row
  s.list_page = 1;
  TEST_ASSERT_EQUAL_INT(26, pages::settingsHit(30, 5 * 24, s).theme);
  TEST_ASSERT_EQUAL(H::NONE, pages::settingsHit(300, 17 * 24, s).kind);   // past the last theme
  TEST_ASSERT_EQUAL(H::PAGE_PREV, pages::settingsHit(20, 18 * 24 + 3, s).kind);
  TEST_ASSERT_EQUAL(H::DONE, pages::settingsHit(240, 18 * 24 + 3, s).kind);
  TEST_ASSERT_EQUAL(H::PAGE_NEXT, pages::settingsHit(470, 18 * 24 + 3, s).kind);
  TEST_ASSERT_EQUAL(H::PAGE_NEXT, pages::settingsHit(442, 475, s).kind);   // a little low: bottom edge
  TEST_ASSERT_EQUAL(H::DONE, pages::settingsHit(27, 468, s).kind);         // ≡ corner
  TEST_ASSERT_EQUAL(H::PAGE_PREV, pages::settingsHit(140, 470, s).kind);   // bottom edge, left third
  TEST_ASSERT_TRUE(pages::isSettingsCorner(20, 470));
  TEST_ASSERT_FALSE(pages::isSettingsCorner(20, 440));   // row above the footer
  TEST_ASSERT_FALSE(pages::isSettingsCorner(300, 470));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_overview_matches_golden);
  RUN_TEST(test_graphs_matches_golden);
  RUN_TEST(test_every_row_is_48_columns);
  RUN_TEST(test_empty_view_says_no_data);
  RUN_TEST(test_settings_screen);
  RUN_TEST(test_themes_and_hits);
  RUN_TEST(test_brand_is_red_in_every_theme);
  return UNITY_END();
}
