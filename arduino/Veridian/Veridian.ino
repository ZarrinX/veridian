// Veridian: loop /default.gif from the microSD card on the
// Waveshare ESP32-S3-Touch-LCD-2.8C.
//
// This sketch is intended to live beside the Waveshare 2.8C demo driver files:
//   Display_ST7701.{h,cpp}, LVGL_Driver.{h,cpp}, SD_Card.{h,cpp},
//   I2C_Driver.{h,cpp}, and TCA9554PWR.{h,cpp}.
// Install the AnimatedGIF Arduino library (bitbank2) and use the LVGL version
// bundled with the Waveshare board demo.

#include <Arduino.h>
#include <SD_MMC.h>
#include <AnimatedGIF.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "Display_ST7701.h"
#include "LVGL_Driver.h"
#include "SD_Card.h"
#include "I2C_Driver.h"
#include "TCA9554PWR.h"

namespace {

constexpr char kGifPath[] = "/default.gif";
constexpr int kDisplayWidth = 480;
constexpr int kDisplayHeight = 480;
constexpr uint8_t kGifSpeedMultiplier = 4;
constexpr size_t kCanvasBytes =
    static_cast<size_t>(kDisplayWidth) * kDisplayHeight * sizeof(lv_color_t);

AnimatedGIF gif;
lv_obj_t *gifCanvas = nullptr;
lv_color_t *canvasPixels = nullptr;
uint8_t *gifData = nullptr;
uint32_t gifDataSize = 0;
int canvasX = 0;
int canvasY = 0;
uint32_t gifDrawLines = 0;
uint32_t gifFramesPlayed = 0;
int gifScale = 1;

void *allocateGifBuffer(uint32_t bytes) {
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void freeGifBuffer(void *buffer) { heap_caps_free(buffer); }

bool loadGifIntoPsram() {
  File file = SD_MMC.open(kGifPath, FILE_READ);
  if (!file || file.isDirectory()) {
    Serial0.printf("[gif] Cannot open %s from SD\n", kGifPath);
    if (file) {
      file.close();
    }
    return false;
  }

  const size_t fileSize = file.size();
  if (fileSize == 0 || fileSize > 0x7fffffffUL) {
    Serial0.printf("[gif] Invalid file size: %lu bytes\n",
                   static_cast<unsigned long>(fileSize));
    file.close();
    return false;
  }

  uint8_t *buffer = static_cast<uint8_t *>(
      heap_caps_malloc(fileSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buffer == nullptr) {
    Serial0.printf("[gif] Cannot allocate %lu bytes in PSRAM\n",
                   static_cast<unsigned long>(fileSize));
    file.close();
    return false;
  }

  size_t totalRead = 0;
  while (totalRead < fileSize) {
    const size_t remaining = fileSize - totalRead;
    const size_t chunkSize = remaining < 8192 ? remaining : 8192;
    const size_t bytesRead = file.read(buffer + totalRead, chunkSize);
    if (bytesRead == 0) {
      break;
    }
    totalRead += bytesRead;
  }
  file.close();

  if (totalRead != fileSize) {
    Serial0.printf("[gif] SD copy incomplete: read=%lu expected=%lu bytes\n",
                   static_cast<unsigned long>(totalRead),
                   static_cast<unsigned long>(fileSize));
    heap_caps_free(buffer);
    return false;
  }

  gifData = buffer;
  gifDataSize = static_cast<uint32_t>(fileSize);
  Serial0.printf("[gif] Cached %lu bytes in PSRAM; free=%lu bytes\n",
                 static_cast<unsigned long>(gifDataSize),
                 static_cast<unsigned long>(ESP.getFreePsram()));
  return true;
}

// AnimatedGIF's cooked mode supplies complete, composited RGB565 scanlines,
// including transparency and disposal handling.
void drawGifLine(GIFDRAW *draw) {
  if (draw == nullptr || canvasPixels == nullptr) {
    return;
  }
  ++gifDrawLines;

  const int sourceY = draw->iY + draw->y;
  const int firstDestY = canvasY + sourceY * gifScale;
  if (firstDestY < 0 || firstDestY + gifScale > kDisplayHeight) {
    return;
  }

  const auto *sourcePixels = reinterpret_cast<const uint16_t *>(draw->pPixels);
  const int sourceX = draw->iX;
  const int destX = canvasX + sourceX * gifScale;
  if (sourceX >= 0 && sourceX + draw->iWidth <= gif.getCanvasWidth() &&
      destX >= 0 && destX + draw->iWidth * gifScale <= kDisplayWidth) {
    lv_color_t *destPixels =
        canvasPixels + firstDestY * kDisplayWidth + destX;
    if (gifScale == 1) {
      memcpy(destPixels, sourcePixels,
             static_cast<size_t>(draw->iWidth) * sizeof(lv_color_t));
      return;
    }
    if (gifScale == 2 && firstDestY + 1 < kDisplayHeight) {
      for (int x = 0; x < draw->iWidth; ++x) {
        const uint32_t doubledPixel =
            static_cast<uint32_t>(sourcePixels[x]) |
            (static_cast<uint32_t>(sourcePixels[x]) << 16);
        memcpy(destPixels + x * 2, &doubledPixel, sizeof(doubledPixel));
      }
      memcpy(destPixels + kDisplayWidth, destPixels,
             static_cast<size_t>(draw->iWidth) * 2 * sizeof(lv_color_t));
      return;
    }
  }

  for (int x = 0; x < draw->iWidth; ++x) {
    const int firstDestX = canvasX + (draw->iX + x) * gifScale;
    if (firstDestX < 0 || firstDestX + gifScale > kDisplayWidth) {
      continue;
    }
    for (int yScale = 0; yScale < gifScale; ++yScale) {
      lv_color_t *destPixels =
          canvasPixels + (firstDestY + yScale) * kDisplayWidth + firstDestX;
      for (int xScale = 0; xScale < gifScale; ++xScale) {
        destPixels[xScale].full = sourcePixels[x];
      }
    }
  }
}

bool openAndPlayGif() {
  Serial0.printf("[gif] Opening %s\n", kGifPath);
  if (gifData == nullptr && !loadGifIntoPsram()) {
    return false;
  }

  gif.begin(GIF_PALETTE_RGB565_LE);
  if (!gif.open(gifData, static_cast<int>(gifDataSize), drawGifLine)) {
    Serial0.printf("Could not open %s (GIF error %d)\n", kGifPath,
                  gif.getLastError());
    return false;
  }

  const int gifWidth = gif.getCanvasWidth();
  const int gifHeight = gif.getCanvasHeight();
  Serial0.printf("[gif] Opened: canvas=%d x %d, cached-file=%lu bytes\n",
                gifWidth, gifHeight, static_cast<unsigned long>(gifDataSize));
  if (gifWidth <= 0 || gifHeight <= 0 ||
      gif.allocFrameBuf(allocateGifBuffer) != GIF_SUCCESS) {
    Serial0.println("GIF canvas is invalid or could not be allocated");
    gif.close();
    return false;
  }
  const bool turboEnabled =
      gif.allocTurboBuf(allocateGifBuffer) == GIF_SUCCESS;
  Serial0.printf("[gif] Turbo LZW decoder %s\n",
                 turboEnabled ? "enabled" : "unavailable; using standard decoder");

  gif.setDrawType(GIF_DRAW_COOKED);
  gifScale = min(kDisplayWidth / gifWidth, kDisplayHeight / gifHeight);
  if (gifScale < 1) {
    gifScale = 1;
  }
  canvasX = (kDisplayWidth - gifWidth * gifScale) / 2;
  canvasY = (kDisplayHeight - gifHeight * gifScale) / 2;

  memset(canvasPixels, 0, kCanvasBytes);
  lv_obj_invalidate(gifCanvas);

  Serial0.printf("Playing %s (%d x %d) scaled %dx at %ux speed\n", kGifPath,
                  gifWidth, gifHeight, gifScale, kGifSpeedMultiplier);
  int frameDelayMs = 0;
  gifDrawLines = 0;
  gifFramesPlayed = 0;
  int playResult = 0;
  while (true) {
    const uint32_t frameStartMs = millis();
    playResult = gif.playFrame(false, &frameDelayMs);
    const bool frameDecoded =
        playResult > 0 ||
        (playResult == 0 && gif.getLastError() == GIF_SUCCESS);
    if (!frameDecoded) {
      break;
    }

    ++gifFramesPlayed;
    lv_obj_invalidate(gifCanvas);
    lv_timer_handler();
    yield();

    const uint32_t targetFrameMs =
        frameDelayMs > 0
            ? static_cast<uint32_t>(frameDelayMs) / kGifSpeedMultiplier
            : 0;
    const uint32_t elapsedMs = millis() - frameStartMs;
    if (elapsedMs < targetFrameMs) {
      delay(targetFrameMs - elapsedMs);
    }

    if (playResult == 0) {
      break;
    }
  }

  // Flush the last decoded frame before closing or reopening the GIF.
  lv_timer_handler();
  Serial0.printf(
      "[gif] Playback ended: result=%d error=%d decoded-frames=%lu "
      "draw-callbacks=%lu final-delay=%d ms\n",
      playResult, gif.getLastError(), static_cast<unsigned long>(gifFramesPlayed),
      static_cast<unsigned long>(gifDrawLines), frameDelayMs);
  gif.close();
  gif.freeFrameBuf(freeGifBuffer);
  if (turboEnabled) {
    gif.freeTurboBuf(freeGifBuffer);
  }
  return true;
}

}  // namespace

void setup() {
  Serial0.begin(115200);
  delay(500);
  Serial0.println();
  Serial0.println("[boot] Veridian GIF player starting");
  Serial0.printf("[memory] PSRAM found=%s size=%lu free=%lu bytes\n",
                psramFound() ? "yes" : "no",
                static_cast<unsigned long>(ESP.getPsramSize()),
                static_cast<unsigned long>(ESP.getFreePsram()));

  // Use the display, I/O expander, SD, and LVGL initialization provided by
  // Waveshare's ESP32-S3-Touch-LCD-2.8C Arduino demo.
  Serial0.println("[init] I2C_Init");
  I2C_Init();
  Serial0.println("[init] TCA9554PWR_Init");
  TCA9554PWR_Init(0x00);
  Serial0.println("[init] LCD_Init");
  LCD_Init();
  Serial0.println("[init] LCD_Init returned");
  Serial0.println("[init] SD_Init");
  SD_Init();
  Serial0.println("[init] SD_Init returned");
  Serial0.println("[init] Lvgl_Init");
  Lvgl_Init();
  Serial0.println("[init] Lvgl_Init returned");
  // EXIO8 controls the onboard buzzer; the Waveshare demo uses High for ON.
  Set_EXIO(EXIO_PIN8, Low);
  Serial0.printf("[buzzer] EXIO8 LOW, expander outputs=0x%02x\n",
                I2C_Read_EXIO(TCA9554_OUTPUT_REG));

  if (sizeof(lv_color_t) != sizeof(uint16_t)) {
    Serial0.println("This sketch requires 16-bit LVGL colors");
    while (true) {
      delay(1000);
    }
  }

  canvasPixels = static_cast<lv_color_t *>(
      heap_caps_malloc(kCanvasBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (canvasPixels == nullptr) {
    Serial0.println("Could not allocate the GIF canvas in PSRAM");
    while (true) {
      delay(1000);
    }
  }
  Serial0.printf("[memory] GIF canvas allocated: %lu bytes; PSRAM free=%lu bytes\n",
                static_cast<unsigned long>(kCanvasBytes),
                static_cast<unsigned long>(ESP.getFreePsram()));

  memset(canvasPixels, 0, kCanvasBytes);
  gifCanvas = lv_canvas_create(lv_scr_act());
  lv_canvas_set_buffer(gifCanvas, canvasPixels, kDisplayWidth, kDisplayHeight,
                       LV_IMG_CF_TRUE_COLOR);
  lv_obj_center(gifCanvas);
  lv_canvas_fill_bg(gifCanvas, lv_color_black(), LV_OPA_COVER);
  lv_timer_handler();
  Serial0.println("[lvgl] canvas created, black background flushed");
}

void loop() {
  if (!openAndPlayGif()) {
    static uint32_t lastRetryMessage = 0;
    const uint32_t now = millis();
    if (now - lastRetryMessage >= 5000) {
      Serial0.println("[gif] Will retry in 1 second");
      lastRetryMessage = now;
    }
    delay(1000);
  }
}
