#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "mda_canvas.h"

// ============================================================================
// MDA Paint - SD Card Driver & Standard Windows/Linux BMP Loader/Saver
// LilyGO VGA32 SD Card Pins: CS = 13, MOSI = 12, MISO = 2, CLK = 14
// Saves standard 1-bit uncompressed Windows/Linux .BMP (640x296 = 23,742 B).
// Loads standard uncompressed 1-bit, 4-bit, 8-bit, and 24-bit .BMP files
// (1-bit loads losslessly; color/gray BMPs dither cleanly with 4x4 Bayer).
// ============================================================================

static constexpr int SD_PIN_CS   = 13;
static constexpr int SD_PIN_MOSI = 12;
static constexpr int SD_PIN_MISO = 2;
static constexpr int SD_PIN_CLK  = 14;

static constexpr int MAX_SD_ENTRIES = 64;

// Defined in header so Arduino's auto-prototype generator sees it before any .ino prototypes
enum FileBrowserMode : uint8_t { FB_CLOSED = 0, FB_OPEN, FB_SAVE };

struct SDFileEntry {
  char     name[32];
  bool     isDir;
  uint32_t size;
};

static bool        gSDMounted      = false;
static char        gSDCurDir[64]   = "/";
static SDFileEntry gSDEntries[MAX_SD_ENTRIES];
static int         gSDEntryCount   = 0;

// 4x4 Bayer ordered dithering thresholds (0..255) for loading 8/24-bit PC BMPs
static const uint8_t BAYER4X4[4][4] = {
  {   8, 136,  40, 168 },
  { 200,  72, 232, 104 },
  {  56, 184,  24, 152 },
  { 248, 120, 216,  88 }
};

inline bool hasBmpExtension(const char * name) {
  size_t len = strlen(name);
  if (len < 4) return false;
  const char * ext = name + len - 4;
  return (ext[0] == '.' &&
          toupper((unsigned char)ext[1]) == 'B' &&
          toupper((unsigned char)ext[2]) == 'M' &&
          toupper((unsigned char)ext[3]) == 'P');
}

inline bool sdMountCard(bool forceRemount = false) {
  if (gSDMounted && !forceRemount) return true;
  if (forceRemount) {
    SD.end();
    gSDMounted = false;
  }
  SPI.begin(SD_PIN_CLK, SD_PIN_MISO, SD_PIN_MOSI, SD_PIN_CS);
  // Try 4 MHz first (clean & reliable alongside VGA DMA), fall back to 1 MHz
  if (SD.begin(SD_PIN_CS, SPI, 4000000) || SD.begin(SD_PIN_CS, SPI, 1000000)) {
    gSDMounted = true;
    return true;
  }
  gSDMounted = false;
  return false;
}

inline void buildFullPath(char * out, size_t outSize, const char * dir, const char * name) {
  if (strcmp(dir, "/") == 0) {
    snprintf(out, outSize, "/%s", name);
  } else {
    snprintf(out, outSize, "%s/%s", dir, name);
  }
}

inline void sdGoParentDir() {
  size_t len = strlen(gSDCurDir);
  if (len <= 1) {
    strcpy(gSDCurDir, "/");
    return;
  }
  // Strip trailing slash if any
  if (gSDCurDir[len - 1] == '/') {
    gSDCurDir[len - 1] = '\0';
    len--;
  }
  char * slash = strrchr(gSDCurDir, '/');
  if (!slash || slash == gSDCurDir) {
    strcpy(gSDCurDir, "/");
  } else {
    *slash = '\0';
  }
}

inline bool sdRefreshDirectory() {
  gSDEntryCount = 0;
  if (!sdMountCard(false)) return false;

  File dir = SD.open(gSDCurDir);
  if (!dir || !dir.isDirectory()) {
    strcpy(gSDCurDir, "/");
    dir = SD.open("/");
    if (!dir || !dir.isDirectory()) {
      gSDMounted = false;
      return false;
    }
  }

  // Add ".." parent entry if not at root
  if (strcmp(gSDCurDir, "/") != 0) {
    strcpy(gSDEntries[gSDEntryCount].name, "..");
    gSDEntries[gSDEntryCount].isDir = true;
    gSDEntries[gSDEntryCount].size  = 0;
    gSDEntryCount++;
  }

  File entry = dir.openNextFile();
  while (entry && gSDEntryCount < MAX_SD_ENTRIES) {
    const char * rawName = entry.name();
    // Strip leading directory path (ESP32 Arduino Core 2.x returns full path)
    const char * base = strrchr(rawName, '/');
    base = base ? (base + 1) : rawName;

    bool skip = (base[0] == '\0' || base[0] == '.');
    bool isD  = entry.isDirectory();

    if (!skip && (isD || hasBmpExtension(base))) {
      strncpy(gSDEntries[gSDEntryCount].name, base, sizeof(gSDEntries[0].name) - 1);
      gSDEntries[gSDEntryCount].name[sizeof(gSDEntries[0].name) - 1] = '\0';
      gSDEntries[gSDEntryCount].isDir = isD;
      gSDEntries[gSDEntryCount].size  = isD ? 0 : (uint32_t)entry.size();
      gSDEntryCount++;
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();

  // Sort: ".." first, then directories alphabetically, then BMP files alphabetically
  for (int i = 0; i < gSDEntryCount - 1; ++i) {
    for (int j = i + 1; j < gSDEntryCount; ++j) {
      bool swapIt = false;
      const SDFileEntry & a = gSDEntries[i];
      const SDFileEntry & b = gSDEntries[j];
      if (strcmp(b.name, "..") == 0) {
        swapIt = true;
      } else if (strcmp(a.name, "..") != 0) {
        if (a.isDir != b.isDir) {
          swapIt = (!a.isDir && b.isDir);
        } else {
          swapIt = (strcasecmp(a.name, b.name) > 0);
        }
      }
      if (swapIt) {
        SDFileEntry tmp = gSDEntries[i];
        gSDEntries[i] = gSDEntries[j];
        gSDEntries[j] = tmp;
      }
    }
  }
  return true;
}

// Little-endian helpers for BMP headers
inline void writeLE16(uint8_t * p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}
inline void writeLE32(uint8_t * p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}
inline uint16_t readLE16(const uint8_t * p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
inline uint32_t readLE32(const uint8_t * p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline int32_t readLE32S(const uint8_t * p) {
  return (int32_t)readLE32(p);
}

// ============================================================================
// Save 640x296 1-Bit Monochrome Canvas as Standard Windows/Linux .BMP
// Format: 1-bit uncompressed BI_RGB, 62-byte header (14 + 40 + 8 palette),
//         bottom-up scanlines, 80 bytes/row (4-byte aligned), MSB-first.
//         Total file size: 23,742 bytes.
// ============================================================================
inline bool sdSaveBMP(const char * fullPath, const MonoCanvas & canvas, char * errOut = nullptr, size_t errSize = 0) {
  if (!sdMountCard(false)) {
    if (errOut) snprintf(errOut, errSize, "SD card not mounted");
    return false;
  }

  // Overwrite cleanly if file already exists
  if (SD.exists(fullPath)) {
    SD.remove(fullPath);
  }

  File f = SD.open(fullPath, FILE_WRITE);
  if (!f) {
    if (errOut) snprintf(errOut, errSize, "Cannot create file");
    return false;
  }

  // 14-byte BITMAPFILEHEADER + 40-byte BITMAPINFOHEADER + 8-byte 2-color palette = 62 bytes
  uint8_t hdr[62];
  memset(hdr, 0, sizeof(hdr));

  static constexpr uint32_t HEADER_SIZE = 62;
  static constexpr uint32_t IMAGE_SIZE  = (uint32_t)CANVAS_STRIDE * (uint32_t)CANVAS_H; // 80 * 296 = 23680
  static constexpr uint32_t FILE_SIZE   = HEADER_SIZE + IMAGE_SIZE;

  // BITMAPFILEHEADER
  hdr[0] = 'B';
  hdr[1] = 'M';
  writeLE32(hdr + 2,  FILE_SIZE);
  writeLE32(hdr + 10, HEADER_SIZE);

  // BITMAPINFOHEADER
  writeLE32(hdr + 14, 40);                 // biSize
  writeLE32(hdr + 18, (uint32_t)CANVAS_W); // biWidth = 640
  writeLE32(hdr + 22, (uint32_t)CANVAS_H); // biHeight = 296 (bottom-up)
  writeLE16(hdr + 26, 1);                  // biPlanes = 1
  writeLE16(hdr + 28, 1);                  // biBitCount = 1 (monochrome)
  writeLE32(hdr + 30, 0);                  // biCompression = BI_RGB (uncompressed)
  writeLE32(hdr + 34, IMAGE_SIZE);         // biSizeImage
  writeLE32(hdr + 38, 2835);               // biXPelsPerMeter (~72 DPI)
  writeLE32(hdr + 42, 2835);               // biYPelsPerMeter (~72 DPI)
  writeLE32(hdr + 46, 2);                  // biClrUsed = 2
  writeLE32(hdr + 50, 2);                  // biClrImportant = 2

  // Color Palette (BGRA):
  // Index 0 = Black (0, 0, 0, 0)
  hdr[54] = 0x00; hdr[55] = 0x00; hdr[56] = 0x00; hdr[57] = 0x00;
  // Index 1 = White (255, 255, 255, 0)
  hdr[58] = 0xFF; hdr[59] = 0xFF; hdr[60] = 0xFF; hdr[61] = 0x00;

  if (f.write(hdr, sizeof(hdr)) != sizeof(hdr)) {
    f.close();
    if (errOut) snprintf(errOut, errSize, "Header write failed");
    return false;
  }

  // Write rows bottom-up (row CANVAS_H - 1 down to 0)
  for (int y = CANVAS_H - 1; y >= 0; --y) {
    const uint8_t * rowPtr = canvas.buf + (size_t)y * CANVAS_STRIDE;
    if (f.write(rowPtr, CANVAS_STRIDE) != CANVAS_STRIDE) {
      f.close();
      if (errOut) snprintf(errOut, errSize, "Pixel write failed");
      return false;
    }
  }

  f.flush();
  f.close();
  return true;
}

// ============================================================================
// Load Standard Windows/Linux .BMP (1-bit, 4-bit, 8-bit, or 24-bit BI_RGB)
// ============================================================================
inline bool sdLoadBMP(const char * fullPath, MonoCanvas & canvas, bool paperVal, char * errOut = nullptr, size_t errSize = 0) {
  if (!sdMountCard(false)) {
    if (errOut) snprintf(errOut, errSize, "SD card not mounted");
    return false;
  }

  File f = SD.open(fullPath, FILE_READ);
  if (!f) {
    if (errOut) snprintf(errOut, errSize, "Cannot open file");
    return false;
  }

  uint8_t fh[54];
  if (f.read(fh, 54) != 54 || fh[0] != 'B' || fh[1] != 'M') {
    f.close();
    if (errOut) snprintf(errOut, errSize, "Not a valid BMP file");
    return false;
  }

  uint32_t dataOffset  = readLE32(fh + 10);
  uint32_t dibSize     = readLE32(fh + 14);
  int32_t  bmpW        = readLE32S(fh + 18);
  int32_t  bmpH_signed = readLE32S(fh + 22);
  uint16_t bpp         = readLE16(fh + 28);
  uint32_t compression = readLE32(fh + 30);

  bool topDown = (bmpH_signed < 0);
  int32_t bmpH = topDown ? -bmpH_signed : bmpH_signed;

  if (bmpW <= 0 || bmpH <= 0 || bmpW > 4096 || bmpH > 4096) {
    f.close();
    if (errOut) snprintf(errOut, errSize, "Invalid BMP dimensions");
    return false;
  }
  if (compression != 0) {
    f.close();
    if (errOut) snprintf(errOut, errSize, "Compressed BMP unsupported");
    return false;
  }
  if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24) {
    f.close();
    if (errOut) snprintf(errOut, errSize, "Unsupported %ubpp BMP", bpp);
    return false;
  }

  // Read palette luminance table for indexed formats (1/4/8 bpp)
  uint8_t palLum[256];
  if (bpp <= 8) {
    int numColors = 1 << bpp;
    uint32_t palOffset = 14 + dibSize;
    f.seek(palOffset);
    for (int i = 0; i < numColors; ++i) {
      uint8_t bgra[4] = { 0, 0, 0, 0 };
      if (f.read(bgra, 4) == 4) {
        palLum[i] = (uint8_t)((bgra[2] * 77 + bgra[1] * 150 + bgra[0] * 29) >> 8);
      } else {
        palLum[i] = (uint8_t)((i * 255) / max(1, numColors - 1));
      }
    }
  }

  // Row stride in BMP is always padded to a 4-byte boundary
  uint32_t rowBytes = (((uint32_t)bmpW * bpp + 31) / 32) * 4;
  uint8_t * rowBuf = (uint8_t *)malloc(rowBytes);
  if (!rowBuf) {
    f.close();
    if (errOut) snprintf(errOut, errSize, "BMP too wide for RAM");
    return false;
  }

  // Center smaller images on canvas; align top-left if larger
  int offX = (bmpW < CANVAS_W) ? (CANVAS_W - bmpW) / 2 : 0;
  int offY = (bmpH < CANVAS_H) ? (CANVAS_H - bmpH) / 2 : 0;

  canvas.pushUndo();
  canvas.clear(paperVal);

  f.seek(dataOffset);
  bool bit1Bright = (bpp == 1) ? (palLum[1] >= palLum[0]) : true;

  for (int srcRow = 0; srcRow < bmpH; ++srcRow) {
    int dstY = offY + (topDown ? srcRow : (bmpH - 1 - srcRow));
    if (f.read(rowBuf, rowBytes) != rowBytes) break;
    if (dstY < 0 || dstY >= CANVAS_H) continue;

    int maxX = min((int)bmpW, CANVAS_W - offX);
    if (bpp == 1) {
      for (int sx = 0; sx < maxX; ++sx) {
        int dstX = offX + sx;
        if (dstX < 0 || dstX >= CANVAS_W) continue;
        bool bit = (rowBuf[sx >> 3] & (0x80 >> (sx & 7))) != 0;
        canvas.setPixelRaw(dstX, dstY, bit1Bright ? bit : !bit);
      }
    } else if (bpp == 4) {
      for (int sx = 0; sx < maxX; ++sx) {
        int dstX = offX + sx;
        if (dstX < 0 || dstX >= CANVAS_W) continue;
        uint8_t idx = (sx & 1) ? (rowBuf[sx >> 1] & 0x0F) : (rowBuf[sx >> 1] >> 4);
        canvas.setPixelRaw(dstX, dstY, palLum[idx] > BAYER4X4[dstY & 3][dstX & 3]);
      }
    } else if (bpp == 8) {
      for (int sx = 0; sx < maxX; ++sx) {
        int dstX = offX + sx;
        if (dstX < 0 || dstX >= CANVAS_W) continue;
        uint8_t lum = palLum[rowBuf[sx]];
        canvas.setPixelRaw(dstX, dstY, lum > BAYER4X4[dstY & 3][dstX & 3]);
      }
    } else if (bpp == 24) {
      for (int sx = 0; sx < maxX; ++sx) {
        int dstX = offX + sx;
        if (dstX < 0 || dstX >= CANVAS_W) continue;
        const uint8_t * px = rowBuf + sx * 3;
        uint8_t lum = (uint8_t)((px[2] * 77 + px[1] * 150 + px[0] * 29) >> 8);
        canvas.setPixelRaw(dstX, dstY, lum > BAYER4X4[dstY & 3][dstX & 3]);
      }
    }
  }

  free(rowBuf);
  f.close();
  canvas.markFullDirty();
  return true;
}

// Suggest a clean next filename like PAINT01.BMP .. PAINT99.BMP that doesn't exist yet
inline void sdSuggestNextFilename(char * outName, size_t outSize) {
  if (!sdMountCard(false)) {
    snprintf(outName, outSize, "PAINT01.BMP");
    return;
  }
  char testPath[96];
  for (int n = 1; n <= 99; ++n) {
    char cand[20];
    snprintf(cand, sizeof(cand), "PAINT%02d.BMP", n);
    buildFullPath(testPath, sizeof(testPath), gSDCurDir, cand);
    if (!SD.exists(testPath)) {
      strncpy(outName, cand, outSize - 1);
      outName[outSize - 1] = '\0';
      return;
    }
  }
  snprintf(outName, outSize, "PAINT99.BMP");
}
