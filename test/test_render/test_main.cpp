// Renders both pages from the demo view and compares the text dump with the
// golden files (spec.md §7 layout). On mismatch the actual dump is printed.
#include <unity.h>

#include <cstdio>
#include <sstream>
#include <string>

#include "golden.h"
#include "gfx/grid.h"
#include "ui/demo_view.h"
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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_overview_matches_golden);
  RUN_TEST(test_graphs_matches_golden);
  RUN_TEST(test_every_row_is_48_columns);
  RUN_TEST(test_empty_view_says_no_data);
  return UNITY_END();
}
