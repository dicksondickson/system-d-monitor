#include "theme.h"

#include <string.h>

#include "themes_btop.h"

namespace theme {

namespace {
// The original palette, from EngineMetrics' curses colours. Order = Role.
const Theme ENGINE_METRICS = {
    "EngineMetrics",
    {
        rgb565(0x000000),  // BG
        rgb565(0x00CDCD),  // BORDER  cyan
        rgb565(0x00CDCD),  // TITLE   cyan (bold)
        rgb565(0xCDCD00),  // LABEL   yellow
        rgb565(0xE5E5E5),  // VALUE   white
        rgb565(0x7F7F7F),  // DIM
        rgb565(0x00CD00),  // GOOD
        rgb565(0xCDCD00),  // WARN
        rgb565(0xCD0000),  // BAD
        rgb565(0x00FF5F),  // DECODE  bright green
        rgb565(0xCD00CD),  // PREFILL magenta
        rgb565(0x00CDCD),  // BOX2
        rgb565(0x00CDCD),  // BOX3
        rgb565(0x00CDCD),  // BOX4
    }};
}  // namespace

int count() { return 1 + BTOP_COUNT; }

const Theme& get(int index) {
  index %= count();
  if (index < 0) index += count();
  return index == 0 ? ENGINE_METRICS : BTOP_THEMES[index - 1];
}

int find(const char* name) {
  for (int i = 0; i < count(); i++)
    if (name && strcmp(get(i).name, name) == 0) return i;
  return 0;
}

}  // namespace theme
