#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <Arduino_GFX_Library.h>
#include <AnimatedGIF.h>

#include "gengar_frames.h"
#include "secrets.h"

#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK  38
#define LCD_RESET 2
#define LCD_CS    12

#define LCD_WIDTH  466
#define LCD_HEIGHT 466

#define FRAME_DELAY_MS 60

#define MDNS_HOSTNAME "gif-buddy"
#define HTTP_PORT     80

#define GIF_MAX_BYTES (4 * 1024 * 1024)  // 4 MB cap per GIF 

// Debug: paint a fixed high-contrast pattern once and flush it in a tight loop
// at the same cadence the flickering GIFs run at. If THIS flickers, the cause
// is panel-layer (tearing / flush vs scan race) and GIFDraw is innocent.
// Set to 0 to restore normal GIF playback.
#define DEBUG_STATIC_FLUSH    0
#define DEBUG_FLUSH_PERIOD_MS 70

// Drive the CO5300 over QSPI at 80 MHz (default in the lib is 40 MHz). Halves
// flush time from ~39 ms to ~20 ms so each flush is closer to one panel scan
// period — reduces visible tearing on fast-changing frames.
#define QSPI_CLOCK_HZ 80000000

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS,
    LCD_SCLK,
    LCD_SDIO0,
    LCD_SDIO1,
    LCD_SDIO2,
    LCD_SDIO3);

Arduino_CO5300 *output = new Arduino_CO5300(
    bus,
    LCD_RESET,
    0,
    LCD_WIDTH,
    LCD_HEIGHT,
    6, 0, 0, 0);

Arduino_Canvas *gfx = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, output);

AsyncWebServer server(HTTP_PORT);
AnimatedGIF    gif;

static uint8_t       *gifBuf            = nullptr;
static size_t         gifSize           = 0;
static bool           gifReady          = false;
static const char    *gifUploadError    = nullptr;
static bool           gifOpened         = false;
static volatile bool  gifNeedsReopen    = false;
static volatile bool  gifBusyUploading  = false;
static int16_t        gifOriginX        = 0;
static int16_t        gifOriginY        = 0;
static uint32_t       gifScaleQ8        = 256;  // 8.8 fixed-point, 256 == 1.0x
static uint8_t        gifPrevDisposal   = 0;    // disposal method of last frame
static uint16_t      *gifFB             = nullptr; // direct ptr into canvas PSRAM framebuffer
static int16_t        gifSrcXForDst[LCD_WIDTH];   // dst-X → src-canvas-X (precomputed per open)

static const uint8_t *const gengar_frames[GENGAR_FRAMES] = {
    gengar_frame_00_map, gengar_frame_01_map, gengar_frame_02_map, gengar_frame_03_map,
    gengar_frame_04_map, gengar_frame_05_map, gengar_frame_06_map, gengar_frame_07_map,
    gengar_frame_08_map, gengar_frame_09_map, gengar_frame_10_map, gengar_frame_11_map,
    gengar_frame_12_map, gengar_frame_13_map, gengar_frame_14_map, gengar_frame_15_map,
    gengar_frame_16_map, gengar_frame_17_map, gengar_frame_18_map, gengar_frame_19_map,
};

static const int16_t GENGAR_X = (LCD_WIDTH  - GENGAR_W) / 2;
static const int16_t GENGAR_Y = (LCD_HEIGHT - GENGAR_H) / 2;

static void GIFDraw(GIFDRAW *pDraw) {
  // At the start of each frame, apply the PREVIOUS frame's disposal method.
  // Disposal 2 = restore to background — clear before drawing the new frame.
  if (pDraw->y == 0) {
    if (gifPrevDisposal == 2 && gifFB) {
      memset(gifFB, 0, (size_t)LCD_WIDTH * LCD_HEIGHT * 2);
    }
    gifPrevDisposal = pDraw->ucDisposalMethod;
  }

  uint8_t  *s        = pDraw->pPixels;
  uint16_t *palette  = pDraw->pPalette;
  int       iWidth   = pDraw->iWidth;
  int       sy       = pDraw->iY + pDraw->y;
  uint32_t  scale    = gifScaleQ8;

  // Destination Y span for this source line.
  int dstY0 = gifOriginY + (int)((sy        * scale) >> 8);
  int dstY1 = gifOriginY + (int)(((sy + 1)  * scale) >> 8);
  if (dstY0 < 0) dstY0 = 0;
  if (dstY1 > LCD_HEIGHT) dstY1 = LCD_HEIGHT;
  if (dstY1 <= dstY0) return;

  // Destination X span for the dirty rect.
  int dstX0 = gifOriginX + (int)((pDraw->iX            * scale) >> 8);
  int dstX1 = gifOriginX + (int)(((pDraw->iX + iWidth) * scale) >> 8);
  if (dstX0 < 0) dstX0 = 0;
  if (dstX1 > LCD_WIDTH) dstX1 = LCD_WIDTH;
  int dstW = dstX1 - dstX0;
  if (dstW <= 0 || !gifFB) return;

  static uint16_t lineBuf[LCD_WIDTH];

  if (pDraw->ucHasTransparency) {
    uint8_t transparent = pDraw->ucTransparent;
    for (int dy = dstY0; dy < dstY1; dy++) {
      uint16_t *row = gifFB + (uint32_t)dy * LCD_WIDTH + dstX0;
      for (int dx = 0; dx < dstW; dx++) {
        int srcIdx = gifSrcXForDst[dstX0 + dx] - pDraw->iX;
        if (srcIdx < 0 || srcIdx >= iWidth) continue;
        uint8_t pix = s[srcIdx];
        if (pix == transparent) continue;
        row[dx] = palette[pix];
      }
    }
  } else {
    for (int dx = 0; dx < dstW; dx++) {
      int srcIdx = gifSrcXForDst[dstX0 + dx] - pDraw->iX;
      if (srcIdx < 0) srcIdx = 0;
      if (srcIdx >= iWidth) srcIdx = iWidth - 1;
      lineBuf[dx] = palette[s[srcIdx]];
    }
    size_t rowBytes = (size_t)dstW * 2;
    for (int dy = dstY0; dy < dstY1; dy++) {
      memcpy(gifFB + (uint32_t)dy * LCD_WIDTH + dstX0, lineBuf, rowBytes);
    }
  }
}

static void connectWiFi() {
  Serial.printf("WiFi: connecting to \"%s\"\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi: connected, IP=%s  RSSI=%d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("WiFi: FAILED to connect");
  }
}

static void startMDNS() {
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", HTTP_PORT);
    Serial.printf("mDNS: http://%s.local/\n", MDNS_HOSTNAME);
  } else {
    Serial.println("mDNS: FAILED");
  }
}

static void startServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    String body = "gif-buddy alive\n";
    body += "ip=" + WiFi.localIP().toString() + "\n";
    body += "rssi=" + String(WiFi.RSSI()) + " dBm\n";
    req->send(200, "text/plain", body);
  });

  server.on("/gif", HTTP_GET, [](AsyncWebServerRequest *req) {
    String resp = "{\"ready\":";
    resp += gifReady ? "true" : "false";
    resp += ",\"size\":" + String((uint32_t)gifSize);
    resp += ",\"capacity\":" + String((uint32_t)GIF_MAX_BYTES);
    resp += "}";
    req->send(200, "application/json", resp);
  });

  server.on(
      "/gif", HTTP_POST,
      [](AsyncWebServerRequest *req) {
        if (gifUploadError) {
          const char *err = gifUploadError;
          gifUploadError = nullptr;
          req->send(413, "text/plain", err);
          return;
        }
        String resp = "{\"ok\":true,\"size\":" + String((uint32_t)gifSize) + "}";
        req->send(200, "application/json", resp);
      },
      nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len,
         size_t index, size_t total) {
        if (!gifBuf) {
          gifUploadError = "psram buffer not allocated";
          return;
        }
        if (total == 0 || total > GIF_MAX_BYTES) {
          gifUploadError = "payload too large or empty";
          return;
        }
        if (index == 0) {
          gifBusyUploading = true;
          gifReady = false;
          gifSize = 0;
          gifUploadError = nullptr;
          Serial.printf("/gif: upload start, total=%u bytes\n",
                        (unsigned)total);
        }
        memcpy(gifBuf + index, data, len);
        gifSize = index + len;
        if (gifSize == total) {
          gifReady = true;
          gifNeedsReopen = true;
          gifBusyUploading = false;
          Serial.printf("/gif: upload complete, %u bytes\n",
                        (unsigned)gifSize);
        }
      });

  server.onNotFound([](AsyncWebServerRequest *req) {
    req->send(404, "text/plain", "not found\n");
  });

  server.begin();
  Serial.printf("HTTP: listening on :%d\n", HTTP_PORT);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("gif-buddy starting");

  if (!gfx->begin(QSPI_CLOCK_HZ)) {
    Serial.println("gfx->begin() FAILED");
  }
  output->setBrightness(200);
  gifFB = gfx->getFramebuffer();

  gfx->fillScreen(0x0000);
  gfx->flush();

#if DEBUG_STATIC_FLUSH
  // Test 1b: alternate two solid patterns every flush at the same cadence as the
  // flickering GIF. If THIS tears (you see partial/mixed frames), the flush vs.
  // panel-scan race is the cause. No GIFDraw involved.
  Serial.printf("DEBUG_STATIC_FLUSH: alternating-pattern test, period %d ms\n",
                DEBUG_FLUSH_PERIOD_MS);
#endif

  gifBuf = (uint8_t *)ps_malloc(GIF_MAX_BYTES);
  if (!gifBuf) {
    Serial.printf("PSRAM: ps_malloc(%u) FAILED\n", (unsigned)GIF_MAX_BYTES);
  } else {
    Serial.printf("PSRAM: gif buffer allocated, %u bytes\n",
                  (unsigned)GIF_MAX_BYTES);
  }

  gif.begin(GIF_PALETTE_RGB565_LE);

  connectWiFi();
  startMDNS();
  startServer();
}

static void playGengarFallback() {
  for (int i = 0; i < GENGAR_FRAMES; i++) {
    if (gifNeedsReopen || gifBusyUploading) return;
    gfx->fillScreen(0x0000);
    gfx->draw16bitRGBBitmap(
        GENGAR_X,
        GENGAR_Y,
        (uint16_t *)gengar_frames[i],
        GENGAR_W,
        GENGAR_H);
    gfx->flush();
    delay(FRAME_DELAY_MS);
  }
}

void loop() {
#if DEBUG_STATIC_FLUSH
  // Diagnostic: alternate two visually distinct patterns each flush.
  // Pattern A: red top / blue bottom, sharp seam at center.
  // Pattern B: blue top / red bottom (the inverse).
  // If the panel tears during flush vs. its own scan-out, the seam will
  // wobble or you'll briefly see "all-red" / "all-blue" mid-frames.
  static uint32_t flushCount = 0;
  if (gifFB) {
    bool flip = (flushCount & 1) != 0;
    uint16_t topColor    = flip ? 0x001F : 0xF800; // blue / red
    uint16_t bottomColor = flip ? 0xF800 : 0x001F; // red  / blue
    for (int y = 0; y < LCD_HEIGHT; y++) {
      uint16_t color = (y < LCD_HEIGHT / 2) ? topColor : bottomColor;
      uint16_t *row = gifFB + (uint32_t)y * LCD_WIDTH;
      for (int x = 0; x < LCD_WIDTH; x++) row[x] = color;
    }
  }
  uint32_t t0 = millis();
  gfx->flush();
  uint32_t flushMs = millis() - t0;
  if ((++flushCount % 50) == 1) {
    Serial.printf("alt flush #%u: %u ms\n",
                  (unsigned)flushCount, (unsigned)flushMs);
  }
  int wait = DEBUG_FLUSH_PERIOD_MS - (int)flushMs;
  if (wait < 1) wait = 1;
  delay(wait);
  return;
#endif

  if (gifBusyUploading) {
    delay(20);
    return;
  }

  if (gifNeedsReopen) {
    if (gifOpened) {
      gif.close();
      gifOpened = false;
    }
    if (gif.open(gifBuf, (int)gifSize, GIFDraw)) {
      int w = gif.getCanvasWidth();
      int h = gif.getCanvasHeight();
      uint32_t sX = ((uint32_t)LCD_WIDTH  << 8) / (uint32_t)w;
      uint32_t sY = ((uint32_t)LCD_HEIGHT << 8) / (uint32_t)h;
      gifScaleQ8 = sX > sY ? sX : sY;        // "cover" — larger axis wins
      int dispW = (int)((w * gifScaleQ8) >> 8);
      int dispH = (int)((h * gifScaleQ8) >> 8);
      gifOriginX = (LCD_WIDTH  - dispW) / 2; // negative when GIF overflows panel
      gifOriginY = (LCD_HEIGHT - dispH) / 2;
      gifPrevDisposal = 0;                   // no previous frame yet
      // Precompute dst-X → src-canvas-X table; saves a 32-bit divide per pixel.
      for (int dx = 0; dx < LCD_WIDTH; dx++) {
        gifSrcXForDst[dx] = (int16_t)(((uint32_t)(dx - gifOriginX) << 8) / gifScaleQ8);
      }
      gfx->fillScreen(0x0000);
      gfx->flush();
      gifOpened = true;
      Serial.printf("gif: opened %dx%d, scaleQ8=%u → %dx%d, origin (%d,%d)\n",
                    w, h, (unsigned)gifScaleQ8,
                    dispW, dispH, gifOriginX, gifOriginY);
    } else {
      Serial.printf("gif: open() failed, err=%d (size=%u)\n",
                    gif.getLastError(), (unsigned)gifSize);
    }
    gifNeedsReopen = false;
  }

  if (gifOpened) {
    uint32_t frameStart = millis();
    int delayMs = 0;
    int rc = gif.playFrame(false, &delayMs);
    gfx->flush();
    uint32_t elapsed = millis() - frameStart;
    int wait = delayMs - (int)elapsed;
    static uint8_t logCount = 0;
    if (logCount++ < 30) {
      Serial.printf("frame: target=%d ms, decode+flush=%u ms, wait=%d ms\n",
                    delayMs, (unsigned)elapsed, wait);
    }
    // Min 16ms gap between flushes: gives the AMOLED panel one full scan period
    // to display the freshly-pushed framebuffer cleanly before the next flush
    // starts mid-scan and tears.
    if (wait < 16) wait = 16;
    delay(wait);
    if (rc == 0) {
      gif.reset();
      gfx->fillScreen(0x0000);
    } else if (rc < 0) {
      Serial.println("gif: playFrame error, closing");
      gif.close();
      gifOpened = false;
    }
  } else {
    playGengarFallback();
  }
}
