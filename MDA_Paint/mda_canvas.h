#pragma once
#include <Arduino.h>
#include <fabgl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "mda_patterns.h"

// ============================================================================
// MDA Paint - 1-Bit Monochrome Offscreen Canvas & Rasterizer
// Canvas: 640 x 296 pixels = 80 bytes/row x 296 rows = 23,680 bytes (~23.1 KB)
// Strictly 1-bit monochrome (0 = Black, 1 = Phosphor White).
// MSB-first bit packing directly matches FabGL PixelFormat::Mask!
// ============================================================================

static constexpr int CANVAS_W      = 640;
static constexpr int CANVAS_H      = 296;
static constexpr int CANVAS_STRIDE = CANVAS_W / 8;         // 80 bytes
static constexpr int CANVAS_BYTES  = CANVAS_STRIDE * CANVAS_H; // 23,680 bytes
static constexpr int MAX_UNDO_SLOTS = 4;

enum BrushShape : uint8_t {
  BRUSH_ROUND = 0,
  BRUSH_SQUARE,
  BRUSH_SLASH,
  BRUSH_BACKSLASH,
  BRUSH_SHAPE_COUNT
};

enum FillMode : uint8_t {
  FILL_OUTLINE = 0, // Hollow outline only
  FILL_BOTH,        // Outline + Pattern fill
  FILL_PATTERN,     // Pattern fill without outline
  FILL_MODE_COUNT
};

static const uint8_t BRUSH_SIZES[4] = { 1, 2, 4, 7 };

// Helper for 1-bit bitmap selection / clipboard storage
struct MonoBuffer {
  uint8_t * data   = nullptr;
  int       width  = 0;
  int       height = 0;
  int       stride = 0;

  void freeBuf() {
    if (data) {
      free(data);
      data = nullptr;
    }
    width = height = stride = 0;
  }

  bool alloc(int w, int h) {
    freeBuf();
    if (w <= 0 || h <= 0) return false;
    stride = (w + 7) / 8;
    data = (uint8_t *)calloc(stride * h, 1);
    if (!data) {
      stride = 0;
      return false;
    }
    width  = w;
    height = h;
    return true;
  }

  inline bool getPixel(int x, int y) const {
    if (!data || x < 0 || y < 0 || x >= width || y >= height) return false;
    return (data[y * stride + (x >> 3)] & (0x80 >> (x & 7))) != 0;
  }

  inline void setPixel(int x, int y, bool v) {
    if (!data || x < 0 || y < 0 || x >= width || y >= height) return;
    uint8_t & b = data[y * stride + (x >> 3)];
    uint8_t mask = 0x80 >> (x & 7);
    if (v) b |= mask;
    else   b &= ~mask;
  }
};

class MonoCanvas {
public:
  uint8_t * buf = nullptr;

  // Dirty bounding box for fast partial screen blits
  bool dirty = false;
  int  dirtyX0 = 0, dirtyY0 = 0, dirtyX1 = 0, dirtyY1 = 0;

  // Undo / Redo circular history
  uint8_t * undoSlots[MAX_UNDO_SLOTS] = { nullptr };
  int       undoCapacity = 0;
  int       undoHead     = 0; // next write slot
  int       undoCount    = 0; // available undos
  int       redoCount    = 0; // available redos

  // Current snapshot stored separately when stepping back for Redo
  uint8_t * redoSlot = nullptr;

  bool init() {
    buf = (uint8_t *)calloc(CANVAS_BYTES, 1);
    if (!buf) return false;

    // Allocate undo slots dynamically while keeping generous heap headroom (~72 KB)
    // for FabGL PS/2 queues, display primitives, and FreeRTOS tasks.
    undoCapacity = 0;
    for (int i = 0; i < MAX_UNDO_SLOTS; ++i) {
      if (ESP.getFreeHeap() > (uint32_t)(CANVAS_BYTES + 72000)) {
        undoSlots[i] = (uint8_t *)malloc(CANVAS_BYTES);
        if (undoSlots[i]) {
          undoCapacity++;
        } else {
          break;
        }
      }
    }
    if (ESP.getFreeHeap() > (uint32_t)(CANVAS_BYTES + 64000)) {
      redoSlot = (uint8_t *)malloc(CANVAS_BYTES);
    }
    markFullDirty();
    return true;
  }

  void markDirty(int x0, int y0, int x1, int y1) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x1 < 0 || y1 < 0 || x0 >= CANVAS_W || y0 >= CANVAS_H) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= CANVAS_W) x1 = CANVAS_W - 1;
    if (y1 >= CANVAS_H) y1 = CANVAS_H - 1;
    if (!dirty) {
      dirty = true;
      dirtyX0 = x0; dirtyY0 = y0;
      dirtyX1 = x1; dirtyY1 = y1;
    } else {
      if (x0 < dirtyX0) dirtyX0 = x0;
      if (y0 < dirtyY0) dirtyY0 = y0;
      if (x1 > dirtyX1) dirtyX1 = x1;
      if (y1 > dirtyY1) dirtyY1 = y1;
    }
  }

  void markFullDirty() {
    dirty = true;
    dirtyX0 = 0; dirtyY0 = 0;
    dirtyX1 = CANVAS_W - 1; dirtyY1 = CANVAS_H - 1;
  }

  void clearDirty() {
    dirty = false;
  }

  void pushUndo() {
    if (undoCapacity <= 0 || !buf) return;
    memcpy(undoSlots[undoHead], buf, CANVAS_BYTES);
    undoHead = (undoHead + 1) % undoCapacity;
    if (undoCount < undoCapacity) undoCount++;
    redoCount = 0;
  }

  bool undo() {
    if (undoCount <= 0 || undoCapacity <= 0 || !buf) return false;
    if (redoSlot) {
      memcpy(redoSlot, buf, CANVAS_BYTES);
      redoCount = 1;
    }
    undoHead = (undoHead - 1 + undoCapacity) % undoCapacity;
    undoCount--;
    memcpy(buf, undoSlots[undoHead], CANVAS_BYTES);
    markFullDirty();
    return true;
  }

  bool redo() {
    if (redoCount <= 0 || !redoSlot || !buf) return false;
    // Save current back into undo slot before restoring redo
    if (undoCapacity > 0) {
      memcpy(undoSlots[undoHead], buf, CANVAS_BYTES);
      undoHead = (undoHead + 1) % undoCapacity;
      if (undoCount < undoCapacity) undoCount++;
    }
    memcpy(buf, redoSlot, CANVAS_BYTES);
    redoCount = 0;
    markFullDirty();
    return true;
  }

  inline bool getPixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H) return false;
    return (buf[y * CANVAS_STRIDE + (x >> 3)] & (0x80 >> (x & 7))) != 0;
  }

  inline void setPixelRaw(int x, int y, bool v) {
    if (x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H) return;
    uint8_t & b = buf[y * CANVAS_STRIDE + (x >> 3)];
    uint8_t mask = 0x80 >> (x & 7);
    if (v) b |= mask;
    else   b &= ~mask;
  }

  inline void setPixel(int x, int y, bool v) {
    if (x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H) return;
    setPixelRaw(x, y, v);
    markDirty(x, y, x, y);
  }

  inline void setPatternPixelRaw(int x, int y, bool ink, uint8_t patIdx) {
    if (x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H) return;
    setPixelRaw(x, y, evaluatePatternPixel(patIdx, x, y, ink));
  }

  // ---- Whole-canvas operations ----

  void clear(bool paperVal) {
    memset(buf, paperVal ? 0xFF : 0x00, CANVAS_BYTES);
    markFullDirty();
  }

  void fillWithPattern(bool ink, uint8_t patIdx) {
    for (int y = 0; y < CANVAS_H; ++y) {
      uint8_t rowPat = MDA_PATTERNS[patIdx % NUM_PATTERNS][y & 7];
      if (!ink) rowPat = ~rowPat;
      memset(buf + y * CANVAS_STRIDE, rowPat, CANVAS_STRIDE);
    }
    markFullDirty();
  }

  void invertAll() {
    for (int i = 0; i < CANVAS_BYTES; ++i) buf[i] ^= 0xFF;
    markFullDirty();
  }

  void flipHorizontal() {
    for (int y = 0; y < CANVAS_H; ++y) {
      for (int x = 0; x < CANVAS_W / 2; ++x) {
        bool a = getPixel(x, y);
        bool b = getPixel(CANVAS_W - 1 - x, y);
        setPixelRaw(x, y, b);
        setPixelRaw(CANVAS_W - 1 - x, y, a);
      }
    }
    markFullDirty();
  }

  void flipVertical() {
    uint8_t rowTmp[CANVAS_STRIDE];
    for (int y = 0; y < CANVAS_H / 2; ++y) {
      uint8_t * top = buf + y * CANVAS_STRIDE;
      uint8_t * bot = buf + (CANVAS_H - 1 - y) * CANVAS_STRIDE;
      memcpy(rowTmp, top, CANVAS_STRIDE);
      memcpy(top, bot, CANVAS_STRIDE);
      memcpy(bot, rowTmp, CANVAS_STRIDE);
    }
    markFullDirty();
  }

  void rotate180() {
    flipVertical();
    flipHorizontal();
  }

  void drawFrameBorder(bool ink, int thickness = 2) {
    drawRect(0, 0, CANVAS_W - 1, CANVAS_H - 1, thickness, FILL_OUTLINE, ink, 0);
  }

  // ---- Brush & Spray ----

  void stampBrushRaw(int cx, int cy, int size, BrushShape shape, bool ink, bool usePattern, uint8_t patIdx) {
    if (size <= 1) {
      if (usePattern) setPatternPixelRaw(cx, cy, ink, patIdx);
      else            setPixelRaw(cx, cy, ink);
      return;
    }
    int r = size;
    if (shape == BRUSH_ROUND) {
      int r2 = r * r;
      for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
          if (dx * dx + dy * dy <= r2) {
            if (usePattern) setPatternPixelRaw(cx + dx, cy + dy, ink, patIdx);
            else            setPixelRaw(cx + dx, cy + dy, ink);
          }
        }
      }
    } else if (shape == BRUSH_SQUARE) {
      for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
          if (usePattern) setPatternPixelRaw(cx + dx, cy + dy, ink, patIdx);
          else            setPixelRaw(cx + dx, cy + dy, ink);
        }
      }
    } else if (shape == BRUSH_SLASH) {
      // Calligraphy '/' nib (thick along +x, -y)
      for (int i = -r; i <= r; ++i) {
        int px = cx + i;
        int py = cy - i;
        for (int w = 0; w <= (size > 2 ? 1 : 0); ++w) {
          if (usePattern) setPatternPixelRaw(px + w, py, ink, patIdx);
          else            setPixelRaw(px + w, py, ink);
        }
      }
    } else if (shape == BRUSH_BACKSLASH) {
      // Calligraphy '\' nib (thick along +x, +y)
      for (int i = -r; i <= r; ++i) {
        int px = cx + i;
        int py = cy + i;
        for (int w = 0; w <= (size > 2 ? 1 : 0); ++w) {
          if (usePattern) setPatternPixelRaw(px + w, py, ink, patIdx);
          else            setPixelRaw(px + w, py, ink);
        }
      }
    }
  }

  void drawBrushLine(int x0, int y0, int x1, int y1, int size, BrushShape shape, bool ink, bool usePattern, uint8_t patIdx) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int minX = min(x0, x1) - size - 1;
    int maxX = max(x0, x1) + size + 1;
    int minY = min(y0, y1) - size - 1;
    int maxY = max(y0, y1) + size + 1;

    while (true) {
      stampBrushRaw(x0, y0, size, shape, ink, usePattern, patIdx);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
    markDirty(minX, minY, maxX, maxY);
  }

  void drawSpray(int cx, int cy, int radius, bool ink, uint8_t patIdx, uint32_t & rng) {
    int count = radius * 2 + 4;
    int r2 = radius * radius;
    for (int i = 0; i < count; ++i) {
      rng = rng * 1664525UL + 1013904223UL;
      int dx = (int)((rng >> 16) % (2 * radius + 1)) - radius;
      rng = rng * 1664525UL + 1013904223UL;
      int dy = (int)((rng >> 16) % (2 * radius + 1)) - radius;
      if (dx * dx + dy * dy <= r2) {
        if (patIdx == 0) setPixelRaw(cx + dx, cy + dy, ink);
        else             setPatternPixelRaw(cx + dx, cy + dy, ink, patIdx);
      }
    }
    markDirty(cx - radius, cy - radius, cx + radius, cy + radius);
  }

  // ---- Lines & Geometric Primitives ----

  void drawLine(int x0, int y0, int x1, int y1, int thickness, bool ink, bool usePattern = false, uint8_t patIdx = 0) {
    int rad = (thickness <= 1) ? 0 : (thickness / 2);
    if (rad == 0) {
      int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
      int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
      int err = dx + dy;
      int minX = min(x0, x1), maxX = max(x0, x1);
      int minY = min(y0, y1), maxY = max(y0, y1);
      while (true) {
        if (usePattern) setPatternPixelRaw(x0, y0, ink, patIdx);
        else            setPixelRaw(x0, y0, ink);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
      }
      markDirty(minX, minY, maxX, maxY);
    } else {
      drawBrushLine(x0, y0, x1, y1, rad, BRUSH_ROUND, ink, usePattern, patIdx);
    }
  }

  void drawHLineRaw(int x0, int x1, int y, bool ink, bool usePattern, uint8_t patIdx) {
    if (y < 0 || y >= CANVAS_H) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x1 < 0 || x0 >= CANVAS_W) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= CANVAS_W) x1 = CANVAS_W - 1;
    for (int x = x0; x <= x1; ++x) {
      if (usePattern) setPatternPixelRaw(x, y, ink, patIdx);
      else            setPixelRaw(x, y, ink);
    }
  }

  void drawRect(int x0, int y0, int x1, int y1, int thickness, FillMode fillMode, bool ink, uint8_t patIdx) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (fillMode == FILL_BOTH || fillMode == FILL_PATTERN) {
      for (int y = y0; y <= y1; ++y) {
        drawHLineRaw(x0, x1, y, ink, true, patIdx);
      }
    }
    if (fillMode == FILL_OUTLINE || fillMode == FILL_BOTH) {
      int t = max(1, thickness);
      for (int i = 0; i < t; ++i) {
        int lx = x0 + i, rx = x1 - i;
        int ty = y0 + i, by = y1 - i;
        if (lx > rx || ty > by) break;
        drawHLineRaw(lx, rx, ty, ink, false, 0);
        drawHLineRaw(lx, rx, by, ink, false, 0);
        for (int y = ty; y <= by; ++y) {
          setPixelRaw(lx, y, ink);
          setPixelRaw(rx, y, ink);
        }
      }
    }
    markDirty(x0 - thickness, y0 - thickness, x1 + thickness, y1 + thickness);
  }

  void drawRoundRect(int x0, int y0, int x1, int y1, int thickness, FillMode fillMode, bool ink, uint8_t patIdx) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    int w = x1 - x0 + 1;
    int h = y1 - y0 + 1;
    int r = min(min(w, h) / 4, 14);
    if (r < 2) {
      drawRect(x0, y0, x1, y1, thickness, fillMode, ink, patIdx);
      return;
    }

    auto insideRoundRect = [&](int px, int py, int bx0, int by0, int bx1, int by1, int cr) -> bool {
      if (px < bx0 || px > bx1 || py < by0 || py > by1) return false;
      int cx = (px < bx0 + cr) ? (bx0 + cr) : (px > bx1 - cr ? (bx1 - cr) : px);
      int cy = (py < by0 + cr) ? (by0 + cr) : (py > by1 - cr ? (by1 - cr) : py);
      int dx = px - cx;
      int dy = py - cy;
      return (dx * dx + dy * dy) <= (cr * cr);
    };

    int t = max(1, thickness);
    int ir = max(0, r - t);
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        if (!insideRoundRect(x, y, x0, y0, x1, y1, r)) continue;
        bool inInner = (x >= x0 + t && x <= x1 - t && y >= y0 + t && y <= y1 - t) &&
                       insideRoundRect(x, y, x0 + t, y0 + t, x1 - t, y1 - t, ir);
        if (!inInner && (fillMode == FILL_OUTLINE || fillMode == FILL_BOTH)) {
          setPixelRaw(x, y, ink);
        } else if (fillMode == FILL_BOTH || fillMode == FILL_PATTERN) {
          setPatternPixelRaw(x, y, ink, patIdx);
        }
      }
    }
    markDirty(x0, y0, x1, y1);
  }

  void drawEllipse(int x0, int y0, int x1, int y1, int thickness, FillMode fillMode, bool ink, uint8_t patIdx) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    float rx = (x1 - x0) * 0.5f;
    float ry = (y1 - y0) * 0.5f;
    if (rx < 0.5f && ry < 0.5f) {
      setPixel(x0, y0, ink);
      return;
    }
    float cx = (x0 + x1) * 0.5f;
    float cy = (y0 + y1) * 0.5f;
    float t = (float)max(1, thickness);
    float irx = max(0.0f, rx - t);
    float iry = max(0.0f, ry - t);
    float invRx2 = (rx > 0.5f) ? (1.0f / (rx * rx)) : 0.0f;
    float invRy2 = (ry > 0.5f) ? (1.0f / (ry * ry)) : 0.0f;
    float invIRx2 = (irx > 0.5f) ? (1.0f / (irx * irx)) : 0.0f;
    float invIRy2 = (iry > 0.5f) ? (1.0f / (iry * iry)) : 0.0f;

    if (fillMode == FILL_BOTH || fillMode == FILL_PATTERN) {
      for (int y = y0; y <= y1; ++y) {
        float dy = y - cy;
        float dyTerm = dy * dy * invRy2;
        if (dyTerm > 1.0f) continue;
        int span = (int)(rx * sqrtf(1.0f - dyTerm) + 0.5f);
        int lx = max(x0, (int)floorf(cx - span));
        int hx = min(x1, (int)ceilf(cx + span));
        drawHLineRaw(lx, hx, y, ink, true, patIdx);
      }
    }

    if (fillMode == FILL_OUTLINE || fillMode == FILL_BOTH) {
      if (thickness <= 1) {
        // Crisp 1px midpoint ellipse outline
        int a = max(1, (x1 - x0) / 2);
        int b = max(1, (y1 - y0) / 2);
        int icx = (x0 + x1) / 2;
        int icy = (y0 + y1) / 2;
        int64_t a2 = (int64_t)a * a;
        int64_t b2 = (int64_t)b * b;
        int64_t x = 0, y = b;
        int64_t px = 0, py = 2 * a2 * y;
        auto plot4 = [&](int ex, int ey) {
          setPixelRaw(icx + ex, icy + ey, ink);
          setPixelRaw(icx - ex, icy + ey, ink);
          setPixelRaw(icx + ex, icy - ey, ink);
          setPixelRaw(icx - ex, icy - ey, ink);
        };
        int64_t p = (int64_t)(b2 - (a2 * b) + (a2 / 4));
        while (px < py) {
          plot4((int)x, (int)y);
          x++;
          px += 2 * b2;
          if (p < 0) {
            p += b2 + px;
          } else {
            y--;
            py -= 2 * a2;
            p += b2 + px - py;
          }
        }
        p = (int64_t)(b2 * (x * x + x) + a2 * (y - 1) * (y - 1) - a2 * b2);
        while (y >= 0) {
          plot4((int)x, (int)y);
          y--;
          py -= 2 * a2;
          if (p > 0) {
            p += a2 - py;
          } else {
            x++;
            px += 2 * b2;
            p += a2 - py + px;
          }
        }
      } else {
        for (int y = y0; y <= y1; ++y) {
          float dy = y - cy;
          for (int x = x0; x <= x1; ++x) {
            float dx = x - cx;
            float outer = dx * dx * invRx2 + dy * dy * invRy2;
            if (outer > 1.02f) continue;
            bool insideInner = (irx > 0.5f && iry > 0.5f) &&
                               ((dx * dx * invIRx2 + dy * dy * invIRy2) < 0.98f);
            if (!insideInner) {
              setPixelRaw(x, y, ink);
            }
          }
        }
      }
    }
    markDirty(x0 - 2, y0 - 2, x1 + 2, y1 + 2);
  }

  void drawQuadBezier(int x0, int y0, int cx, int cy, int x1, int y1, int thickness, bool ink) {
    int approxLen = abs(cx - x0) + abs(cy - y0) + abs(x1 - cx) + abs(y1 - cy);
    int steps = constrain(approxLen / 2, 16, 256);
    int px = x0, py = y0;
    for (int i = 1; i <= steps; ++i) {
      float t = (float)i / (float)steps;
      float u = 1.0f - t;
      int nx = (int)(u * u * x0 + 2.0f * u * t * cx + t * t * x1 + 0.5f);
      int ny = (int)(u * u * y0 + 2.0f * u * t * cy + t * t * y1 + 0.5f);
      drawLine(px, py, nx, ny, thickness, ink);
      px = nx; py = ny;
    }
  }

  void drawPolygon(const int16_t * px, const int16_t * py, int count, int thickness, FillMode fillMode, bool ink, uint8_t patIdx) {
    if (count < 2) return;
    int minY = CANVAS_H - 1, maxY = 0;
    int minX = CANVAS_W - 1, maxX = 0;
    for (int i = 0; i < count; ++i) {
      if (px[i] < minX) minX = px[i];
      if (px[i] > maxX) maxX = px[i];
      if (py[i] < minY) minY = py[i];
      if (py[i] > maxY) maxY = py[i];
    }

    if ((fillMode == FILL_BOTH || fillMode == FILL_PATTERN) && count >= 3) {
      int nodes[64];
      int sy0 = max(0, minY);
      int sy1 = min(CANVAS_H - 1, maxY);
      for (int y = sy0; y <= sy1; ++y) {
        int n = 0;
        int j = count - 1;
        for (int i = 0; i < count; ++i) {
          if ((py[i] < y && py[j] >= y) || (py[j] < y && py[i] >= y)) {
            if (n < 64) {
              nodes[n++] = (int)(px[i] + (float)(y - py[i]) / (float)(py[j] - py[i]) * (px[j] - px[i]));
            }
          }
          j = i;
        }
        // Insertion sort intersections
        for (int i = 1; i < n; ++i) {
          int key = nodes[i];
          int k = i - 1;
          while (k >= 0 && nodes[k] > key) {
            nodes[k + 1] = nodes[k];
            k--;
          }
          nodes[k + 1] = key;
        }
        for (int i = 0; i + 1 < n; i += 2) {
          drawHLineRaw(nodes[i], nodes[i + 1], y, ink, true, patIdx);
        }
      }
    }

    if (fillMode == FILL_OUTLINE || fillMode == FILL_BOTH) {
      for (int i = 0; i < count; ++i) {
        int j = (i + 1) % count;
        drawLine(px[i], py[i], px[j], py[j], thickness, ink);
      }
    }
    markDirty(minX - thickness, minY - thickness, maxX + thickness, maxY + thickness);
  }

  // ---- Pattern-Safe Scanline Flood Fill ----
  // Uses a temporary 1-bit visited mask so non-solid monochrome patterns
  // fill cleanly in a single pass with ZERO risk of infinite loops.
  void floodFill(int startX, int startY, bool ink, uint8_t patIdx) {
    if (startX < 0 || startY < 0 || startX >= CANVAS_W || startY >= CANVAS_H) return;
    bool targetVal = getPixel(startX, startY);

    // If filling with solid pattern and target already matches, nothing to do
    if (patIdx == 0 && targetVal == ink) return;
    if (patIdx == 1 && targetVal == !ink) return;

    uint8_t * visited = (uint8_t *)calloc(CANVAS_BYTES, 1);
    bool useVisited = (visited != nullptr);

    auto isVisited = [&](int x, int y) -> bool {
      if (!useVisited) return getPixel(x, y) != targetVal;
      return (visited[y * CANVAS_STRIDE + (x >> 3)] & (0x80 >> (x & 7))) != 0;
    };
    auto markVisited = [&](int x, int y) {
      if (useVisited) {
        visited[y * CANVAS_STRIDE + (x >> 3)] |= (0x80 >> (x & 7));
      }
    };
    auto canFill = [&](int x, int y) -> bool {
      if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H) return false;
      if (useVisited) {
        return !isVisited(x, y) && (getPixel(x, y) == targetVal);
      }
      return getPixel(x, y) == targetVal;
    };

    // Fallback to solid if OOM for visited mask while using dither pattern
    uint8_t effectivePat = useVisited ? patIdx : (ink ? 0 : 1);

    struct Span { int16_t x, y; };
    static constexpr int STACK_CAP = 2048;
    Span * stack = (Span *)malloc(sizeof(Span) * STACK_CAP);
    if (!stack) {
      if (visited) free(visited);
      return;
    }

    int sp = 0;
    stack[sp++] = { (int16_t)startX, (int16_t)startY };

    int minX = startX, maxX = startX, minY = startY, maxY = startY;

    while (sp > 0) {
      Span s = stack[--sp];
      int x = s.x;
      int y = s.y;
      if (!canFill(x, y)) continue;

      int lx = x;
      while (lx > 0 && canFill(lx - 1, y)) lx--;
      int rx = x;
      while (rx < CANVAS_W - 1 && canFill(rx + 1, y)) rx++;

      if (lx < minX) minX = lx;
      if (rx > maxX) maxX = rx;
      if (y  < minY) minY = y;
      if (y  > maxY) maxY = y;

      bool spanAbove = false;
      bool spanBelow = false;

      for (int cx = lx; cx <= rx; ++cx) {
        markVisited(cx, y);
        if (useVisited) {
          setPatternPixelRaw(cx, y, ink, effectivePat);
        } else {
          setPixelRaw(cx, y, ink);
        }

        if (y > 0) {
          bool ok = canFill(cx, y - 1);
          if (!spanAbove && ok && sp < STACK_CAP) {
            stack[sp++] = { (int16_t)cx, (int16_t)(y - 1) };
            spanAbove = true;
          } else if (spanAbove && !ok) {
            spanAbove = false;
          }
        }
        if (y < CANVAS_H - 1) {
          bool ok = canFill(cx, y + 1);
          if (!spanBelow && ok && sp < STACK_CAP) {
            stack[sp++] = { (int16_t)cx, (int16_t)(y + 1) };
            spanBelow = true;
          } else if (spanBelow && !ok) {
            spanBelow = false;
          }
        }
      }
    }

    free(stack);
    if (visited) free(visited);
    markDirty(minX, minY, maxX, maxY);
  }

  // ---- Direct 1-Bit Font Rendering onto Canvas ----

  void drawChar(int x, int y, uint8_t ch, const fabgl::FontInfo * font, bool bold, bool scale2x, bool ink) {
    if (!font || !font->data) return;
    int fw = font->width;
    int fh = font->height;
    int rowBytes = (fw + 7) / 8;
    const uint8_t * glyph = font->data + (uint32_t)ch * fh * rowBytes;
    int sc = scale2x ? 2 : 1;

    for (int r = 0; r < fh; ++r) {
      bool prevOn = false;
      for (int c = 0; c < fw; ++c) {
        uint8_t b = glyph[r * rowBytes + (c >> 3)];
        bool on = (b & (0x80 >> (c & 7))) != 0;
        bool plot = on || (bold && prevOn);
        prevOn = on;
        if (plot) {
          for (int sy = 0; sy < sc; ++sy) {
            for (int sx = 0; sx < sc; ++sx) {
              setPixelRaw(x + c * sc + sx, y + r * sc + sy, ink);
            }
          }
        }
      }
      if (bold && prevOn) {
        for (int sy = 0; sy < sc; ++sy) {
          for (int sx = 0; sx < sc; ++sx) {
            setPixelRaw(x + fw * sc + sx, y + r * sc + sy, ink);
          }
        }
      }
    }
    markDirty(x, y, x + (fw + (bold ? 1 : 0)) * sc, y + fh * sc);
  }

  // ---- Rectangular Selection & Clipboard Operations ----

  bool copyRect(int x0, int y0, int x1, int y1, MonoBuffer & dst) const {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    x0 = constrain(x0, 0, CANVAS_W - 1);
    x1 = constrain(x1, 0, CANVAS_W - 1);
    y0 = constrain(y0, 0, CANVAS_H - 1);
    y1 = constrain(y1, 0, CANVAS_H - 1);
    int w = x1 - x0 + 1;
    int h = y1 - y0 + 1;
    if (!dst.alloc(w, h)) return false;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        dst.setPixel(x, y, getPixel(x0 + x, y0 + y));
      }
    }
    return true;
  }

  void pasteBuffer(int dstX, int dstY, const MonoBuffer & src, bool transparent, bool transparentVal) {
    if (!src.data) return;
    for (int y = 0; y < src.height; ++y) {
      int cy = dstY + y;
      if (cy < 0 || cy >= CANVAS_H) continue;
      for (int x = 0; x < src.width; ++x) {
        int cx = dstX + x;
        if (cx < 0 || cx >= CANVAS_W) continue;
        bool v = src.getPixel(x, y);
        if (transparent && (v == transparentVal)) continue;
        setPixelRaw(cx, cy, v);
      }
    }
    markDirty(dstX, dstY, dstX + src.width - 1, dstY + src.height - 1);
  }

  void clearRect(int x0, int y0, int x1, int y1, bool fillVal) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; ++y) {
      drawHLineRaw(x0, x1, y, fillVal, false, 0);
    }
    markDirty(x0, y0, x1, y1);
  }

  void invertRect(int x0, int y0, int x1, int y1) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    x0 = constrain(x0, 0, CANVAS_W - 1);
    x1 = constrain(x1, 0, CANVAS_W - 1);
    y0 = constrain(y0, 0, CANVAS_H - 1);
    y1 = constrain(y1, 0, CANVAS_H - 1);
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        setPixelRaw(x, y, !getPixel(x, y));
      }
    }
    markDirty(x0, y0, x1, y1);
  }

  void flipRectH(int x0, int y0, int x1, int y1) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    x0 = constrain(x0, 0, CANVAS_W - 1);
    x1 = constrain(x1, 0, CANVAS_W - 1);
    y0 = constrain(y0, 0, CANVAS_H - 1);
    y1 = constrain(y1, 0, CANVAS_H - 1);
    int w = x1 - x0 + 1;
    for (int y = y0; y <= y1; ++y) {
      for (int dx = 0; dx < w / 2; ++dx) {
        bool a = getPixel(x0 + dx, y);
        bool b = getPixel(x1 - dx, y);
        setPixelRaw(x0 + dx, y, b);
        setPixelRaw(x1 - dx, y, a);
      }
    }
    markDirty(x0, y0, x1, y1);
  }

  void flipRectV(int x0, int y0, int x1, int y1) {
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    x0 = constrain(x0, 0, CANVAS_W - 1);
    x1 = constrain(x1, 0, CANVAS_W - 1);
    y0 = constrain(y0, 0, CANVAS_H - 1);
    y1 = constrain(y1, 0, CANVAS_H - 1);
    int h = y1 - y0 + 1;
    for (int dy = 0; dy < h / 2; ++dy) {
      for (int x = x0; x <= x1; ++x) {
        bool a = getPixel(x, y0 + dy);
        bool b = getPixel(x, y1 - dy);
        setPixelRaw(x, y0 + dy, b);
        setPixelRaw(x, y1 - dy, a);
      }
    }
    markDirty(x0, y0, x1, y1);
  }

  // ---- Export to Netpbm ASCII P1 (.pbm) over USB Serial ----
  // Standard monochrome 1-bit bitmap format viewable in GIMP, Photoshop, IrfanView, etc.
  void exportPBM(Stream & out, bool invertForPaper = false) const {
    out.println("P1");
    out.println("# Created with MDA Paint (Hercules 1-bit Monochrome)");
    out.printf("%d %d\n", CANVAS_W, CANVAS_H);
    for (int y = 0; y < CANVAS_H; ++y) {
      for (int x = 0; x < CANVAS_W; ++x) {
        bool px = getPixel(x, y);
        // In PBM, 1 = black ink, 0 = white paper
        bool pbmBit = invertForPaper ? px : !px;
        out.write(pbmBit ? '1' : '0');
        if ((x & 63) == 63) out.write('\n');
      }
      out.write('\n');
    }
    out.println("# END MDA_PAINT PBM");
  }
};
