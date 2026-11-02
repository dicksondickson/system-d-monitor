// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace backlight {

// LEDC PWM on GPIO38 at 1 kHz, starts off. Call after panel::begin().
void begin();
void set(uint8_t level);      // 0 = off … 255 = full

}  // namespace backlight
