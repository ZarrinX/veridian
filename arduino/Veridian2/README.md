# Veridian2 (EFC player)

This is a separate sketch folder so Arduino does not concatenate its `setup()`
and `loop()` with the original GIF sketch. It plays `/default.efc` from the SD
card, decodes RGB565 frames into PSRAM, and copies the scaled frame to the
ST7701S RGB panel's framebuffer without an LVGL full-screen redraw.

## EFC dependency and build note

Espressif publishes `esp_efc_codec` as an ESP-IDF component, not an Arduino
Library Manager library. The sketch includes its C API (`esp_efc_codec.h`), so
the component must be made available to the firmware build. Component Manager
installation for an ESP-IDF project:

```text
idf.py add-dependency "espressif/esp_efc_codec^1.1.0"
```

The current project uses Arduino IDE and the Waveshare Arduino demo drivers.
This `.ino` has not been compiled; it may need an ESP-IDF/Arduino-component
build or an Arduino library wrapper for EFC before Arduino IDE can link it.

## Asset

Convert `default.gif` to `default.efc` on a computer with Espressif's EFC
encoder, then copy `default.efc` to the root of the SD card. The sketch accepts
an EFC asset up to 480x480 and scales smaller assets to fit. EFC does not store
frame timing, so playback is currently set to 35 ms per frame (2x the GIF's
reported 70 ms delay).
