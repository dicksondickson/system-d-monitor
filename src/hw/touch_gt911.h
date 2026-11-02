// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

// Minimal polled GT911 driver: INT/RST aren't wired on this board, so we
// probe both I²C addresses and read the first touch point on demand.
namespace touch {

struct Point {
  bool fresh;  // the GT911 had a new report (false: nothing new, keep last state)
  bool down;   // finger on the panel (only meaningful when fresh)
  uint16_t x, y;
};

struct Info {
  uint8_t addr;          // 0x5D or 0x14, 0 if not found
  char product[5];       // "911" on a healthy chip
  uint16_t max_x, max_y; // resolution from the GT911 config block
};

bool begin(Info* info);  // false if no GT911 answers
Point read();            // call at ~30 Hz

}  // namespace touch
