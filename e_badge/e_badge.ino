#include <Arduino.h>
#include <LittleFS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <TJpg_Decoder.h>
#include "MatrixScroller.h"

constexpr int TFT_SCLK = 4;
constexpr int TFT_MOSI = 5;
constexpr int TFT_DC = 6;
constexpr int TFT_CS = 7;
constexpr int TFT_RST = 8;
constexpr char PHOTO_PATH[] = "/david_gc9a01_240.jpg";

Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);

bool drawJpegBlock(int16_t x, int16_t y, uint16_t width,
                   uint16_t height, uint16_t* pixels) {
  if (y >= tft.height()) return false;
  tft.drawRGBBitmap(x, y, pixels, width, height);
  return true;
}

// Keep the original LittleFS photo as the startup screen.
void showStartupPhoto() {
  SPI.begin(TFT_SCLK, -1, TFT_MOSI);
  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(GC9A01A_BLACK);

  // Do not format if mounting fails.
  if (!LittleFS.begin(false)) {
    Serial.println("ERROR: LittleFS mount failed.");
    return;
  }

  Serial.println("LittleFS mounted successfully.");

  File photo = LittleFS.open(PHOTO_PATH, "r");
  if (!photo) {
    Serial.println("ERROR: Photo not found.");
    return;
  }

  Serial.printf("Photo found: %lu bytes\n",
                static_cast<unsigned long>(photo.size()));
  photo.close();

  TJpgDec.setJpgScale(1);
  // Adafruit drawRGBBitmap expects native RGB565 values.
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(drawJpegBlock);

  uint16_t width = 0, height = 0;
  JRESULT result = TJpgDec.getFsJpgSize(&width, &height, PHOTO_PATH, LittleFS);
  if (result != JDR_OK) {
    Serial.printf("ERROR: JPEG header failed (code %d).\n", int(result));
    return;
  }
  Serial.printf("JPEG dimensions: %u x %u\n", width, height);
  if (width != 240 || height != 240) {
    Serial.println("ERROR: Expected a 240 x 240 JPEG.");
    return;
  }

  result = TJpgDec.drawFsJpg(0, 0, PHOTO_PATH, LittleFS);
  if (result != JDR_OK) {
    Serial.printf("ERROR: JPEG rendering failed (code %d).\n", int(result));
    return;
  }
  Serial.println("Photo rendered successfully.");
}

#include "BadgeReceiver.h"

void setup() {
  Serial.begin(115200);
  MatrixScroller::begin();
  showStartupPhoto();
  BadgeReceiver::begin();
}

void loop() {
  MatrixScroller::update();
  BadgeReceiver::update();
  delay(1);
}
