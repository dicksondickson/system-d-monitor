// Display diagnostic round 6 (archived, see README.md in this folder).
//
// Round 5: re-sending INVON (0x21) + MADCTL=BGR never changed anything, so
// the theory is now: the panel powers up *inverted* (0x21 is a no-op; 0x20
// INVOFF is the fix) and MADCTL's BGR bit doesn't apply to the RGB bus (the
// R/B swap must be fixed in the pin map). Phases loop, 4 s each:
//
//   A  send 0x20 INVOFF, colours drawn as-is
//   B  send 0x20 INVOFF, R/B swapped in software (= swapping the R/B pins)
//   C  send 0x21 INVON,  colours drawn as ~swapRB(c) (pure software fix,
//      correct if the panel ignores every command — the control)
//
// Result 2026-11-01: reported "B correct". INVOFF was confirmed, but the
// R/B swap was not: swapping the pins afterwards made red and blue come out
// swapped, so the original pin order + INVOFF (i.e. phase A) is the real
// fix.

#include <Arduino.h>

#include "board_pins.h"
#include "hw/backlight.h"
#include "hw/panel.h"

namespace {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
uint16_t swapRB(uint16_t c) {
  return ((c & 0x001F) << 11) | (c & 0x07E0) | ((c & 0xF800) >> 11);
}

enum class Xform { None, SwapRB, InvSwapRB };

struct Phase {
  char letter;
  uint8_t inv_cmd;
  Xform xf;
  const char* name;
};

const Phase PHASES[] = {
    {'A', 0x20, Xform::None, "INVOFF"},
    {'B', 0x20, Xform::SwapRB, "INVOFF + swapRB"},
    {'C', 0x21, Xform::InvSwapRB, "INVON + ~swapRB"},
};

Arduino_RGB_Display* gfx = nullptr;

uint16_t apply(Xform xf, uint16_t c) {
  switch (xf) {
    case Xform::SwapRB: return swapRB(c);
    case Xform::InvSwapRB: return (uint16_t)~swapRB(c);
    default: return c;
  }
}

void sendInv(uint8_t cmd) {
  const uint8_t ops[] = {
      BEGIN_WRITE,
      WRITE_COMMAND_8, 0xFF, WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
      WRITE_COMMAND_8, cmd,
      END_WRITE,
  };
  panel::initBus()->batchOperation(ops, sizeof(ops));
}

void drawPhase(const Phase& p) {
  struct Bar { uint16_t colour, text; const char* name; };
  const Bar bars[] = {{rgb565(255, 0, 0), rgb565(255, 255, 255), "RED"},
                      {rgb565(0, 255, 0), rgb565(0, 0, 0), "GREEN"},
                      {rgb565(0, 0, 255), rgb565(255, 255, 255), "BLUE"},
                      {rgb565(255, 255, 255), rgb565(0, 0, 0), "WHITE"}};
  auto c = [&](uint16_t v) { return apply(p.xf, v); };
  gfx->fillScreen(c(rgb565(0, 0, 0)));
  for (int i = 0; i < 4; i++) {
    gfx->fillRect(0, i * 80, SCREEN_W, 80, c(bars[i].colour));
    gfx->setTextSize(4);
    gfx->setTextColor(c(bars[i].text));
    gfx->setCursor(20, i * 80 + 24);
    gfx->print(bars[i].name);
  }
  gfx->setTextColor(c(rgb565(255, 255, 255)));
  gfx->setTextSize(12);
  gfx->setCursor(SCREEN_W / 2 - 36, 350);
  gfx->print(p.letter);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n\ndisplay diag round 6 (INVOFF / pin swap)");
  gfx = panel::begin();
  if (!gfx) {
    Serial.println("FATAL panel begin failed");
    return;
  }
  backlight::begin();
  backlight::set(200);
}

void loop() {
  if (!gfx) return;
  for (const Phase& p : PHASES) {
    Serial.printf("phase %c: %s\n", p.letter, p.name);
    sendInv(p.inv_cmd);
    drawPhase(p);
    delay(4000);
  }
}
