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
