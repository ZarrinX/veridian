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
constexpr size_t kCanvasBytes =
    static_cast<size_t>(kDisplayWidth) * kDisplayHeight * sizeof(lv_color_t);

AnimatedGIF gif;
File gifFile;
lv_obj_t *gifCanvas = nullptr;
lv_color_t *canvasPixels = nullptr;
int canvasX = 0;
int canvasY = 0;
uint32_t gifDrawLines = 0;
uint32_t gifFramesPlayed = 0;

void *allocateGifBuffer(uint32_t bytes) {
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void freeGifBuffer(void *buffer) { heap_caps_free(buffer); }

void *openGifFile(const char *filename, int32_t *fileSize) {
  gifFile = SD_MMC.open(filename, FILE_READ);
  if (!gifFile || gifFile.isDirectory()) {
    if (gifFile) {
      gifFile.close();
    }
    return nullptr;
  }

  *fileSize = static_cast<int32_t>(gifFile.size());
  return &gifFile;
}

void closeGifFile(void *handle) {
  auto *file = static_cast<File *>(handle);
  if (file != nullptr) {
    file->close();
  }
}

int32_t readGifFile(GIFFILE *fileInfo, uint8_t *buffer, int32_t length) {
  auto *file = static_cast<File *>(fileInfo->fHandle);
  if (file == nullptr || length <= 0) {
    return 0;
  }

  const int32_t bytesRead = static_cast<int32_t>(file->read(buffer, length));
  fileInfo->iPos = static_cast<int32_t>(file->position());
  return bytesRead;
}

int32_t seekGifFile(GIFFILE *fileInfo, int32_t position) {
  auto *file = static_cast<File *>(fileInfo->fHandle);
  if (file == nullptr || !file->seek(static_cast<uint32_t>(position))) {
    return -1;
  }

  fileInfo->iPos = static_cast<int32_t>(file->position());
  return fileInfo->iPos;
}

// AnimatedGIF's cooked mode supplies complete, composited RGB565 scanlines,
// including transparency and disposal handling.
void drawGifLine(GIFDRAW *draw) {
  if (draw == nullptr || canvasPixels == nullptr) {
    return;
  }
  ++gifDrawLines;

  const int destY = canvasY + draw->iY + draw->y;
  if (destY < 0 || destY >= kDisplayHeight) {
    return;
  }

  const int sourceX = draw->iX;
  const int left = max(0, -canvasX - sourceX);
  const int right = min(draw->iWidth, kDisplayWidth - canvasX - sourceX);
  if (right <= left) {
    return;
  }

  const int destX = canvasX + sourceX + left;
  const auto *sourcePixels = reinterpret_cast<const uint16_t *>(draw->pPixels);
  lv_color_t *destPixels = canvasPixels + destY * kDisplayWidth + destX;

  for (int x = left; x < right; ++x) {
    destPixels[x - left].full = sourcePixels[x];
  }
  if (gifCanvas != nullptr) {
    lv_obj_invalidate(gifCanvas);
  }
}

bool openAndPlayGif() {
  Serial.printf("[gif] Opening %s\n", kGifPath);
  gif.begin(GIF_PALETTE_RGB565_LE);
  if (!gif.open(kGifPath, openGifFile, closeGifFile, readGifFile, seekGifFile,
                drawGifLine)) {
    Serial.printf("Could not open %s (GIF error %d)\n", kGifPath,
                  gif.getLastError());
    return false;
  }

  const int gifWidth = gif.getCanvasWidth();
  const int gifHeight = gif.getCanvasHeight();
  Serial.printf("[gif] Opened: canvas=%d x %d, file=%lu bytes\n", gifWidth,
                gifHeight, static_cast<unsigned long>(gifFile.size()));
  if (gifWidth <= 0 || gifHeight <= 0 ||
      gif.allocFrameBuf(allocateGifBuffer) != GIF_SUCCESS) {
    Serial.println("GIF canvas is invalid or could not be allocated");
    gif.close();
    return false;
  }

  gif.setDrawType(GIF_DRAW_COOKED);
  canvasX = (kDisplayWidth - gifWidth) / 2;
  canvasY = (kDisplayHeight - gifHeight) / 2;

  memset(canvasPixels, 0, kCanvasBytes);
  lv_obj_invalidate(gifCanvas);

  Serial.printf("Playing %s (%d x %d)\n", kGifPath, gifWidth, gifHeight);
  int frameDelayMs = 0;
  gifDrawLines = 0;
  gifFramesPlayed = 0;
  int playResult = 0;
  while ((playResult = gif.playFrame(true, &frameDelayMs)) > 0) {
    ++gifFramesPlayed;
    lv_timer_handler();
    yield();
  }

  // The decoder may draw the final frame on the call that reports end-of-file.
  lv_timer_handler();
  Serial.printf(
      "[gif] Playback ended: result=%d error=%d decoded-frames=%lu "
      "draw-callbacks=%lu final-delay=%d ms\n",
      playResult, gif.getLastError(), static_cast<unsigned long>(gifFramesPlayed),
      static_cast<unsigned long>(gifDrawLines), frameDelayMs);
  gif.close();
  gif.freeFrameBuf(freeGifBuffer);
  return true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("[boot] Veridian GIF player starting");
  Serial.printf("[memory] PSRAM found=%s size=%lu free=%lu bytes\n",
                psramFound() ? "yes" : "no",
                static_cast<unsigned long>(ESP.getPsramSize()),
                static_cast<unsigned long>(ESP.getFreePsram()));

  // Use the display, I/O expander, SD, and LVGL initialization provided by
  // Waveshare's ESP32-S3-Touch-LCD-2.8C Arduino demo.
  Serial.println("[init] I2C_Init");
  I2C_Init();
  Serial.println("[init] TCA9554PWR_Init");
  TCA9554PWR_Init(0x00);
  Serial.println("[init] LCD_Init");
  LCD_Init();
  Serial.println("[init] LCD_Init returned");
  Serial.println("[init] SD_Init");
  SD_Init();
  Serial.println("[init] SD_Init returned");
  Serial.printf("[sd] card-type=%u card-size=%llu bytes\n",
                static_cast<unsigned>(SD_MMC.cardType()),
                static_cast<unsigned long long>(SD_MMC.cardSize()));
  Serial.printf("[sd] %s exists=%s\n", kGifPath,
                SD_MMC.exists(kGifPath) ? "yes" : "no");
  if (SD_MMC.exists(kGifPath)) {
    File infoFile = SD_MMC.open(kGifPath, FILE_READ);
    if (infoFile) {
      Serial.printf("[sd] %s size=%lu bytes\n", kGifPath,
                    static_cast<unsigned long>(infoFile.size()));
      infoFile.close();
    }
  }
  Serial.println("[init] Lvgl_Init");
  Lvgl_Init();
  Serial.println("[init] Lvgl_Init returned");
  // EXIO8 controls the onboard buzzer. Drive it low after board setup; the
  // previous high setting did not silence this board.
  Set_EXIO(EXIO_PIN8, Low);

  if (sizeof(lv_color_t) != sizeof(uint16_t)) {
    Serial.println("This sketch requires 16-bit LVGL colors");
    while (true) {
      delay(1000);
    }
  }

  canvasPixels = static_cast<lv_color_t *>(
      heap_caps_malloc(kCanvasBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (canvasPixels == nullptr) {
    Serial.println("Could not allocate the GIF canvas in PSRAM");
    while (true) {
      delay(1000);
    }
  }
  Serial.printf("[memory] GIF canvas allocated: %lu bytes; PSRAM free=%lu bytes\n",
                static_cast<unsigned long>(kCanvasBytes),
                static_cast<unsigned long>(ESP.getFreePsram()));

  memset(canvasPixels, 0, kCanvasBytes);
  gifCanvas = lv_canvas_create(lv_scr_act());
  lv_canvas_set_buffer(gifCanvas, canvasPixels, kDisplayWidth, kDisplayHeight,
                       LV_IMG_CF_TRUE_COLOR);
  lv_obj_center(gifCanvas);
  lv_canvas_fill_bg(gifCanvas, lv_color_black(), LV_OPA_COVER);
  lv_timer_handler();
  Serial.println("[lvgl] canvas created, black background flushed");

  if (!SD_MMC.exists(kGifPath)) {
    Serial.printf("Missing %s at the root of the SD card\n", kGifPath);
  }
}

void loop() {
  if (!openAndPlayGif()) {
    static uint32_t lastRetryMessage = 0;
    const uint32_t now = millis();
    if (now - lastRetryMessage >= 5000) {
      Serial.println("[gif] Will retry in 1 second");
      lastRetryMessage = now;
    }
    delay(1000);
  }
}
