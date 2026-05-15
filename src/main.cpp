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
  uint8_t  *s         = pDraw->pPixels;
  uint16_t *palette   = pDraw->pPalette;
  int       iWidth    = pDraw->iWidth;
  int       lineY     = pDraw->iY + pDraw->y;
  int16_t   dispX     = gifOriginX + pDraw->iX;
  int16_t   dispY     = gifOriginY + lineY;

  if (pDraw->ucHasTransparency) {
    uint8_t transparent = pDraw->ucTransparent;
    for (int x = 0; x < iWidth; x++) {
      uint8_t idx = s[x];
      if (idx != transparent) {
        gfx->drawPixel(dispX + x, dispY, palette[idx]);
      }
    }
  } else {
    static uint16_t lineBuf[LCD_WIDTH];
    int n = iWidth > LCD_WIDTH ? LCD_WIDTH : iWidth;
    for (int x = 0; x < n; x++) {
      lineBuf[x] = palette[s[x]];
    }
    gfx->draw16bitRGBBitmap(dispX, dispY, lineBuf, n, 1);
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

  if (!gfx->begin()) {
    Serial.println("gfx->begin() FAILED");
  }
  output->setBrightness(200);

  gfx->fillScreen(0x0000);
  gfx->flush();

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
      gifOriginX = (LCD_WIDTH  - w) / 2;
      gifOriginY = (LCD_HEIGHT - h) / 2;
      gfx->fillScreen(0x0000);
      gfx->flush();
      gifOpened = true;
      Serial.printf("gif: opened %dx%d, origin (%d,%d)\n",
                    w, h, gifOriginX, gifOriginY);
    } else {
      Serial.println("gif: open() failed");
    }
    gifNeedsReopen = false;
  }

  if (gifOpened) {
    int delayMs = 0;
    int rc = gif.playFrame(false, &delayMs);
    gfx->flush();
    if (delayMs > 0) delay(delayMs);
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
