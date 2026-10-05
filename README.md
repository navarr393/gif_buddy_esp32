# GIF Buddy ESP32 firmware

Two hardware targets are kept in this repository:

| Firmware | Hardware | Getting started |
| --- | --- | --- |
| **Arduino e-badge** | ESP32-S3-WROOM-1-N8R8, 240×240 GC9A01 SPI TFT, optional 8×8 WS2812B matrix | [Badge setup, wiring, and flashing guide](e_badge/README.md) |
| Original PlatformIO firmware | ESP32-S3 with 466×466 CO5300 QSPI display | `platformio.ini` and `src/main.cpp` |

## Arduino e-badge

The Arduino sketch receives GIFs over Wi-Fi and displays them on the round TFT.
Messages sent from the companion app scroll on the LED matrix. It also supports
an optional LittleFS startup JPEG. Received GIFs/text reset on reboot.

1. Download or clone this repository.
2. Copy `e_badge/BadgeSecrets.example.h` to `e_badge/BadgeSecrets.h` and enter your Wi-Fi credentials.
3. Open `e_badge/e_badge.ino` in Arduino IDE.
4. Follow the [Arduino flashing guide](e_badge/README.md#3-flash-the-custom-e-badge-pcb).

For hardware without the custom PCB, see the [DevKit wiring and setup guide](e_badge/README.md#4-use-a-development-board-instead-of-the-badge-pcb).
Full GIF reception requires a PSRAM-equipped board; ordinary ESP32-WROOM DevKits
without PSRAM cannot run this GIF receiver's playback path.

Your Wi-Fi credentials and personal startup JPEG are intentionally not included.
Add your own photo using the instructions in `e_badge/data/README.txt`.

## Companion Flutter app

**[GIF Buddy Flutter app →](https://github.com/navarr393/gif_buddy)**

The app README explains how to install Flutter, obtain a GIPHY API key, run the
app, and send GIFs or text. Use `gif-buddy.local` or the badge's IP address in app
settings, with the badge and app on a reachable local network.

## Original PlatformIO target

The root `platformio.ini` builds the original `src/main.cpp` for the CO5300 QSPI
hardware. It does **not** build the Arduino e-badge sketch. Its display pins,
flash settings, and driver differ from the badge; do not flash that target onto
the badge using the badge wiring. The original target has been preserved.
