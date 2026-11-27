# Veridian Agent Guide

## Project

Veridian displays an animated GIF on a Waveshare ESP32-S3 round display mounted inside a Christmas ornament. Keep hardware-specific choices aligned with the exact board below.

## Target Hardware

**Board:** Waveshare ESP32-S3-Touch-LCD-2.8C (SKU 29086)

Use the `2.8C` model name when looking up drivers, pin mappings, and examples. Waveshare sells several similarly named 2.8-inch ESP32-S3 boards with different display hardware; do not assume their specifications or examples are interchangeable.

### Main specifications

| Component | Specification |
|---|---|
| Display | 2.8-inch round capacitive-touch LCD, 480 × 480 pixels |
| Display controller | ST7701S |
| Touch controller | GT911, I2C |
| MCU | ESP32-S3R8, dual-core Xtensa LX7, up to 240 MHz |
| Memory | 16 MB Flash, 8 MB PSRAM; 512 KB SRAM and 384 KB ROM listed for the MCU |
| Wireless | 2.4 GHz Wi-Fi (802.11 b/g/n) and Bluetooth 5 LE, onboard antenna |

### Onboard hardware and connectors

- QMI8658 six-axis accelerometer/gyroscope
- PCF85063 RTC
- TF (microSD) card slot
- Buzzer
- USB Type-C programming/debug/power connector with CH343P USB-to-UART and automatic download circuitry
- Separate USB Type-C connector for UART selection, plus a 4-pin UART header. The 4-pin UART header is disabled while the UART Type-C port is connected.
- MX1.25 two-pin connector for a 3.7 V lithium battery, with charging management
- 12-pin multifunction connector exposing USB signals, 3.3 V, I2C, UART, and GPIO0

### Exposed interface pin mapping

| Header signal | ESP32-S3 signal | Notes |
|---|---|---|
| SCL | GPIO7 | I2C clock; reserved for the I2C interface |
| SDA | GPIO15 | I2C data; reserved for the I2C interface |
| TXD | GPIO43 | UART TX; can also be used as GPIO |
| RXD | GPIO44 | UART RX; can also be used as GPIO |
| IO0 | GPIO0 | Spare GPIO |
| D− / D+ | GPIO19 / GPIO20 | USB differential signals or GPIO |
| VBus | 5 V | USB power |
| 3V3 | 3.3 V | Power output |

The board also has a TCA9554PWR GPIO expander, but Waveshare states that all of its pins are used internally and are not brought out. Confirm any additional pin use against the board schematic before assigning GPIOs.

## Manufacturer References

- [Product documentation and board overview](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C)
- [Resources: schematic, dimensional drawing, 3D model, datasheets, and examples](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C/Resources-And-Documents)
- [Arduino setup and examples](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C/Arduino)
- [ESP-IDF setup and examples](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C/ESP-IDF)

## Arduino Build and Driver Lessons

- Keep sketches in separate folders. Arduino combines every `.ino` in one sketch folder into the same translation unit, so do not put another sketch with its own `setup()` and `loop()` beside `Veridian.ino`.
- The Waveshare demo driver files are part of this project, not supplied by installing LVGL alone. Missing `Display_ST7701.h` means the board driver files are missing from the sketch source set.
- `lvgl.h`, `lv_conf.h`, and `demos/lv_demos.h` are separate requirements. Match the LVGL version expected by the Waveshare demo, provide the corresponding `lv_conf.h`, and include/build the demos only when the driver source requires them. Do not solve an `esp_lcd_panel_rgb.h` error by installing another display library: it is an ESP-IDF/Arduino-core header and depends on the selected ESP32 board package.
- Use Serial Monitor diagnostics at 115200 baud. Preserve startup logs around PSRAM, I2C, expander, LCD, SD, and LVGL initialization; they identify which subsystem failed before animation starts.
- The board buzzer is controlled through TCA9554PWR expander output EXIO8. The current sketches drive it LOW after expander initialization, but verify the physical buzzer on the actual board rather than assuming the polarity is correct.

## Display and SD Card Bring-Up

- Initialize the expander before display, touch, or SD setup. The LCD uses SPI2 for its ST7701S command sequence, and the SD card shares the bus pins. Finish LCD setup and release the SPI2 device and bus before mounting SDMMC; the successful log reported `device=ESP_OK bus=ESP_OK`.
- The known SDMMC setup is 1-bit mode with CLK=GPIO2, CMD=GPIO1, and D0=GPIO42. The current driver mounts at 10 MHz. Avoid adding other users of those pins or reinitializing SPI2 after the SD mount.
- A successful `SD_MMC.begin()` and valid card-capacity report do not prove that later file reads are reliable. The failed card initialized successfully but returned repeated `sdmmc_read_sectors_dma` error `0x107` and short reads; swapping to another card resolved playback. When reads fail partway through a file, check the media and its formatting and test another known-good card before changing the decoder.
- Log byte counts and offsets for file reads. Treat any short read as a playback failure; do not retry indefinitely against a broken stream or report the GIF as corrupt until card reads are confirmed.

## Animation Playback Findings

- The confirmed board is a 480×480 RGB565 panel. The user’s `default.gif` is 240×240 and should be centered and scaled 2× to fill the panel unless the asset is replaced with another size.
- PSRAM is required for the canvas and animation buffers. A prior boot reported about 7.1 MB total PSRAM and about 5.2 MB free after display/LVGL allocations. Check free PSRAM after each large allocation; the GIF is about 2.6 MB and can be cached once at startup to avoid repeated SD reads during looping.
- AnimatedGIF cooked drawing requires its frame buffer. `allocTurboBuf()` is an optional faster LZW path; log whether it succeeded. Caching and Turbo decoding made playback only “a little better,” so do not claim smooth playback from those changes alone.
- The current LVGL GIF path invalidates the whole canvas and flushes through `LCD_addWindow()` / `esp_lcd_panel_draw_bitmap()` each frame. If performance remains poor, measure decode and display-flush time separately before tuning frame delays; screen refresh may cost more than GIF decoding.
- The reported final GIF delay of 70 ms is not proof that every frame uses that delay. Inspect actual frame metadata or measure playback before selecting a fixed rate. A speed multiplier changes the requested pacing but cannot exceed decode/render throughput.

## EFC Alternative and Validation Status

- `arduino/Veridian2/Veridian2.ino` is a separate EFC experiment. It reads `/default.efc`, decodes RGB565, and writes to the RGB panel framebuffer to avoid LVGL's full-screen redraw. Its supporting Waveshare driver files are copied into that sketch folder; keep those copies synchronized if the shared board drivers change.
- Espressif distributes `esp_efc_codec` as an ESP-IDF component, not an Arduino Library Manager library. The sketch includes `esp_efc_codec.h`; adding `idf.py add-dependency "espressif/esp_efc_codec^1.1.0"` to an ESP-IDF project alone does not make it available to the existing Arduino IDE build. Confirm the component is actually integrated before expecting the sketch to compile.
- EFC can skip unchanged blocks and decode directly to RGB565, but its measured performance depends on the animation. Encode the asset on a computer, put `default.efc` at the root of the SD card, and preserve the frame buffer between EFC frames because unchanged blocks reuse its previous contents.
- EFC files do not carry frame timing. The current 35 ms constant is a playback choice based on an earlier, unverified 70 ms report; validate the source animation timing and adjust it deliberately.
- Neither the latest GIF optimization nor the EFC sketch has been compiled and validated on the target board. After any library, driver, or frame-buffer changes, compile with the documented toolchain and test actual SD reads, display output, and sustained looping before marking playback as working or smooth.
