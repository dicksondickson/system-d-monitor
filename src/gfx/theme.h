#pragma once
#include <stdint.h>

// Colour roles and themes (spec.md §6). Grid cells store a Role; the theme
// maps it to RGB565 at blit time, so switching theme is a full repaint with
// no re-layout. Theme 0 is the EngineMetrics palette
// (https://github.com/0rand/EngineMetrics); the rest are btop's Default +
// bundled themes (https://github.com/aristocratos/btop, Apache-2.0; by
// aristocratos and the theme authors), converted by
// tools/themes/import_btop.py. Arduino-free.
namespace theme {

enum Role : uint8_t {
  BG,
  BORDER,   // box 1 (btop cpu_box); also the default box colour
  TITLE,
  LABEL,
  VALUE,
  DIM,
  GOOD,
  WARN,
  BAD,
  DECODE,   // TG numbers, sparklines, graphs
  PREFILL,  // PP numbers, graphs
  BOX2,     // btop mem_box
  BOX3,     // btop net_box
  BOX4,     // btop proc_box
  BRAND,    // "SYSTEM-D": the same red in every theme (user request)
  ROLE_COUNT
};

struct Theme {
  const char* name;
  uint16_t rgb565[ROLE_COUNT];
};

int count();
const Theme& get(int index);           // wraps around
int find(const char* name);            // index, or 0 if unknown

constexpr uint16_t rgb565(uint32_t rgb) {
  return (uint16_t)((((rgb >> 16) & 0xF8) << 8) | (((rgb >> 8) & 0xFC) << 3) |
                    ((rgb & 0xFF) >> 3));
}

constexpr uint16_t BRAND_RED = rgb565(0xFF3B30);  // fixed, not themed

}  // namespace theme
