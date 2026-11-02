#pragma once
// Guition ESP32-S3-4848S040 pin map (spec.md §2, docs/hardware-bringup.md).
// R/B order matches the vendor demo and Arduino_GFX's 86BOX config; the
// snippet in esp32-4848s040-guide.md §3 has them swapped (verified on the
// panel 2026-11-01: with R=4–7,15 red and blue come out swapped).

namespace pins {

// ST7701 3-wire SPI (init sequence only)
constexpr int LCD_CS   = 39;
constexpr int LCD_SCK  = 48;
constexpr int LCD_MOSI = 47;

// RGB parallel bus
constexpr int LCD_DE    = 18;
constexpr int LCD_VSYNC = 17;
constexpr int LCD_HSYNC = 16;
constexpr int LCD_PCLK  = 21;
constexpr int LCD_R[5] = {11, 12, 13, 14, 0};
constexpr int LCD_G[6] = {8, 20, 3, 46, 9, 10};
constexpr int LCD_B[5] = {4, 5, 6, 7, 15};

constexpr int BACKLIGHT = 38;

// GT911 touch (INT/RST not wired → polled)
constexpr int TOUCH_SDA = 19;
constexpr int TOUCH_SCL = 45;

}  // namespace pins

constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 480;
