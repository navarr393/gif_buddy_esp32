#pragma once

#include <Adafruit_GFX.h>
#include <FastLED.h>

namespace MatrixScroller {
constexpr uint8_t DATA_PIN = 13;
constexpr uint8_t BRIGHTNESS = 50;
constexpr uint8_t WIDTH = 8;
constexpr uint8_t HEIGHT = 8;
constexpr uint16_t SCROLL_INTERVAL_MS = 90;
char text[161] = "DAVID - IT - SWE - 18330";


// Default: LED 0 at top left, with alternating horizontal rows.
// Adjust these if the panel's mounting/wiring has a different orientation.
constexpr bool COLUMN_SERPENTINE = false;
constexpr bool FLIP_X = false;
constexpr bool FLIP_Y = false;
constexpr uint8_t ROTATION = 0;  // 0, 1, 2, 3 quarter turns

CRGB leds[WIDTH * HEIGHT];
GFXcanvas1 canvas(WIDTH, HEIGHT);
int16_t textX = WIDTH;
uint32_t lastFrameMs = 0;

uint16_t pixelIndex(uint8_t x, uint8_t y) {
  if (FLIP_X) x = WIDTH - 1 - x;
  if (FLIP_Y) y = HEIGHT - 1 - y;
  if (COLUMN_SERPENTINE) {
    return x * HEIGHT + ((x & 1) ? HEIGHT - 1 - y : y);
  }
  return y * WIDTH + ((y & 1) ? WIDTH - 1 - x : x);
}

void begin() {
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, WIDTH * HEIGHT);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);
  canvas.setRotation(ROTATION);
  canvas.setTextWrap(false);
  canvas.setTextSize(1);
  canvas.setTextColor(1);
}

void setText(const char* value) {
  snprintf(text, sizeof(text), "%s", value);
  textX = WIDTH;
}

void update() {
  const uint32_t now = millis();
  if (now - lastFrameMs < SCROLL_INTERVAL_MS) return;
  lastFrameMs = now;

  canvas.fillScreen(0);
  canvas.setCursor(textX, 0);
  canvas.print(text);
  for (uint8_t y = 0; y < HEIGHT; ++y) {
    for (uint8_t x = 0; x < WIDTH; ++x) {
      // Read physical canvas bits so ROTATION also rotates the LED output.
      const bool lit = canvas.getBuffer()[y * ((WIDTH + 7) / 8) + x / 8]
                       & (0x80 >> (x & 7));
      leds[pixelIndex(x, y)] = lit
                                  ? CRGB::Red : CRGB::Black;
    }
  }
  FastLED.show();
  if (--textX < -static_cast<int16_t>(strlen(text) * 6)) textX = WIDTH;
}
}  // namespace MatrixScroller
