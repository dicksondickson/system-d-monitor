// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT
#pragma once
#include <Arduino_GFX_Library.h>

#include "gfx/grid.h"
#include "gfx/theme.h"

// Puts a Grid on the panel: diffs it against what's shown and rasterises
// only the changed cells straight into the PSRAM framebuffer, then writes
// back the cache for the touched cell rows (SPEC.md §6).
namespace screen {

void begin(Arduino_RGB_Display* gfx);

// Returns the number of cells repainted.
int present(const Grid& next, const theme::Theme& t);

// Forces the next present() to repaint everything (e.g. after a theme change).
void invalidate();

}  // namespace screen
