#pragma once
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <AnimatedGIF.h>
#include "BadgeSecrets.h"

// GIF bytes and compositing buffers live in the board's 8 MB OPI PSRAM.
namespace BadgeReceiver {
constexpr size_t MAX_GIF = 4 * 1024 * 1024;
constexpr size_t FRAME_BYTES = 240 * 240 * sizeof(uint16_t);
AsyncWebServer server(80);
AnimatedGIF decoder;
SemaphoreHandle_t mutex;
QueueHandle_t textQueue;
struct Message { char text[161]; };
struct Upload { uint8_t* data; size_t size; size_t received; int error; };
uint8_t* pending = nullptr;
size_t pendingSize = 0;
uint8_t* active = nullptr;
size_t activeSize = 0;
uint16_t* frame = nullptr;
uint16_t* saved = nullptr;
bool opened = false, firstLine = false;
bool serverStarted = false;
int mode = 0; // fit=0, full/cover=1, native=2
int requestedMode = 0;
uint32_t nextFrame = 0;
float scale = 1;
int originX = 0, originY = 0;
int previousDisposal = 0, previousX = 0, previousY = 0, previousW = 0, previousH = 0;
uint16_t previousBackground = 0;

void fillRect(int x, int y, int w, int h, uint16_t color) {
  int x0 = max(x, 0), y0 = max(y, 0), x1 = min(x + w, 240), y1 = min(y + h, 240);
  for (int row = y0; row < y1; ++row)
    for (int col = x0; col < x1; ++col) frame[row * 240 + col] = color;
}
void draw(GIFDRAW* line) {
  if (firstLine) {
    if (previousDisposal == 2) fillRect(previousX, previousY, previousW, previousH, previousBackground);
    if (previousDisposal == 3) memcpy(frame, saved, FRAME_BYTES);
    if (line->ucDisposalMethod == 3) memcpy(saved, frame, FRAME_BYTES);
    previousDisposal = line->ucDisposalMethod;
    previousX = originX + int(line->iX * scale);
    previousY = originY + int(line->iY * scale);
    previousW = originX + int((line->iX + line->iWidth) * scale) - previousX;
    previousH = originY + int((line->iY + line->iHeight) * scale) - previousY;
    previousBackground = line->pPalette[line->ucBackground];
    firstLine = false;
  }
  int y0 = originY + int((line->iY + line->y) * scale);
  int y1 = originY + int((line->iY + line->y + 1) * scale);
  for (int x = 0; x < line->iWidth; ++x) {
    uint8_t index = line->pPixels[x];
    if (line->ucHasTransparency && index == line->ucTransparent) continue;
    int x0 = originX + int((line->iX + x) * scale);
    int x1 = originX + int((line->iX + x + 1) * scale);
    fillRect(x0, y0, x1 - x0, y1 - y0, line->pPalette[index]);
  }
}
void reopen() {
  if (opened) decoder.close();
  opened = active && decoder.open(active, activeSize, draw);
  if (!opened) { Serial.println("GIF: unable to decode upload"); return; }
  int w = decoder.getCanvasWidth(), h = decoder.getCanvasHeight();
  scale = mode == 2 ? 1.0f : mode == 1 ? max(240.0f / w, 240.0f / h) : min(240.0f / w, 240.0f / h);
  originX = (240 - int(w * scale)) / 2;
  originY = (240 - int(h * scale)) / 2;
  memset(frame, 0, FRAME_BYTES);
  memset(saved, 0, FRAME_BYTES);
  previousDisposal = 0;
  nextFrame = millis();
  Serial.printf("GIF: %dx%d opened\n", w, h);
}
void respond(AsyncWebServerRequest* req, int code, const char* error) {
  req->send(code, "application/json", String("{\"error\":\"") + error + "\"}");
}
void begin() {
  mutex = xSemaphoreCreateMutex();
  textQueue = xQueueCreate(1, sizeof(Message));
  frame = static_cast<uint16_t*>(ps_malloc(FRAME_BYTES));
  saved = static_cast<uint16_t*>(ps_malloc(FRAME_BYTES));
  Serial.printf("PSRAM: total=%u bytes, free=%u bytes\n",
                unsigned(ESP.getPsramSize()), unsigned(ESP.getFreePsram()));
  if (!mutex || !textQueue) { Serial.println("Receiver allocation failed"); return; }
  if (!frame || !saved) Serial.println("GIF unavailable: enable Tools > PSRAM > OPI PSRAM");
  decoder.begin(LITTLE_ENDIAN_PIXELS);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) { req->send(200, "text/plain", "gif-buddy e-badge alive\n"); });
  server.on("/gif", HTTP_GET, [](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    size_t size = pending ? pendingSize : activeSize;
    xSemaphoreGive(mutex);
    req->send(200, "application/json", String("{\"ready\":") + (size ? "true" : "false") + ",\"size\":" + size + ",\"capacity\":" + MAX_GIF + "}");
  });
  server.on("/scale", HTTP_GET, [](AsyncWebServerRequest* req) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (req->hasParam("mode")) {
      String value = req->getParam("mode")->value();
      requestedMode = (value == "full" || value == "fill") ? 1 : value == "native" ? 2 : 0;
    }
    int value = requestedMode;
    xSemaphoreGive(mutex);
    req->send(200, "application/json", String("{\"scale\":\"") + (value == 1 ? "full" : value == 2 ? "native" : "fit") + "\"}");
  });
  server.on("/text", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!req->hasParam("text", true)) { respond(req, 400, "Missing text field"); return; }
    String value = req->getParam("text", true)->value();
    if (value.length() > 160) { respond(req, 413, "Maximum 160 characters"); return; }
    for (size_t i = 0; i < value.length(); ++i) {
      if (value[i] < 32 || value[i] > 126) { respond(req, 400, "Use printable ASCII text"); return; }
    }
    Message msg = {};
    value.toCharArray(msg.text, sizeof(msg.text));
    xQueueOverwrite(textQueue, &msg);
    req->send(200, "application/json", "{\"ok\":true}");
  });
  server.on("/gif", HTTP_POST, [](AsyncWebServerRequest* req) {
    Upload* upload = static_cast<Upload*>(req->_tempObject);
    if (!upload) { respond(req, 400, "Empty upload"); return; }
    if (upload->error) { respond(req, upload->error, "GIF rejected: size, format, or PSRAM capacity"); return; }
    if (upload->received != upload->size || upload->size < 13 ||
        (memcmp(upload->data, "GIF87a", 6) && memcmp(upload->data, "GIF89a", 6))) {
      respond(req, 400, "Invalid GIF"); return;
    }
    int w = upload->data[6] | (upload->data[7] << 8), h = upload->data[8] | (upload->data[9] << 8);
    if (w < 1 || w > MAX_WIDTH || h < 1 || h > 2048) {
      respond(req, 422, "GIF width must be at most 480 pixels; use a smaller rendition"); return;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    free(pending);
    pending = upload->data;
    pendingSize = upload->size;
    upload->data = nullptr; // ownership transfers to the playback loop
    if (req->hasParam("scale")) {
      String value = req->getParam("scale")->value();
      requestedMode = (value == "full" || value == "fill") ? 1 : value == "native" ? 2 : 0;
    }
    xSemaphoreGive(mutex);
    req->send(200, "application/json", String("{\"ok\":true,\"size\":") + upload->size + "}");
  }, nullptr, [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
    if (index == 0) {
      Upload* upload = static_cast<Upload*>(calloc(1, sizeof(Upload)));
      if (!upload) return;
      req->_tempObject = upload; // server frees this POD after onDisconnect
      upload->size = total;
      upload->error = (!total || total > MAX_GIF) ? 413 : (!frame || !saved) ? 503 : 0;
      if (!upload->error) {
        upload->data = static_cast<uint8_t*>(ps_malloc(total));
        if (!upload->data) upload->error = 503;
      }
      req->onDisconnect([upload]() { free(upload->data); });
    }
    Upload* upload = static_cast<Upload*>(req->_tempObject);
    if (!upload || upload->error) return;
    if (index != upload->received || index > upload->size || len > upload->size - index) { upload->error = 400; return; }
    memcpy(upload->data + index, data, len);
    upload->received += len;
  });
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("gif-buddy");
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}
void update() {
  if (!mutex || !textQueue) return;
  static bool connected = false;
  bool online = WiFi.status() == WL_CONNECTED;
  if (online && !connected) {
    // AsyncTCP requires an initialized network stack. Starting the listener
    // before WiFi initialization can assert on an uninitialized lwIP queue.
    if (!serverStarted) {
      server.begin();
      serverStarted = true;
      Serial.println("HTTP receiver started after Wi-Fi connected");
    }
    Serial.printf("GIF Buddy: http://%s/ (or gif-buddy.local)\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin("gif-buddy")) MDNS.addService("http", "tcp", 80);
  }
  if (!online && connected) MDNS.end();
  connected = online;
  Message msg;
  if (xQueueReceive(textQueue, &msg, 0) == pdTRUE) MatrixScroller::setText(msg.text);
  bool changed = false;
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (pending) {
    if (opened) { decoder.close(); opened = false; }
    free(active);
    active = pending; activeSize = pendingSize;
    pending = nullptr; pendingSize = 0;
    changed = true;
  }
  if (mode != requestedMode) { mode = requestedMode; changed = true; }
  xSemaphoreGive(mutex);
  if (changed) reopen();
  if (opened && int32_t(millis() - nextFrame) >= 0) {
    int duration = 100;
    uint32_t started = millis();
    firstLine = true;
    int result = decoder.playFrame(false, &duration);
    if (result < 0) { decoder.close(); opened = false; Serial.println("GIF decode failed"); return; }
    tft.drawRGBBitmap(0, 0, frame, 240, 240);
    nextFrame = started + max(duration, 20);
    if (result == 0) { decoder.reset(); previousDisposal = 0; memset(frame, 0, FRAME_BYTES); }
  }
}
} // namespace BadgeReceiver
