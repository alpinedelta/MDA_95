// ============================================================================
// MDA Paint - Strictly 1-Bit Monochrome Paint Application for Hercules / MDA
// Built for ESP32 / LilyGO VGA32 using FabGL VGA2Controller + PS/2 Mouse & Kbd
// Display: 720 x 350 @ 50Hz TTL Monochrome (No Grayscale, No Intensity)
// ============================================================================

#include <Arduino.h>
#include <fabgl.h>
#include "mda_launcher_sdk.h"
#include "mda_patterns.h"
#include "mda_icons.h"
#include "mda_canvas.h"
#include "mda_sd_bmp.h"

// ============================================================================
// Hardware Controllers (LilyGO VGA32 MDA TTL + Explicit PS/2 Keyboard & Mouse)
// ============================================================================
fabgl::VGA2Controller DisplayController;
fabgl::PS2Controller  PS2Controller;
fabgl::Keyboard       Keyboard;
fabgl::Mouse          Mouse;
fabgl::Canvas         Canvas(&DisplayController);

// Persistent Bitmap wrappers for fast pattern-bar rendering
static const fabgl::Bitmap gPatTileBmp[NUM_PATTERNS] = {
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[0],  fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255)),
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[1],  fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255)),
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[2],  fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255)),
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[3],  fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255)),
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[4],  fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255)),
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[5],  fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255)),
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[6],  fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255)),
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[7],  fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255)),
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[8],  fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255)),
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[9],  fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255)),
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[10], fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255)),
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[11], fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255)),
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[12], fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255)),
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[13], fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255)),
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[14], fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255)),
  fabgl::Bitmap(PAT_BMP_W, PAT_BMP_H, gPatSwatchBuf[15], fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255))
};
static const fabgl::Bitmap gActivePatBmp(ACT_BMP_W, ACT_BMP_H, gActivePatBuf, fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255));

// Live heartbeat state (blinks a 2x2 dot in top-right menu bar so liveness is visible)
uint32_t lastHeartbeatMs = 0;
bool     heartbeatOn     = false;

// ============================================================================
// Adjustable Mouse Sensitivity (Hardware PS/2 + FabGL Accel + Software Gain)
// ============================================================================
struct MouseSensPreset {
  const char * name;
  int          gainQ8;   // fixed-point gain (256 = 1.0x)
  int          accel;    // FabGL Mouse.movementAcceleration()
  int          hwRes;    // PS/2 resolution 0..3 (3 = 8 counts/mm = 200 dpi)
  int          hwScale;  // PS/2 scaling: 1 (1:1) or 2 (2:1)
};

static constexpr int NUM_MOUSE_SENS = 6;
static const MouseSensPreset MOUSE_SENS_PRESETS[NUM_MOUSE_SENS] = {
  { "0.5x Precision",  128,  100, 2, 1 },
  { "1.0x Normal",     256,  220, 3, 1 },
  { "2.0x Fast",       512,  450, 3, 1 },
  { "3.0x Faster",     768,  700, 3, 2 },
  { "4.0x High",      1024,  950, 3, 2 },
  { "6.0x Turbo",     1536, 1300, 3, 2 }
};

int mouseSensIdx = 2; // Default Level 3 (2.0x Fast)

static constexpr int MOUSE_VIRT_MAX = 30000;
static constexpr int MOUSE_VIRT_CTR = 15000;
int lastVirtX = MOUSE_VIRT_CTR;
int lastVirtY = MOUSE_VIRT_CTR;
int mouseSubX = 0;
int mouseSubY = 0;

// ============================================================================
// Screen & Layout Geometry (720 x 350 Hercules MDA)
// ============================================================================
static constexpr int SCREEN_W = 720;
static constexpr int SCREEN_H = 350;

// Top Menu Bar
static constexpr int MENUBAR_H = 15;

// Left Toolbar
static constexpr int TOOLBAR_X = 0;
static constexpr int TOOLBAR_Y = 16;
static constexpr int TOOLBAR_W = 73;
static constexpr int TOOLBAR_H = 299;

static constexpr int TOOL_BTN_W    = 32;
static constexpr int TOOL_BTN_H    = 20;
static constexpr int TOOL_GRID_X   = 3;
static constexpr int TOOL_GRID_Y   = 18;
static constexpr int TOOL_PITCH_X  = 34;
static constexpr int TOOL_PITCH_Y  = 22;

static constexpr int OPT_BOX_X     = 3;
static constexpr int OPT_BOX_Y     = 174;
static constexpr int OPT_BOX_W     = 66;
static constexpr int OPT_BOX_H     = 78;

static constexpr int SWATCH_BOX_X  = 3;
static constexpr int SWATCH_BOX_Y  = 256;
static constexpr int SWATCH_BOX_W  = 66;
static constexpr int SWATCH_BOX_H  = 56;

// Main Drawing Canvas Viewport (640 x 296)
static constexpr int CANVAS_X = 76;
static constexpr int CANVAS_Y = 18;
// CANVAS_W = 640, CANVAS_H = 296 (from mda_canvas.h)

// Bottom Pattern Bar
static constexpr int PATBAR_Y      = 316;
static constexpr int PATBAR_H      = 19;
static constexpr int PAT_START_X   = 44;
static constexpr int PAT_TILE_W    = 24;
static constexpr int PAT_TILE_H    = 15;
static constexpr int PAT_PITCH_X   = 26;

// Bottom Status Bar
static constexpr int STATUS_Y      = 336;
static constexpr int STATUS_H      = 14;

// 4x FatBits Zoom constants
static constexpr int ZOOM_SCALE    = 4;
static constexpr int ZOOM_VIEW_W   = CANVAS_W / ZOOM_SCALE; // 160 canvas px
static constexpr int ZOOM_VIEW_H   = CANVAS_H / ZOOM_SCALE; // 74 canvas px

// ============================================================================
// Application State
// ============================================================================
MonoCanvas      gCanvas;
fabgl::Bitmap * gCanvasBmp = nullptr;

// Input state
int  curX = CANVAS_X + CANVAS_W / 2;
int  curY = CANVAS_Y + CANVAS_H / 2;
bool btnLeft  = false;
bool btnRight = false;
bool btnMid   = false;
bool prevLeft = false;
bool prevRight = false;
bool prevMid  = false;
bool shiftDown = false;
bool ctrlDown  = false;
fabgl::CursorName currentCursor = fabgl::CursorName::CursorPointerSimple;

// Tool & Drawing Settings
Tool       activeTool   = TOOL_PENCIL;
uint8_t    activeSize   = 0;             // index into BRUSH_SIZES[0..3] => 1, 2, 4, 7
BrushShape brushShape   = BRUSH_ROUND;
FillMode   fillMode     = FILL_OUTLINE;
uint8_t    activePat    = 0;             // 0..15
bool       fgInk        = true;          // true = phosphor (1), false = black (0)
bool       paperVal     = false;         // default dark CRT paper (false = black)
bool       pasteTransp  = false;         // selection paste transparency
uint8_t    textFontIdx  = 1;             // 0 = 6x8, 1 = 8x8, 2 = 8x14
bool       textBold     = false;
bool       textScale2x  = false;

// View modes
bool zoomMode        = false;
int  zoomOriginX     = 0;
int  zoomOriginY     = 0;
bool showGrid        = false;
bool fullscreenMode  = false;
bool showHelpModal   = false;
bool showAboutModal  = false;

// SD Card BMP File Browser Modal State (FileBrowserMode defined in mda_sd_bmp.h)
FileBrowserMode fbMode            = FB_CLOSED;
int             fbSelectedIdx     = 0;
int             fbScrollTop       = 0;
char            fbFilename[32]    = "PAINT01.BMP";
int             fbFilenameLen     = 11;
char            fbStatusText[64]  = "";
bool            fbConfirmDelete   = false;
bool            fbTypingName      = false;
int             fbLastClickIdx    = -1;
uint32_t        fbLastClickMs     = 0;
char            currentFilePath[96] = ""; // Current opened/saved SD path for quick Ctrl+S

static constexpr int FB_X         = 120;
static constexpr int FB_Y         = 34;
static constexpr int FB_W         = 480;
static constexpr int FB_H         = 282;
static constexpr int FB_LIST_X    = 128;
static constexpr int FB_LIST_Y    = 72;
static constexpr int FB_LIST_W    = 444;
static constexpr int FB_LIST_H    = 160;
static constexpr int FB_ROW_H     = 14;
static constexpr int FB_VIS_ROWS  = 11;

// Active Drag / Shape / Multi-step State
bool drawingActive   = false;
bool dragUseInk      = true;
int  dragStartX      = 0, dragStartY = 0;
int  dragLastX       = 0, dragLastY  = 0;
int  dragCurX        = 0, dragCurY   = 0;
bool hasPreview      = false;
int  prevBoxX0 = 0, prevBoxY0 = 0, prevBoxX1 = 0, prevBoxY1 = 0;

// Curve Tool (2-phase: 0=idle, 1=dragging endpoints, 2=bending control point)
uint8_t curvePhase = 0;
int     curveX0 = 0, curveY0 = 0, curveX1 = 0, curveY1 = 0, curveCX = 0, curveCY = 0;

// Polygon Tool
static constexpr int MAX_POLY_PTS = 48;
int16_t polyX[MAX_POLY_PTS];
int16_t polyY[MAX_POLY_PTS];
int     polyCount = 0;
bool    polyInk   = true;

// Spray Timer & RNG
uint32_t sprayRng    = 0x13579BDFUL;
uint32_t lastSprayMs = 0;

// Selection & Clipboard State
bool       hasSelection  = false;
int        selX0 = 0, selY0 = 0, selX1 = 0, selY1 = 0;
bool       selFloating   = false;
bool       selDragging   = false;
int        selDragOffX   = 0, selDragOffY = 0;
MonoBuffer selBuffer;
MonoBuffer clipboardBuf;

// Text Entry State
bool     textActive  = false;
int      textOriginX = 0, textOriginY = 0;
int      textCurX    = 0, textCurY    = 0;
static constexpr int MAX_TEXT_LEN = 128;
char     textBuf[MAX_TEXT_LEN + 1];
int      textLen     = 0;
bool     textInk     = true;
uint32_t lastBlinkMs = 0;
bool     textCaretOn = true;

// Status Message Toast
char     statusMsg[48]   = "";
uint32_t statusMsgUntil  = 0;
bool     uiDirtyToolbar  = true;
bool     uiDirtyPatterns = true;
bool     uiDirtyStatus   = true;
bool     uiDirtyMenuBar  = true;

// ============================================================================
// Pull-Down Menu System
// ============================================================================
struct MenuItem {
  const char * label;
  const char * shortcut;
  uint8_t      actionId;
};

enum MenuAction : uint8_t {
  ACT_NONE = 0,
  ACT_NEW,
  ACT_OPEN_BMP,
  ACT_SAVE_BMP,
  ACT_SAVE_AS_BMP,
  ACT_BORDER,
  ACT_EXPORT_PBM,
  ACT_EXIT_DESKTOP,
  ACT_UNDO,
  ACT_REDO,
  ACT_CUT,
  ACT_COPY,
  ACT_PASTE,
  ACT_SELECT_ALL,
  ACT_CLEAR_SEL,
  ACT_INVERT,
  ACT_FLIP_H,
  ACT_FLIP_V,
  ACT_ROT180,
  ACT_FILL_PAT,
  ACT_TOGGLE_ZOOM,
  ACT_TOGGLE_GRID,
  ACT_SWAP_PAPER,
  ACT_FULLSCREEN,
  ACT_MOUSE_SLOWER,
  ACT_MOUSE_FASTER,
  ACT_SHOW_HELP,
  ACT_RESET_MOUSE,
  ACT_SHOW_ABOUT
};

static const MenuItem MENU_FILE[] = {
  { "New Canvas",     "Ctrl+N", ACT_NEW },
  { "Open BMP...",    "Ctrl+O", ACT_OPEN_BMP },
  { "Save BMP",       "Ctrl+S", ACT_SAVE_BMP },
  { "Save BMP As...", "",       ACT_SAVE_AS_BMP },
  { "Add Border",     "",       ACT_BORDER },
  { "Export PBM USB", "",       ACT_EXPORT_PBM },
  { "Exit to Desk",   "Alt+F4", ACT_EXIT_DESKTOP }
};
static const MenuItem MENU_EDIT[] = {
  { "Undo",           "Ctrl+Z", ACT_UNDO },
  { "Redo",           "Ctrl+Y", ACT_REDO },
  { "Cut",            "Ctrl+X", ACT_CUT },
  { "Copy",           "Ctrl+C", ACT_COPY },
  { "Paste",          "Ctrl+V", ACT_PASTE },
  { "Select All",     "Ctrl+A", ACT_SELECT_ALL },
  { "Clear Sel",      "Del",    ACT_CLEAR_SEL }
};
static const MenuItem MENU_IMAGE[] = {
  { "Invert",         "Ctrl+I", ACT_INVERT },
  { "Flip Horiz",     "",       ACT_FLIP_H },
  { "Flip Vert",      "",       ACT_FLIP_V },
  { "Rotate 180",     "",       ACT_ROT180 },
  { "Fill Pattern",   "",       ACT_FILL_PAT }
};
static const MenuItem MENU_VIEW[] = {
  { "Zoom 4x",        "Z",      ACT_TOGGLE_ZOOM },
  { "Grid Overlay",   "G",      ACT_TOGGLE_GRID },
  { "Dark/Lt Paper",  "I",      ACT_SWAP_PAPER },
  { "Fullscreen",     "Tab",    ACT_FULLSCREEN },
  { "Mouse Slower",   "-",      ACT_MOUSE_SLOWER },
  { "Mouse Faster",   "+",      ACT_MOUSE_FASTER }
};
static const MenuItem MENU_HELP[] = {
  { "Shortcuts",      "F1",     ACT_SHOW_HELP },
  { "Reset Mouse",    "F5",     ACT_RESET_MOUSE },
  { "About MDA",      "",       ACT_SHOW_ABOUT }
};

struct MenuDef {
  const char *     title;
  int              x;
  int              w;
  const MenuItem * items;
  int              count;
};

static constexpr int NUM_MENUS = 5;
static const MenuDef MENUS[NUM_MENUS] = {
  { "File",  86,  42, MENU_FILE,  7 },
  { "Edit",  130, 42, MENU_EDIT,  7 },
  { "Image", 174, 48, MENU_IMAGE, 5 },
  { "View",  224, 42, MENU_VIEW,  6 },
  { "Help",  268, 42, MENU_HELP,  3 }
};

int  openMenuIdx    = -1; // -1 = pull-down closed
int  hoverMenuIdx   = -1;
bool menuBarFocused = false; // true when Alt / F10 highlights top menu bar
int  menuFocusIdx   = 0;     // 0..NUM_MENUS-1
bool altHeld        = false;
bool altUsedAsCombo = false;
static constexpr int MENU_POP_W     = 148;
static constexpr int MENU_ITEM_H    = 14;

// ============================================================================
// Forward Declarations
// ============================================================================
void redrawAll();
void redrawMenuBar();
void redrawToolbar();
void redrawPatternBar();
void redrawStatusBar();
void flushCanvasViewport(bool forceFull = false);
void drawSelectionOverlay();
void drawPreviewOverlay();
void commitFloatingSelection();
void clearSelection(bool keepPixels);
void commitTextSession();
void cancelActiveOperation();
void executeMenuAction(uint8_t act);
void setStatusToast(const char * msg, uint32_t ms = 2200);

// ============================================================================
// Utility Helpers
// ============================================================================
const fabgl::FontInfo * getActiveTextFont() {
  if (textFontIdx == 0) return &fabgl::FONT_6x8;
  if (textFontIdx == 2) return &fabgl::FONT_8x14;
  return &fabgl::FONT_8x8;
}

inline bool ptInRect(int x, int y, int rx, int ry, int rw, int rh) {
  return (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
}

inline bool ptInCanvasViewport(int sx, int sy) {
  return ptInRect(sx, sy, CANVAS_X, CANVAS_Y, CANVAS_W, CANVAS_H);
}

inline int screenToCanvasX(int sx) {
  int lx = constrain(sx - CANVAS_X, 0, CANVAS_W - 1);
  if (!zoomMode) return lx;
  return constrain(zoomOriginX + lx / ZOOM_SCALE, 0, CANVAS_W - 1);
}

inline int screenToCanvasY(int sy) {
  int ly = constrain(sy - CANVAS_Y, 0, CANVAS_H - 1);
  if (!zoomMode) return ly;
  return constrain(zoomOriginY + ly / ZOOM_SCALE, 0, CANVAS_H - 1);
}

inline int canvasToScreenX(int cx) {
  if (!zoomMode) return CANVAS_X + cx;
  return CANVAS_X + (cx - zoomOriginX) * ZOOM_SCALE;
}

inline int canvasToScreenY(int cy) {
  if (!zoomMode) return CANVAS_Y + cy;
  return CANVAS_Y + (cy - zoomOriginY) * ZOOM_SCALE;
}

void setStatusToast(const char * msg, uint32_t ms) {
  strncpy(statusMsg, msg, sizeof(statusMsg) - 1);
  statusMsg[sizeof(statusMsg) - 1] = '\0';
  statusMsgUntil = millis() + ms;
  uiDirtyStatus = true;
}

// Constrain drag endpoint when Shift is held
void applyShiftConstraint(Tool tool, int sx, int sy, int & ex, int & ey) {
  if (!shiftDown) return;
  int dx = ex - sx;
  int dy = ey - sy;
  if (tool == TOOL_LINE || tool == TOOL_POLYGON) {
    int adx = abs(dx), ady = abs(dy);
    if (adx > ady * 2) {
      ey = sy; // horizontal
    } else if (ady > adx * 2) {
      ex = sx; // vertical
    } else {
      int m = max(adx, ady);
      ex = sx + (dx >= 0 ? m : -m);
      ey = sy + (dy >= 0 ? m : -m);
    }
  } else if (tool == TOOL_RECT || tool == TOOL_ROUNDRECT || tool == TOOL_ELLIPSE || tool == TOOL_SELECT) {
    int m = max(abs(dx), abs(dy));
    ex = sx + (dx >= 0 ? m : -m);
    ey = sy + (dy >= 0 ? m : -m);
  }
  ex = constrain(ex, 0, CANVAS_W - 1);
  ey = constrain(ey, 0, CANVAS_H - 1);
}

void updateHardwareCursor() {
  fabgl::CursorName want = fabgl::CursorName::CursorPointerSimple;
  if (openMenuIdx < 0 && !showHelpModal && !showAboutModal && fbMode == FB_CLOSED && !fullscreenMode && ptInCanvasViewport(curX, curY)) {
    if (activeTool == TOOL_SELECT && hasSelection && !drawingActive) {
      int cx = screenToCanvasX(curX);
      int cy = screenToCanvasY(curY);
      if (cx >= selX0 && cx <= selX1 && cy >= selY0 && cy <= selY1) {
        want = fabgl::CursorName::CursorMove;
      } else {
        want = fabgl::CursorName::CursorCross1;
      }
    } else {
      switch (activeTool) {
        case TOOL_PENCIL:
        case TOOL_BRUSH:
          want = fabgl::CursorName::CursorPen;
          break;
        case TOOL_TEXT:
          want = fabgl::CursorName::CursorTextInput;
          break;
        case TOOL_FILL:
        case TOOL_SPRAY:
        case TOOL_ZOOM:
          want = fabgl::CursorName::CursorCross2;
          break;
        default:
          want = fabgl::CursorName::CursorCross1;
          break;
      }
    }
  }
  if (want != currentCursor) {
    currentCursor = want;
    DisplayController.setMouseCursor(currentCursor);
  }
}

// ============================================================================
// Canvas Viewport Rendering (1x Fast Bitmap Blit & 4x FatBits Editor)
// ============================================================================
void drawGridOnScreen(int sx0, int sy0, int sx1, int sy1) {
  if (!showGrid) return;
  fabgl::PaintOptions po;
  po.NOT = 1;
  Canvas.setPaintOptions(po);
  if (!zoomMode) {
    // Subtle 16x16 pixel crosshair dots in XOR so grid works on both black & white
    int startX = ((sx0 - CANVAS_X + 15) / 16) * 16 + CANVAS_X;
    int startY = ((sy0 - CANVAS_Y + 15) / 16) * 16 + CANVAS_Y;
    Canvas.setPenColor(fabgl::Color::White);
    for (int y = startY; y <= sy1; y += 16) {
      for (int x = startX; x <= sx1; x += 16) {
        Canvas.drawLine(x, y, x, y);
      }
    }
  } else {
    // In 4x FatBits mode, draw crisp XOR pixel grid dots every 4px
    int startX = ((sx0 - CANVAS_X + ZOOM_SCALE - 1) / ZOOM_SCALE) * ZOOM_SCALE + CANVAS_X;
    int startY = ((sy0 - CANVAS_Y + ZOOM_SCALE - 1) / ZOOM_SCALE) * ZOOM_SCALE + CANVAS_Y;
    Canvas.setPenColor(fabgl::Color::White);
    for (int y = startY; y <= sy1; y += ZOOM_SCALE) {
      for (int x = startX; x <= sx1; x += ZOOM_SCALE) {
        Canvas.drawLine(x, y, x, y);
      }
    }
  }
  po.NOT = 0;
  Canvas.setPaintOptions(po);
}

void drawFloatingSelectionOnScreen() {
  if (!selFloating || !selBuffer.data) return;
  Canvas.setClippingRect(fabgl::Rect(CANVAS_X, CANVAS_Y, CANVAS_X + CANVAS_W - 1, CANVAS_Y + CANVAS_H - 1));
  if (!zoomMode) {
    for (int y = 0; y < selBuffer.height; ++y) {
      int cy = selY0 + y;
      if (cy < 0 || cy >= CANVAS_H) continue;
      for (int x = 0; x < selBuffer.width; ++x) {
        int cx = selX0 + x;
        if (cx < 0 || cx >= CANVAS_W) continue;
        bool v = selBuffer.getPixel(x, y);
        if (pasteTransp && (v == paperVal)) continue;
        Canvas.setPixel(CANVAS_X + cx, CANVAS_Y + cy, v ? fabgl::RGB888(255, 255, 255) : fabgl::RGB888(0, 0, 0));
      }
    }
  } else {
    for (int y = 0; y < selBuffer.height; ++y) {
      int cy = selY0 + y;
      if (cy < zoomOriginY || cy >= zoomOriginY + ZOOM_VIEW_H) continue;
      int sy = canvasToScreenY(cy);
      for (int x = 0; x < selBuffer.width; ++x) {
        int cx = selX0 + x;
        if (cx < zoomOriginX || cx >= zoomOriginX + ZOOM_VIEW_W) continue;
        bool v = selBuffer.getPixel(x, y);
        if (pasteTransp && (v == paperVal)) continue;
        int sx = canvasToScreenX(cx);
        Canvas.setBrushColor(v ? fabgl::Color::White : fabgl::Color::Black);
        Canvas.fillRectangle(sx, sy, sx + ZOOM_SCALE - 1, sy + ZOOM_SCALE - 1);
      }
    }
  }
  Canvas.setClippingRect(fabgl::Rect(0, 0, SCREEN_W - 1, SCREEN_H - 1));
}

void drawDashedXORRect(int x0, int y0, int x1, int y1) {
  if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
  if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
  Canvas.setClippingRect(fabgl::Rect(CANVAS_X, CANVAS_Y, CANVAS_X + CANVAS_W - 1, CANVAS_Y + CANVAS_H - 1));
  fabgl::PaintOptions po;
  po.NOT = 1;
  Canvas.setPaintOptions(po);
  Canvas.setPenColor(fabgl::Color::White);
  for (int x = x0; x <= x1; x += 6) {
    int ex = min(x + 2, x1);
    Canvas.drawLine(x, y0, ex, y0);
    if (y1 != y0) Canvas.drawLine(x, y1, ex, y1);
  }
  for (int y = y0; y <= y1; y += 6) {
    int ey = min(y + 2, y1);
    Canvas.drawLine(x0, y, x0, ey);
    if (x1 != x0) Canvas.drawLine(x1, y, x1, ey);
  }
  po.NOT = 0;
  Canvas.setPaintOptions(po);
  Canvas.setClippingRect(fabgl::Rect(0, 0, SCREEN_W - 1, SCREEN_H - 1));
}

void drawSelectionOverlay() {
  if (!hasSelection) return;
  if (selFloating) drawFloatingSelectionOnScreen();
  int sx0 = canvasToScreenX(selX0);
  int sy0 = canvasToScreenY(selY0);
  int sx1 = canvasToScreenX(selX1) + (zoomMode ? ZOOM_SCALE - 1 : 0);
  int sy1 = canvasToScreenY(selY1) + (zoomMode ? ZOOM_SCALE - 1 : 0);
  drawDashedXORRect(sx0, sy0, sx1, sy1);
}

void drawTextCaretOverlay() {
  if (!textActive || !textCaretOn) return;
  const fabgl::FontInfo * f = getActiveTextFont();
  int sc = textScale2x ? 2 : 1;
  int h  = f->height * sc;
  int sx = canvasToScreenX(textCurX);
  int sy = canvasToScreenY(textCurY);
  int eh = zoomMode ? h * ZOOM_SCALE : h;
  Canvas.setClippingRect(fabgl::Rect(CANVAS_X, CANVAS_Y, CANVAS_X + CANVAS_W - 1, CANVAS_Y + CANVAS_H - 1));
  fabgl::PaintOptions po;
  po.NOT = 1;
  Canvas.setPaintOptions(po);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawLine(sx, sy, sx, sy + eh - 1);
  Canvas.drawLine(sx + 1, sy, sx + 1, sy + eh - 1);
  po.NOT = 0;
  Canvas.setPaintOptions(po);
  Canvas.setClippingRect(fabgl::Rect(0, 0, SCREEN_W - 1, SCREEN_H - 1));
}

void drawPreviewOverlay() {
  Canvas.setClippingRect(fabgl::Rect(CANVAS_X, CANVAS_Y, CANVAS_X + CANVAS_W - 1, CANVAS_Y + CANVAS_H - 1));
  fabgl::PaintOptions po;
  po.NOT = 1;
  Canvas.setPaintOptions(po);
  Canvas.setPenColor(fabgl::Color::White);

  // Polygon in-progress preview
  if (activeTool == TOOL_POLYGON && polyCount > 0) {
    for (int i = 0; i < polyCount - 1; ++i) {
      Canvas.drawLine(canvasToScreenX(polyX[i]), canvasToScreenY(polyY[i]),
                      canvasToScreenX(polyX[i + 1]), canvasToScreenY(polyY[i + 1]));
    }
    int tx = screenToCanvasX(curX), ty = screenToCanvasY(curY);
    applyShiftConstraint(TOOL_POLYGON, polyX[polyCount - 1], polyY[polyCount - 1], tx, ty);
    Canvas.drawLine(canvasToScreenX(polyX[polyCount - 1]), canvasToScreenY(polyY[polyCount - 1]),
                    canvasToScreenX(tx), canvasToScreenY(ty));
  }

  // Curve phase 2 preview
  if (activeTool == TOOL_CURVE && curvePhase == 2) {
    int steps = 28;
    int px = canvasToScreenX(curveX0);
    int py = canvasToScreenY(curveY0);
    for (int i = 1; i <= steps; ++i) {
      float t = (float)i / (float)steps;
      float u = 1.0f - t;
      int cx = (int)(u * u * curveX0 + 2.0f * u * t * curveCX + t * t * curveX1 + 0.5f);
      int cy = (int)(u * u * curveY0 + 2.0f * u * t * curveCY + t * t * curveY1 + 0.5f);
      int nx = canvasToScreenX(cx);
      int ny = canvasToScreenY(cy);
      Canvas.drawLine(px, py, nx, ny);
      px = nx; py = ny;
    }
  }

  // Active rubber-band drag preview
  if (drawingActive && hasPreview) {
    int sx0 = canvasToScreenX(dragStartX);
    int sy0 = canvasToScreenY(dragStartY);
    int sx1 = canvasToScreenX(dragCurX);
    int sy1 = canvasToScreenY(dragCurY);
    if (activeTool == TOOL_LINE || (activeTool == TOOL_CURVE && curvePhase == 1)) {
      Canvas.drawLine(sx0, sy0, sx1, sy1);
    } else if (activeTool == TOOL_RECT || activeTool == TOOL_ROUNDRECT) {
      Canvas.drawRectangle(sx0, sy0, sx1, sy1);
    } else if (activeTool == TOOL_SELECT) {
      po.NOT = 0;
      Canvas.setPaintOptions(po);
      drawDashedXORRect(sx0, sy0, sx1, sy1);
      Canvas.setClippingRect(fabgl::Rect(0, 0, SCREEN_W - 1, SCREEN_H - 1));
      return;
    } else if (activeTool == TOOL_ELLIPSE) {
      // XOR segmented ellipse outline preview
      int ex0 = min(sx0, sx1), ex1 = max(sx0, sx1);
      int ey0 = min(sy0, sy1), ey1 = max(sy0, sy1);
      float cx = (ex0 + ex1) * 0.5f;
      float cy = (ey0 + ey1) * 0.5f;
      float rx = (ex1 - ex0) * 0.5f;
      float ry = (ey1 - ey0) * 0.5f;
      if (rx >= 1.0f && ry >= 1.0f) {
        int segs = 36;
        int px = (int)(cx + rx + 0.5f);
        int py = (int)(cy + 0.5f);
        for (int i = 1; i <= segs; ++i) {
          float ang = (float)i * 6.2831853f / (float)segs;
          int nx = (int)(cx + cosf(ang) * rx + 0.5f);
          int ny = (int)(cy + sinf(ang) * ry + 0.5f);
          Canvas.drawLine(px, py, nx, ny);
          px = nx; py = ny;
        }
      } else {
        Canvas.drawLine(ex0, ey0, ex1, ey1);
      }
    }
  }

  po.NOT = 0;
  Canvas.setPaintOptions(po);
  Canvas.setClippingRect(fabgl::Rect(0, 0, SCREEN_W - 1, SCREEN_H - 1));
}

void flushCanvasRegion(int x0, int y0, int x1, int y1) {
  if (fullscreenMode) {
    // Fullscreen centers the 640x296 canvas on 720x350
    int fx = (SCREEN_W - CANVAS_W) / 2;
    int fy = (SCREEN_H - CANVAS_H) / 2;
    Canvas.setBrushColor(fabgl::Color::Black);
    Canvas.fillRectangle(0, 0, SCREEN_W - 1, SCREEN_H - 1);
    Canvas.drawBitmap(fx, fy, gCanvasBmp);
    Canvas.setPenColor(fabgl::Color::White);
    Canvas.drawRectangle(fx - 1, fy - 1, fx + CANVAS_W, fy + CANVAS_H);
    return;
  }

  x0 = constrain(x0, 0, CANVAS_W - 1);
  y0 = constrain(y0, 0, CANVAS_H - 1);
  x1 = constrain(x1, 0, CANVAS_W - 1);
  y1 = constrain(y1, 0, CANVAS_H - 1);
  if (x0 > x1 || y0 > y1) return;

  if (!zoomMode) {
    int sx0 = CANVAS_X + x0;
    int sy0 = CANVAS_Y + y0;
    int sx1 = CANVAS_X + x1;
    int sy1 = CANVAS_Y + y1;
    Canvas.setClippingRect(fabgl::Rect(sx0, sy0, sx1, sy1));
    Canvas.setBrushColor(fabgl::Color::Black);
    Canvas.fillRectangle(sx0, sy0, sx1, sy1);
    Canvas.drawBitmap(CANVAS_X, CANVAS_Y, gCanvasBmp);
    drawGridOnScreen(sx0, sy0, sx1, sy1);
    Canvas.setClippingRect(fabgl::Rect(0, 0, SCREEN_W - 1, SCREEN_H - 1));
  } else {
    // 4x FatBits viewport blit
    int vx0 = max(x0, zoomOriginX);
    int vy0 = max(y0, zoomOriginY);
    int vx1 = min(x1, zoomOriginX + ZOOM_VIEW_W - 1);
    int vy1 = min(y1, zoomOriginY + ZOOM_VIEW_H - 1);
    if (vx0 <= vx1 && vy0 <= vy1) {
      int sx0 = canvasToScreenX(vx0);
      int sy0 = canvasToScreenY(vy0);
      int sx1 = canvasToScreenX(vx1) + ZOOM_SCALE - 1;
      int sy1 = canvasToScreenY(vy1) + ZOOM_SCALE - 1;
      Canvas.setBrushColor(fabgl::Color::Black);
      Canvas.fillRectangle(sx0, sy0, sx1, sy1);
      Canvas.setBrushColor(fabgl::Color::White);
      for (int cy = vy0; cy <= vy1; ++cy) {
        int sy = canvasToScreenY(cy);
        int runStart = -1;
        for (int cx = vx0; cx <= vx1 + 1; ++cx) {
          bool on = (cx <= vx1) && gCanvas.getPixel(cx, cy);
          if (on && runStart < 0) {
            runStart = cx;
          } else if (!on && runStart >= 0) {
            int rx0 = canvasToScreenX(runStart);
            int rx1 = canvasToScreenX(cx - 1) + ZOOM_SCALE - 1;
            Canvas.fillRectangle(rx0, sy, rx1, sy + ZOOM_SCALE - 1);
            runStart = -1;
          }
        }
      }
      drawGridOnScreen(sx0, sy0, sx1, sy1);
    }
  }
}

void flushCanvasViewport(bool forceFull) {
  if (fullscreenMode) {
    flushCanvasRegion(0, 0, CANVAS_W - 1, CANVAS_H - 1);
    gCanvas.clearDirty();
    return;
  }
  if (forceFull) {
    flushCanvasRegion(0, 0, CANVAS_W - 1, CANVAS_H - 1);
    gCanvas.clearDirty();
    drawSelectionOverlay();
    drawPreviewOverlay();
    drawTextCaretOverlay();
  } else if (gCanvas.dirty) {
    flushCanvasRegion(gCanvas.dirtyX0, gCanvas.dirtyY0, gCanvas.dirtyX1, gCanvas.dirtyY1);
    gCanvas.clearDirty();
    if (hasSelection) drawSelectionOverlay();
    if (drawingActive || polyCount > 0 || curvePhase == 2) drawPreviewOverlay();
    if (textActive) drawTextCaretOverlay();
  }
}

// ============================================================================
// UI Chrome Rendering (Strictly 1-Bit Monochrome, Crisp Pixel Alignment)
// ============================================================================
void drawHeartbeatDot() {
  if (fullscreenMode || showHelpModal || showAboutModal || fbMode != FB_CLOSED) return;
  Canvas.setBrushColor(heartbeatOn ? fabgl::Color::White : fabgl::Color::Black);
  Canvas.fillRectangle(SCREEN_W - 5, 6, SCREEN_W - 3, 8);
}

void redrawMenuBar() {
  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));

  // Clear menu bar
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(0, 0, SCREEN_W - 1, MENUBAR_H - 1);

  // Title Badge (inverted monochrome pill)
  Canvas.setBrushColor(fabgl::Color::White);
  Canvas.fillRectangle(2, 1, 78, 13);
  Canvas.setPenColor(fabgl::Color::Black);
  Canvas.drawText(6, 4, " MDA PAINT ");

  // Menu headers (with underlined Alt-mnemonic first letter)
  for (int i = 0; i < NUM_MENUS; ++i) {
    bool active = (openMenuIdx == i) || (menuBarFocused && openMenuIdx < 0 && menuFocusIdx == i);
    if (active) {
      Canvas.setBrushColor(fabgl::Color::White);
      Canvas.fillRectangle(MENUS[i].x, 1, MENUS[i].x + MENUS[i].w - 1, 13);
      Canvas.setPenColor(fabgl::Color::Black);
    } else {
      Canvas.setPenColor(fabgl::Color::White);
    }
    Canvas.drawText(MENUS[i].x + 5, 4, MENUS[i].title);
    // Crisp 1px underline under mnemonic first letter (F, E, I, V, H)
    Canvas.drawLine(MENUS[i].x + 5, 12, MENUS[i].x + 10, 12);
  }

  // Right-side status indicators (strictly 1-bit badges + PS/2 status + mouse speed + heartbeat)
  Canvas.setPenColor(fabgl::Color::White);
  char infoBuf[64];
  snprintf(infoBuf, sizeof(infoBuf), "K:%c M:%c S:%d | %s | %s | U:%d",
           Keyboard.isKeyboardAvailable() ? 'Y' : 'N',
           Mouse.isMouseAvailable() ? 'Y' : 'N',
           mouseSensIdx + 1,
           zoomMode ? "4X" : "1X",
           paperVal ? "LT" : "DK",
           gCanvas.undoCount);
  Canvas.drawText(SCREEN_W - (int)strlen(infoBuf) * 6 - 10, 4, infoBuf);
  drawHeartbeatDot();

  // Bottom separator line
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawLine(0, MENUBAR_H, SCREEN_W - 1, MENUBAR_H);
  uiDirtyMenuBar = false;
}

void drawOpenMenuPopup() {
  if (openMenuIdx < 0 || openMenuIdx >= NUM_MENUS) return;
  const MenuDef & m = MENUS[openMenuIdx];
  int px = m.x;
  int py = MENUBAR_H + 1;
  int pw = MENU_POP_W;
  int ph = m.count * MENU_ITEM_H + 4;

  // Drop-shadow frame (crisp 1-bit double offset)
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(px, py, px + pw - 1, py + ph - 1);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(px, py, px + pw - 1, py + ph - 1);
  Canvas.drawLine(px + 2, py + ph, px + pw, py + ph);
  Canvas.drawLine(px + pw, py + 2, px + pw, py + ph);

  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));

  for (int i = 0; i < m.count; ++i) {
    int iy = py + 2 + i * MENU_ITEM_H;
    bool hovered = (hoverMenuIdx == i);
    if (hovered) {
      Canvas.setBrushColor(fabgl::Color::White);
      Canvas.fillRectangle(px + 2, iy, px + pw - 3, iy + MENU_ITEM_H - 1);
      Canvas.setPenColor(fabgl::Color::Black);
    } else {
      Canvas.setPenColor(fabgl::Color::White);
    }
    Canvas.drawText(px + 6, iy + 3, m.items[i].label);
    if (m.items[i].shortcut[0]) {
      int sw = (int)strlen(m.items[i].shortcut) * 6;
      Canvas.drawText(px + pw - sw - 6, iy + 3, m.items[i].shortcut);
    }
  }
}

void closeMenuAndRestore() {
  bool wasOpen = (openMenuIdx >= 0);
  if (!wasOpen && !menuBarFocused) return;
  openMenuIdx    = -1;
  hoverMenuIdx   = -1;
  menuBarFocused = false;
  redrawMenuBar();
  uiDirtyStatus = true;
  if (wasOpen) {
    redrawToolbar();
    flushCanvasViewport(true);
  }
}

void redrawToolbar() {
  // Clear left toolbar area
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(TOOLBAR_X, TOOLBAR_Y, TOOLBAR_X + TOOLBAR_W - 1, TOOLBAR_Y + TOOLBAR_H - 1);

  // Right vertical divider
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawLine(TOOLBAR_W - 1, TOOLBAR_Y, TOOLBAR_W - 1, TOOLBAR_Y + TOOLBAR_H - 1);

  // 14 Tool Buttons (2 cols x 7 rows)
  for (int i = 0; i < TOOL_COUNT; ++i) {
    int col = i & 1;
    int row = i >> 1;
    int bx = TOOL_GRID_X + col * TOOL_PITCH_X;
    int by = TOOL_GRID_Y + row * TOOL_PITCH_Y;
    bool sel = (activeTool == (Tool)i);

    if (sel) {
      Canvas.setBrushColor(fabgl::Color::White);
      Canvas.fillRectangle(bx, by, bx + TOOL_BTN_W - 1, by + TOOL_BTN_H - 1);
      Canvas.drawBitmap(bx + (TOOL_BTN_W - ICON_W) / 2, by + (TOOL_BTN_H - ICON_H) / 2, &gToolIconBlack[i]);
    } else {
      Canvas.setBrushColor(fabgl::Color::Black);
      Canvas.fillRectangle(bx, by, bx + TOOL_BTN_W - 1, by + TOOL_BTN_H - 1);
      Canvas.setPenColor(fabgl::Color::White);
      Canvas.drawRectangle(bx, by, bx + TOOL_BTN_W - 1, by + TOOL_BTN_H - 1);
      Canvas.drawBitmap(bx + (TOOL_BTN_W - ICON_W) / 2, by + (TOOL_BTN_H - ICON_H) / 2, &gToolIconWhite[i]);
    }
  }

  // ---- Context Options Box (y = 174..251) ----
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(OPT_BOX_X, OPT_BOX_Y, OPT_BOX_X + OPT_BOX_W - 1, OPT_BOX_Y + OPT_BOX_H - 1);
  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));

  if (activeTool == TOOL_TEXT) {
    // Font size selector (6x8, 8x8, 8x14) + Bold + 2X
    const char * fLabels[3] = { "6x8", "8x8", "8x14" };
    for (int i = 0; i < 3; ++i) {
      int ry = OPT_BOX_Y + 3 + i * 15;
      bool sel = (textFontIdx == i);
      Canvas.setBrushColor(sel ? fabgl::Color::White : fabgl::Color::Black);
      Canvas.fillRectangle(OPT_BOX_X + 3, ry, OPT_BOX_X + OPT_BOX_W - 4, ry + 12);
      if (!sel) {
        Canvas.setPenColor(fabgl::Color::White);
        Canvas.drawRectangle(OPT_BOX_X + 3, ry, OPT_BOX_X + OPT_BOX_W - 4, ry + 12);
      }
      Canvas.setPenColor(sel ? fabgl::Color::Black : fabgl::Color::White);
      Canvas.drawText(OPT_BOX_X + 18, ry + 3, fLabels[i]);
    }
    // Bold & 2X toggles
    int by = OPT_BOX_Y + 51;
    // Bold
    Canvas.setBrushColor(textBold ? fabgl::Color::White : fabgl::Color::Black);
    Canvas.fillRectangle(OPT_BOX_X + 3, by, OPT_BOX_X + 31, by + 22);
    Canvas.setPenColor(textBold ? fabgl::Color::Black : fabgl::Color::White);
    if (!textBold) Canvas.drawRectangle(OPT_BOX_X + 3, by, OPT_BOX_X + 31, by + 22);
    Canvas.drawText(OPT_BOX_X + 7, by + 7, "BLD");
    // 2X
    Canvas.setBrushColor(textScale2x ? fabgl::Color::White : fabgl::Color::Black);
    Canvas.fillRectangle(OPT_BOX_X + 34, by, OPT_BOX_X + 62, by + 22);
    Canvas.setPenColor(textScale2x ? fabgl::Color::Black : fabgl::Color::White);
    if (!textScale2x) Canvas.drawRectangle(OPT_BOX_X + 34, by, OPT_BOX_X + 62, by + 22);
    Canvas.drawText(OPT_BOX_X + 42, by + 7, "2X");
  } else if (activeTool == TOOL_SELECT) {
    // Opaque vs Transparent paste toggle
    Canvas.setPenColor(fabgl::Color::White);
    Canvas.drawText(OPT_BOX_X + 10, OPT_BOX_Y + 5, "PASTE:");
    const char * mLabels[2] = { "OPAQUE", "TRANSP" };
    for (int i = 0; i < 2; ++i) {
      int ry = OPT_BOX_Y + 18 + i * 26;
      bool sel = (pasteTransp == (i == 1));
      Canvas.setBrushColor(sel ? fabgl::Color::White : fabgl::Color::Black);
      Canvas.fillRectangle(OPT_BOX_X + 4, ry, OPT_BOX_X + OPT_BOX_W - 5, ry + 20);
      if (!sel) {
        Canvas.setPenColor(fabgl::Color::White);
        Canvas.drawRectangle(OPT_BOX_X + 4, ry, OPT_BOX_X + OPT_BOX_W - 5, ry + 20);
      }
      Canvas.setPenColor(sel ? fabgl::Color::Black : fabgl::Color::White);
      Canvas.drawText(OPT_BOX_X + 14, ry + 6, mLabels[i]);
    }
  } else {
    // Top half of options: 4 Size rows (y = OPT_BOX_Y + 3 .. + 43)
    for (int i = 0; i < 4; ++i) {
      int ry = OPT_BOX_Y + 3 + i * 10;
      bool sel = (activeSize == i);
      Canvas.setBrushColor(sel ? fabgl::Color::White : fabgl::Color::Black);
      Canvas.fillRectangle(OPT_BOX_X + 3, ry, OPT_BOX_X + OPT_BOX_W - 4, ry + 8);
      int barH = min(7, (int)BRUSH_SIZES[i]);
      int midY = ry + 4 - barH / 2;
      Canvas.setBrushColor(sel ? fabgl::Color::Black : fabgl::Color::White);
      Canvas.fillRectangle(OPT_BOX_X + 8, midY, OPT_BOX_X + OPT_BOX_W - 9, midY + barH - 1);
    }

    // Bottom half of options: Fill Mode (for shapes) OR Brush Shape (for Brush)
    int subY = OPT_BOX_Y + 46;
    Canvas.setPenColor(fabgl::Color::White);
    Canvas.drawLine(OPT_BOX_X + 2, subY - 2, OPT_BOX_X + OPT_BOX_W - 3, subY - 2);

    if (activeTool == TOOL_BRUSH) {
      // 4 Brush shapes in 2x2 mini-grid: Round, Square, '/', '\'
      for (int i = 0; i < 4; ++i) {
        int col = i & 1;
        int row = i >> 1;
        int sx = OPT_BOX_X + 4 + col * 30;
        int sy = subY + row * 15;
        bool sel = (brushShape == (BrushShape)i);
        Canvas.setBrushColor(sel ? fabgl::Color::White : fabgl::Color::Black);
        Canvas.fillRectangle(sx, sy, sx + 27, sy + 13);
        Canvas.setPenColor(sel ? fabgl::Color::Black : fabgl::Color::White);
        if (!sel) Canvas.drawRectangle(sx, sy, sx + 27, sy + 13);
        int mx = sx + 13, my = sy + 6;
        if (i == BRUSH_ROUND) {
          Canvas.setBrushColor(sel ? fabgl::Color::Black : fabgl::Color::White);
          Canvas.fillEllipse(mx, my, 8, 8);
        } else if (i == BRUSH_SQUARE) {
          Canvas.setBrushColor(sel ? fabgl::Color::Black : fabgl::Color::White);
          Canvas.fillRectangle(mx - 4, my - 3, mx + 4, my + 3);
        } else if (i == BRUSH_SLASH) {
          Canvas.drawLine(mx - 4, my + 4, mx + 4, my - 4);
          Canvas.drawLine(mx - 3, my + 4, mx + 5, my - 4);
        } else {
          Canvas.drawLine(mx - 4, my - 4, mx + 4, my + 4);
          Canvas.drawLine(mx - 3, my - 4, mx + 5, my + 4);
        }
      }
    } else {
      // 3 Fill Modes side-by-side: Outline, Outline+Pattern, Pattern Only
      for (int i = 0; i < 3; ++i) {
        int fx = OPT_BOX_X + 3 + i * 20;
        int fy = subY + 2;
        bool sel = (fillMode == (FillMode)i);
        Canvas.setBrushColor(sel ? fabgl::Color::White : fabgl::Color::Black);
        Canvas.fillRectangle(fx, fy, fx + 19, fy + 24);
        if (!sel) {
          Canvas.setPenColor(fabgl::Color::White);
          Canvas.drawRectangle(fx, fy, fx + 19, fy + 24);
        }
        // Mini icon showing mode
        int ix0 = fx + 4, iy0 = fy + 5, ix1 = fx + 15, iy1 = fy + 19;
        if (i == FILL_OUTLINE) {
          Canvas.setPenColor(sel ? fabgl::Color::Black : fabgl::Color::White);
          Canvas.drawRectangle(ix0, iy0, ix1, iy1);
        } else if (i == FILL_BOTH) {
          for (int dy = iy0 + 1; dy < iy1; ++dy) {
            for (int dx = ix0 + 1; dx < ix1; ++dx) {
              if (((dx + dy) & 1) == 0) {
                Canvas.setPixel(dx, dy, sel ? fabgl::RGB888(0, 0, 0) : fabgl::RGB888(255, 255, 255));
              }
            }
          }
          Canvas.setPenColor(sel ? fabgl::Color::Black : fabgl::Color::White);
          Canvas.drawRectangle(ix0, iy0, ix1, iy1);
        } else {
          Canvas.setBrushColor(sel ? fabgl::Color::Black : fabgl::Color::White);
          Canvas.fillRectangle(ix0, iy0, ix1, iy1);
        }
      }
    }
  }

  // ---- FG / BG Overlapping Swatch + UNDO / CLR Buttons (y = 256..311) ----
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(SWATCH_BOX_X, SWATCH_BOX_Y, SWATCH_BOX_X + SWATCH_BOX_W - 1, SWATCH_BOX_Y + 32);

  // Back (BG) square
  int bgX = SWATCH_BOX_X + 28, bgY = SWATCH_BOX_Y + 11;
  Canvas.setBrushColor(!fgInk ? fabgl::Color::White : fabgl::Color::Black);
  Canvas.fillRectangle(bgX, bgY, bgX + 20, bgY + 15);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(bgX, bgY, bgX + 20, bgY + 15);

  // Front (FG) square
  int fgX = SWATCH_BOX_X + 12, fgY = SWATCH_BOX_Y + 4;
  Canvas.setBrushColor(fgInk ? fabgl::Color::White : fabgl::Color::Black);
  Canvas.fillRectangle(fgX, fgY, fgX + 20, fgY + 15);
  Canvas.setPenColor(fabgl::Color::Black);
  Canvas.drawRectangle(fgX - 1, fgY - 1, fgX + 21, fgY + 16);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(fgX, fgY, fgX + 20, fgY + 15);

  // Quick Action Buttons: UNDO & CLR
  int qy = SWATCH_BOX_Y + 36;
  // UNDO
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(SWATCH_BOX_X, qy, SWATCH_BOX_X + 31, qy + 18);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(SWATCH_BOX_X, qy, SWATCH_BOX_X + 31, qy + 18);
  Canvas.drawText(SWATCH_BOX_X + 4, qy + 6, "UNDO");

  // CLR
  Canvas.fillRectangle(SWATCH_BOX_X + 34, qy, SWATCH_BOX_X + 65, qy + 18);
  Canvas.drawRectangle(SWATCH_BOX_X + 34, qy, SWATCH_BOX_X + 65, qy + 18);
  Canvas.drawText(SWATCH_BOX_X + 41, qy + 6, "CLR");

  uiDirtyToolbar = false;
}

void redrawPatternBar() {
  // Drain queued primitives before updating shared swatch bitmap buffers
  Canvas.waitCompletion(false);
  rebuildPatternSwatchBuffers(fgInk, activePat);

  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(0, PATBAR_Y - 1, SCREEN_W - 1, PATBAR_Y + PATBAR_H);

  // Top & Bottom separators
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawLine(0, PATBAR_Y - 1, SCREEN_W - 1, PATBAR_Y - 1);
  Canvas.drawLine(0, PATBAR_Y + PATBAR_H, SCREEN_W - 1, PATBAR_Y + PATBAR_H);

  // Active Pattern Preview Box on left (x = 4..37) - single fast bitmap blit
  Canvas.drawBitmap(5, PATBAR_Y + 2, &gActivePatBmp);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(4, PATBAR_Y + 1, 37, PATBAR_Y + PAT_TILE_H + 2);

  // 16 Classic Monochrome Pattern Tiles - 16 fast bitmap blits instead of ~2,800 setPixels
  for (int i = 0; i < NUM_PATTERNS; ++i) {
    int tx = PAT_START_X + i * PAT_PITCH_X;
    int ty = PATBAR_Y + 2;
    Canvas.drawBitmap(tx, ty + 1, &gPatTileBmp[i]);
    if (activePat == i) {
      Canvas.setPenColor(fabgl::Color::White);
      Canvas.drawRectangle(tx - 1, ty - 1, tx + PAT_TILE_W, ty + PAT_TILE_H);
      Canvas.setPenColor(fabgl::Color::Black);
      Canvas.drawRectangle(tx, ty, tx + PAT_TILE_W - 1, ty + PAT_TILE_H - 1);
    } else {
      Canvas.setPenColor(fabgl::Color::White);
      Canvas.drawRectangle(tx, ty, tx + PAT_TILE_W - 1, ty + PAT_TILE_H - 1);
    }
  }

  // Right-side Quick Toggle Buttons (x = 468..716)
  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));

  struct QuickBtn { int x; int w; const char * label; bool active; };
  QuickBtn qbs[5] = {
    { 468, 46, "SWAP", false },
    { 518, 46, "GRID", showGrid },
    { 568, 48, "ZOOM", zoomMode },
    { 620, 46, "INV",  false },
    { 670, 46, "HELP", showHelpModal }
  };
  for (int i = 0; i < 5; ++i) {
    Canvas.setBrushColor(qbs[i].active ? fabgl::Color::White : fabgl::Color::Black);
    Canvas.fillRectangle(qbs[i].x, PATBAR_Y + 2, qbs[i].x + qbs[i].w - 1, PATBAR_Y + 16);
    Canvas.setPenColor(qbs[i].active ? fabgl::Color::Black : fabgl::Color::White);
    if (!qbs[i].active) {
      Canvas.drawRectangle(qbs[i].x, PATBAR_Y + 2, qbs[i].x + qbs[i].w - 1, PATBAR_Y + 16);
    }
    int tw = (int)strlen(qbs[i].label) * 6;
    Canvas.drawText(qbs[i].x + (qbs[i].w - tw) / 2, PATBAR_Y + 6, qbs[i].label);
  }

  uiDirtyPatterns = false;
}

void redrawStatusBar() {
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(0, STATUS_Y, SCREEN_W - 1, SCREEN_H - 1);
  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
  Canvas.setPenColor(fabgl::Color::White);

  if (statusMsg[0] && millis() < statusMsgUntil) {
    Canvas.setBrushColor(fabgl::Color::White);
    Canvas.fillRectangle(4, STATUS_Y + 1, 360, SCREEN_H - 2);
    Canvas.setPenColor(fabgl::Color::Black);
    Canvas.drawText(10, STATUS_Y + 3, statusMsg);
    Canvas.setPenColor(fabgl::Color::White);
  } else {
    statusMsg[0] = '\0';
    char leftBuf[64];
    snprintf(leftBuf, sizeof(leftBuf), "TOOL:%-7s SZ:%d PAT:%-10s INK:%s",
             TOOL_NAMES[activeTool],
             BRUSH_SIZES[activeSize],
             MDA_PATTERN_NAMES[activePat],
             fgInk ? "ON " : "OFF");
    Canvas.drawText(6, STATUS_Y + 3, leftBuf);
  }

  // Coordinates / Drag dimensions
  char coordBuf[32];
  if (ptInCanvasViewport(curX, curY)) {
    int cx = screenToCanvasX(curX);
    int cy = screenToCanvasY(curY);
    if (drawingActive && (activeTool == TOOL_RECT || activeTool == TOOL_ROUNDRECT ||
                          activeTool == TOOL_ELLIPSE || activeTool == TOOL_SELECT ||
                          activeTool == TOOL_LINE)) {
      snprintf(coordBuf, sizeof(coordBuf), "%3d,%3d (%dx%d)", cx, cy,
               abs(dragCurX - dragStartX) + 1, abs(dragCurY - dragStartY) + 1);
    } else {
      snprintf(coordBuf, sizeof(coordBuf), "X:%3d Y:%3d", cx, cy);
    }
  } else {
    snprintf(coordBuf, sizeof(coordBuf), "X:--- Y:---");
  }
  Canvas.drawText(374, STATUS_Y + 3, coordBuf);

  // Right contextual hint
  const char * hint = "Alt:Menu L:Draw R:Bg F1:Help";
  if (openMenuIdx >= 0) {
    hint = "Up/Dn:Item L/R:Menu Ent:Run Esc";
  } else if (menuBarFocused) {
    hint = "L/R:Menu Dn/Ent:Open F/E/I/V/H";
  } else if (activeTool == TOOL_CURVE) {
    hint = (curvePhase == 2) ? "Move/Click:Bend R-Click:Cancel" : "Drag Endpoints, Then Bend";
  } else if (activeTool == TOOL_POLYGON) {
    hint = "Click:Vertex R-Click/Ent:Close";
  } else if (activeTool == TOOL_TEXT) {
    hint = textActive ? "Type Text | Ent:NL | Esc:Done" : "Click Canvas To Place Text";
  } else if (activeTool == TOOL_SELECT) {
    hint = hasSelection ? "Drag:Move Ctrl+C/X/V Del:Clr" : "Drag Box To Select Area";
  } else if (zoomMode) {
    hint = "4x FatBits | Arrows:Pan | Z:1x";
  }
  int hw = (int)strlen(hint) * 6;
  Canvas.drawText(SCREEN_W - hw - 6, STATUS_Y + 3, hint);

  uiDirtyStatus = false;
}

void drawCanvasFrame() {
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(CANVAS_X - 1, CANVAS_Y - 1, CANVAS_X + CANVAS_W, CANVAS_Y + CANVAS_H);
}

void drawHelpModal() {
  int mx = 110, my = 26, mw = 500, mh = 298;
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(mx, my, mx + mw - 1, my + mh - 1);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(mx, my, mx + mw - 1, my + mh - 1);
  Canvas.drawRectangle(mx + 2, my + 2, mx + mw - 3, my + mh - 3);

  // Title bar
  Canvas.setBrushColor(fabgl::Color::White);
  Canvas.fillRectangle(mx + 4, my + 4, mx + mw - 5, my + 18);
  Canvas.selectFont(&fabgl::FONT_8x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
  Canvas.setPenColor(fabgl::Color::Black);
  Canvas.drawText(mx + 124, my + 7, "MDA PAINT - CONTROLS & SHORTCUTS");

  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setPenColor(fabgl::Color::White);
  const char * lines[] = {
    "MOUSE (Primary Input):",
    "  Left Drag/Click  : Draw with Foreground Ink / Active Pattern",
    "  Right Drag/Click : Draw with Background Ink / Close Poly / Cancel",
    "  Middle Click     : Toggle 4x FatBits Pixel Zoom at Cursor",
    "  Scroll Wheel     : Brush/Line Size (+Shift: Cycle Pattern)",
    "",
    "TOOLS (Single-Key Shortcuts):",
    "  S:Select   P:Pencil   B:Brush    E:Eraser   F:Fill Bucket",
    "  A:Spray    L:Line     C:Curve    R:Rect     U:Round Rect",
    "  O:Ellipse  Y:Polygon  T:Text     Z:Zoom 4x FatBits",
    "",
    "EDIT & VIEW SHORTCUTS:",
    "  Ctrl+Z / Ctrl+Y  : Undo / Redo        Ctrl+N : Clear Canvas",
    "  Ctrl+C / X / V   : Copy / Cut / Paste Ctrl+A : Select All",
    "  [ / ]            : Smaller/Larger Sz  , / .  : Prev/Next Pattern",
    "  X : Swap FG/BG   G : Grid Overlay     I : Dark/Light Paper",
    "  Ctrl+I : Invert  Tab : Fullscreen     Shift+Drag : Snap/Square",
    "  Arrows : 1px Cursor Nudge (+Shift=8px, or Pan in 4x Zoom)",
    "  Alt/F10: Top Menu Bar (Alt+F/E/I/V/H) - / + : Mouse Speed",
    "  Space  : Keyboard Draw / Click  F5:Reset Mouse  Esc:Cancel",
    "",
    "          Press F1, Esc, or Click Anywhere to Close"
  };
  int y = my + 26;
  for (int i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); ++i) {
    Canvas.drawText(mx + 14, y, lines[i]);
    y += 12;
  }
}

void drawAboutModal() {
  int mx = 175, my = 86, mw = 370, mh = 174;
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(mx, my, mx + mw - 1, my + mh - 1);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(mx, my, mx + mw - 1, my + mh - 1);
  Canvas.drawRectangle(mx + 2, my + 2, mx + mw - 3, my + mh - 3);

  Canvas.setBrushColor(fabgl::Color::White);
  Canvas.fillRectangle(mx + 4, my + 4, mx + mw - 5, my + 20);
  Canvas.selectFont(&fabgl::FONT_8x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
  Canvas.setPenColor(fabgl::Color::Black);
  Canvas.drawText(mx + 116, my + 8, "ABOUT MDA PAINT");

  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawText(mx + 16, my + 32, "MDA Paint v1.0 - Hercules Monochrome Edition");
  Canvas.drawText(mx + 16, my + 48, "Strictly 1-bit ON/OFF TTL Video (No Grayscale)");
  Canvas.drawText(mx + 16, my + 62, "Mode  : 720 x 350 @ 50Hz MDA Hercules");
  Canvas.drawText(mx + 16, my + 76, "Canvas: 640 x 296 (1-bit, 23.1 KB)");
  char memBuf[48];
  snprintf(memBuf, sizeof(memBuf), "Undo  : %d levels | Free Heap: %u KB",
           gCanvas.undoCapacity, (unsigned)(ESP.getFreeHeap() / 1024));
  Canvas.drawText(mx + 16, my + 90, memBuf);
  Canvas.drawText(mx + 16, my + 108, "Input : PS/2 Mouse (Port 1) + Keyboard (Port 0)");
  Canvas.drawText(mx + 16, my + 122, "SD BMP: CS=13 MOSI=12 MISO=2 CLK=14 (1-bit BMP)");
  Canvas.drawText(mx + 68, my + 150, "Click or Press Any Key to Continue");
}

// ============================================================================
// SD Card BMP File Browser Modal (Mouse + Full Keyboard Navigation)
// ============================================================================
void fbEnsureSelectionVisible() {
  if (gSDEntryCount <= 0) {
    fbSelectedIdx = 0;
    fbScrollTop   = 0;
    return;
  }
  fbSelectedIdx = constrain(fbSelectedIdx, 0, gSDEntryCount - 1);
  if (fbSelectedIdx < fbScrollTop) fbScrollTop = fbSelectedIdx;
  if (fbSelectedIdx >= fbScrollTop + FB_VIS_ROWS) fbScrollTop = fbSelectedIdx - FB_VIS_ROWS + 1;
  int maxTop = max(0, gSDEntryCount - FB_VIS_ROWS);
  fbScrollTop = constrain(fbScrollTop, 0, maxTop);
}

void drawFileBrowserModal() {
  if (fbMode == FB_CLOSED) return;

  // Frame with double 1-bit border
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(FB_X, FB_Y, FB_X + FB_W - 1, FB_Y + FB_H - 1);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(FB_X, FB_Y, FB_X + FB_W - 1, FB_Y + FB_H - 1);
  Canvas.drawRectangle(FB_X + 2, FB_Y + 2, FB_X + FB_W - 3, FB_Y + FB_H - 3);

  // Inverted title bar
  Canvas.setBrushColor(fabgl::Color::White);
  Canvas.fillRectangle(FB_X + 4, FB_Y + 4, FB_X + FB_W - 5, FB_Y + 18);
  Canvas.selectFont(&fabgl::FONT_8x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
  Canvas.setPenColor(fabgl::Color::Black);
  const char * title = (fbMode == FB_OPEN) ? "OPEN BMP FROM SD CARD" : "SAVE BMP TO SD CARD";
  Canvas.drawText(FB_X + (FB_W - (int)strlen(title) * 8) / 2, FB_Y + 7, title);

  // Directory path & SD status bar
  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setPenColor(fabgl::Color::White);
  char dirLine[56];
  snprintf(dirLine, sizeof(dirLine), "DIR: %.40s", gSDCurDir);
  Canvas.drawText(FB_LIST_X, FB_Y + 24, dirLine);

  char sdInfo[28];
  if (gSDMounted) snprintf(sdInfo, sizeof(sdInfo), "SD:OK (%d)", gSDEntryCount);
  else            snprintf(sdInfo, sizeof(sdInfo), "SD:NO CARD");
  Canvas.drawText(FB_X + FB_W - (int)strlen(sdInfo) * 6 - 10, FB_Y + 24, sdInfo);

  // File list box
  Canvas.drawRectangle(FB_LIST_X, FB_LIST_Y, FB_LIST_X + FB_LIST_W - 1, FB_LIST_Y + FB_LIST_H - 1);

  if (!gSDMounted) {
    Canvas.drawText(FB_LIST_X + 24, FB_LIST_Y + 56, "No SD card detected on CS=13 MOSI=12 MISO=2 CLK=14.");
    Canvas.drawText(FB_LIST_X + 24, FB_LIST_Y + 72, "Insert FAT32 microSD card and click [REMOUNT].");
  } else if (gSDEntryCount == 0) {
    Canvas.drawText(FB_LIST_X + 24, FB_LIST_Y + 64, "(No .BMP files or subfolders in this directory)");
  } else {
    fbEnsureSelectionVisible();
    for (int row = 0; row < FB_VIS_ROWS; ++row) {
      int idx = fbScrollTop + row;
      if (idx >= gSDEntryCount) break;
      int ry = FB_LIST_Y + 3 + row * FB_ROW_H;
      bool sel = (idx == fbSelectedIdx);
      if (sel) {
        Canvas.setBrushColor(fabgl::Color::White);
        Canvas.fillRectangle(FB_LIST_X + 2, ry, FB_LIST_X + FB_LIST_W - 3, ry + FB_ROW_H - 1);
        Canvas.setPenColor(fabgl::Color::Black);
      } else {
        Canvas.setPenColor(fabgl::Color::White);
      }
      const SDFileEntry & e = gSDEntries[idx];
      char line[72];
      if (e.isDir) {
        snprintf(line, sizeof(line), "[DIR]  %-32.32s", e.name);
      } else {
        uint32_t kb = (e.size + 1023) / 1024;
        snprintf(line, sizeof(line), "       %-32.32s %5u KB", e.name, (unsigned)kb);
      }
      Canvas.drawText(FB_LIST_X + 6, ry + 3, line);
    }
  }

  // Right scrollbar (x = 574..591, y = 72..231)
  int sbX = FB_LIST_X + FB_LIST_W + 2;
  int sbW = 18;
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(sbX, FB_LIST_Y, sbX + sbW - 1, FB_LIST_Y + FB_LIST_H - 1);
  // Up arrow button
  Canvas.drawRectangle(sbX, FB_LIST_Y, sbX + sbW - 1, FB_LIST_Y + 16);
  Canvas.drawText(sbX + 6, FB_LIST_Y + 5, "^");
  // Down arrow button
  Canvas.drawRectangle(sbX, FB_LIST_Y + FB_LIST_H - 17, sbX + sbW - 1, FB_LIST_Y + FB_LIST_H - 1);
  Canvas.drawText(sbX + 6, FB_LIST_Y + FB_LIST_H - 12, "v");
  // Proportional thumb
  int trackY0 = FB_LIST_Y + 18;
  int trackH  = FB_LIST_H - 36;
  if (gSDEntryCount > FB_VIS_ROWS) {
    int thumbH = max(14, (trackH * FB_VIS_ROWS) / gSDEntryCount);
    int maxTop = gSDEntryCount - FB_VIS_ROWS;
    int thumbY = trackY0 + ((trackH - thumbH) * fbScrollTop) / max(1, maxTop);
    Canvas.setBrushColor(fabgl::Color::White);
    Canvas.fillRectangle(sbX + 3, thumbY, sbX + sbW - 4, thumbY + thumbH - 1);
  } else {
    Canvas.setBrushColor(fabgl::Color::White);
    Canvas.fillRectangle(sbX + 3, trackY0 + 2, sbX + sbW - 4, trackY0 + trackH - 3);
  }

  // Filename box (SAVE mode) or Hint line (OPEN mode)
  Canvas.setPenColor(fabgl::Color::White);
  if (fbMode == FB_SAVE) {
    Canvas.drawText(FB_LIST_X, 243, "FILE:");
    Canvas.drawRectangle(166, 238, 591, 256);
    char disp[40];
    snprintf(disp, sizeof(disp), "%s_", fbFilename);
    Canvas.drawText(172, 243, disp);
  } else {
    Canvas.drawText(FB_LIST_X, 243, "Up/Dn:Select  Enter/DblClick:Open  Bksp:Parent  Del:Delete");
  }

  // Status / error banner line
  if (fbStatusText[0]) {
    Canvas.setBrushColor(fabgl::Color::White);
    Canvas.fillRectangle(FB_LIST_X, 261, 591, 273);
    Canvas.setPenColor(fabgl::Color::Black);
    Canvas.drawText(FB_LIST_X + 6, 263, fbStatusText);
  } else if (fbMode == FB_SAVE) {
    Canvas.setPenColor(fabgl::Color::White);
    Canvas.drawText(FB_LIST_X, 263, "Type filename + Enter to Save (.BMP added automatically)");
  }

  // Bottom action buttons (y = 280..304)
  struct FbBtn { int x; int w; const char * label; bool primary; };
  FbBtn btns[4] = {
    { 128,  96, (fbMode == FB_OPEN) ? "OPEN" : "SAVE", true },
    { 236,  96, fbConfirmDelete ? "CONFIRM!" : "DELETE", fbConfirmDelete },
    { 344, 104, "REMOUNT", false },
    { 460, 132, "CANCEL",  false }
  };
  for (int i = 0; i < 4; ++i) {
    Canvas.setBrushColor(btns[i].primary ? fabgl::Color::White : fabgl::Color::Black);
    Canvas.fillRectangle(btns[i].x, 280, btns[i].x + btns[i].w - 1, 304);
    Canvas.setPenColor(btns[i].primary ? fabgl::Color::Black : fabgl::Color::White);
    if (!btns[i].primary) {
      Canvas.drawRectangle(btns[i].x, 280, btns[i].x + btns[i].w - 1, 304);
    }
    int tw = (int)strlen(btns[i].label) * 6;
    Canvas.drawText(btns[i].x + (btns[i].w - tw) / 2, 289, btns[i].label);
  }
}

void openFileBrowser(FileBrowserMode mode) {
  clearSelection(true);
  if (textActive) commitTextSession();
  fbMode          = mode;
  fbSelectedIdx   = 0;
  fbScrollTop     = 0;
  fbConfirmDelete = false;
  fbTypingName    = (mode == FB_SAVE);
  fbLastClickIdx  = -1;
  fbStatusText[0] = '\0';

  if (!sdRefreshDirectory()) {
    snprintf(fbStatusText, sizeof(fbStatusText), "SD Mount Failed - Check Card & Click REMOUNT");
  }
  if (mode == FB_SAVE) {
    if (currentFilePath[0]) {
      const char * base = strrchr(currentFilePath, '/');
      base = base ? (base + 1) : currentFilePath;
      strncpy(fbFilename, base, sizeof(fbFilename) - 1);
      fbFilename[sizeof(fbFilename) - 1] = '\0';
    } else {
      sdSuggestNextFilename(fbFilename, sizeof(fbFilename));
    }
    fbFilenameLen = (int)strlen(fbFilename);
  }
  updateHardwareCursor();
  drawFileBrowserModal();
}

void closeFileBrowser() {
  fbMode = FB_CLOSED;
  fbConfirmDelete = false;
  redrawAll();
  updateHardwareCursor();
}

void fbExecutePrimary() {
  fbConfirmDelete = false;
  if (!gSDMounted) {
    snprintf(fbStatusText, sizeof(fbStatusText), "No SD card mounted! Click REMOUNT.");
    drawFileBrowserModal();
    return;
  }

  // Enter selected folder if user is browsing directories
  bool selectedIsDir = (gSDEntryCount > 0 && fbSelectedIdx >= 0 && fbSelectedIdx < gSDEntryCount &&
                        gSDEntries[fbSelectedIdx].isDir);
  if (selectedIsDir && (fbMode == FB_OPEN || !fbTypingName)) {
    if (strcmp(gSDEntries[fbSelectedIdx].name, "..") == 0) {
      sdGoParentDir();
    } else {
      char nextDir[64];
      buildFullPath(nextDir, sizeof(nextDir), gSDCurDir, gSDEntries[fbSelectedIdx].name);
      strncpy(gSDCurDir, nextDir, sizeof(gSDCurDir) - 1);
      gSDCurDir[sizeof(gSDCurDir) - 1] = '\0';
    }
    fbSelectedIdx = 0;
    fbScrollTop   = 0;
    fbStatusText[0] = '\0';
    sdRefreshDirectory();
    drawFileBrowserModal();
    return;
  }

  if (fbMode == FB_OPEN) {
    if (gSDEntryCount <= 0 || fbSelectedIdx < 0 || fbSelectedIdx >= gSDEntryCount) {
      snprintf(fbStatusText, sizeof(fbStatusText), "Select a .BMP file to open");
      drawFileBrowserModal();
      return;
    }
    char fullPath[96];
    buildFullPath(fullPath, sizeof(fullPath), gSDCurDir, gSDEntries[fbSelectedIdx].name);
    char err[48] = "";
    if (sdLoadBMP(fullPath, gCanvas, paperVal, err, sizeof(err))) {
      strncpy(currentFilePath, fullPath, sizeof(currentFilePath) - 1);
      currentFilePath[sizeof(currentFilePath) - 1] = '\0';
      fbMode = FB_CLOSED;
      char toast[48];
      snprintf(toast, sizeof(toast), "Opened: %.34s", gSDEntries[fbSelectedIdx].name);
      setStatusToast(toast, 3000);
      redrawAll();
      updateHardwareCursor();
    } else {
      snprintf(fbStatusText, sizeof(fbStatusText), "Open failed: %s", err);
      drawFileBrowserModal();
    }
  } else if (fbMode == FB_SAVE) {
    if (fbFilenameLen <= 0) {
      snprintf(fbStatusText, sizeof(fbStatusText), "Enter a filename first!");
      drawFileBrowserModal();
      return;
    }
    char cleanName[32];
    strncpy(cleanName, fbFilename, sizeof(cleanName) - 1);
    cleanName[sizeof(cleanName) - 1] = '\0';
    if (!hasBmpExtension(cleanName) && strlen(cleanName) + 4 < sizeof(cleanName)) {
      strcat(cleanName, ".BMP");
    }
    char fullPath[96];
    buildFullPath(fullPath, sizeof(fullPath), gSDCurDir, cleanName);
    char err[48] = "";
    if (sdSaveBMP(fullPath, gCanvas, err, sizeof(err))) {
      strncpy(currentFilePath, fullPath, sizeof(currentFilePath) - 1);
      currentFilePath[sizeof(currentFilePath) - 1] = '\0';
      fbMode = FB_CLOSED;
      char toast[48];
      snprintf(toast, sizeof(toast), "Saved: %.35s", cleanName);
      setStatusToast(toast, 3000);
      redrawAll();
      updateHardwareCursor();
    } else {
      snprintf(fbStatusText, sizeof(fbStatusText), "Save failed: %s", err);
      drawFileBrowserModal();
    }
  }
}

void fbExecuteDelete() {
  if (!gSDMounted || gSDEntryCount <= 0 || fbSelectedIdx < 0 || fbSelectedIdx >= gSDEntryCount) return;
  if (gSDEntries[fbSelectedIdx].isDir) {
    snprintf(fbStatusText, sizeof(fbStatusText), "Cannot delete directories here");
    fbConfirmDelete = false;
    drawFileBrowserModal();
    return;
  }
  if (!fbConfirmDelete) {
    fbConfirmDelete = true;
    snprintf(fbStatusText, sizeof(fbStatusText), "Delete %s? Click CONFIRM! or press Del", gSDEntries[fbSelectedIdx].name);
    drawFileBrowserModal();
    return;
  }
  char fullPath[96];
  buildFullPath(fullPath, sizeof(fullPath), gSDCurDir, gSDEntries[fbSelectedIdx].name);
  if (SD.remove(fullPath)) {
    snprintf(fbStatusText, sizeof(fbStatusText), "Deleted %s", gSDEntries[fbSelectedIdx].name);
    if (strcmp(currentFilePath, fullPath) == 0) currentFilePath[0] = '\0';
  } else {
    snprintf(fbStatusText, sizeof(fbStatusText), "Delete failed!");
  }
  fbConfirmDelete = false;
  sdRefreshDirectory();
  fbEnsureSelectionVisible();
  drawFileBrowserModal();
}

void handleFileBrowserClick(int sx, int sy) {
  // Click outside dialog -> ignore (use CANCEL or Esc to prevent accidental dismissal)
  if (!ptInRect(sx, sy, FB_X, FB_Y, FB_W, FB_H)) return;

  // File list click
  if (ptInRect(sx, sy, FB_LIST_X, FB_LIST_Y, FB_LIST_W, FB_LIST_H)) {
    int row = (sy - (FB_LIST_Y + 3)) / FB_ROW_H;
    int idx = fbScrollTop + row;
    if (row >= 0 && row < FB_VIS_ROWS && idx >= 0 && idx < gSDEntryCount) {
      uint32_t now = millis();
      bool dblClick = (idx == fbLastClickIdx && (now - fbLastClickMs) <= 450);
      fbLastClickIdx  = idx;
      fbLastClickMs   = now;
      fbSelectedIdx   = idx;
      fbConfirmDelete = false;
      fbStatusText[0] = '\0';
      fbTypingName    = !gSDEntries[idx].isDir;
      if (fbMode == FB_SAVE && !gSDEntries[idx].isDir) {
        strncpy(fbFilename, gSDEntries[idx].name, sizeof(fbFilename) - 1);
        fbFilename[sizeof(fbFilename) - 1] = '\0';
        fbFilenameLen = (int)strlen(fbFilename);
      }
      if (dblClick) {
        fbExecutePrimary();
      } else {
        drawFileBrowserModal();
      }
    }
    return;
  }

  // Scrollbar buttons
  int sbX = FB_LIST_X + FB_LIST_W + 2;
  if (ptInRect(sx, sy, sbX, FB_LIST_Y, 18, 17)) {
    if (fbScrollTop > 0) { fbScrollTop--; drawFileBrowserModal(); }
    return;
  }
  if (ptInRect(sx, sy, sbX, FB_LIST_Y + FB_LIST_H - 17, 18, 17)) {
    if (fbScrollTop + FB_VIS_ROWS < gSDEntryCount) { fbScrollTop++; drawFileBrowserModal(); }
    return;
  }

  // Filename box click -> focus typing
  if (fbMode == FB_SAVE && ptInRect(sx, sy, 166, 238, 425, 19)) {
    fbTypingName = true;
    fbConfirmDelete = false;
    drawFileBrowserModal();
    return;
  }

  // Bottom buttons (y = 280..304)
  if (sy >= 280 && sy <= 304) {
    if (sx >= 128 && sx < 224) { fbExecutePrimary(); return; }
    if (sx >= 236 && sx < 332) { fbExecuteDelete(); return; }
    if (sx >= 344 && sx < 448) {
      fbConfirmDelete = false;
      if (sdMountCard(true) && sdRefreshDirectory()) {
        snprintf(fbStatusText, sizeof(fbStatusText), "SD Card Remounted OK");
      } else {
        snprintf(fbStatusText, sizeof(fbStatusText), "SD Mount Failed (CS=13 MOSI=12 MISO=2 CLK=14)");
      }
      fbSelectedIdx = 0; fbScrollTop = 0;
      drawFileBrowserModal();
      return;
    }
    if (sx >= 460 && sx < 592) { closeFileBrowser(); return; }
  }
}

void handleFileBrowserKey(const fabgl::VirtualKeyItem & item) {
  if (item.vk == fabgl::VK_ESCAPE) {
    closeFileBrowser();
    return;
  }
  if (item.vk == fabgl::VK_UP) {
    if (fbSelectedIdx > 0) fbSelectedIdx--;
    fbConfirmDelete = false;
    if (gSDEntryCount > 0 && gSDEntries[fbSelectedIdx].isDir) fbTypingName = false;
    fbEnsureSelectionVisible();
    drawFileBrowserModal();
    return;
  }
  if (item.vk == fabgl::VK_DOWN) {
    if (fbSelectedIdx + 1 < gSDEntryCount) fbSelectedIdx++;
    fbConfirmDelete = false;
    if (gSDEntryCount > 0 && gSDEntries[fbSelectedIdx].isDir) fbTypingName = false;
    fbEnsureSelectionVisible();
    drawFileBrowserModal();
    return;
  }
  if (item.vk == fabgl::VK_PAGEUP) {
    fbSelectedIdx = max(0, fbSelectedIdx - 8);
    fbConfirmDelete = false;
    fbEnsureSelectionVisible();
    drawFileBrowserModal();
    return;
  }
  if (item.vk == fabgl::VK_PAGEDOWN) {
    fbSelectedIdx = min(max(0, gSDEntryCount - 1), fbSelectedIdx + 8);
    fbConfirmDelete = false;
    fbEnsureSelectionVisible();
    drawFileBrowserModal();
    return;
  }
  if (item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_KP_ENTER) {
    fbExecutePrimary();
    return;
  }
  if (item.vk == fabgl::VK_DELETE) {
    fbExecuteDelete();
    return;
  }
  if (item.vk == fabgl::VK_TAB && fbMode == FB_SAVE) {
    // Copy highlighted BMP name into filename box
    if (gSDEntryCount > 0 && fbSelectedIdx < gSDEntryCount && !gSDEntries[fbSelectedIdx].isDir) {
      strncpy(fbFilename, gSDEntries[fbSelectedIdx].name, sizeof(fbFilename) - 1);
      fbFilename[sizeof(fbFilename) - 1] = '\0';
      fbFilenameLen = (int)strlen(fbFilename);
      fbTypingName = true;
      drawFileBrowserModal();
    }
    return;
  }
  if (item.vk == fabgl::VK_BACKSPACE) {
    fbConfirmDelete = false;
    if (fbMode == FB_SAVE) {
      fbTypingName = true;
      if (fbFilenameLen > 0) {
        fbFilename[--fbFilenameLen] = '\0';
        drawFileBrowserModal();
      }
    } else {
      // In Open mode, Backspace navigates to parent directory
      if (strcmp(gSDCurDir, "/") != 0) {
        sdGoParentDir();
        fbSelectedIdx = 0; fbScrollTop = 0;
        sdRefreshDirectory();
        drawFileBrowserModal();
      }
    }
    return;
  }
  // Printable filename characters in Save mode
  if (fbMode == FB_SAVE && !item.CTRL && item.ASCII >= 32 && item.ASCII <= 126) {
    char c = (char)item.ASCII;
    bool valid = isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.';
    if (valid && fbFilenameLen < 24) {
      fbTypingName = true;
      fbConfirmDelete = false;
      fbStatusText[0] = '\0';
      fbFilename[fbFilenameLen++] = (char)toupper((unsigned char)c);
      fbFilename[fbFilenameLen] = '\0';
      drawFileBrowserModal();
    }
  }
}

void redrawAll() {
  if (fullscreenMode) {
    flushCanvasViewport(true);
    return;
  }
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(0, 0, SCREEN_W - 1, SCREEN_H - 1);
  redrawMenuBar();
  redrawToolbar();
  drawCanvasFrame();
  flushCanvasViewport(true);
  redrawPatternBar();
  redrawStatusBar();
  if (openMenuIdx >= 0)   drawOpenMenuPopup();
  if (showHelpModal)      drawHelpModal();
  if (showAboutModal)     drawAboutModal();
  if (fbMode != FB_CLOSED) drawFileBrowserModal();
}

// ============================================================================
// Selection, Text & Active Operation Helpers
// ============================================================================
void commitFloatingSelection() {
  if (!selFloating) return;
  gCanvas.pasteBuffer(selX0, selY0, selBuffer, pasteTransp, paperVal);
  selFloating = false;
  selDragging = false;
  selBuffer.freeBuf();
  flushCanvasViewport(true);
}

void clearSelection(bool keepPixels) {
  if (!hasSelection) return;
  if (selFloating) {
    if (keepPixels) commitFloatingSelection();
    else {
      selFloating = false;
      selDragging = false;
      selBuffer.freeBuf();
    }
  }
  hasSelection = false;
  flushCanvasViewport(true);
}

void renderLiveTextOntoCanvas() {
  // Restore canvas from top undo snapshot (taken when text started), then draw textBuf
  if (!textActive || gCanvas.undoCount <= 0 || gCanvas.undoCapacity <= 0) return;
  int snapIdx = (gCanvas.undoHead - 1 + gCanvas.undoCapacity) % gCanvas.undoCapacity;
  memcpy(gCanvas.buf, gCanvas.undoSlots[snapIdx], CANVAS_BYTES);

  const fabgl::FontInfo * f = getActiveTextFont();
  int sc = textScale2x ? 2 : 1;
  int advX = (f->width + (textBold ? 1 : 0)) * sc;
  int advY = (f->height + 2) * sc;
  int tx = textOriginX;
  int ty = textOriginY;

  for (int i = 0; i < textLen; ++i) {
    if (textBuf[i] == '\n') {
      tx = textOriginX;
      ty += advY;
    } else {
      gCanvas.drawChar(tx, ty, (uint8_t)textBuf[i], f, textBold, textScale2x, textInk);
      tx += advX;
      if (tx + advX > CANVAS_W) {
        tx = textOriginX;
        ty += advY;
      }
    }
  }
  textCurX = tx;
  textCurY = ty;
  gCanvas.markFullDirty();
  flushCanvasViewport(true);
}

void commitTextSession() {
  if (!textActive) return;
  textActive = false;
  textLen = 0;
  textBuf[0] = '\0';
  flushCanvasViewport(true);
  uiDirtyStatus = true;
}

void cancelActiveOperation() {
  if (openMenuIdx >= 0 || menuBarFocused) {
    closeMenuAndRestore();
    return;
  }
  if (showHelpModal || showAboutModal) {
    showHelpModal = false;
    showAboutModal = false;
    redrawAll();
    return;
  }
  if (fullscreenMode) {
    fullscreenMode = false;
    redrawAll();
    return;
  }
  if (textActive) {
    commitTextSession();
    return;
  }
  if (drawingActive) {
    drawingActive = false;
    hasPreview = false;
    curvePhase = 0;
    flushCanvasViewport(true);
    return;
  }
  if (curvePhase > 0) {
    curvePhase = 0;
    flushCanvasViewport(true);
    uiDirtyStatus = true;
    return;
  }
  if (polyCount > 0) {
    polyCount = 0;
    flushCanvasViewport(true);
    uiDirtyStatus = true;
    return;
  }
  if (hasSelection) {
    clearSelection(true);
    return;
  }
  if (zoomMode) {
    zoomMode = false;
    uiDirtyMenuBar = true;
    uiDirtyPatterns = true;
    uiDirtyStatus = true;
    redrawAll();
  }
}

void selectTool(Tool t) {
  if (t >= TOOL_COUNT) return;
  if (textActive) commitTextSession();
  if (polyCount > 0) { polyCount = 0; flushCanvasViewport(true); }
  if (curvePhase > 0) { curvePhase = 0; flushCanvasViewport(true); }
  if (t != TOOL_SELECT && hasSelection) clearSelection(true);

  activeTool = t;
  uiDirtyToolbar = true;
  uiDirtyStatus  = true;
  updateHardwareCursor();
}

void toggleZoomAt(int cx, int cy) {
  zoomMode = !zoomMode;
  if (zoomMode) {
    zoomOriginX = constrain(cx - ZOOM_VIEW_W / 2, 0, CANVAS_W - ZOOM_VIEW_W);
    zoomOriginY = constrain(cy - ZOOM_VIEW_H / 2, 0, CANVAS_H - ZOOM_VIEW_H);
    setStatusToast("4x FatBits Zoom ON (Arrows: Pan, Z: Exit)");
  } else {
    setStatusToast("1x Normal View");
  }
  uiDirtyMenuBar  = true;
  uiDirtyPatterns = true;
  uiDirtyStatus   = true;
  redrawMenuBar();
  redrawPatternBar();
  flushCanvasViewport(true);
}

void initMousePositionerAndSensitivity(bool showToast) {
  const MouseSensPreset & p = MOUSE_SENS_PRESETS[mouseSensIdx];
  // Apply hardware PS/2 rate/resolution/scaling (ignored harmlessly by mice that don't support them)
  Mouse.setSampleRate(100);
  Mouse.setResolution(p.hwRes);
  Mouse.setScaling(p.hwScale);
  // Apply FabGL software movement acceleration
  Mouse.movementAcceleration() = p.accel;
  mouseSubX = 0;
  mouseSubY = 0;
  uiDirtyMenuBar = true;
  if (showToast) {
    mdaSaveMouseSens(mouseSensIdx);
    char msg[48];
    snprintf(msg, sizeof(msg), "Mouse Sensitivity: %d/%d (%s)", mouseSensIdx + 1, NUM_MOUSE_SENS, p.name);
    setStatusToast(msg, 2600);
  }
}

void attachVirtualMousePositioner() {
  Mouse.terminateAbsolutePositioner();
  Mouse.setupAbsolutePositioner(MOUSE_VIRT_MAX, MOUSE_VIRT_MAX, true, nullptr);
  Mouse.status().X = MOUSE_VIRT_CTR;
  Mouse.status().Y = MOUSE_VIRT_CTR;
  lastVirtX = MOUSE_VIRT_CTR;
  lastVirtY = MOUSE_VIRT_CTR;
  mouseSubX = 0;
  mouseSubY = 0;
  initMousePositionerAndSensitivity(false);
}

// ============================================================================
// Menu Actions
// ============================================================================
void executeMenuAction(uint8_t act) {
  if (openMenuIdx >= 0) closeMenuAndRestore();
  if (textActive) commitTextSession();

  switch (act) {
    case ACT_NEW:
      clearSelection(true);
      gCanvas.pushUndo();
      gCanvas.clear(paperVal);
      currentFilePath[0] = '\0';
      flushCanvasViewport(true);
      setStatusToast("New Blank Canvas");
      uiDirtyMenuBar = true;
      break;

    case ACT_OPEN_BMP:
      openFileBrowser(FB_OPEN);
      break;

    case ACT_SAVE_BMP:
      if (currentFilePath[0]) {
        clearSelection(true);
        char err[48] = "";
        if (sdSaveBMP(currentFilePath, gCanvas, err, sizeof(err))) {
          const char * base = strrchr(currentFilePath, '/');
          char toast[48];
          snprintf(toast, sizeof(toast), "Saved: %.35s", base ? (base + 1) : currentFilePath);
          setStatusToast(toast, 2600);
        } else {
          char toast[48];
          snprintf(toast, sizeof(toast), "Save Error: %.32s", err);
          setStatusToast(toast, 3000);
        }
      } else {
        openFileBrowser(FB_SAVE);
      }
      break;

    case ACT_SAVE_AS_BMP:
      openFileBrowser(FB_SAVE);
      break;

    case ACT_BORDER:
      clearSelection(true);
      gCanvas.pushUndo();
      gCanvas.drawFrameBorder(fgInk, max(1, (int)BRUSH_SIZES[activeSize]));
      flushCanvasViewport(true);
      setStatusToast("Border Drawn");
      uiDirtyMenuBar = true;
      break;

    case ACT_EXPORT_PBM:
      clearSelection(true);
      setStatusToast("Exporting PBM to USB Serial...");
      redrawStatusBar();
      gCanvas.exportPBM(Serial, !paperVal);
      setStatusToast("PBM Exported (115200 baud)");
      break;

    case ACT_EXIT_DESKTOP:
      setStatusToast("Returning to MDA Desktop...", 1500);
      redrawStatusBar();
      Canvas.waitCompletion(false);
      mdaReturnToDesktop();
      break;

    case ACT_UNDO:
      clearSelection(false);
      if (gCanvas.undo()) {
        flushCanvasViewport(true);
        setStatusToast("Undo");
        uiDirtyMenuBar = true;
      } else {
        setStatusToast("Nothing to Undo");
      }
      break;

    case ACT_REDO:
      clearSelection(false);
      if (gCanvas.redo()) {
        flushCanvasViewport(true);
        setStatusToast("Redo");
        uiDirtyMenuBar = true;
      } else {
        setStatusToast("Nothing to Redo");
      }
      break;

    case ACT_COPY:
      if (hasSelection) {
        if (selFloating) {
          clipboardBuf.alloc(selBuffer.width, selBuffer.height);
          if (clipboardBuf.data) memcpy(clipboardBuf.data, selBuffer.data, selBuffer.stride * selBuffer.height);
        } else {
          gCanvas.copyRect(selX0, selY0, selX1, selY1, clipboardBuf);
        }
        setStatusToast("Selection Copied");
      } else {
        setStatusToast("Make a Selection First (S)");
      }
      break;

    case ACT_CUT:
      if (hasSelection) {
        if (selFloating) {
          clipboardBuf.alloc(selBuffer.width, selBuffer.height);
          if (clipboardBuf.data) memcpy(clipboardBuf.data, selBuffer.data, selBuffer.stride * selBuffer.height);
          selFloating = false;
          selBuffer.freeBuf();
        } else {
          gCanvas.pushUndo();
          gCanvas.copyRect(selX0, selY0, selX1, selY1, clipboardBuf);
          gCanvas.clearRect(selX0, selY0, selX1, selY1, paperVal);
        }
        hasSelection = false;
        flushCanvasViewport(true);
        setStatusToast("Selection Cut");
        uiDirtyMenuBar = true;
      }
      break;

    case ACT_PASTE:
      if (clipboardBuf.data) {
        clearSelection(true);
        selectTool(TOOL_SELECT);
        gCanvas.pushUndo();
        if (selBuffer.alloc(clipboardBuf.width, clipboardBuf.height)) {
          memcpy(selBuffer.data, clipboardBuf.data, clipboardBuf.stride * clipboardBuf.height);
          selX0 = zoomMode ? zoomOriginX + 4 : 16;
          selY0 = zoomMode ? zoomOriginY + 4 : 16;
          selX1 = selX0 + selBuffer.width - 1;
          selY1 = selY0 + selBuffer.height - 1;
          hasSelection = true;
          selFloating  = true;
          flushCanvasViewport(true);
          setStatusToast("Pasted (Drag to Position)");
          uiDirtyMenuBar = true;
        }
      } else {
        setStatusToast("Clipboard Empty");
      }
      break;

    case ACT_SELECT_ALL:
      clearSelection(true);
      selectTool(TOOL_SELECT);
      hasSelection = true;
      selFloating  = false;
      selX0 = 0; selY0 = 0;
      selX1 = CANVAS_W - 1; selY1 = CANVAS_H - 1;
      flushCanvasViewport(true);
      setStatusToast("Selected Entire Canvas");
      break;

    case ACT_CLEAR_SEL:
      if (hasSelection) {
        if (selFloating) {
          selFloating = false;
          selBuffer.freeBuf();
        } else {
          gCanvas.pushUndo();
          gCanvas.clearRect(selX0, selY0, selX1, selY1, paperVal);
        }
        hasSelection = false;
        flushCanvasViewport(true);
        setStatusToast("Selection Cleared");
        uiDirtyMenuBar = true;
      }
      break;

    case ACT_INVERT:
      if (hasSelection && !selFloating) {
        gCanvas.pushUndo();
        gCanvas.invertRect(selX0, selY0, selX1, selY1);
        setStatusToast("Selection Inverted");
      } else {
        clearSelection(true);
        gCanvas.pushUndo();
        gCanvas.invertAll();
        setStatusToast("Canvas Inverted");
      }
      flushCanvasViewport(true);
      uiDirtyMenuBar = true;
      break;

    case ACT_FLIP_H:
      if (hasSelection && !selFloating) {
        gCanvas.pushUndo();
        gCanvas.flipRectH(selX0, selY0, selX1, selY1);
        setStatusToast("Selection Flipped Horiz");
      } else {
        clearSelection(true);
        gCanvas.pushUndo();
        gCanvas.flipHorizontal();
        setStatusToast("Canvas Flipped Horiz");
      }
      flushCanvasViewport(true);
      uiDirtyMenuBar = true;
      break;

    case ACT_FLIP_V:
      if (hasSelection && !selFloating) {
        gCanvas.pushUndo();
        gCanvas.flipRectV(selX0, selY0, selX1, selY1);
        setStatusToast("Selection Flipped Vert");
      } else {
        clearSelection(true);
        gCanvas.pushUndo();
        gCanvas.flipVertical();
        setStatusToast("Canvas Flipped Vert");
      }
      flushCanvasViewport(true);
      uiDirtyMenuBar = true;
      break;

    case ACT_ROT180:
      clearSelection(true);
      gCanvas.pushUndo();
      gCanvas.rotate180();
      flushCanvasViewport(true);
      setStatusToast("Rotated 180 deg");
      uiDirtyMenuBar = true;
      break;

    case ACT_FILL_PAT:
      clearSelection(true);
      gCanvas.pushUndo();
      gCanvas.fillWithPattern(fgInk, activePat);
      flushCanvasViewport(true);
      setStatusToast("Filled with Active Pattern");
      uiDirtyMenuBar = true;
      break;

    case ACT_TOGGLE_ZOOM:
      toggleZoomAt(CANVAS_W / 2, CANVAS_H / 2);
      break;

    case ACT_TOGGLE_GRID:
      showGrid = !showGrid;
      uiDirtyPatterns = true;
      redrawPatternBar();
      flushCanvasViewport(true);
      setStatusToast(showGrid ? "Grid ON" : "Grid OFF");
      break;

    case ACT_SWAP_PAPER:
      clearSelection(true);
      gCanvas.pushUndo();
      gCanvas.invertAll();
      paperVal = !paperVal;
      fgInk    = !fgInk;
      uiDirtyToolbar  = true;
      uiDirtyPatterns = true;
      uiDirtyMenuBar  = true;
      redrawToolbar();
      redrawPatternBar();
      redrawMenuBar();
      flushCanvasViewport(true);
      setStatusToast(paperVal ? "Light Paper Mode" : "Dark CRT Mode");
      break;

    case ACT_FULLSCREEN:
      clearSelection(true);
      fullscreenMode = true;
      redrawAll();
      break;

    case ACT_MOUSE_SLOWER:
      if (mouseSensIdx > 0) mouseSensIdx--;
      initMousePositionerAndSensitivity(true);
      break;

    case ACT_MOUSE_FASTER:
      if (mouseSensIdx < NUM_MOUSE_SENS - 1) mouseSensIdx++;
      initMousePositionerAndSensitivity(true);
      break;

    case ACT_SHOW_HELP:
      showAboutModal = false;
      showHelpModal  = true;
      drawHelpModal();
      break;

    case ACT_RESET_MOUSE:
      Mouse.terminateAbsolutePositioner();
      Mouse.reset();
      attachVirtualMousePositioner();
      DisplayController.setMouseCursor(currentCursor);
      DisplayController.setMouseCursorPos(curX, curY);
      uiDirtyMenuBar = true;
      setStatusToast(Mouse.isMouseAvailable() ? "PS/2 Mouse Reset: OK" : "PS/2 Mouse Not Detected (Port 1: 26/27)");
      Serial.printf("Mouse Reset -> available=%d\n", Mouse.isMouseAvailable() ? 1 : 0);
      break;

    case ACT_SHOW_ABOUT:
      showHelpModal  = false;
      showAboutModal = true;
      drawAboutModal();
      break;

    default:
      break;
  }
}

// ============================================================================
// UI Hit Testing (Menu Bar, Toolbar, Options, Pattern Strip)
// ============================================================================
bool handleUIClick(int sx, int sy, bool isRight) {
  // File Browser Modal captures all clicks while open
  if (fbMode != FB_CLOSED) {
    if (!isRight) handleFileBrowserClick(sx, sy);
    return true;
  }
  // Modals dismiss on any click
  if (showHelpModal || showAboutModal) {
    showHelpModal = false;
    showAboutModal = false;
    redrawAll();
    return true;
  }
  if (fullscreenMode) {
    fullscreenMode = false;
    redrawAll();
    return true;
  }

  // Open Pull-Down Menu Popup hit test
  if (openMenuIdx >= 0) {
    const MenuDef & m = MENUS[openMenuIdx];
    int px = m.x;
    int py = MENUBAR_H + 1;
    int ph = m.count * MENU_ITEM_H + 4;
    if (ptInRect(sx, sy, px, py, MENU_POP_W, ph)) {
      int idx = (sy - (py + 2)) / MENU_ITEM_H;
      if (idx >= 0 && idx < m.count) {
        executeMenuAction(m.items[idx].actionId);
      } else {
        closeMenuAndRestore();
      }
      return true;
    }
    // Check if clicked another menu header
    if (sy < MENUBAR_H) {
      for (int i = 0; i < NUM_MENUS; ++i) {
        if (ptInRect(sx, sy, MENUS[i].x, 0, MENUS[i].w, MENUBAR_H)) {
          if (i == openMenuIdx) {
            closeMenuAndRestore();
          } else {
            closeMenuAndRestore();
            openMenuIdx = i;
            hoverMenuIdx = -1;
            redrawMenuBar();
            drawOpenMenuPopup();
          }
          return true;
        }
      }
    }
    closeMenuAndRestore();
    return true;
  }

  // Top Menu Bar click
  if (sy < MENUBAR_H) {
    for (int i = 0; i < NUM_MENUS; ++i) {
      if (ptInRect(sx, sy, MENUS[i].x, 0, MENUS[i].w, MENUBAR_H)) {
        openMenuIdx = i;
        hoverMenuIdx = -1;
        redrawMenuBar();
        drawOpenMenuPopup();
        return true;
      }
    }
    return true;
  }

  // Left Toolbar click
  if (ptInRect(sx, sy, TOOLBAR_X, TOOLBAR_Y, TOOLBAR_W, TOOLBAR_H)) {
    // 1. Tool Grid
    for (int i = 0; i < TOOL_COUNT; ++i) {
      int bx = TOOL_GRID_X + (i & 1) * TOOL_PITCH_X;
      int by = TOOL_GRID_Y + (i >> 1) * TOOL_PITCH_Y;
      if (ptInRect(sx, sy, bx, by, TOOL_BTN_W, TOOL_BTN_H)) {
        if ((Tool)i == TOOL_ZOOM && zoomMode) {
          toggleZoomAt(CANVAS_W / 2, CANVAS_H / 2);
        } else {
          selectTool((Tool)i);
        }
        return true;
      }
    }

    // 2. Options Box
    if (ptInRect(sx, sy, OPT_BOX_X, OPT_BOX_Y, OPT_BOX_W, OPT_BOX_H)) {
      if (activeTool == TOOL_TEXT) {
        for (int i = 0; i < 3; ++i) {
          int ry = OPT_BOX_Y + 3 + i * 15;
          if (ptInRect(sx, sy, OPT_BOX_X + 3, ry, OPT_BOX_W - 6, 13)) {
            textFontIdx = i;
            uiDirtyToolbar = true;
            if (textActive) renderLiveTextOntoCanvas();
            return true;
          }
        }
        int by = OPT_BOX_Y + 51;
        if (ptInRect(sx, sy, OPT_BOX_X + 3, by, 29, 23)) {
          textBold = !textBold;
          uiDirtyToolbar = true;
          if (textActive) renderLiveTextOntoCanvas();
          return true;
        }
        if (ptInRect(sx, sy, OPT_BOX_X + 34, by, 29, 23)) {
          textScale2x = !textScale2x;
          uiDirtyToolbar = true;
          if (textActive) renderLiveTextOntoCanvas();
          return true;
        }
      } else if (activeTool == TOOL_SELECT) {
        if (sy < OPT_BOX_Y + 44) pasteTransp = false;
        else                     pasteTransp = true;
        uiDirtyToolbar = true;
        if (selFloating) flushCanvasViewport(true);
        return true;
      } else {
        // Size rows
        for (int i = 0; i < 4; ++i) {
          int ry = OPT_BOX_Y + 3 + i * 10;
          if (ptInRect(sx, sy, OPT_BOX_X + 2, ry, OPT_BOX_W - 4, 10)) {
            activeSize = i;
            uiDirtyToolbar = true;
            uiDirtyStatus  = true;
            return true;
          }
        }
        // Bottom half: Brush shape or Fill mode
        int subY = OPT_BOX_Y + 46;
        if (sy >= subY) {
          if (activeTool == TOOL_BRUSH) {
            int col = (sx < OPT_BOX_X + 34) ? 0 : 1;
            int row = (sy < subY + 15) ? 0 : 1;
            brushShape = (BrushShape)(row * 2 + col);
            uiDirtyToolbar = true;
            return true;
          } else {
            int idx = (sx - (OPT_BOX_X + 3)) / 20;
            fillMode = (FillMode)constrain(idx, 0, FILL_MODE_COUNT - 1);
            uiDirtyToolbar = true;
            return true;
          }
        }
      }
      return true;
    }

    // 3. FG/BG Swatch click -> swap FG/BG
    if (ptInRect(sx, sy, SWATCH_BOX_X, SWATCH_BOX_Y, SWATCH_BOX_W, 33)) {
      fgInk = !fgInk;
      uiDirtyToolbar  = true;
      uiDirtyPatterns = true;
      uiDirtyStatus   = true;
      return true;
    }

    // 4. UNDO / CLR mini buttons
    int qy = SWATCH_BOX_Y + 36;
    if (ptInRect(sx, sy, SWATCH_BOX_X, qy, 32, 19)) {
      executeMenuAction(isRight ? ACT_REDO : ACT_UNDO);
      return true;
    }
    if (ptInRect(sx, sy, SWATCH_BOX_X + 34, qy, 32, 19)) {
      executeMenuAction(ACT_NEW);
      return true;
    }
    return true;
  }

  // Bottom Pattern Bar click
  if (sy >= PATBAR_Y && sy < STATUS_Y) {
    // Active swatch box -> swap ink
    if (ptInRect(sx, sy, 4, PATBAR_Y, 34, PATBAR_H)) {
      fgInk = !fgInk;
      uiDirtyToolbar  = true;
      uiDirtyPatterns = true;
      uiDirtyStatus   = true;
      return true;
    }
    // 16 Pattern tiles
    for (int i = 0; i < NUM_PATTERNS; ++i) {
      int tx = PAT_START_X + i * PAT_PITCH_X;
      if (ptInRect(sx, sy, tx, PATBAR_Y, PAT_TILE_W, PATBAR_H)) {
        activePat = i;
        uiDirtyPatterns = true;
        uiDirtyStatus   = true;
        return true;
      }
    }
    // Right Quick Buttons
    if (ptInRect(sx, sy, 468, PATBAR_Y, 46, PATBAR_H)) {
      fgInk = !fgInk;
      uiDirtyToolbar = true; uiDirtyPatterns = true; uiDirtyStatus = true;
      return true;
    }
    if (ptInRect(sx, sy, 518, PATBAR_Y, 46, PATBAR_H)) {
      executeMenuAction(ACT_TOGGLE_GRID);
      return true;
    }
    if (ptInRect(sx, sy, 568, PATBAR_Y, 48, PATBAR_H)) {
      executeMenuAction(ACT_TOGGLE_ZOOM);
      return true;
    }
    if (ptInRect(sx, sy, 620, PATBAR_Y, 46, PATBAR_H)) {
      executeMenuAction(ACT_INVERT);
      return true;
    }
    if (ptInRect(sx, sy, 670, PATBAR_Y, 46, PATBAR_H)) {
      executeMenuAction(ACT_SHOW_HELP);
      return true;
    }
    return true;
  }

  return false;
}

// ============================================================================
// Canvas Pointer Event Handlers (Press, Move/Drag, Release)
// ============================================================================
void onCanvasPress(int cx, int cy, bool isRight) {
  bool useInk = isRight ? !fgInk : fgInk;

  // Right-click cancels/finishes multi-step tools cleanly
  if (isRight) {
    if (activeTool == TOOL_POLYGON && polyCount >= 3) {
      gCanvas.pushUndo();
      gCanvas.drawPolygon(polyX, polyY, polyCount, BRUSH_SIZES[activeSize], fillMode, polyInk, activePat);
      polyCount = 0;
      flushCanvasViewport(true);
      uiDirtyMenuBar = true;
      uiDirtyStatus  = true;
      return;
    } else if (activeTool == TOOL_POLYGON && polyCount > 0) {
      polyCount = 0;
      flushCanvasViewport(true);
      return;
    }
    if (activeTool == TOOL_CURVE && curvePhase == 2) {
      curvePhase = 0;
      flushCanvasViewport(true);
      uiDirtyStatus = true;
      return;
    }
  }

  // Handle Curve Phase 2 (dragging/clicking bend point)
  if (activeTool == TOOL_CURVE && curvePhase == 2) {
    curveCX = cx;
    curveCY = cy;
    drawingActive = true;
    flushCanvasViewport(true);
    return;
  }

  switch (activeTool) {
    case TOOL_PENCIL:
      gCanvas.pushUndo();
      drawingActive = true;
      dragUseInk    = useInk;
      dragLastX = cx; dragLastY = cy;
      gCanvas.setPixel(cx, cy, useInk);
      flushCanvasViewport();
      uiDirtyMenuBar = true;
      break;

    case TOOL_BRUSH:
      gCanvas.pushUndo();
      drawingActive = true;
      dragUseInk    = useInk;
      dragLastX = cx; dragLastY = cy;
      gCanvas.drawBrushLine(cx, cy, cx, cy, BRUSH_SIZES[activeSize], brushShape, useInk, activePat != 0, activePat);
      flushCanvasViewport();
      uiDirtyMenuBar = true;
      break;

    case TOOL_ERASER: {
      gCanvas.pushUndo();
      drawingActive = true;
      // Left click erases to paper/bg; right click erases to fg
      dragUseInk = isRight ? fgInk : !fgInk;
      dragLastX = cx; dragLastY = cy;
      int erSize = BRUSH_SIZES[activeSize] * 2;
      gCanvas.drawBrushLine(cx, cy, cx, cy, erSize, BRUSH_SQUARE, dragUseInk, false, 0);
      flushCanvasViewport();
      uiDirtyMenuBar = true;
      break;
    }

    case TOOL_FILL:
      gCanvas.pushUndo();
      gCanvas.floodFill(cx, cy, useInk, activePat);
      flushCanvasViewport();
      uiDirtyMenuBar = true;
      break;

    case TOOL_SPRAY:
      gCanvas.pushUndo();
      drawingActive = true;
      dragUseInk    = useInk;
      dragLastX = cx; dragLastY = cy;
      gCanvas.drawSpray(cx, cy, BRUSH_SIZES[activeSize] * 3 + 3, useInk, activePat, sprayRng);
      lastSprayMs = millis();
      flushCanvasViewport();
      uiDirtyMenuBar = true;
      break;

    case TOOL_LINE:
    case TOOL_RECT:
    case TOOL_ROUNDRECT:
    case TOOL_ELLIPSE:
      drawingActive = true;
      dragUseInk    = useInk;
      dragStartX = dragCurX = cx;
      dragStartY = dragCurY = cy;
      hasPreview = true;
      flushCanvasViewport(true);
      break;

    case TOOL_CURVE:
      curvePhase    = 1;
      drawingActive = true;
      dragUseInk    = useInk;
      dragStartX = dragCurX = cx;
      dragStartY = dragCurY = cy;
      hasPreview = true;
      flushCanvasViewport(true);
      break;

    case TOOL_POLYGON:
      if (polyCount == 0) {
        polyInk = useInk;
        polyX[0] = cx;
        polyY[0] = cy;
        polyCount = 1;
        uiDirtyStatus = true;
      } else {
        int tx = cx, ty = cy;
        applyShiftConstraint(TOOL_POLYGON, polyX[polyCount - 1], polyY[polyCount - 1], tx, ty);
        // Close polygon if clicking near starting vertex
        if (polyCount >= 3 && abs(tx - polyX[0]) <= 5 && abs(ty - polyY[0]) <= 5) {
          gCanvas.pushUndo();
          gCanvas.drawPolygon(polyX, polyY, polyCount, BRUSH_SIZES[activeSize], fillMode, polyInk, activePat);
          polyCount = 0;
          uiDirtyMenuBar = true;
          uiDirtyStatus  = true;
        } else if (polyCount < MAX_POLY_PTS) {
          polyX[polyCount] = tx;
          polyY[polyCount] = ty;
          polyCount++;
        }
      }
      flushCanvasViewport(true);
      break;

    case TOOL_SELECT:
      // Check if clicking inside existing selection -> lift & drag!
      if (hasSelection && cx >= selX0 && cx <= selX1 && cy >= selY0 && cy <= selY1) {
        if (!selFloating) {
          gCanvas.pushUndo();
          if (gCanvas.copyRect(selX0, selY0, selX1, selY1, selBuffer)) {
            if (!ctrlDown) {
              // Move (clear source area); Ctrl+Drag stamps a clone!
              gCanvas.clearRect(selX0, selY0, selX1, selY1, paperVal);
            }
            selFloating = true;
            uiDirtyMenuBar = true;
          }
        }
        selDragging = true;
        selDragOffX = cx - selX0;
        selDragOffY = cy - selY0;
        flushCanvasViewport(true);
      } else {
        // Start a brand-new marquee selection
        clearSelection(true);
        drawingActive = true;
        dragStartX = dragCurX = cx;
        dragStartY = dragCurY = cy;
        hasPreview = true;
        flushCanvasViewport(true);
      }
      break;

    case TOOL_TEXT:
      if (textActive) commitTextSession();
      gCanvas.pushUndo();
      textActive  = true;
      textOriginX = textCurX = cx;
      textOriginY = textCurY = cy;
      textLen     = 0;
      textBuf[0]  = '\0';
      textInk     = useInk;
      textCaretOn = true;
      lastBlinkMs = millis();
      uiDirtyMenuBar = true;
      uiDirtyStatus  = true;
      flushCanvasViewport(true);
      break;

    case TOOL_ZOOM:
      toggleZoomAt(cx, cy);
      break;

    default:
      break;
  }
}

void onCanvasDrag(int cx, int cy) {
  if (selDragging && selFloating) {
    int w = selX1 - selX0;
    int h = selY1 - selY0;
    selX0 = cx - selDragOffX;
    selY0 = cy - selDragOffY;
    selX1 = selX0 + w;
    selY1 = selY0 + h;
    flushCanvasViewport(true);
    return;
  }

  if (activeTool == TOOL_CURVE && curvePhase == 2) {
    curveCX = cx;
    curveCY = cy;
    flushCanvasViewport(true);
    return;
  }

  if (activeTool == TOOL_POLYGON && polyCount > 0) {
    flushCanvasViewport(true);
    return;
  }

  if (!drawingActive) return;

  switch (activeTool) {
    case TOOL_PENCIL:
      if (cx != dragLastX || cy != dragLastY) {
        gCanvas.drawLine(dragLastX, dragLastY, cx, cy, 1, dragUseInk);
        dragLastX = cx; dragLastY = cy;
        flushCanvasViewport();
      }
      break;

    case TOOL_BRUSH:
      if (cx != dragLastX || cy != dragLastY) {
        gCanvas.drawBrushLine(dragLastX, dragLastY, cx, cy, BRUSH_SIZES[activeSize], brushShape,
                              dragUseInk, activePat != 0, activePat);
        dragLastX = cx; dragLastY = cy;
        flushCanvasViewport();
      }
      break;

    case TOOL_ERASER:
      if (cx != dragLastX || cy != dragLastY) {
        int erSize = BRUSH_SIZES[activeSize] * 2;
        gCanvas.drawBrushLine(dragLastX, dragLastY, cx, cy, erSize, BRUSH_SQUARE, dragUseInk, false, 0);
        dragLastX = cx; dragLastY = cy;
        flushCanvasViewport();
      }
      break;

    case TOOL_SPRAY:
      dragLastX = cx; dragLastY = cy;
      if (millis() - lastSprayMs >= 20) {
        gCanvas.drawSpray(cx, cy, BRUSH_SIZES[activeSize] * 3 + 3, dragUseInk, activePat, sprayRng);
        lastSprayMs = millis();
        flushCanvasViewport();
      }
      break;

    case TOOL_LINE:
    case TOOL_RECT:
    case TOOL_ROUNDRECT:
    case TOOL_ELLIPSE:
    case TOOL_SELECT:
    case TOOL_CURVE: {
      int tx = cx, ty = cy;
      applyShiftConstraint(activeTool, dragStartX, dragStartY, tx, ty);
      if (tx != dragCurX || ty != dragCurY) {
        dragCurX = tx;
        dragCurY = ty;
        flushCanvasViewport(true);
        uiDirtyStatus = true;
      }
      break;
    }

    default:
      break;
  }
}

void onCanvasRelease(int cx, int cy) {
  if (selDragging) {
    selDragging = false;
    flushCanvasViewport(true);
    return;
  }

  if (activeTool == TOOL_CURVE && curvePhase == 2 && drawingActive) {
    drawingActive = false;
    curveCX = cx;
    curveCY = cy;
    gCanvas.pushUndo();
    gCanvas.drawQuadBezier(curveX0, curveY0, curveCX, curveCY, curveX1, curveY1,
                           BRUSH_SIZES[activeSize], dragUseInk);
    curvePhase = 0;
    flushCanvasViewport(true);
    uiDirtyMenuBar = true;
    uiDirtyStatus  = true;
    return;
  }

  if (!drawingActive) return;
  drawingActive = false;

  int tx = cx, ty = cy;
  applyShiftConstraint(activeTool, dragStartX, dragStartY, tx, ty);
  dragCurX = tx;
  dragCurY = ty;

  switch (activeTool) {
    case TOOL_LINE:
      hasPreview = false;
      gCanvas.pushUndo();
      gCanvas.drawLine(dragStartX, dragStartY, dragCurX, dragCurY,
                       BRUSH_SIZES[activeSize], dragUseInk);
      flushCanvasViewport(true);
      uiDirtyMenuBar = true;
      break;

    case TOOL_CURVE:
      hasPreview = false;
      curveX0 = dragStartX; curveY0 = dragStartY;
      curveX1 = dragCurX;   curveY1 = dragCurY;
      curveCX = (curveX0 + curveX1) / 2;
      curveCY = (curveY0 + curveY1) / 2;
      curvePhase = 2;
      flushCanvasViewport(true);
      uiDirtyStatus = true;
      break;

    case TOOL_RECT:
      hasPreview = false;
      gCanvas.pushUndo();
      gCanvas.drawRect(dragStartX, dragStartY, dragCurX, dragCurY,
                       BRUSH_SIZES[activeSize], fillMode, dragUseInk, activePat);
      flushCanvasViewport(true);
      uiDirtyMenuBar = true;
      break;

    case TOOL_ROUNDRECT:
      hasPreview = false;
      gCanvas.pushUndo();
      gCanvas.drawRoundRect(dragStartX, dragStartY, dragCurX, dragCurY,
                            BRUSH_SIZES[activeSize], fillMode, dragUseInk, activePat);
      flushCanvasViewport(true);
      uiDirtyMenuBar = true;
      break;

    case TOOL_ELLIPSE:
      hasPreview = false;
      gCanvas.pushUndo();
      gCanvas.drawEllipse(dragStartX, dragStartY, dragCurX, dragCurY,
                          BRUSH_SIZES[activeSize], fillMode, dragUseInk, activePat);
      flushCanvasViewport(true);
      uiDirtyMenuBar = true;
      break;

    case TOOL_SELECT:
      hasPreview = false;
      if (abs(dragCurX - dragStartX) >= 2 && abs(dragCurY - dragStartY) >= 2) {
        selX0 = min(dragStartX, dragCurX);
        selY0 = min(dragStartY, dragCurY);
        selX1 = max(dragStartX, dragCurX);
        selY1 = max(dragStartY, dragCurY);
        hasSelection = true;
        selFloating  = false;
      } else {
        hasSelection = false;
      }
      flushCanvasViewport(true);
      uiDirtyStatus = true;
      break;

    default:
      hasPreview = false;
      break;
  }
}

// ============================================================================
// PS/2 Mouse Processing (Virtual Space + Adjustable Gain + Zero Arrow Conflict)
// ============================================================================
void processMouse() {
  bool moved    = false;
  int  wheelSum = 0;
  int  gainQ8   = MOUSE_SENS_PRESETS[mouseSensIdx].gainQ8;

  while (Mouse.availableStatus() > 0) {
    fabgl::MouseStatus st = Mouse.getNextStatus(0);
    int dx = (int)st.X - lastVirtX;
    int dy = (int)st.Y - lastVirtY;
    lastVirtX = st.X;
    lastVirtY = st.Y;

    // Ignore any large jump caused by virtual-space recentering
    if (abs(dx) < 1000 && abs(dy) < 1000 && (dx != 0 || dy != 0)) {
      mouseSubX += dx * gainQ8;
      mouseSubY += dy * gainQ8;
      int stepX = mouseSubX / 256;
      int stepY = mouseSubY / 256;
      mouseSubX -= stepX * 256;
      mouseSubY -= stepY * 256;

      int nx = constrain(curX + stepX, 0, SCREEN_W - 1);
      int ny = constrain(curY + stepY, 0, SCREEN_H - 1);
      if (nx != curX || ny != curY) {
        curX  = nx;
        curY  = ny;
        moved = true;
      }
    }

    wheelSum += st.wheelDelta;
    btnLeft  = st.buttons.left;
    btnRight = st.buttons.right;
    btnMid   = st.buttons.middle;
  }

  // Safely recenter virtual coordinates when queue is empty and near virtual edges
  if (Mouse.availableStatus() == 0 &&
      (lastVirtX < 3000 || lastVirtX > MOUSE_VIRT_MAX - 3000 ||
       lastVirtY < 3000 || lastVirtY > MOUSE_VIRT_MAX - 3000)) {
    Mouse.status().X = MOUSE_VIRT_CTR;
    Mouse.status().Y = MOUSE_VIRT_CTR;
    lastVirtX = MOUSE_VIRT_CTR;
    lastVirtY = MOUSE_VIRT_CTR;
  }

  if (moved) {
    DisplayController.setMouseCursorPos(curX, curY);
    updateHardwareCursor();
    uiDirtyStatus = true;

    // Hover highlight inside open menu
    if (openMenuIdx >= 0) {
      const MenuDef & m = MENUS[openMenuIdx];
      int py = MENUBAR_H + 1;
      int newHover = -1;
      if (ptInRect(curX, curY, m.x, py + 2, MENU_POP_W, m.count * MENU_ITEM_H)) {
        newHover = (curY - (py + 2)) / MENU_ITEM_H;
      }
      if (newHover != hoverMenuIdx) {
        hoverMenuIdx = newHover;
        drawOpenMenuPopup();
      }
    }
  }

  // Scroll wheel -> file list scroll when browser open, else brush size or pattern (with Shift)
  if (wheelSum != 0) {
    int dir = (wheelSum > 0) ? 1 : -1;
    if (fbMode != FB_CLOSED) {
      int maxTop = max(0, gSDEntryCount - FB_VIS_ROWS);
      int nextTop = constrain(fbScrollTop - dir * 2, 0, maxTop);
      if (nextTop != fbScrollTop) {
        fbScrollTop = nextTop;
        drawFileBrowserModal();
      }
    } else if (shiftDown) {
      activePat = (activePat + NUM_PATTERNS + dir) % NUM_PATTERNS;
      uiDirtyPatterns = true;
      uiDirtyStatus   = true;
    } else {
      activeSize = (uint8_t)constrain((int)activeSize + dir, 0, 3);
      uiDirtyToolbar = true;
      uiDirtyStatus  = true;
    }
  }

  // Middle click -> toggle 4x FatBits Zoom centered on cursor
  if (btnMid && !prevMid && fbMode == FB_CLOSED) {
    if (ptInCanvasViewport(curX, curY)) {
      toggleZoomAt(screenToCanvasX(curX), screenToCanvasY(curY));
    } else {
      toggleZoomAt(CANVAS_W / 2, CANVAS_H / 2);
    }
  }

  // Left / Right button press transitions
  bool leftPressed   = (btnLeft && !prevLeft);
  bool rightPressed  = (btnRight && !prevRight);
  bool leftReleased  = (!btnLeft && prevLeft);
  bool rightReleased = (!btnRight && prevRight);

  if (leftPressed || rightPressed) {
    bool isRight = rightPressed && !leftPressed;
    if (!handleUIClick(curX, curY, isRight)) {
      if (ptInCanvasViewport(curX, curY)) {
        onCanvasPress(screenToCanvasX(curX), screenToCanvasY(curY), isRight);
      }
    }
  } else if (moved || (drawingActive && activeTool == TOOL_SPRAY)) {
    if (openMenuIdx < 0 && !showHelpModal && !showAboutModal && fbMode == FB_CLOSED && !fullscreenMode) {
      if (btnLeft || btnRight || selDragging ||
          (activeTool == TOOL_CURVE && curvePhase == 2) ||
          (activeTool == TOOL_POLYGON && polyCount > 0)) {
        onCanvasDrag(screenToCanvasX(curX), screenToCanvasY(curY));
      }
    }
  }

  if (leftReleased || rightReleased) {
    if (drawingActive || selDragging) {
      onCanvasRelease(screenToCanvasX(curX), screenToCanvasY(curY));
    }
  }

  prevLeft  = btnLeft;
  prevRight = btnRight;
  prevMid   = btnMid;
}

// Helper to open a specific top menu via keyboard (Alt / F10 / Alt+Letter)
void openMenuByIndex(int idx) {
  idx = (idx + NUM_MENUS) % NUM_MENUS;
  if (textActive) commitTextSession();
  if (openMenuIdx >= 0) {
    // Restore previous menu background cleanly before drawing new popup
    openMenuIdx = -1;
    redrawToolbar();
    flushCanvasViewport(true);
  }
  menuBarFocused = true;
  menuFocusIdx   = idx;
  openMenuIdx    = idx;
  hoverMenuIdx   = 0;
  redrawMenuBar();
  drawOpenMenuPopup();
  uiDirtyStatus  = true;
}

int menuIndexFromLetter(char ch) {
  switch (toupper(ch)) {
    case 'F': return 0; // File
    case 'E': return 1; // Edit
    case 'I': return 2; // Image
    case 'V': return 3; // View
    case 'H': return 4; // Help
    default:  return -1;
  }
}

// ============================================================================
// PS/2 Keyboard Processing (Full Shortcut, Alt-Menu & Text Integration)
// ============================================================================
void processKeyboard() {
  while (Keyboard.virtualKeyAvailable()) {
    fabgl::VirtualKeyItem item;
    if (!Keyboard.getNextVirtualKey(&item, 0)) break;

    shiftDown = item.SHIFT;
    ctrlDown  = item.CTRL;

    bool isAltKey = (item.vk == fabgl::VK_LALT || item.vk == fabgl::VK_RALT);

    if (!item.down) {
      // Tapping Alt (press + release without a combo key) toggles top Menu Bar focus!
      if (isAltKey) {
        if (altHeld && !altUsedAsCombo && !fullscreenMode && !showHelpModal && !showAboutModal) {
          if (openMenuIdx >= 0 || menuBarFocused) {
            closeMenuAndRestore();
          } else {
            if (textActive) commitTextSession();
            menuBarFocused = true;
            menuFocusIdx   = 0;
            redrawMenuBar();
            uiDirtyStatus  = true;
          }
        }
        altHeld        = false;
        altUsedAsCombo = false;
        continue;
      }

      // Spacebar release ends keyboard-simulated drawing
      if (item.vk == fabgl::VK_SPACE && !textActive && !menuBarFocused && openMenuIdx < 0 && (drawingActive || selDragging)) {
        onCanvasRelease(screenToCanvasX(curX), screenToCanvasY(curY));
      }
      continue;
    }

    // Track Alt key press
    if (isAltKey) {
      if (!altHeld) {
        altHeld        = true;
        altUsedAsCombo = false;
      }
      continue;
    }

    // File Browser Modal captures all keyboard input while open
    if (fbMode != FB_CLOSED) {
      handleFileBrowserKey(item);
      continue;
    }

    // Dismiss fullscreen or modals on any key
    if (fullscreenMode) {
      fullscreenMode = false;
      redrawAll();
      continue;
    }
    if (showHelpModal || showAboutModal) {
      if (item.vk == fabgl::VK_ESCAPE || item.vk == fabgl::VK_F1 || item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_SPACE) {
        showHelpModal = false;
        showAboutModal = false;
        redrawAll();
      }
      continue;
    }

    // Alt + F4 returns straight to MDA Desktop; Alt + F / E / I / V / H opens top menus!
    if (item.LALT || item.RALT || altHeld) {
      altUsedAsCombo = true;
      if (item.vk == fabgl::VK_F4) {
        executeMenuAction(ACT_EXIT_DESKTOP);
        continue;
      }
      int mIdx = menuIndexFromLetter((char)item.ASCII);
      if (mIdx >= 0) {
        openMenuByIndex(mIdx);
        continue;
      }
    }

    // F10 activates/deactivates the top Menu Bar (classic Windows/DOS shortcut)
    if (item.vk == fabgl::VK_F10) {
      if (openMenuIdx >= 0 || menuBarFocused) {
        closeMenuAndRestore();
      } else {
        if (textActive) commitTextSession();
        menuBarFocused = true;
        menuFocusIdx   = 0;
        redrawMenuBar();
        uiDirtyStatus  = true;
      }
      continue;
    }

    // F5 resets PS/2 Mouse live
    if (item.vk == fabgl::VK_F5) {
      executeMenuAction(ACT_RESET_MOUSE);
      continue;
    }

    // F1 toggles Help overlay
    if (item.vk == fabgl::VK_F1) {
      closeMenuAndRestore();
      executeMenuAction(ACT_SHOW_HELP);
      continue;
    }

    // Escape cancels active operation or closes menu
    if (item.vk == fabgl::VK_ESCAPE) {
      cancelActiveOperation();
      continue;
    }

    // ---- Mouseless Keyboard Navigation when Menu Bar is Focused or Open ----
    if (menuBarFocused || openMenuIdx >= 0) {
      if (item.vk == fabgl::VK_LEFT) {
        int nextIdx = ((openMenuIdx >= 0 ? openMenuIdx : menuFocusIdx) + NUM_MENUS - 1) % NUM_MENUS;
        if (openMenuIdx >= 0) {
          openMenuByIndex(nextIdx);
        } else {
          menuFocusIdx = nextIdx;
          redrawMenuBar();
          uiDirtyStatus = true;
        }
        continue;
      }
      if (item.vk == fabgl::VK_RIGHT) {
        int nextIdx = ((openMenuIdx >= 0 ? openMenuIdx : menuFocusIdx) + 1) % NUM_MENUS;
        if (openMenuIdx >= 0) {
          openMenuByIndex(nextIdx);
        } else {
          menuFocusIdx = nextIdx;
          redrawMenuBar();
          uiDirtyStatus = true;
        }
        continue;
      }
      if (item.vk == fabgl::VK_DOWN) {
        if (openMenuIdx < 0) {
          openMenuByIndex(menuFocusIdx);
        } else {
          const MenuDef & m = MENUS[openMenuIdx];
          hoverMenuIdx = (hoverMenuIdx + 1) % m.count;
          drawOpenMenuPopup();
        }
        continue;
      }
      if (item.vk == fabgl::VK_UP) {
        if (openMenuIdx >= 0) {
          const MenuDef & m = MENUS[openMenuIdx];
          hoverMenuIdx = (hoverMenuIdx <= 0) ? (m.count - 1) : (hoverMenuIdx - 1);
          drawOpenMenuPopup();
        }
        continue;
      }
      if (item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_KP_ENTER || item.vk == fabgl::VK_SPACE) {
        if (openMenuIdx < 0) {
          openMenuByIndex(menuFocusIdx);
        } else {
          const MenuDef & m = MENUS[openMenuIdx];
          int chosen = (hoverMenuIdx >= 0 && hoverMenuIdx < m.count) ? hoverMenuIdx : 0;
          executeMenuAction(m.items[chosen].actionId);
        }
        continue;
      }
      // Mnemonic letter jumps directly to a menu while bar is focused
      int mIdx = menuIndexFromLetter((char)item.ASCII);
      if (mIdx >= 0 && openMenuIdx < 0) {
        openMenuByIndex(mIdx);
        continue;
      }
      // Any other key exits menu focus
      closeMenuAndRestore();
      continue;
    }

    // ---- Active Text Tool Typing Mode ----
    if (textActive) {
      if (item.vk == fabgl::VK_BACKSPACE) {
        if (textLen > 0) {
          textLen--;
          textBuf[textLen] = '\0';
          renderLiveTextOntoCanvas();
        }
        continue;
      }
      if (item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_KP_ENTER) {
        if (textLen < MAX_TEXT_LEN) {
          textBuf[textLen++] = '\n';
          textBuf[textLen] = '\0';
          renderLiveTextOntoCanvas();
        }
        continue;
      }
      if (!item.CTRL && item.ASCII >= 32 && item.ASCII <= 126) {
        if (textLen < MAX_TEXT_LEN) {
          textBuf[textLen++] = (char)item.ASCII;
          textBuf[textLen] = '\0';
          renderLiveTextOntoCanvas();
        }
        continue;
      }
    }

    // ---- Ctrl + Key Shortcuts ----
    if (item.CTRL) {
      char c = tolower(item.ASCII);
      if (item.vk == fabgl::VK_z || item.vk == fabgl::VK_Z || c == 'z' || item.ASCII == 26) {
        executeMenuAction(ACT_UNDO); continue;
      }
      if (item.vk == fabgl::VK_y || item.vk == fabgl::VK_Y || c == 'y' || item.ASCII == 25) {
        executeMenuAction(ACT_REDO); continue;
      }
      if (item.vk == fabgl::VK_c || item.vk == fabgl::VK_C || c == 'c' || item.ASCII == 3) {
        executeMenuAction(ACT_COPY); continue;
      }
      if (item.vk == fabgl::VK_x || item.vk == fabgl::VK_X || c == 'x' || item.ASCII == 24) {
        executeMenuAction(ACT_CUT); continue;
      }
      if (item.vk == fabgl::VK_v || item.vk == fabgl::VK_V || c == 'v' || item.ASCII == 22) {
        executeMenuAction(ACT_PASTE); continue;
      }
      if (item.vk == fabgl::VK_a || item.vk == fabgl::VK_A || c == 'a' || item.ASCII == 1) {
        executeMenuAction(ACT_SELECT_ALL); continue;
      }
      if (item.vk == fabgl::VK_n || item.vk == fabgl::VK_N || c == 'n' || item.ASCII == 14) {
        executeMenuAction(ACT_NEW); continue;
      }
      if (item.vk == fabgl::VK_o || item.vk == fabgl::VK_O || c == 'o' || item.ASCII == 15) {
        executeMenuAction(ACT_OPEN_BMP); continue;
      }
      if (item.vk == fabgl::VK_s || item.vk == fabgl::VK_S || c == 's' || item.ASCII == 19) {
        executeMenuAction(ACT_SAVE_BMP); continue;
      }
      if (item.vk == fabgl::VK_i || item.vk == fabgl::VK_I || c == 'i' || item.ASCII == 9) {
        executeMenuAction(ACT_INVERT); continue;
      }
      continue;
    }

    // ---- Navigation & Pixel-Precision Arrow Keys ----
    int step = item.SHIFT ? 8 : 1;
    bool arrow = false;
    int adx = 0, ady = 0;
    if (item.vk == fabgl::VK_LEFT)  { adx = -step; arrow = true; }
    if (item.vk == fabgl::VK_RIGHT) { adx =  step; arrow = true; }
    if (item.vk == fabgl::VK_UP)    { ady = -step; arrow = true; }
    if (item.vk == fabgl::VK_DOWN)  { ady =  step; arrow = true; }

    if (arrow) {
      if (zoomMode && !drawingActive && !selDragging) {
        // Pan 4x FatBits viewport!
        int panStep = item.SHIFT ? 24 : 8;
        int nx = constrain(zoomOriginX + (adx > 0 ? panStep : (adx < 0 ? -panStep : 0)), 0, CANVAS_W - ZOOM_VIEW_W);
        int ny = constrain(zoomOriginY + (ady > 0 ? panStep : (ady < 0 ? -panStep : 0)), 0, CANVAS_H - ZOOM_VIEW_H);
        if (nx != zoomOriginX || ny != zoomOriginY) {
          zoomOriginX = nx;
          zoomOriginY = ny;
          flushCanvasViewport(true);
          uiDirtyStatus = true;
        }
      } else {
        // Move cursor precisely by 1px (or 8px with Shift)
        curX = constrain(curX + adx, 0, SCREEN_W - 1);
        curY = constrain(curY + ady, 0, SCREEN_H - 1);
        DisplayController.setMouseCursorPos(curX, curY);
        updateHardwareCursor();
        uiDirtyStatus = true;
        if (drawingActive || selDragging ||
            (activeTool == TOOL_CURVE && curvePhase == 2) ||
            (activeTool == TOOL_POLYGON && polyCount > 0)) {
          onCanvasDrag(screenToCanvasX(curX), screenToCanvasY(curY));
        }
      }
      continue;
    }

    // ---- Spacebar acts as Left-Click / Draw for Keyboard-Only Precision ----
    if (item.vk == fabgl::VK_SPACE) {
      if (!handleUIClick(curX, curY, false)) {
        if (ptInCanvasViewport(curX, curY)) {
          onCanvasPress(screenToCanvasX(curX), screenToCanvasY(curY), false);
        }
      }
      continue;
    }

    // ---- Enter closes Polygon / commits Curve or Selection ----
    if (item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_KP_ENTER) {
      if (activeTool == TOOL_POLYGON && polyCount >= 3) {
        gCanvas.pushUndo();
        gCanvas.drawPolygon(polyX, polyY, polyCount, BRUSH_SIZES[activeSize], fillMode, polyInk, activePat);
        polyCount = 0;
        flushCanvasViewport(true);
        uiDirtyMenuBar = true;
        uiDirtyStatus  = true;
      } else if (activeTool == TOOL_CURVE && curvePhase == 2) {
        gCanvas.pushUndo();
        gCanvas.drawQuadBezier(curveX0, curveY0, curveCX, curveCY, curveX1, curveY1,
                               BRUSH_SIZES[activeSize], dragUseInk);
        curvePhase = 0;
        flushCanvasViewport(true);
        uiDirtyMenuBar = true;
        uiDirtyStatus  = true;
      } else if (hasSelection) {
        clearSelection(true);
      }
      continue;
    }

    // ---- Delete / Backspace ----
    if (item.vk == fabgl::VK_DELETE) {
      executeMenuAction(ACT_CLEAR_SEL);
      continue;
    }
    if (item.vk == fabgl::VK_BACKSPACE) {
      if (activeTool == TOOL_POLYGON && polyCount > 0) {
        polyCount--;
        flushCanvasViewport(true);
      } else if (hasSelection) {
        executeMenuAction(ACT_CLEAR_SEL);
      }
      continue;
    }

    // ---- Tab -> Fullscreen Preview ----
    if (item.vk == fabgl::VK_TAB) {
      executeMenuAction(ACT_FULLSCREEN);
      continue;
    }

    // ---- Single-Character Hotkeys ----
    char ch = (char)toupper(item.ASCII);
    switch (ch) {
      // Tools
      case 'S': selectTool(TOOL_SELECT);    break;
      case 'P': selectTool(TOOL_PENCIL);    break;
      case 'B': selectTool(TOOL_BRUSH);     break;
      case 'E': selectTool(TOOL_ERASER);    break;
      case 'F': selectTool(TOOL_FILL);      break;
      case 'A': selectTool(TOOL_SPRAY);     break;
      case 'L': selectTool(TOOL_LINE);      break;
      case 'C': selectTool(TOOL_CURVE);     break;
      case 'R': selectTool(TOOL_RECT);      break;
      case 'U': selectTool(TOOL_ROUNDRECT); break;
      case 'O': selectTool(TOOL_ELLIPSE);   break;
      case 'Y': selectTool(TOOL_POLYGON);   break;
      case 'T': selectTool(TOOL_TEXT);      break;
      case 'Z':
        if (ptInCanvasViewport(curX, curY)) toggleZoomAt(screenToCanvasX(curX), screenToCanvasY(curY));
        else toggleZoomAt(CANVAS_W / 2, CANVAS_H / 2);
        break;

      // Size / Pattern / Mode / View
      case '[':
        if (activeSize > 0) { activeSize--; uiDirtyToolbar = true; uiDirtyStatus = true; }
        break;
      case ']':
        if (activeSize < 3) { activeSize++; uiDirtyToolbar = true; uiDirtyStatus = true; }
        break;
      case ',':
      case '<':
        activePat = (activePat + NUM_PATTERNS - 1) % NUM_PATTERNS;
        uiDirtyPatterns = true; uiDirtyStatus = true;
        break;
      case '.':
      case '>':
        activePat = (activePat + 1) % NUM_PATTERNS;
        uiDirtyPatterns = true; uiDirtyStatus = true;
        break;
      case 'X':
        fgInk = !fgInk;
        uiDirtyToolbar = true; uiDirtyPatterns = true; uiDirtyStatus = true;
        break;
      case 'M':
        fillMode = (FillMode)((fillMode + 1) % FILL_MODE_COUNT);
        uiDirtyToolbar = true;
        break;
      case 'G':
        executeMenuAction(ACT_TOGGLE_GRID);
        break;
      case 'I':
        executeMenuAction(ACT_SWAP_PAPER);
        break;
      case '-':
      case '_':
        executeMenuAction(ACT_MOUSE_SLOWER);
        break;
      case '=':
      case '+':
        executeMenuAction(ACT_MOUSE_FASTER);
        break;

      // Number keys 1-8 pick patterns 0-7 (Shift+1..8 pick patterns 8-15)
      case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8':
        activePat = (uint8_t)(ch - '1');
        uiDirtyPatterns = true; uiDirtyStatus = true;
        break;
      case '!': activePat = 8;  uiDirtyPatterns = true; uiDirtyStatus = true; break;
      case '@': activePat = 9;  uiDirtyPatterns = true; uiDirtyStatus = true; break;
      case '#': activePat = 10; uiDirtyPatterns = true; uiDirtyStatus = true; break;
      case '$': activePat = 11; uiDirtyPatterns = true; uiDirtyStatus = true; break;
      case '%': activePat = 12; uiDirtyPatterns = true; uiDirtyStatus = true; break;
      case '^': activePat = 13; uiDirtyPatterns = true; uiDirtyStatus = true; break;
      case '&': activePat = 14; uiDirtyPatterns = true; uiDirtyStatus = true; break;
      case '*': activePat = 15; uiDirtyPatterns = true; uiDirtyStatus = true; break;

      default:
        break;
    }
  }
}

// ============================================================================
// Arduino setup() & loop()
// ============================================================================
void setup() {
  Serial.begin(115200);

  // If launched from MDA Desktop (running in ota_1), arm ota_0 on next reset
  // so hardware RST or power cycle returns safely to MDA Desktop!
  mdaArmDesktopOnReset();
  mouseSensIdx = mdaLoadMouseSens(2, NUM_MOUSE_SENS - 1);

  delay(500); // Allow PS/2 keyboard & mouse to finish power-on self-test (BAT)

  // Explicit LilyGO VGA32 PS/2 GPIO pinout:
  //   Port 0 (Keyboard): CLK = GPIO 33, DATA = GPIO 32
  //   Port 1 (Mouse):    CLK = GPIO 26, DATA = GPIO 27
  PS2Controller.begin(GPIO_NUM_33, GPIO_NUM_32, GPIO_NUM_26, GPIO_NUM_27);
  Keyboard.begin(true, true, 0);   // generateVirtualKeys=true, createVKQueue=true, Port 0
  Keyboard.setLayout(&fabgl::USLayout);
  Mouse.begin(1);                  // Port 1

  // Retry mouse reset once if mouse was slow waking up
  if (!Mouse.isMouseAvailable()) {
    delay(250);
    Mouse.reset();
  }

  Serial.println("\n=== MDA Paint Startup ===");
  Serial.printf("PS/2 Keyboard (33/32): %s\n", Keyboard.isKeyboardAvailable() ? "OK" : "NOT DETECTED");
  Serial.printf("PS/2 Mouse    (26/27): %s\n", Mouse.isMouseAvailable() ? "OK" : "NOT DETECTED");

  // Start VGA in monochrome mode with alternate ESP32 pin map:
  // DisplayController.begin(GPIO_NUM_22, GPIO_NUM_22, GPIO_NUM_22, GPIO_NUM_23, GPIO_NUM_15);

  // Start TTL Video in monochrome mode with VGA32 pin map (from NetworkTerminal_MDA):
  DisplayController.begin(GPIO_NUM_21, GPIO_NUM_21, GPIO_NUM_21, GPIO_NUM_23, GPIO_NUM_15);

  // 720x350 @ 50Hz MDA Hercules TTL Modeline (from NetworkTerminal_MDA)
  DisplayController.setResolution("\"MDA_50HZ_LOCK\" 16.000 720 736 848 880 350 352 356 364 +HSync -VSync");

  // Enforce strict 1-bit monochrome palette (0 = Black, 1 = Phosphor On, no grayscale)
  DisplayController.setPaletteItem(0, fabgl::RGB888(0, 0, 0));
  DisplayController.setPaletteItem(1, fabgl::RGB888(255, 255, 255));

  // Mount integrated LilyGO VGA32 SD Card (CS=13, MOSI=12, MISO=2, CLK=14)
  // Done before gCanvas.init() so FATFS heap usage is accounted for in Undo allocation.
  bool sdOk = sdMountCard(false);
  Serial.printf("SD Card (13/12/2/14) : %s\n", sdOk ? "MOUNTED OK" : "NOT DETECTED");

  // Allocate 1-bit offscreen canvas & dynamic undo stack
  if (!gCanvas.init()) {
    Canvas.selectFont(&fabgl::FONT_8x14);
    Canvas.setBrushColor(fabgl::Color::Black);
    Canvas.setPenColor(fabgl::Color::White);
    Canvas.clear();
    Canvas.drawText(20, 20, "MDA PAINT ERROR: Out of memory allocating 1-bit canvas!");
    while (true) delay(1000);
  }

  Serial.printf("Canvas ready: %dx%d | Undo slots: %d | Free heap: %u bytes\n",
                CANVAS_W, CANVAS_H, gCanvas.undoCapacity, (unsigned)ESP.getFreeHeap());

  // Persistent Bitmap wrapper pointing directly at gCanvas.buf (MSB-first 1-bit Mask)
  gCanvasBmp = new fabgl::Bitmap(CANVAS_W, CANVAS_H, gCanvas.buf,
                                 fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255));

  // Start with CRT-friendly black paper and phosphor ink
  gCanvas.clear(paperVal);

  // Attach virtual mouse positioner with adjustable sensitivity (defaults to Level 3: 2.0x Fast)
  attachVirtualMousePositioner();

  // Enable hardware mouse cursor at canvas center
  DisplayController.setMouseCursor(currentCursor);
  DisplayController.setMouseCursorPos(curX, curY);

  setStatusToast("MDA Paint Ready! Alt:Menus | -/+:Mouse Speed | F1:Help", 4500);
  redrawAll();
  Canvas.waitCompletion(false);
}

void loop() {
  processMouse();
  processKeyboard();

  // Continuous spray when holding mouse button without moving
  if (drawingActive && activeTool == TOOL_SPRAY && (btnLeft || btnRight)) {
    if (millis() - lastSprayMs >= 24) {
      gCanvas.drawSpray(dragLastX, dragLastY, BRUSH_SIZES[activeSize] * 3 + 3, dragUseInk, activePat, sprayRng);
      lastSprayMs = millis();
      flushCanvasViewport();
    }
  }

  // Blinking text caret during Text tool entry
  if (textActive && (millis() - lastBlinkMs >= 450)) {
    lastBlinkMs = millis();
    textCaretOn = !textCaretOn;
    flushCanvasViewport(true);
  }

  // Heartbeat dot in top-right menu bar (confirms main loop is running)
  if (millis() - lastHeartbeatMs >= 500) {
    lastHeartbeatMs = millis();
    heartbeatOn = !heartbeatOn;
    drawHeartbeatDot();
  }

  // Auto-clear expired status toast
  if (statusMsg[0] && millis() >= statusMsgUntil) {
    statusMsg[0] = '\0';
    uiDirtyStatus = true;
  }

  // Incremental UI chrome updates (skipped while in fullscreen preview or modals)
  if (!fullscreenMode && !showHelpModal && !showAboutModal && fbMode == FB_CLOSED) {
    if (uiDirtyMenuBar)  { redrawMenuBar(); if (openMenuIdx >= 0) drawOpenMenuPopup(); }
    if (uiDirtyToolbar)  redrawToolbar();
    if (uiDirtyPatterns) redrawPatternBar();
    if (uiDirtyStatus)   redrawStatusBar();
  }

  delay(4);
}
