#pragma once
#include <stdint.h>

// ============================================================================
// MDA Paint - 16 Classic 8x8 1-Bit Monochrome Patterns (Hercules / MDA)
// Strictly 1-bit ON/OFF (no grayscale, no intensity).
// Each row is 8 bits, MSB (bit 7) = leftmost pixel.
// ============================================================================

static constexpr uint8_t NUM_PATTERNS = 16;

static const uint8_t MDA_PATTERNS[NUM_PATTERNS][8] = {
  // 0: Solid Ink (100%)
  { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
  // 1: Solid Paper (0% / Hollow)
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
  // 2: 50% Checkerboard Dither
  { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
  // 3: 25% Light Stipple
  { 0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00 },
  // 4: 75% Heavy Stipple
  { 0x77, 0xFF, 0xDD, 0xFF, 0x77, 0xFF, 0xDD, 0xFF },
  // 5: 12.5% Sparse Dots
  { 0x80, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00 },
  // 6: Horizontal Scanlines
  { 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00 },
  // 7: Vertical Pin-Stripes
  { 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA },
  // 8: Wide Horizontal Bars
  { 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00 },
  // 9: Wide Vertical Bars
  { 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC },
  // 10: Diagonal Hatch (/)
  { 0x11, 0x22, 0x44, 0x88, 0x11, 0x22, 0x44, 0x88 },
  // 11: Diagonal Hatch (\)
  { 0x88, 0x44, 0x22, 0x11, 0x88, 0x44, 0x22, 0x11 },
  // 12: Square Crosshatch
  { 0xFF, 0x88, 0x88, 0x88, 0xFF, 0x88, 0x88, 0x88 },
  // 13: Diamond / Diagonal Grid
  { 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81 },
  // 14: Classic Brick Wall
  { 0xFF, 0x80, 0x80, 0x80, 0xFF, 0x08, 0x08, 0x08 },
  // 15: Basket Weave
  { 0xE7, 0x42, 0x42, 0xE7, 0x18, 0xBD, 0xBD, 0x18 }
};

static const char * MDA_PATTERN_NAMES[NUM_PATTERNS] = {
  "Solid",
  "Clear",
  "50% Dither",
  "25% Light",
  "75% Heavy",
  "12% Sparse",
  "Horiz Line",
  "Vert Line",
  "Horiz Bar",
  "Vert Bar",
  "Diag /",
  "Diag \\",
  "Crosshatch",
  "Diamond",
  "Brick",
  "Weave"
};

// Returns true if pattern bit at (x, y) is set (1)
inline bool samplePatternBit(uint8_t patIdx, int x, int y) {
  if (patIdx >= NUM_PATTERNS) patIdx = 0;
  uint8_t row = MDA_PATTERNS[patIdx][y & 7];
  return (row & (0x80 >> (x & 7))) != 0;
}

// Evaluates final monochrome pixel value (0 = black, 1 = phosphor)
// for a given pattern and active ink polarity.
// When ink == true  (phosphor ink): pattern 1 -> phosphor (1), pattern 0 -> black (0)
// When ink == false (black ink):    pattern 1 -> black (0),    pattern 0 -> phosphor (1)
inline bool evaluatePatternPixel(uint8_t patIdx, int x, int y, bool ink) {
  bool bit = samplePatternBit(patIdx, x, y);
  return ink ? bit : !bit;
}

// Pre-baked 24x13 (3 bytes/row) pattern swatch bitmaps so the pattern bar renders
// with 17 fast drawBitmap primitives instead of ~2,800 individual setPixel calls.
static constexpr int PAT_BMP_W      = 24;
static constexpr int PAT_BMP_H      = 13;
static constexpr int PAT_BMP_STRIDE = PAT_BMP_W / 8; // 3 bytes
static uint8_t gPatSwatchBuf[NUM_PATTERNS][PAT_BMP_STRIDE * PAT_BMP_H];

static constexpr int ACT_BMP_W      = 32;
static constexpr int ACT_BMP_H      = 15;
static constexpr int ACT_BMP_STRIDE = ACT_BMP_W / 8; // 4 bytes
static uint8_t gActivePatBuf[ACT_BMP_STRIDE * ACT_BMP_H];

inline void rebuildPatternSwatchBuffers(bool ink, uint8_t activePatIdx) {
  for (int p = 0; p < NUM_PATTERNS; ++p) {
    for (int r = 0; r < PAT_BMP_H; ++r) {
      uint8_t rowByte = MDA_PATTERNS[p][r & 7];
      if (!ink) rowByte = ~rowByte;
      gPatSwatchBuf[p][r * PAT_BMP_STRIDE + 0] = rowByte;
      gPatSwatchBuf[p][r * PAT_BMP_STRIDE + 1] = rowByte;
      gPatSwatchBuf[p][r * PAT_BMP_STRIDE + 2] = rowByte;
    }
  }
  uint8_t ap = activePatIdx % NUM_PATTERNS;
  for (int r = 0; r < ACT_BMP_H; ++r) {
    uint8_t rowByte = MDA_PATTERNS[ap][r & 7];
    if (!ink) rowByte = ~rowByte;
    gActivePatBuf[r * ACT_BMP_STRIDE + 0] = rowByte;
    gActivePatBuf[r * ACT_BMP_STRIDE + 1] = rowByte;
    gActivePatBuf[r * ACT_BMP_STRIDE + 2] = rowByte;
    gActivePatBuf[r * ACT_BMP_STRIDE + 3] = rowByte;
  }
}
