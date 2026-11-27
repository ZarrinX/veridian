// Veridian2: play /default.efc using Espressif's EFC decoder.
// Target: Waveshare ESP32-S3-Touch-LCD-2.8C, 480x480 RGB565.
//
// EFC is an ESP-IDF component, not a Library Manager Arduino library. This
// sketch uses its public C API; the EFC component must be present in the
// firmware build. See README.md in this folder for the dependency and asset
// conversion notes.

#include <Arduino.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_efc_codec.h>

#include "Display_ST7701.h"
#include "I2C_Driver.h"
#include "SD_Card.h"
#include "TCA9554PWR.h"

namespace {

constexpr char kEfcPath[] = "/default.efc";
constexpr int kDisplayWidth = ESP_PANEL_LCD_WIDTH;
constexpr int kDisplayHeight = ESP_PANEL_LCD_HEIGHT;
// The source GIF reports 49 frames at 70 ms. Keep the previously requested
// 2x playback speed; EFC files do not carry frame timing metadata.
constexpr uint32_t kFrameDelayMs = 35;

uint8_t *efcData = nullptr;
size_t efcDataSize = 0;
esp_efc_dec_handle_t decoder = nullptr;
esp_efc_dec_info_t animationInfo = {};
uint16_t *displayPixels = nullptr;
uint16_t *decodedPixels = nullptr;
int scaleFactor = 1;
int imageX = 0;
int imageY = 0;

bool loadEfcFromSd() {
  File file = SD_MMC.open(kEfcPath, FILE_READ);
  if (!file || file.isDirectory()) {
    Serial0.printf("[efc] Cannot open %s\n", kEfcPath);
    if (file) file.close();
    return false;
  }

  const size_t size = file.size();
  if (size == 0) {
    Serial0.println("[efc] Empty asset");
    file.close();
    return false;
  }
  uint8_t *data = static_cast<uint8_t *>(
      heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (data == nullptr) {
    Serial0.printf("[efc] Invalid size or PSRAM allocation failed (%lu bytes)\n",
                   static_cast<unsigned long>(size));
    file.close();
    return false;
  }

  size_t bytesRead = 0;
  while (bytesRead < size) {
    const size_t request = min(size - bytesRead, static_cast<size_t>(8192));
    const size_t count = file.read(data + bytesRead, request);
    if (count == 0) break;
    bytesRead += count;
  }
  file.close();

  if (bytesRead != size) {
    Serial0.printf("[efc] Short SD read: %lu of %lu bytes\n",
                   static_cast<unsigned long>(bytesRead),
                   static_cast<unsigned long>(size));
    heap_caps_free(data);
    return false;
  }

  efcData = data;
  efcDataSize = size;
  Serial0.printf("[efc] Cached %lu bytes in PSRAM\n",
                 static_cast<unsigned long>(efcDataSize));
  return true;
}

bool initializeEfc() {
  if (!loadEfcFromSd()) return false;

  auto result = esp_efc_dec_open(efcData, efcDataSize, &decoder);
  if (result != ESP_EFC_OK) {
    Serial0.printf("[efc] Decoder open failed: %d\n", static_cast<int>(result));
    return false;
  }

  result = esp_efc_dec_get_info(decoder, &animationInfo);
  if (result != ESP_EFC_OK || animationInfo.width <= 0 ||
      animationInfo.height <= 0 || animationInfo.frame_count <= 0) {
    Serial0.printf("[efc] Invalid animation metadata: %d\n",
                   static_cast<int>(result));
    return false;
  }

  if (animationInfo.width > kDisplayWidth ||
      animationInfo.height > kDisplayHeight) {
    Serial0.printf("[efc] Asset %dx%d exceeds display; encode a smaller file\n",
                   animationInfo.width, animationInfo.height);
    return false;
  }

  // Keep the EFC decoder's RGB565 framebuffer persistent between frames:
  // unchanged EFC tiles intentionally reuse pixels from the prior frame.
  const size_t imageBytes = static_cast<size_t>(animationInfo.width) *
                            animationInfo.height * sizeof(uint16_t);
  decodedPixels = static_cast<uint16_t *>(
      heap_caps_malloc(imageBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (decodedPixels == nullptr) {
    Serial0.println("[efc] Cannot allocate decode framebuffer in PSRAM");
    return false;
  }
  memset(decodedPixels, 0, imageBytes);

  void *frameBuffer = nullptr;
  const esp_err_t fbResult = esp_lcd_rgb_panel_get_frame_buffer(
      panel_handle, ESP_PANEL_LCD_RGB_FRAME_BUF_NUM, &frameBuffer);
  if (fbResult != ESP_OK || frameBuffer == nullptr) {
    Serial0.printf("[lcd] Cannot get RGB framebuffer: %s\n",
                   esp_err_to_name(fbResult));
    return false;
  }
  displayPixels = static_cast<uint16_t *>(frameBuffer);
  memset(displayPixels, 0,
         static_cast<size_t>(kDisplayWidth) * kDisplayHeight *
             sizeof(uint16_t));

  scaleFactor = min(kDisplayWidth / animationInfo.width,
                    kDisplayHeight / animationInfo.height);
  if (scaleFactor < 1) scaleFactor = 1;
  imageX = (kDisplayWidth - animationInfo.width * scaleFactor) / 2;
  imageY = (kDisplayHeight - animationInfo.height * scaleFactor) / 2;
  if (animationInfo.width == kDisplayWidth &&
      animationInfo.height == kDisplayHeight) {
    // Decode full-size frames directly into the RGB panel framebuffer.
    heap_caps_free(decodedPixels);
    decodedPixels = displayPixels;
  }

  Serial0.printf("[efc] Ready: %dx%d, %d frames, scale=%dx, delay=%lu ms\n",
                 animationInfo.width, animationInfo.height,
                 animationInfo.frame_count, scaleFactor,
                 static_cast<unsigned long>(kFrameDelayMs));
  return true;
}

void copyScaledFrameToDisplay() {
  // The display framebuffer was cleared once before playback. Do not clear it
  // here: EFC's delta frames leave unchanged pixels in place.
  for (int y = 0; y < animationInfo.height; ++y) {
    const uint16_t *source = decodedPixels + y * animationInfo.width;
    const int destY = imageY + y * scaleFactor;
    for (int yScale = 0; yScale < scaleFactor; ++yScale) {
      uint16_t *dest = displayPixels + (destY + yScale) * kDisplayWidth + imageX;
      for (int x = 0; x < animationInfo.width; ++x) {
        const uint16_t color = source[x];
        for (int xScale = 0; xScale < scaleFactor; ++xScale) {
          dest[x * scaleFactor + xScale] = color;
        }
      }
    }
  }
}

void playEfcForever() {
  uint32_t nextFrameAt = millis();
  while (true) {
    for (int frame = 0; frame < animationInfo.frame_count; ++frame) {
      const auto result =
          esp_efc_dec_frame(decoder, frame, decodedPixels);
      if (result != ESP_EFC_OK) {
        Serial0.printf("[efc] Frame %d decode failed: %d\n", frame,
                       static_cast<int>(result));
        delay(250);
        break;
      }

      if (decodedPixels != displayPixels) {
        copyScaledFrameToDisplay();
      }

      nextFrameAt += kFrameDelayMs;
      const int32_t waitMs = static_cast<int32_t>(nextFrameAt - millis());
      if (waitMs > 0) delay(static_cast<uint32_t>(waitMs));
      else yield();
    }
  }
}

}  // namespace

void setup() {
  Serial0.begin(115200);
  delay(500);
  Serial0.println("\n[boot] Veridian EFC player starting");
  Serial0.printf("[memory] PSRAM found=%s size=%lu free=%lu bytes\n",
                 psramFound() ? "yes" : "no",
                 static_cast<unsigned long>(ESP.getPsramSize()),
                 static_cast<unsigned long>(ESP.getFreePsram()));

  I2C_Init();
  TCA9554PWR_Init(0x00);
  LCD_Init();
  SD_Init();
  Set_EXIO(EXIO_PIN8, Low);  // Buzzer off on this board.

  if (!initializeEfc()) {
    Serial0.println("[efc] Initialization failed; check component and SD asset");
    while (true) delay(1000);
  }
}

void loop() { playEfcForever(); }
