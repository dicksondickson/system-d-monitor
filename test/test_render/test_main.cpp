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
  TEST_ASSERT_TRUE(d.find("\xE2\x96\xB6nord") != std::string::npos);  // ▶ marks the current theme
}

static void test_themes_and_hits() {
  TEST_ASSERT_EQUAL_INT(43, theme::count());  // EngineMetrics + btop Default + 41
  TEST_ASSERT_EQUAL_INT(0, theme::find("EngineMetrics"));
  TEST_ASSERT_EQUAL_STRING("Default", theme::get(1).name);
  TEST_ASSERT_TRUE(theme::find("tokyo-night") > 1);
  TEST_ASSERT_EQUAL_INT(0, theme::find("no-such-theme"));
  TEST_ASSERT_EQUAL_INT(2, pages::themePages());

  pages::Settings s{2000, 0, 0};
  using H = pages::SettingsHit;
  TEST_ASSERT_EQUAL(H::POLL_PREV, pages::settingsHit(50, 30, s).kind);    // row 1, left half
  TEST_ASSERT_EQUAL(H::POLL_NEXT, pages::settingsHit(400, 30, s).kind);   // row 1, right half
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
  return UNITY_END();
}
