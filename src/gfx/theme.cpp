#include "theme.h"

namespace theme {

namespace {
// Order must match enum Role.
const Theme THEMES[COUNT] = {
    {"ansi",
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
     }},
    {"btop",
     {
         rgb565(0x0B0D10),  // BG
         rgb565(0x3D7B46),  // BORDER  muted green
         rgb565(0xEEEEEE),  // TITLE
         rgb565(0x8A8F98),  // LABEL
         rgb565(0xD0D0D0),  // VALUE
         rgb565(0x4A4F57),  // DIM
         rgb565(0x77CA9B),  // GOOD
         rgb565(0xCBC06C),  // WARN
         rgb565(0xDC4C4C),  // BAD
         rgb565(0x77CA9B),  // DECODE
         rgb565(0x4897D4),  // PREFILL
     }},
};
}  // namespace

const Theme& get(int index) { return THEMES[(unsigned)index % COUNT]; }

}  // namespace theme
