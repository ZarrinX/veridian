# Veridian

Veridian recreates the Christmas tree ornament Captain Picard sees after entering the Nexus in *Star Trek: Generations*. It loops that animation on a **Waveshare ESP32-S3-Touch-LCD-2.8C (SKU 29086)**: a 480×480 round RGB display with an ST7701S controller, GT911 capacitive touch, ESP32-S3R8, and 8 MB PSRAM. See [AGENTS.md](AGENTS.md) for the full hardware reference and buildout notes.

![The Christmas tree ornament animation from Star Trek: Generations](arduino/default.gif)

## Sketches

| Sketch | Purpose | Status |
|---|---|---|
| [`arduino/Veridian/Veridian.ino`](arduino/Veridian/Veridian.ino) | Loops `/default.gif` from microSD using AnimatedGIF and an LVGL canvas. The 240×240 sample is scaled 2× to fill the display. | GIF and SD playback worked on the board; caching in PSRAM and Turbo decoding helped only a little. Smooth playback is not confirmed. |
| [`arduino/Veridian2/Veridian2.ino`](arduino/Veridian2/Veridian2.ino) | Experimental EFC player that decodes RGB565 and writes to the RGB panel framebuffer. | Not compiled or tested on hardware. Requires Espressif's `esp_efc_codec` component; see [its README](arduino/Veridian2/README.md). |

The EFC sketch is in a separate folder because Arduino combines `.ino` files in the same folder. It has its own copies of the Waveshare driver sources; keep them in sync with `arduino/Veridian/` when changing shared board behavior.

## Arduino GIF setup

1. Open `arduino/Veridian/Veridian.ino` in Arduino IDE.
2. Select the ESP32-S3 board settings required by Waveshare's [Arduino setup guide](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C/Arduino). The board driver files are included in the sketch folder.
3. Install the AnimatedGIF library and the LVGL version expected by the Waveshare demo. LVGL also needs a compatible `lv_conf.h`; the driver source includes the LVGL demos header.
4. Upload the sketch and open Serial Monitor at **115200 baud** to follow the startup and playback diagnostics.
5. Copy [`arduino/default.gif`](arduino/default.gif) to the root of the microSD card as `/default.gif`.

The GIF sketch requests 4× playback pacing, but decoding and display rendering determine the actual frame rate. A playback multiplier does not make the decoder or screen refresh faster.

## SD card and display notes

- The board's LCD initialization uses SPI2 for ST7701S commands. The driver releases SPI2 before mounting SDMMC because the display and card share pins.
- SDMMC is configured for 1-bit mode on CLK GPIO2, CMD GPIO1, and D0 GPIO42 at 10 MHz. Keep these pins and initialization order unchanged unless the board wiring is deliberately revised.
- A card can mount and report its capacity while later reads still fail. Repeated `sdmmc_read_sectors_dma` errors (`0x107`) and short reads were traced to one card; another card played the same file successfully. Check complete file reads and try another card before changing the GIF decoder.
- EXIO8 on the TCA9554PWR expander controls the buzzer. The sketch drives it LOW after expander setup; confirm the buzzer is actually silent on the board.

## EFC experiment

EFC is an embedded animation format that can decode directly to RGB565 and reuse unchanged blocks. The current experiment reads `/default.efc` from the card and targets the panel framebuffer, avoiding the GIF sketch's LVGL full-canvas redraw path. EFC files do not contain frame timing; the experimental sketch currently uses a configurable 35 ms delay.

Espressif publishes `esp_efc_codec` as an ESP-IDF component, not an Arduino Library Manager library. The current Arduino IDE project does not yet integrate that component, so the EFC sketch will not build until the component is made available to its build. Convert the GIF to EFC on a computer and copy the result to the SD card root as `/default.efc` after setting up that dependency.

## Hardware references

- [Waveshare product documentation](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C)
- [Waveshare resources and documents](https://docs.waveshare.com/ESP32-S3-Touch-LCD-2.8C/Resources-And-Documents)
- [Espressif EFC component](https://components.espressif.com/components/espressif/esp_efc_codec/versions/1.1.0/readme)
