#pragma once
#include <stdint.h>

// Colour roles and the two themes from spec.md §6. Grid cells store a Role;
// the theme maps it to RGB565 at blit time, so switching theme is a full
// repaint with no re-layout. Arduino-free.
namespace theme {

enum Role : uint8_t {
  BG,
  BORDER,
  TITLE,
  LABEL,
  VALUE,
  DIM,
  GOOD,
  WARN,
  BAD,
  DECODE,   // TG numbers, sparklines, graphs
  PREFILL,  // PP numbers, sparklines, graphs
  ROLE_COUNT
};

struct Theme {
  const char* name;
  uint16_t rgb565[ROLE_COUNT];
};

constexpr int COUNT = 2;
const Theme& get(int index);  // 0 = ansi (default), 1 = btop

constexpr uint16_t rgb565(uint32_t rgb) {
  return (uint16_t)((((rgb >> 16) & 0xF8) << 8) | (((rgb >> 8) & 0xFC) << 3) |
                    ((rgb & 0xFF) >> 3));
}

}  // namespace theme
