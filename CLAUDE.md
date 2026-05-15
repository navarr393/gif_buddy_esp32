# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`gif_buddy` is firmware for a **Waveshare ESP32-S3 Touch AMOLED 1.75"** (466×466 round panel, CO5300 controller, QSPI interface, 8 MB OPI PSRAM, 16 MB flash). Goal: a network-loadable GIF player that a phone app can push GIFs to over the LAN.

## Build, flash, monitor

PlatformIO CLI lives at `~/.platformio/penv/bin/pio` (not always on `PATH`).

```bash
~/.platformio/penv/bin/pio run -e esp32-s3-devkitm-1            # build
~/.platformio/penv/bin/pio run -t upload                         # flash
~/.platformio/penv/bin/pio device monitor                        # serial @ 115200
~/.platformio/penv/bin/pio run -t upload -t monitor              # all three
```

After changing `lib_deps` or `platform` in `platformio.ini`:

```bash
~/.platformio/penv/bin/pio pkg install -e esp32-s3-devkitm-1
~/.platformio/penv/bin/pio project init --ide vscode -e esp32-s3-devkitm-1
```

…then **Developer: Reload Window** in VS Code to refresh clangd's include path. Squiggles about `Arduino.h not found` / `Unknown type name 'Arduino_Canvas'` are clangd-only and clear after reload; the PlatformIO build itself doesn't use them.

## Platform constraints (non-obvious)

- **Must use the `pioarduino` platform fork** (pinned in `platformio.ini`), not stock `espressif32`. The GFX library needs Arduino-ESP32 v3.x headers (`esp32-hal-periman.h`); the stock platform still ships v2.x and fails to compile.
- **Memory config is load-bearing:** `board_build.arduino.memory_type = qio_opi` + `board_build.flash_mode = qio`. Changing these breaks PSRAM or the AMOLED bus.
- `ARDUINO_USB_CDC_ON_BOOT=1` + `ARDUINO_USB_MODE=1` route `Serial` to native USB-CDC; without them the monitor stays blank.

## Display architecture (don't skip this)

Three-layer stack, set up at file scope in `src/main.cpp`:

```
Arduino_ESP32QSPI (bus)  →  Arduino_CO5300 (driver)  →  Arduino_Canvas (framebuffer)  ↘ flush() ↘ panel
```

**Always draw to the `Arduino_Canvas` (`gfx`) and call `gfx->flush()` once per frame.** Drawing primitives directly into `Arduino_CO5300` issues hundreds of small QSPI transactions and the panel desyncs — early debug showed text/circles rendering as the fragment "ello!" until we switched to canvas+flush. The canvas lives in PSRAM (466×466×2 = ~434 KB).

Two more gotchas:

- `setBrightness()` lives on `Arduino_CO5300`, not on the base `Arduino_GFX`. Keep `output` typed as `Arduino_CO5300 *`, not the base class — otherwise it won't compile.
- GFX 1.6.x dropped/renamed the color name macros, and `WHITE` collides with ESP-IDF's `FWRITE`. Use raw RGB565 hex: `0x0000` black, `0xFFFF` white, `0xF800` red, `0x07E0` green, `0x001F` blue, `0xFFE0` yellow, `0xF81F` magenta.

Pin map (CO5300, QSPI) is `#define`d at the top of `main.cpp`: CS=12, SCLK=38, SDIO0–3=4,5,6,7, RESET=2.

## Network architecture

The phone is the bridge between the internet (Giphy/Tenor/etc.) and the ESP32; the ESP32 only listens on the LAN. WiFi creds live in `src/secrets.h` (gitignored — see `.gitignore`). Hostname and mDNS service name are both `gif-buddy`, so the device is reachable at `http://gif-buddy.local/` on networks that resolve mDNS.

Endpoints served by `AsyncWebServer` on port 80:

- `GET /` — plain-text liveness + IP + RSSI
- `GET /gif` — JSON: `{ready, size, capacity}` of the upload buffer
- `POST /gif` — raw bytes (any content type). Body handler streams chunks into `gifBuf` in PSRAM, flips `gifReady=true` on the final chunk.

`AsyncWebServer` runs on its own FreeRTOS task, so the main `loop()` (which renders frames) is not blocked by request handling. The single shared GIF buffer is `ps_malloc`'d in `setup()` (capacity `GIF_MAX_BYTES`, currently 4 MB) and never resized.

## GIF data path

- **Baseline (current `loop()`):** 20 baked-in Gengar frames are stored in flash as `const uint8_t[]` RGB565 LE arrays (`src/gengar/frame_NN_delay-0.c`, declared in `src/gengar_frames.h`). The loop iterates them with `gfx->draw16bitRGBBitmap(...)` + `flush()` + `delay()`.
- **Target (step 3, not yet implemented):** replace that loop with `bitbank2/AnimatedGIF` decoding the in-PSRAM buffer at `gifBuf`, honoring each frame's own delay.

## Source layout (only the non-obvious bits)

- `src/main.cpp` — single TU, everything wired up at file scope.
- `src/secrets.h` — gitignored; holds `WIFI_SSID` / `WIFI_PASS` defines.
- `src/gengar/` + `src/gengar_frames.h` — the baked-in Gengar frames. Header uses `extern "C"` so the C arrays link against C++.
- `sample.ino` (project root, NOT in `src/`) — the original Waveshare reference sketch. **Do not move it into `src/`** — PlatformIO converts `.ino` → `.cpp` and compiles everything in `src/`, so it would conflict with `main.cpp`. `build_src_filter` does not help because the filter runs after the `.ino` conversion.
- `include/`, `lib/`, `test/` — PlatformIO scaffolding, unused.

## Operational notes

- **Don't run a VS Code build and a CLI build at the same time** — they race on `.pio/libdeps` and leave it half-populated (symptom: stray `.cpp` files, no headers). Fix: `rm -rf .pio` and rebuild from scratch.
- `default_16MB.csv` partition table is required for the 16 MB flash size declared in `platformio.ini`; changing the partition table changes the maximum sketch size.
