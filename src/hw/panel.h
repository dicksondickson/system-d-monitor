#pragma once
#include <Arduino_GFX_Library.h>

namespace panel {

// Brings up the ST7701 (init over 3-wire SPI) and the RGB bus with a full
// 480×480 RGB565 framebuffer in PSRAM. Returns nullptr on failure.
Arduino_RGB_Display* begin();

// The 3-wire SPI bus used for the ST7701 init sequence (valid after begin).
// Used by the display diagnostics (tools/display-diag/) to send commands
// without restarting the RGB bus.
Arduino_DataBus* initBus();

}  // namespace panel
