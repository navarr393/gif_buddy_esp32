# Project Zelda — GIF Buddy e-badge

Display a startup photo or phone-sent GIF on a **240×240 GC9A01 round TFT**, and
scroll phone-sent text on an optional **8×8 WS2812B LED matrix**.

The custom badge uses an **ESP32-S3-WROOM-1-N8R8** (8 MB flash, 8 MB octal PSRAM).
You can also wire a GC9A01 breakout to a development board; see the DevKit guide
below before flashing. The checked-in pin assignments are for the custom badge.

## What it does

- Loads `data/david_gc9a01_240.jpg` from LittleFS as the optional startup photo.
- Receives GIFs from the companion Flutter GIF Buddy app over local Wi-Fi.
- Fits GIFs into 240×240 without stretching; native and fill/crop modes are also available.
- Scrolls received text in **red** on GPIO13 (the current `CRGB::Red` setting). An empty message clears the matrix.
- Accepts up to 160 printable ASCII characters; emoji and newlines are not supported.
- Keeps received GIFs and text in RAM: rebooting restores the startup photo/default text.

The LED brightness currently checked into `MatrixScroller.h` is **50/255**.
For initial hardware testing, set `BRIGHTNESS` to **20**. The default text,
scroll speed, and matrix orientation are also configured in that file.

## 1. Install the tools and libraries

Use Arduino IDE 2.x and install **esp32 by Espressif Systems** in Boards Manager.
If needed, add this URL under Settings/Preferences → Additional Boards Manager URLs:

```text
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

The local firmware build has been checked with ESP32 Arduino core **3.3.4**.
Keep an already-working ESP32 core installation; changing versions is not a
routine flashing step. Be sure to choose the correct chip family—ESP32-S3 and
classic ESP32 use different board selections.

Install these libraries through Library Manager, or their upstream release ZIPs
using Sketch → Include Library → Add .ZIP Library:

| Library | Version used for the local build |
| --- | --- |
| Adafruit GFX Library | 1.12.6 |
| Adafruit GC9A01A | 1.1.1 |
| Adafruit BusIO | 1.17.4 |
| FastLED | 3.10.5 |
| TJpg_Decoder by Bodmer | 1.1.0 |
| AnimatedGIF by Larry Bank | 2.2.0 |
| ESP Async WebServer | 3.6.0 |
| Async TCP | 3.3.2 |

SPI, WiFi, ESPmDNS, and LittleFS come with the ESP32 core. Install dependencies
when prompted. The last two libraries above are the versions copied from the
working GIF Buddy PlatformIO project. Use the ESP32-compatible projects
[ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) and
[AsyncTCP](https://github.com/ESP32Async/AsyncTCP); avoid keeping duplicate copies
of older forks in the Arduino libraries folder. AsyncTCP is the ESP32 library;
ESPAsyncTCP is for ESP8266.

## 2. Open the sketch and configure Wi-Fi

Keep the sketch folder named `e_badge` and open `e_badge.ino`:

```text
e_badge/
├── e_badge.ino
├── BadgeReceiver.h
├── BadgeSecrets.h
├── MatrixScroller.h
└── data/
    └── david_gc9a01_240.jpg
```

Copy `BadgeSecrets.example.h` to `BadgeSecrets.h`, then set your own **2.4 GHz**
network details. The local credentials file is ignored by Git. Its contents are:

```cpp
#pragma once
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
```

The phone/computer and badge must be on a network that allows devices to reach
one another. The Giphy API key belongs to the Flutter app; the badge receives
GIF bytes and does not need that key. Do not distribute your personal Wi-Fi
credentials with a public copy of the project.

## 3. Flash the custom e-badge PCB

Connect the badge with a USB data cable. Select these Arduino IDE settings:

| Setting | Value for the badge |
| --- | --- |
| Board | **ESP32S3 Dev Module** |
| Flash Size | **8MB (64Mb)** |
| Partition Scheme | **8M with spiffs (3MB APP/1.5MB SPIFFS)** |
| PSRAM | **OPI PSRAM** |
| USB CDC On Boot | **Enabled** |
| USB Mode, if shown | **Hardware CDC and JTAG** |
| Erase All Flash Before Sketch Upload | **Disabled** |
| Port | The badge's currently connected USB port |

On macOS, the native USB port is commonly `/dev/cu.usbmodem...`; its suffix can
change after a reset or entering download mode. Select the actual listed port.

1. Click **Verify** to compile.
2. Close Serial Monitor/Plotter, then click **Upload**.
3. Wait for the completed upload, then press **RESET** if the sketch does not start.
4. Open Serial Monitor at **115200 baud**. Check for a nonzero PSRAM capacity,
   Wi-Fi IP address, and `HTTP receiver started after Wi-Fi connected`.
5. If the photo is not already in LittleFS, follow the next section.

The startup photo is optional: the receiver still starts if the filesystem or
photo is missing. The TFT stays blank until a GIF arrives in that case.

### Upload the startup photo to LittleFS

The repository does not include a personal photo. Use your own **240×240 baseline JPEG** named `david_gc9a01_240.jpg` in `data/`.
The firmware opens `/david_gc9a01_240.jpg`; `data` is not part of that device path.

For Arduino IDE 2.x on macOS:

1. Download the `.vsix` from the [LittleFS uploader releases](https://github.com/earlephilhower/arduino-littlefs-upload/releases).
2. Copy it to `~/.arduinoIDE/plugins/` (create that folder if needed).
3. Fully quit and reopen Arduino IDE.
4. Open **this sketch**, select the same board, port, and partition scheme used
   to upload its firmware, and close Serial Monitor.
5. Press **⌘⇧P**, then run **Upload LittleFS to Pico/ESP8266/ESP32**.
   On Windows/Linux, use **Ctrl+Shift+P**; see the uploader's installation guide
   for the platform-specific plugin directory.
6. After upload completes, press RESET.

LittleFS upload writes a filesystem image containing the current sketch's entire
`data/` folder. It replaces the previous filesystem contents—it does not add one
file to the existing files. Switching from the weather sketch therefore requires
re-uploading this photo. Changing the partition scheme also requires rebuilding
and uploading the filesystem at the new location.

**Normal code-only uploads and GIFs sent from the app do not require LittleFS uploads.**
Keep “Erase All Flash” disabled to preserve the existing filesystem.

### If flashing cannot connect

For `No serial data received` or an upload stuck at `Connecting...`:

1. Close Serial Monitor/Plotter and leave USB connected.
2. Hold **BOOT**.
3. While holding BOOT, press and release **RESET/EN**.
4. Release BOOT, reselect the USB port if it changed, and click Upload.
5. After uploading, press RESET/EN with BOOT released.

This enters the ROM downloader even if the existing sketch is repeatedly crashing.
See [Espressif's manual bootloader procedure](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html#manual-bootloader).

## 4. Use a development board instead of the badge PCB

### Choose the board according to the features you need

| Development board | What this firmware can do | Required changes |
| --- | --- | --- |
| ESP32-S3 DevKit with 8 MB flash and 8 MB octal PSRAM | Full GIF receiver, photo, optional LED text | Wire the badge GPIO map; use the badge settings above |
| Classic ESP32 DevKit with a PSRAM-equipped WROVER module | GIF receiver subject to available PSRAM, photo, optional LED text | Change TFT pins and use classic ESP32 settings below |
| Common classic ESP32-WROOM DevKit **without PSRAM** | Startup JPEG and optional LED text; **received GIF playback is unavailable** | Change TFT pins; disable PSRAM in board settings |

**Flash size is not PSRAM size.** Selecting “PSRAM enabled” does not add RAM to a
board without it. This implementation allocates two 240×240 RGB565 buffers plus
uploaded GIF bytes using `ps_malloc`. A standard no-PSRAM WROOM board needs a
separate low-memory/streaming GIF implementation to support GIF reception.
A PSRAM-equipped ESP32-S3 DevKit is the closest substitute for the badge.

Espressif lists both WROOM and WROVER variants of the
[ESP32-DevKitC](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html).
Check the module marking and the board's actual flash/PSRAM capacity.

### GC9A01 breakout wiring

Use a **240×240 SPI GC9A01 breakout**, not an I2C display or the original GIF
Buddy project's QSPI panel. GPIO numbers below are chip GPIO labels, not physical
header positions.

| TFT breakout signal | Custom badge / equivalent ESP32-S3 DevKit | Classic ESP32 DevKit example |
| --- | --- | --- |
| SCL / SCK / CLK | GPIO4 | GPIO18 |
| SDA / DIN / MOSI | GPIO5 | GPIO23 |
| DC | GPIO6 | GPIO27 |
| CS | GPIO7 | GPIO26 |
| RES / RST | GPIO8 | GPIO25 |
| GND | GND | GND |
| VCC | 3.3 V for the badge display | Supply specified by your breakout; 3.3 V for a 3.3 V-compatible module |
| BL / BLK, if present | Follow breakout documentation | Follow breakout documentation; firmware does not control backlight |

The pin labelled SDA on many SPI TFT modules is **MOSI**, not I2C SDA. No MISO
connection is needed. Use 3.3 V logic. Verify the breakout's supply/backlight
requirements; do not assume a bare panel or backlight can be powered directly
from a GPIO. Keep wiring short and share ground with the DevKit.

**Do not copy the badge's GPIO6–8 wiring onto a classic ESP32.** Classic ESP32
GPIO6–11 are normally used by flash. The example above also avoids GPIO16/17,
which are reserved for PSRAM on WROVER modules.

### Change the sketch pins for a classic ESP32

In your DevKit copy of `e_badge.ino`, replace the five TFT constants with:

```cpp
constexpr int TFT_SCLK = 18;
constexpr int TFT_MOSI = 23;
constexpr int TFT_DC = 27;
constexpr int TFT_CS = 26;
constexpr int TFT_RST = 25;
```

No pin changes are needed in `BadgeReceiver.h`: it uses the `tft` instance
configured by the sketch. Keep the original GPIO4–8 values for the custom badge.
For other DevKits, check the board schematic before choosing pins.

### Arduino settings for a classic ESP32 DevKit

- Board: the matching board, or **ESP32 Dev Module** for a generic classic ESP32.
- Flash Size: match the actual board (commonly **4MB**, not the badge's 8MB).
- Partition Scheme: choose an app partition large enough for the Wi-Fi firmware.
  On a 4MB board, **Huge APP (3MB No OTA/1MB SPIFFS)** is a suitable starting point.
- PSRAM: **Enabled** for a compatible WROVER/PSRAM-equipped board;
  **Disabled** for a WROOM board without PSRAM. **OPI PSRAM is the S3 badge setting**,
  not the setting for a classic ESP32.
- Port: the DevKit's USB-to-serial port. Classic boards commonly use CP210x or CH340
  bridges, so macOS port names may differ from the badge's native USB port.
- Erase All Flash: **Disabled**. Native USB CDC settings generally do not apply
  to classic ESP32 DevKits with USB-to-serial bridges.

Then follow the sketch and LittleFS upload steps above using these settings.
If a 4MB-PSRAM board cannot buffer a replacement GIF alongside the current one,
try a smaller GIF or reset first. A 4MB upload limit is a ceiling, not a guarantee
that every board can accept a 4MB GIF.

The classic ESP32 configuration and wiring are a porting guide; they have not
been validated on a physical classic DevKit. The badge configuration has been
compiled and brought up on the custom PCB.

### TFT-only setup: no LED matrix required

Leave GPIO13 unconnected. The current sketch can run its matrix code without
physical LEDs; you can keep FastLED installed and leave the code unchanged.
App-sent GIFs/photo display work according to the board's PSRAM capability.
Text is output only to the LED matrix, so it is not visible in a TFT-only setup.

To add the optional 8×8 WS2812B matrix:

- DIN → GPIO13 (via a suitable 3.3 V-to-5 V logic buffer if required by the module).
- Matrix power → a suitably rated 5 V supply; **not the DevKit's 3.3 V regulator**.
- Matrix GND → shared ground with the DevKit and TFT.
- Start at `BRIGHTNESS = 20`; a 64-LED matrix can draw substantial current.
- Avoid connecting separate 5 V sources together/backfeeding USB; follow the
  DevKit's power-input documentation.

Default layout is horizontal serpentine, LED 0 at the top left. Adjust
`COLUMN_SERPENTINE`, `FLIP_X`, `FLIP_Y`, or `ROTATION` in `MatrixScroller.h` if needed.

## 5. Connect the Flutter app and send content

Get the companion [Flutter GIF Buddy app](https://github.com/navarr393/gif_buddy).
Its README covers installing Flutter and obtaining your GIPHY API key.
From the Flutter project folder, run:

```sh
flutter pub get
flutter run
```

For iOS, the current app uses Swift Package Manager. The simulator platform is
enabled in the Xcode project; do not restore the old CocoaPods configuration.

1. Connect the badge and phone/computer to the same reachable local network.
2. Open app Settings and set the host to **gif-buddy.local**, or the IP printed
   by the badge's Serial Monitor. Enter the host/IP without `http://`.
3. Pick a GIF to send it to the TFT. The app prefers Giphy's animated 200px-wide rendition.
4. Enter text and press **Send text** for the matrix. Send an empty field to clear it.

If two GIF Buddy boards are online, they may share the same mDNS hostname; use
the intended badge's IP. GIFs/text received through the app are not saved to
LittleFS and disappear after power-off/reset.

## Troubleshooting

| Symptom | Action |
| --- | --- |
| Sketch too big / maximum 1310720 bytes | Choose the larger app partition appropriate to the board's actual flash size. |
| LittleFS uploader says “No port specified” despite a selection | Open another sketch, close this one, reopen it, reselect board/port, and retry. This is a known uploader stale-state issue. |
| LittleFS mount failure or photo missing | Upload this sketch's `data/` folder with the same partition scheme as the firmware. |
| `GIF unavailable` / zero PSRAM | On the badge select OPI PSRAM and reflash. On classic ESP32 check whether PSRAM physically exists; its setting is not OPI. |
| HTTP 413 | GIF exceeds the size cap or text exceeds 160 characters. |
| HTTP 422 on GIF upload | Select a GIF no more than 480px wide and 2048px high. |
| HTTP 503 on GIF upload | PSRAM buffers unavailable or insufficient memory to stage the upload. Check PSRAM; try a smaller GIF. |
| Photo “flickers” with repeated `Rebooting...` | This is a reset loop. Use the fixed sketch and inspect the full panic log. HTTP starts only after Wi-Fi connects, fixing the earlier null-queue assertion. |
| App cannot connect | Check IP/host, Wi-Fi credentials, local-network access, and isolation on guest networks. |

The local HTTP API has no authentication; it is intended for a trusted development
network. GIFs are staged before replacing the active buffer, so incomplete uploads
do not overwrite it. HTTP upload success confirms receipt/header checks; malformed
GIF frame data can still fail later during decoding (see Serial Monitor).

## HTTP API

| Request | Purpose |
| --- | --- |
| `GET /` | Liveness |
| `GET /gif` | Buffered GIF status/size and 4MB capacity ceiling |
| `POST /gif` | Raw GIF bytes, `application/octet-stream`, with Content-Length |
| `GET /scale?mode=fit` | Fit with aspect ratio preserved (default) |
| `GET /scale?mode=full` | Fill/crop with aspect ratio preserved |
| `GET /scale?mode=native` | Center at native resolution |
| `POST /text` | Form field `text`, `application/x-www-form-urlencoded` |

For example, replacing the host with your board's IP if necessary:

```sh
curl --data-urlencode 'text=HELLO FROM GIF BUDDY' http://gif-buddy.local/text
curl --data-urlencode 'text=' http://gif-buddy.local/text
curl -H 'Content-Type: application/octet-stream' --data-binary @animation.gif http://gif-buddy.local/gif
```

## Project files and validation

- `e_badge.ino`: TFT pins, optional startup JPEG, setup/main loop.
- `BadgeReceiver.h`: Wi-Fi/HTTP receiver, GIF scaling/compositing and PSRAM buffers.
- `MatrixScroller.h`: LED wiring, orientation, text color, brightness and scrolling.
- `BadgeSecrets.h`: local Wi-Fi credentials.

The S3 firmware compiled using the badge settings above. The previous startup
reset loop was corrected and the user confirmed it runs. Flutter analysis and
four text client/widget tests passed; the iPhone 17 simulator build/launch was
also verified. The classic ESP32 guide does not claim a hardware-tested port.

## References

- [Arduino ESP32 board/settings guide](https://docs.espressif.com/projects/arduino-esp32/en/latest/guides/tools_menu.html)
- [ESP32-DevKitC hardware guide and pin restrictions](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32/esp32-devkitc/user_guide.html)
- [LittleFS uploader installation and troubleshooting](https://github.com/earlephilhower/arduino-littlefs-upload)
