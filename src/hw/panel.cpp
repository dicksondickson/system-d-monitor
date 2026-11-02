#include "panel.h"

#include <vector>

#include "board_pins.h"

namespace panel {

namespace {
// Timings from spec.md §2 (same as Arduino_GFX's 4848S040 config).
constexpr uint16_t HSYNC_POL = 1, HSYNC_FP = 10, HSYNC_PW = 8, HSYNC_BP = 50;
constexpr uint16_t VSYNC_POL = 1, VSYNC_FP = 10, VSYNC_PW = 8, VSYNC_BP = 20;
constexpr int32_t PCLK_HZ = 12000000;
// Bounce buffers: the LCD DMA streams from two 10-line buffers in internal
// SRAM, refilled by the CPU from the PSRAM framebuffer. Without them the DMA
// reads PSRAM directly and underruns when Wi-Fi traffic and framebuffer
// writes compete for PSRAM bandwidth, which shows as flicker every poll.
constexpr size_t BOUNCE_PX = SCREEN_W * 10;

// This board's glass shows inverted colours unless it gets an explicit
// INVOFF (0x20), which Arduino_GFX's type9 sequence doesn't send (display
// diag, 2026-11-01: 0x21 and MADCTL=BGR change nothing, 0x20 fixes it).
// See docs/hardware-bringup.md.
const uint8_t FIXUPS[] = {
    WRITE_COMMAND_8, 0x20,  // INVOFF
};
// Insert point: type9's colour-mode command, which runs on command page 0
// just before Sleep Out.
const uint8_t ANCHOR[] = {WRITE_C8_D8, 0x3A, 0x60};

std::vector<uint8_t> g_init;  // type9 + fixups; must outlive gfx->begin()
Arduino_DataBus* g_spi = nullptr;

// Returns false if the anchor isn't found exactly once (Arduino_GFX changed
// type9); the caller then falls back to a delayed re-send.
bool buildInitSequence() {
  const uint8_t* src = st7701_type9_init_operations;
  const size_t n = sizeof(st7701_type9_init_operations);
  size_t at = n, hits = 0;
  for (size_t i = 0; i + sizeof(ANCHOR) <= n; i++) {
    if (memcmp(src + i, ANCHOR, sizeof(ANCHOR)) == 0) {
      at = i;
      hits++;
    }
  }
  g_init.assign(src, src + n);
  if (hits != 1) return false;
  g_init.insert(g_init.begin() + at, FIXUPS, FIXUPS + sizeof(FIXUPS));
  return true;
}

void resendFixups() {
  const uint8_t ops[] = {
      BEGIN_WRITE,
      WRITE_COMMAND_8, 0xFF, WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,  // page 0
      WRITE_COMMAND_8, 0x20,  // INVOFF
      END_WRITE,
  };
  g_spi->batchOperation(ops, sizeof(ops));
}
}  // namespace

Arduino_RGB_Display* begin() {
  using namespace pins;
  bool spliced = buildInitSequence();
  auto* spi = g_spi = new Arduino_SWSPI(GFX_NOT_DEFINED /* DC */, LCD_CS, LCD_SCK, LCD_MOSI,
                                        GFX_NOT_DEFINED /* MISO */);
  auto* rgb = new Arduino_ESP32RGBPanel(
      LCD_DE, LCD_VSYNC, LCD_HSYNC, LCD_PCLK,
      LCD_R[0], LCD_R[1], LCD_R[2], LCD_R[3], LCD_R[4],
      LCD_G[0], LCD_G[1], LCD_G[2], LCD_G[3], LCD_G[4], LCD_G[5],
      LCD_B[0], LCD_B[1], LCD_B[2], LCD_B[3], LCD_B[4],
      HSYNC_POL, HSYNC_FP, HSYNC_PW, HSYNC_BP,
      VSYNC_POL, VSYNC_FP, VSYNC_PW, VSYNC_BP,
      0 /* pclk_active_neg */, PCLK_HZ, false /* useBigEndian */,
      0 /* de_idle_high */, 0 /* pclk_idle_high */, BOUNCE_PX);
  auto* gfx = new Arduino_RGB_Display(
      SCREEN_W, SCREEN_H, rgb, 0 /* rotation: native, spec §2 */, true /* auto_flush */,
      spi, GFX_NOT_DEFINED /* RST */, g_init.data(), g_init.size());
  if (!gfx->begin()) return nullptr;
  if (!spliced) {
    Serial.println("panel: type9 anchor not found, re-sending fixups after init");
  }
  // Re-send after the RGB bus is up: the form proven on the panel
  // (docs/hardware-bringup.md §2.2); the copy in the init sequence is
  // belt and braces.
  delay(50);
  resendFixups();
  return gfx;
}

Arduino_DataBus* initBus() { return g_spi; }

}  // namespace panel
