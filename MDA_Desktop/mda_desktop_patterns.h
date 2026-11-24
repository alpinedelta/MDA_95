#pragma once
#include <stdint.h>
#include <string.h>
#include <fabgl.h>

// ============================================================================
// MDA Desktop - 16 Classic 1-Bit Monochrome Hercules Wallpaper Patterns
// Pre-baked into a full-width 720x8 tile strip (720 bytes!) so painting any
// wallpaper rectangle uses fast FabGL Bitmap blits instead of per-pixel calls.
// ============================================================================

static constexpr int NUM_WALLPAPERS = 16;

static const uint8_t WALLPAPER_PATTERNS[NUM_WALLPAPERS][8] = {
  // 0: Solid Black (Classic CRT-Safe Dark)
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
  // 1: Night Sky (Sparse Stars)
  { 0x80, 0x00, 0x00, 0x08, 0x00, 0x00, 0x20, 0x00 },
  // 2: 12% Fine Stipple
  { 0x80, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00 },
  // 3: 25% Classic Win95 Monochrome Dither
  { 0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00 },
  // 4: 50% Classic Checkerboard
  { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 },
  // 5: Diagonal Pinstripe (/)
  { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 },
  // 6: Diagonal Pinstripe (\)
  { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 },
  // 7: Fine Grid / Graph Paper
  { 0xFF, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 },
  // 8: Diamond Lattice
  { 0x10, 0x28, 0x44, 0x82, 0x01, 0x82, 0x44, 0x28 },
  // 9: Subtle Scanlines
  { 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00 },
  // 10: Vertical Pinstripes
  { 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88 },
  // 11: Classic Brick Wall
  { 0xFF, 0x80, 0x80, 0x80, 0xFF, 0x08, 0x08, 0x08 },
  // 12: Basket Weave
  { 0xE7, 0x42, 0x42, 0xE7, 0x18, 0xBD, 0xBD, 0x18 },
  // 13: Herringbone Tweed
  { 0x88, 0x50, 0x20, 0x50, 0x88, 0x05, 0x02, 0x05 },
  // 14: Waves / Scales
  { 0x3C, 0x42, 0x81, 0x00, 0x81, 0x42, 0x3C, 0x00 },
  // 15: Solid Phosphor (Light Desktop)
  { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
};

static const char * WALLPAPER_NAMES[NUM_WALLPAPERS] = {
  "Solid Dark (CRT)",
  "Night Stars",
  "12% Fine Dots",
  "25% Win95 Dither",
  "50% Checkerboard",
  "Diagonal /",
  "Diagonal \\",
  "Graph Grid",
  "Diamond Lattice",
  "Scanlines",
  "Vertical Stripes",
  "Brick Wall",
  "Basket Weave",
  "Herringbone",
  "Retro Waves",
  "Solid Phosphor"
};

// Persistent 720x8 tile strip (90 bytes/row x 8 rows = 720 bytes)
static constexpr int WP_STRIP_W      = 720;
static constexpr int WP_STRIP_H      = 8;
static constexpr int WP_STRIP_STRIDE = WP_STRIP_W / 8; // 90 bytes
static uint8_t gWallpaperStripBuf[WP_STRIP_STRIDE * WP_STRIP_H];
static const fabgl::Bitmap gWallpaperStripBmp(
    WP_STRIP_W, WP_STRIP_H, gWallpaperStripBuf,
    fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255));

// Preview tile for Display Properties mini-monitor (128x64 = 16 bytes/row x 64 = 1024 bytes)
static constexpr int WP_PREV_W      = 128;
static constexpr int WP_PREV_H      = 64;
static constexpr int WP_PREV_STRIDE = WP_PREV_W / 8;
static uint8_t gWallpaperPrevBuf[WP_PREV_STRIDE * WP_PREV_H];
static const fabgl::Bitmap gWallpaperPrevBmp(
    WP_PREV_W, WP_PREV_H, gWallpaperPrevBuf,
    fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255));

inline void buildWallpaperStrip(int wpIdx) {
  wpIdx = (wpIdx < 0 || wpIdx >= NUM_WALLPAPERS) ? 0 : wpIdx;
  for (int r = 0; r < WP_STRIP_H; ++r) {
    memset(gWallpaperStripBuf + r * WP_STRIP_STRIDE, WALLPAPER_PATTERNS[wpIdx][r], WP_STRIP_STRIDE);
  }
}

inline void buildWallpaperPreview(int wpIdx) {
  wpIdx = (wpIdx < 0 || wpIdx >= NUM_WALLPAPERS) ? 0 : wpIdx;
  for (int r = 0; r < WP_PREV_H; ++r) {
    memset(gWallpaperPrevBuf + r * WP_PREV_STRIDE, WALLPAPER_PATTERNS[wpIdx][r & 7], WP_PREV_STRIDE);
  }
}

// Fast clipped wallpaper fill over any screen rectangle [x0..x1, y0..y1]
inline void drawWallpaperRect(fabgl::Canvas & canvas, int x0, int y0, int x1, int y1, int wpIdx) {
  if (x0 > x1 || y0 > y1) return;
  canvas.setClippingRect(fabgl::Rect(x0, y0, x1, y1));
  if (wpIdx == 0) {
    canvas.setBrushColor(fabgl::Color::Black);
    canvas.fillRectangle(x0, y0, x1, y1);
  } else if (wpIdx == NUM_WALLPAPERS - 1) {
    canvas.setBrushColor(fabgl::Color::White);
    canvas.fillRectangle(x0, y0, x1, y1);
  } else {
    canvas.setBrushColor(fabgl::Color::Black);
    canvas.fillRectangle(x0, y0, x1, y1);
    int startY = (y0 / WP_STRIP_H) * WP_STRIP_H;
    for (int sy = startY; sy <= y1; sy += WP_STRIP_H) {
      canvas.drawBitmap(0, sy, &gWallpaperStripBmp);
    }
  }
  canvas.setClippingRect(fabgl::Rect(0, 0, 719, 349));
}
