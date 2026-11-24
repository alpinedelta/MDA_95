#pragma once
#include <Arduino.h>
#include <fabgl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// MDA Solitaire - Ultra-Fast 720x350 1-Bit Offscreen Framebuffer Compositor
// Size: 90 bytes/row x 350 rows = 31,500 bytes (~30.8 KB), MSB-first matching
// FabGL PixelFormat::Mask.
//
// Combined with VGA2Controller hardware double-buffering + Canvas.swapBuffers(),
// this enables 100% tear-free, flicker-free full-card & full-stack dragging!
// ============================================================================

static constexpr int FB_W      = 720;
static constexpr int FB_H      = 350;
static constexpr int FB_STRIDE = FB_W / 8;         // 90 bytes
static constexpr int FB_BYTES  = FB_STRIDE * FB_H; // 31,500 bytes

class FrameBuf1Bit {
public:
  uint8_t * data = nullptr;

  bool alloc() {
    if (data) return true;
    data = (uint8_t *)calloc(FB_BYTES, 1);
    return data != nullptr;
  }

  void copyFrom(const FrameBuf1Bit & src) {
    if (data && src.data) memcpy(data, src.data, FB_BYTES);
  }

  void clear(bool white = false) {
    if (data) memset(data, white ? 0xFF : 0x00, FB_BYTES);
  }

  inline void setPixel(int x, int y, bool white) {
    if ((unsigned)x >= (unsigned)FB_W || (unsigned)y >= (unsigned)FB_H) return;
    uint8_t & b = data[y * FB_STRIDE + (x >> 3)];
    uint8_t m = 0x80 >> (x & 7);
    if (white) b |= m;
    else       b &= ~m;
  }

  inline void xorPixel(int x, int y) {
    if ((unsigned)x >= (unsigned)FB_W || (unsigned)y >= (unsigned)FB_H) return;
    data[y * FB_STRIDE + (x >> 3)] ^= (0x80 >> (x & 7));
  }

  void drawHLine(int x0, int x1, int y, bool white) {
    if ((unsigned)y >= (unsigned)FB_H) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x1 < 0 || x0 >= FB_W) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= FB_W) x1 = FB_W - 1;

    uint8_t * row = data + y * FB_STRIDE;
    int b0 = x0 >> 3;
    int b1 = x1 >> 3;
    uint8_t m0 = (uint8_t)(0xFF >> (x0 & 7));
    uint8_t m1 = (uint8_t)(0xFF << (7 - (x1 & 7)));

    if (b0 == b1) {
      uint8_t m = m0 & m1;
      if (white) row[b0] |= m;
      else       row[b0] &= ~m;
    } else {
      if (white) {
        row[b0] |= m0;
        if (b1 > b0 + 1) memset(row + b0 + 1, 0xFF, b1 - b0 - 1);
        row[b1] |= m1;
      } else {
        row[b0] &= ~m0;
        if (b1 > b0 + 1) memset(row + b0 + 1, 0x00, b1 - b0 - 1);
        row[b1] &= ~m1;
      }
    }
  }

  void drawVLine(int x, int y0, int y1, bool white) {
    if ((unsigned)x >= (unsigned)FB_W) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y1 < 0 || y0 >= FB_H) return;
    if (y0 < 0) y0 = 0;
    if (y1 >= FB_H) y1 = FB_H - 1;
    uint8_t m = 0x80 >> (x & 7);
    int bx = x >> 3;
    if (white) {
      for (int y = y0; y <= y1; ++y) data[y * FB_STRIDE + bx] |= m;
    } else {
      for (int y = y0; y <= y1; ++y) data[y * FB_STRIDE + bx] &= ~m;
    }
  }

  void fillRect(int x, int y, int w, int h, bool white) {
    if (w <= 0 || h <= 0) return;
    int y0 = max(0, y);
    int y1 = min(FB_H - 1, y + h - 1);
    int x0 = x, x1 = x + w - 1;
    for (int r = y0; r <= y1; ++r) drawHLine(x0, x1, r, white);
  }

  void drawRect(int x, int y, int w, int h, bool white) {
    if (w <= 0 || h <= 0) return;
    drawHLine(x, x + w - 1, y, white);
    drawHLine(x, x + w - 1, y + h - 1, white);
    drawVLine(x, y, y + h - 1, white);
    drawVLine(x + w - 1, y, y + h - 1, white);
  }

  // Rounded card rectangle (2px clipped corners for authentic playing-card silhouette)
  void fillRoundRect(int x, int y, int w, int h, bool white) {
    if (w < 6 || h < 6) { fillRect(x, y, w, h, white); return; }
    drawHLine(x + 2, x + w - 3, y,         white);
    drawHLine(x + 1, x + w - 2, y + 1,     white);
    for (int r = y + 2; r <= y + h - 3; ++r) {
      drawHLine(x, x + w - 1, r, white);
    }
    drawHLine(x + 1, x + w - 2, y + h - 2, white);
    drawHLine(x + 2, x + w - 3, y + h - 1, white);
  }

  void drawRoundRect(int x, int y, int w, int h, bool white) {
    if (w < 6 || h < 6) { drawRect(x, y, w, h, white); return; }
    drawHLine(x + 2, x + w - 3, y,         white);
    drawHLine(x + 2, x + w - 3, y + h - 1, white);
    drawVLine(x,         y + 2, y + h - 3, white);
    drawVLine(x + w - 1, y + 2, y + h - 3, white);
    setPixel(x + 1,     y + 1,     white);
    setPixel(x + w - 2, y + 1,     white);
    setPixel(x + 1,     y + h - 2, white);
    setPixel(x + w - 2, y + h - 2, white);
  }

  void drawDashedRoundRect(int x, int y, int w, int h, bool white) {
    for (int dx = 2; dx < w - 2; dx += 4) {
      setPixel(x + dx, y, white);
      setPixel(x + dx + 1, y, white);
      setPixel(x + dx, y + h - 1, white);
      setPixel(x + dx + 1, y + h - 1, white);
    }
    for (int dy = 2; dy < h - 2; dy += 4) {
      setPixel(x, y + dy, white);
      setPixel(x, y + dy + 1, white);
      setPixel(x + w - 1, y + dy, white);
      setPixel(x + w - 1, y + dy + 1, white);
    }
  }

  // Fast 8x8 repeating pattern fill (used for CRT-safe dark felt & card backs)
  void fillPatternRect(int x, int y, int w, int h, const uint8_t pat[8]) {
    int y0 = max(0, y);
    int y1 = min(FB_H - 1, y + h - 1);
    int x0 = max(0, x);
    int x1 = min(FB_W - 1, x + w - 1);
    if (x0 > x1 || y0 > y1) return;

    // Fast byte-aligned path for full-width felt fills
    if (x0 == 0 && x1 == FB_W - 1) {
      for (int r = y0; r <= y1; ++r) {
        memset(data + r * FB_STRIDE, pat[r & 7], FB_STRIDE);
      }
      return;
    }
    for (int r = y0; r <= y1; ++r) {
      uint8_t p = pat[r & 7];
      for (int c = x0; c <= x1; ++c) {
        setPixel(c, r, (p & (0x80 >> (c & 7))) != 0);
      }
    }
  }

  void drawLine(int x0, int y0, int x1, int y1, bool white) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
      setPixel(x0, y0, white);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }

  // Direct 1-bit font rendering into RAM framebuffer (zero FabGL queue overhead)
  void drawChar(int x, int y, uint8_t ch, const fabgl::FontInfo * font, bool white, bool bold = false) {
    if (!font || !font->data) return;
    int fw = font->width;
    int fh = font->height;
    int rowBytes = (fw + 7) / 8;
    const uint8_t * glyph = font->data + (uint32_t)ch * fh * rowBytes;
    for (int r = 0; r < fh; ++r) {
      bool prevOn = false;
      for (int c = 0; c < fw; ++c) {
        bool on = (glyph[r * rowBytes + (c >> 3)] & (0x80 >> (c & 7))) != 0;
        if (on || (bold && prevOn)) setPixel(x + c, y + r, white);
        prevOn = on;
      }
      if (bold && prevOn) setPixel(x + fw, y + r, white);
    }
  }

  void drawText(int x, int y, const char * str, const fabgl::FontInfo * font, bool white, bool bold = false) {
    if (!str || !font) return;
    int adv = font->width + (bold ? 1 : 0);
    while (*str) {
      drawChar(x, y, (uint8_t)(*str++), font, white, bold);
      x += adv;
    }
  }

  // Blit a small 1-bit MSB-first icon (e.g. suit pip) in either black or white ink
  void drawMonoSprite(int x, int y, int w, int h, const uint8_t * bits, bool white) {
    int stride = (w + 7) / 8;
    for (int r = 0; r < h; ++r) {
      for (int c = 0; c < w; ++c) {
        if (bits[r * stride + (c >> 3)] & (0x80 >> (c & 7))) {
          setPixel(x + c, y + r, white);
        }
      }
    }
  }

  // Crisp 1-bit Monochrome Mouse Cursor (white interior + 1px black outline, visible everywhere!)
  void drawCursor(int cx, int cy, bool grabbing) {
    // 12x16 arrow mask (outline) and fill
    static const uint16_t ARROW_OUTLINE[16] = {
      0x8000, 0xC000, 0xE000, 0xF000,
      0xF800, 0xFC00, 0xFE00, 0xFF00,
      0xFF80, 0xFFC0, 0xFE00, 0xEF00,
      0xC780, 0x0780, 0x03C0, 0x03C0
    };
    static const uint16_t ARROW_FILL[16] = {
      0x0000, 0x4000, 0x6000, 0x7000,
      0x7800, 0x7C00, 0x7E00, 0x7F00,
      0x7F80, 0x7E00, 0x6C00, 0x4600,
      0x0600, 0x0300, 0x0300, 0x0000
    };
    // 14x14 closed-hand grabbing cursor while dragging cards!
    static const uint16_t GRAB_OUTLINE[14] = {
      0x0000, 0x0DC0, 0x1FE0, 0x3FF0,
      0x7FF8, 0xFFF8, 0xFFF8, 0xFFF8,
      0x7FF8, 0x7FF0, 0x3FF0, 0x1FE0,
      0x1FE0, 0x0FC0
    };
    static const uint16_t GRAB_FILL[14] = {
      0x0000, 0x0000, 0x0D40, 0x1550,
      0x3FF0, 0x7FF0, 0x7FF0, 0x7FF0,
      0x3FF0, 0x3FE0, 0x1FE0, 0x0FC0,
      0x0FC0, 0x0000
    };

    if (grabbing) {
      int ox = cx - 6, oy = cy - 5;
      for (int r = 0; r < 14; ++r) {
        uint16_t o = GRAB_OUTLINE[r], f = GRAB_FILL[r];
        for (int c = 0; c < 14; ++c) {
          uint16_t m = 0x8000 >> c;
          if (f & m)      setPixel(ox + c, oy + r, true);
          else if (o & m) setPixel(ox + c, oy + r, false);
        }
      }
    } else {
      for (int r = 0; r < 16; ++r) {
        uint16_t o = ARROW_OUTLINE[r], f = ARROW_FILL[r];
        for (int c = 0; c < 12; ++c) {
          uint16_t m = 0x8000 >> c;
          if (f & m)      setPixel(cx + c, cy + r, true);
          else if (o & m) setPixel(cx + c, cy + r, false);
        }
      }
    }
  }
};
