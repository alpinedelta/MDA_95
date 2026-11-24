// ============================================================================
// MDA Desktop - Minimal Windows 95-Style Desktop Launcher for Hercules / MDA
// Built for ESP32 / LilyGO VGA32 using FabGL VGA2Controller + PS/2 + MicroSD
// Display: 720 x 350 @ 50Hz TTL Monochrome (Strict 1-Bit, No Grayscale)
//
// Partition Scheme Requirement (Arduino IDE -> Tools -> Partition Scheme):
//   "Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)" (recommended)
//   or "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"
//
// MDA Desktop lives permanently in ota_0. Clicking any .BIN app from /APPS/
// on the SD card flashes it into ota_1 (or boots instantly if already cached)
// and restarts into that app. Exiting the app (or pressing RST) returns here!
// ============================================================================

#include <Arduino.h>
#include <fabgl.h>
#include "mda_launcher_sdk.h"
#include "mda_desktop_patterns.h"
#include "mda_desktop_icons.h"
#include "mda_sd_launcher.h"
#include "mda_notepad.h"

// ============================================================================
// Hardware Controllers (LilyGO VGA32 MDA TTL + Explicit PS/2 Keyboard & Mouse)
// ============================================================================
fabgl::VGA2Controller DisplayController;
fabgl::PS2Controller  PS2Controller;
fabgl::Keyboard       Keyboard;
fabgl::Mouse          Mouse;
fabgl::Canvas         Canvas(&DisplayController);

// ============================================================================
// Screen & Layout Geometry (720 x 350 Hercules MDA)
// ============================================================================
static constexpr int SCREEN_W  = 720;
static constexpr int SCREEN_H  = 350;
static constexpr int TASKBAR_H = 24;
static constexpr int TASKBAR_Y = SCREEN_H - TASKBAR_H; // 326
static constexpr int DESK_H    = TASKBAR_Y;            // 326 (y = 0..325)

// Start button geometry
static constexpr int START_X   = 3;
static constexpr int START_Y   = TASKBAR_Y + 3;
static constexpr int START_W   = 66;
static constexpr int START_H   = 18;

// Desktop Icon Grid (columns left-to-right, 5 icons per column)
static constexpr int ICON_GRID_X0   = 14;
static constexpr int ICON_GRID_Y0   = 12;
static constexpr int ICON_CELL_W    = 88;
static constexpr int ICON_CELL_H    = 60;
static constexpr int ICON_ROWS_COL  = 5;
static constexpr int BUILTIN_ICONS  = 4; // My Computer, Wallpaper, SD Apps, Notepad

NotepadApp gNotepad;

// ============================================================================
// Adjustable Mouse Sensitivity (Shared with MDA Paint via NVS)
// ============================================================================
struct MouseSensPreset {
  const char * name;
  int          gainQ8;
  int          accel;
  int          hwRes;
  int          hwScale;
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

int mouseSensIdx = 2;
static constexpr int MOUSE_VIRT_MAX = 30000;
static constexpr int MOUSE_VIRT_CTR = 15000;
int lastVirtX = MOUSE_VIRT_CTR;
int lastVirtY = MOUSE_VIRT_CTR;
int mouseSubX = 0;
int mouseSubY = 0;

// ============================================================================
// Desktop UI State
// ============================================================================
int  curX = SCREEN_W / 2;
int  curY = DESK_H / 2;
bool btnLeft = false, btnRight = false, btnMid = false;
bool prevLeft = false, prevRight = false, prevMid = false;
bool shiftDown = false, ctrlDown = false;
bool altHeld = false, altUsedAsCombo = false;

int wallpaperIdx      = 0;
int selectedIconIdx   = -1; // -1 = none, 0..2 = built-in, 3.. = SD apps
int lastIconClickIdx  = -1;
uint32_t lastIconClickMs = 0;

// Start Menu & Programs Cascading Submenu
bool startMenuOpen     = false;
int  startHoverIdx     = 0;
bool progSubmenuOpen   = false;
int  progHoverIdx      = 0;

static constexpr int START_MENU_X    = 2;
static constexpr int START_MENU_W    = 176;
static constexpr int START_SIDE_W    = 18;
static constexpr int START_ITEM_H    = 18;
static constexpr int START_ITEM_CNT  = 9;

static const char * START_ITEMS[START_ITEM_CNT] = {
  "Programs       >",
  "Notepad",
  "SD Apps Folder",
  "Wallpaper...",
  "Mouse Speed +",
  "My Computer",
  "Remount SD",
  "Help (F1)",
  "Restart ESP32"
};

// Right-Click Desktop Context Menu
bool ctxMenuOpen  = false;
int  ctxMenuX     = 0;
int  ctxMenuY     = 0;
int  ctxHoverIdx  = -1;
static constexpr int CTX_MENU_W   = 136;
static constexpr int CTX_ITEM_H   = 16;
static constexpr int CTX_ITEM_CNT = 6;
static const char * CTX_ITEMS[CTX_ITEM_CNT] = {
  "Refresh SD Apps",
  "Wallpaper...",
  "Mouse Faster (+)",
  "Mouse Slower (-)",
  "My Computer",
  "Restart ESP32"
};

// Built-In Windows (ActiveWindow enum defined in mda_desktop_icons.h)
ActiveWindow activeWin = WIN_NONE;
int  winX = 160, winY = 44, winW = 400, winH = 228;
bool winDragging = false;
int  winDragOffX = 0, winDragOffY = 0;

// Wallpaper Dialog State
int wpPreviewIdx   = 0;
int wpScrollTop    = 0;
static constexpr int WP_VIS_ROWS = 9;

// SD Apps Folder Window State
int sdWinSelIdx    = 0;
int sdWinScrollTop = 0;
int sdWinLastClick = -1;
uint32_t sdWinLastClickMs = 0;
static constexpr int SDWIN_VIS_ROWS = 8;

// Launch Progress / Error Dialog State
char launchTitle[48]  = "";
char launchStage[56]  = "";
int  launchPct        = 0;
char errorMsg[72]     = "";

// Taskbar toast & clock
char     toastMsg[52]  = "";
uint32_t toastUntilMs  = 0;
uint32_t lastClockSec  = 999999;

// ============================================================================
// Forward Declarations
// ============================================================================
void redrawDesktopAll();
void redrawTaskbar();
void drawActiveWindow();
void drawStartMenu();
void drawContextMenu();
void openWindow(ActiveWindow w);
void closeActiveWindow();
void triggerLaunchApp(int appIdx);

// ============================================================================
// Helpers
// ============================================================================
inline bool ptInRect(int x, int y, int rx, int ry, int rw, int rh) {
  return (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
}

int totalDesktopIcons() {
  return BUILTIN_ICONS + gSDAppCount;
}

void getDesktopIconRect(int idx, int & x, int & y, int & w, int & h) {
  int col = idx / ICON_ROWS_COL;
  int row = idx % ICON_ROWS_COL;
  x = ICON_GRID_X0 + col * ICON_CELL_W;
  y = ICON_GRID_Y0 + row * ICON_CELL_H;
  w = ICON_CELL_W - 6;
  h = ICON_CELL_H - 4;
}

void setToast(const char * msg, uint32_t durationMs = 2600) {
  strncpy(toastMsg, msg, sizeof(toastMsg) - 1);
  toastMsg[sizeof(toastMsg) - 1] = '\0';
  toastUntilMs = millis() + durationMs;
  redrawTaskbar();
}

void applyMouseSensitivity(bool saveAndToast) {
  const MouseSensPreset & p = MOUSE_SENS_PRESETS[mouseSensIdx];
  Mouse.setSampleRate(100);
  Mouse.setResolution(p.hwRes);
  Mouse.setScaling(p.hwScale);
  Mouse.movementAcceleration() = p.accel;
  mouseSubX = 0;
  mouseSubY = 0;
  if (saveAndToast) {
    mdaSaveMouseSens(mouseSensIdx);
    char msg[52];
    snprintf(msg, sizeof(msg), "Mouse Speed: %d/%d (%s)", mouseSensIdx + 1, NUM_MOUSE_SENS, p.name);
    setToast(msg, 2400);
  }
}

void attachVirtualMouse() {
  Mouse.terminateAbsolutePositioner();
  Mouse.setupAbsolutePositioner(MOUSE_VIRT_MAX, MOUSE_VIRT_MAX, true, nullptr);
  Mouse.status().X = MOUSE_VIRT_CTR;
  Mouse.status().Y = MOUSE_VIRT_CTR;
  lastVirtX = MOUSE_VIRT_CTR;
  lastVirtY = MOUSE_VIRT_CTR;
  mouseSubX = 0;
  mouseSubY = 0;
  applyMouseSensitivity(false);
}

// ============================================================================
// Classic Win95 1-Bit Monochrome UI Primitives
// ============================================================================
void drawWin95Button(int x, int y, int w, int h, const char * label, bool inverted = false) {
  Canvas.setBrushColor(inverted ? fabgl::Color::White : fabgl::Color::Black);
  Canvas.fillRectangle(x, y, x + w - 1, y + h - 1);
  Canvas.setPenColor(inverted ? fabgl::Color::Black : fabgl::Color::White);
  Canvas.drawRectangle(x, y, x + w - 1, y + h - 1);
  // Subtle 1-bit Win95 raised drop-edge on bottom/right
  if (!inverted) {
    Canvas.drawLine(x + 1, y + h, x + w, y + h);
    Canvas.drawLine(x + w, y + 1, x + w, y + h);
  }
  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
  int tw = (int)strlen(label) * 6;
  Canvas.drawText(x + (w - tw) / 2, y + (h - 8) / 2 + 1, label);
}

void drawWindowChrome(int x, int y, int w, int h, const char * title, bool showCloseBtn = true) {
  // 2px crisp drop shadow
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(x + 3, y + 3, x + w + 2, y + h + 2);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawLine(x + 2, y + h + 1, x + w + 1, y + h + 1);
  Canvas.drawLine(x + w + 1, y + 2, x + w + 1, y + h + 1);

  // Main window body & classic double border
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(x, y, x + w - 1, y + h - 1);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(x, y, x + w - 1, y + h - 1);
  Canvas.drawRectangle(x + 2, y + 2, x + w - 3, y + h - 3);

  // Inverted Win95 Title Bar
  Canvas.setBrushColor(fabgl::Color::White);
  Canvas.fillRectangle(x + 4, y + 4, x + w - 5, y + 20);
  Canvas.selectFont(&fabgl::FONT_8x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
  Canvas.setPenColor(fabgl::Color::Black);
  Canvas.drawText(x + 9, y + 8, title);

  // Classic [X] close button in top-right of title bar
  if (showCloseBtn) {
    int bx = x + w - 21;
    int by = y + 6;
    Canvas.setBrushColor(fabgl::Color::Black);
    Canvas.fillRectangle(bx, by, bx + 13, by + 12);
    Canvas.setPenColor(fabgl::Color::White);
    Canvas.drawRectangle(bx, by, bx + 13, by + 12);
    Canvas.drawLine(bx + 3, by + 3, bx + 10, by + 9);
    Canvas.drawLine(bx + 4, by + 3, bx + 10, by + 8);
    Canvas.drawLine(bx + 10, by + 3, bx + 3, by + 9);
    Canvas.drawLine(bx + 9, by + 3, bx + 3, by + 8);
  }
}

// ============================================================================
// Desktop Icon Rendering
// ============================================================================
void drawSingleDesktopIcon(int idx) {
  int total = totalDesktopIcons();
  if (idx < 0 || idx >= total) return;

  int x, y, w, h;
  getDesktopIconRect(idx, x, y, w, h);
  bool sel = (idx == selectedIconIdx);

  const char * label = "";
  const fabgl::Bitmap * bmpWhite = &gDesktopIconWhite[ICON_APP_GENERIC];
  const fabgl::Bitmap * bmpBlack = &gDesktopIconBlack[ICON_APP_GENERIC];

  if (idx == 0) {
    label    = "My Computer";
    bmpWhite = &gDesktopIconWhite[ICON_COMPUTER];
    bmpBlack = &gDesktopIconBlack[ICON_COMPUTER];
  } else if (idx == 1) {
    label    = "Wallpaper";
    bmpWhite = &gDesktopIconWhite[ICON_WALLPAPER];
    bmpBlack = &gDesktopIconBlack[ICON_WALLPAPER];
  } else if (idx == 2) {
    label    = "SD Apps";
    bmpWhite = &gDesktopIconWhite[ICON_FOLDER];
    bmpBlack = &gDesktopIconBlack[ICON_FOLDER];
  } else if (idx == 3) {
    label    = "Notepad";
    bmpWhite = &gDesktopIconWhite[ICON_NOTEPAD];
    bmpBlack = &gDesktopIconBlack[ICON_NOTEPAD];
  } else {
    int appIdx = idx - BUILTIN_ICONS;
    label = gSDApps[appIdx].label;
    if (gSDApps[appIdx].hasCustomIcon) {
      bmpWhite = &gCustomIconWhite[appIdx];
      bmpBlack = &gCustomIconBlack[appIdx];
    } else {
      bmpWhite = &gDesktopIconWhite[gSDApps[appIdx].iconKind];
      bmpBlack = &gDesktopIconBlack[gSDApps[appIdx].iconKind];
    }
  }

  // Restore wallpaper behind icon cell first
  drawWallpaperRect(Canvas, x, y, x + w - 1, y + h - 1, wallpaperIdx);

  // Crisp icon plaque (38x30 centered) so icon is readable on ANY wallpaper pattern
  int px = x + (w - 38) / 2;
  int py = y + 2;
  if (sel) {
    Canvas.setBrushColor(fabgl::Color::White);
    Canvas.fillRectangle(px, py, px + 37, py + 29);
    Canvas.setPenColor(fabgl::Color::Black);
    Canvas.drawRectangle(px, py, px + 37, py + 29);
    Canvas.drawBitmap(px + 3, py + 3, bmpBlack);
  } else {
    Canvas.setBrushColor(fabgl::Color::Black);
    Canvas.fillRectangle(px, py, px + 37, py + 29);
    Canvas.setPenColor(fabgl::Color::White);
    Canvas.drawRectangle(px, py, px + 37, py + 29);
    Canvas.drawBitmap(px + 3, py + 3, bmpWhite);
  }

  // Label pill underneath (truncated cleanly to 13 chars = 78 px)
  char shortLbl[14];
  strncpy(shortLbl, label, 13);
  shortLbl[13] = '\0';
  int tw = (int)strlen(shortLbl) * 6;
  int lx = x + (w - tw) / 2;
  int ly = y + 36;
  int boxX0 = max(x, lx - 3);
  int boxX1 = min(x + w - 1, lx + tw + 2);

  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));

  if (sel) {
    Canvas.setBrushColor(fabgl::Color::White);
    Canvas.fillRectangle(boxX0, ly - 2, boxX1, ly + 10);
    Canvas.setPenColor(fabgl::Color::Black);
    Canvas.drawText(lx, ly, shortLbl);
  } else {
    Canvas.setBrushColor(fabgl::Color::Black);
    Canvas.fillRectangle(boxX0, ly - 2, boxX1, ly + 10);
    Canvas.setPenColor(fabgl::Color::White);
    Canvas.drawRectangle(boxX0, ly - 2, boxX1, ly + 10);
    Canvas.drawText(lx, ly, shortLbl);
  }
}

void drawAllDesktopIcons() {
  int total = totalDesktopIcons();
  for (int i = 0; i < total; ++i) {
    drawSingleDesktopIcon(i);
  }
}

// ============================================================================
// Windows 95 Taskbar Rendering
// ============================================================================
void drawTaskbarClockOnly() {
  uint32_t sec = millis() / 1000;
  uint32_t mins = (sec / 60) % 100;
  uint32_t secs = sec % 60;
  char clk[8];
  snprintf(clk, sizeof(clk), "%02u:%02u", (unsigned)mins, (unsigned)secs);

  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(679, TASKBAR_Y + 5, 713, SCREEN_H - 4);
  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawText(681, TASKBAR_Y + 8, clk);
}

void redrawTaskbar() {
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(0, TASKBAR_Y, SCREEN_W - 1, SCREEN_H - 1);

  // Classic Win95 1-bit double-ridge top border
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawLine(0, TASKBAR_Y, SCREEN_W - 1, TASKBAR_Y);
  Canvas.drawLine(0, TASKBAR_Y + 2, SCREEN_W - 1, TASKBAR_Y + 2);

  // [ Start ] Button with 16x10 4-pane window emblem
  if (startMenuOpen) {
    Canvas.setBrushColor(fabgl::Color::White);
    Canvas.fillRectangle(START_X, START_Y, START_X + START_W - 1, START_Y + START_H - 1);
    Canvas.drawBitmap(START_X + 5, START_Y + 4, &gStartLogoBlack);
    Canvas.selectFont(&fabgl::FONT_8x8);
    Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
    Canvas.setPenColor(fabgl::Color::Black);
    Canvas.drawText(START_X + 23, START_Y + 5, "Start");
  } else {
    Canvas.setBrushColor(fabgl::Color::Black);
    Canvas.fillRectangle(START_X, START_Y, START_X + START_W - 1, START_Y + START_H - 1);
    Canvas.setPenColor(fabgl::Color::White);
    Canvas.drawRectangle(START_X, START_Y, START_X + START_W - 1, START_Y + START_H - 1);
    Canvas.drawLine(START_X + 1, START_Y + START_H, START_X + START_W, START_Y + START_H);
    Canvas.drawLine(START_X + START_W, START_Y + 1, START_X + START_W, START_Y + START_H);
    Canvas.drawBitmap(START_X + 5, START_Y + 4, &gStartLogoWhite);
    Canvas.selectFont(&fabgl::FONT_8x8);
    Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
    Canvas.drawText(START_X + 23, START_Y + 5, "Start");
  }

  // Vertical divider right of Start button
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawLine(73, TASKBAR_Y + 4, 73, SCREEN_H - 3);

  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));

  // Center Taskbar area: Toast OR Active Window Tab OR Ready Status
  if (toastMsg[0] && millis() < toastUntilMs) {
    Canvas.setBrushColor(fabgl::Color::White);
    Canvas.fillRectangle(78, TASKBAR_Y + 4, 576, SCREEN_H - 3);
    Canvas.setPenColor(fabgl::Color::Black);
    Canvas.drawText(84, TASKBAR_Y + 8, toastMsg);
  } else if (activeWin != WIN_NONE) {
    const char * tabName = "Window";
    if (activeWin == WIN_COMPUTER)  tabName = "My Computer";
    if (activeWin == WIN_WALLPAPER) tabName = "Display Properties";
    if (activeWin == WIN_SD_APPS)   tabName = "SD Apps (/APPS)";
    if (activeWin == WIN_NOTEPAD)   tabName = "Notepad";
    if (activeWin == WIN_HELP)      tabName = "MDA Desktop Help";
    if (activeWin == WIN_LAUNCHING) tabName = "Launching Program...";
    if (activeWin == WIN_ERROR)     tabName = "Launch Error";

    Canvas.setBrushColor(fabgl::Color::White);
    Canvas.fillRectangle(78, TASKBAR_Y + 4, 246, SCREEN_H - 3);
    Canvas.setPenColor(fabgl::Color::Black);
    Canvas.drawText(86, TASKBAR_Y + 8, tabName);
  } else {
    Canvas.setPenColor(fabgl::Color::White);
    char st[76];
    if (gCachedAppPath[0]) {
      const char * base = strrchr(gCachedAppPath, '/');
      snprintf(st, sizeof(st), "MDA 95 | %d SD App(s) | Cached: %.18s | Alt/Win:Start",
               gSDAppCount, base ? (base + 1) : gCachedAppPath);
    } else {
      snprintf(st, sizeof(st), "MDA 95 | %d SD App(s) | Double-Click Icon or Press Start", gSDAppCount);
    }
    Canvas.drawText(80, TASKBAR_Y + 8, st);
  }

  // System Tray (sunken box x = 582..716)
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(582, TASKBAR_Y + 4, 716, SCREEN_H - 3);
  char tray[24];
  snprintf(tray, sizeof(tray), "SD:%c S:%d |", gSDMounted ? 'Y' : 'N', mouseSensIdx + 1);
  Canvas.drawText(588, TASKBAR_Y + 8, tray);
  drawTaskbarClockOnly();
}

// ============================================================================
// Start Menu & Cascading Programs Submenu
// ============================================================================
void drawProgramsSubmenu() {
  if (!progSubmenuOpen) return;
  int sx = START_MENU_X + START_MENU_W - 1;
  int cnt = max(1, gSDAppCount);
  int sh  = cnt * START_ITEM_H + 6;
  int sy  = min(TASKBAR_Y - sh - 1, TASKBAR_Y - START_ITEM_CNT * START_ITEM_H - 6);
  if (sy < 4) sy = 4;
  int sw  = 170;

  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(sx, sy, sx + sw - 1, sy + sh - 1);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(sx, sy, sx + sw - 1, sy + sh - 1);
  Canvas.drawLine(sx + 2, sy + sh, sx + sw, sy + sh);
  Canvas.drawLine(sx + sw, sy + 2, sx + sw, sy + sh);

  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));

  if (gSDAppCount == 0) {
    Canvas.setPenColor(fabgl::Color::White);
    Canvas.drawText(sx + 8, sy + 7, "(No .BIN in /APPS)");
    return;
  }

  for (int i = 0; i < gSDAppCount; ++i) {
    int iy = sy + 3 + i * START_ITEM_H;
    bool hov = (i == progHoverIdx);
    if (hov) {
      Canvas.setBrushColor(fabgl::Color::White);
      Canvas.fillRectangle(sx + 2, iy, sx + sw - 3, iy + START_ITEM_H - 1);
      Canvas.setPenColor(fabgl::Color::Black);
    } else {
      Canvas.setPenColor(fabgl::Color::White);
    }
    char line[30];
    snprintf(line, sizeof(line), "%-16.16s %4uK", gSDApps[i].label, (unsigned)((gSDApps[i].size + 1023) / 1024));
    Canvas.drawText(sx + 8, iy + 5, line);
  }
}

void drawStartMenu() {
  if (!startMenuOpen) return;
  int mh = START_ITEM_CNT * START_ITEM_H + 6;
  int my = TASKBAR_Y - mh;

  // Main menu box + 1-bit shadow
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(START_MENU_X, my, START_MENU_X + START_MENU_W - 1, my + mh - 1);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(START_MENU_X, my, START_MENU_X + START_MENU_W - 1, my + mh - 1);
  Canvas.drawLine(START_MENU_X + START_MENU_W, my + 2, START_MENU_X + START_MENU_W, my + mh - 1);

  // Classic Win95 vertical sidebar banner ("MDA 95")
  Canvas.setBrushColor(fabgl::Color::White);
  Canvas.fillRectangle(START_MENU_X + 2, my + 2, START_MENU_X + START_SIDE_W, my + mh - 3);
  Canvas.selectFont(&fabgl::FONT_8x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
  Canvas.setPenColor(fabgl::Color::Black);
  const char * banner = "MDA95";
  for (int i = 0; banner[i]; ++i) {
    char s[2] = { banner[i], 0 };
    Canvas.drawText(START_MENU_X + 6, my + 26 + i * 26, s);
  }

  // Menu items
  Canvas.selectFont(&fabgl::FONT_6x8);
  int ix = START_MENU_X + START_SIDE_W + 2;
  int iw = START_MENU_W - START_SIDE_W - 4;
  for (int i = 0; i < START_ITEM_CNT; ++i) {
    int iy = my + 3 + i * START_ITEM_H;
    bool hov = (i == startHoverIdx);
    if (hov) {
      Canvas.setBrushColor(fabgl::Color::White);
      Canvas.fillRectangle(ix, iy, ix + iw - 1, iy + START_ITEM_H - 1);
      Canvas.setPenColor(fabgl::Color::Black);
    } else {
      Canvas.setPenColor(fabgl::Color::White);
    }
    Canvas.drawText(ix + 8, iy + 5, START_ITEMS[i]);
    if (i == 0 || i == 6) {
      Canvas.setPenColor(fabgl::Color::White);
      Canvas.drawLine(ix + 2, iy + START_ITEM_H - 1, ix + iw - 3, iy + START_ITEM_H - 1);
    }
  }

  if (progSubmenuOpen) drawProgramsSubmenu();
}

void drawContextMenu() {
  if (!ctxMenuOpen) return;
  int ch = CTX_ITEM_CNT * CTX_ITEM_H + 4;
  int cx = constrain(ctxMenuX, 2, SCREEN_W - CTX_MENU_W - 4);
  int cy = constrain(ctxMenuY, 2, TASKBAR_Y - ch - 4);
  ctxMenuX = cx; ctxMenuY = cy;

  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(cx, cy, cx + CTX_MENU_W - 1, cy + ch - 1);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(cx, cy, cx + CTX_MENU_W - 1, cy + ch - 1);
  Canvas.drawLine(cx + 2, cy + ch, cx + CTX_MENU_W, cy + ch);
  Canvas.drawLine(cx + CTX_MENU_W, cy + 2, cx + CTX_MENU_W, cy + ch);

  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
  for (int i = 0; i < CTX_ITEM_CNT; ++i) {
    int iy = cy + 2 + i * CTX_ITEM_H;
    bool hov = (i == ctxHoverIdx);
    if (hov) {
      Canvas.setBrushColor(fabgl::Color::White);
      Canvas.fillRectangle(cx + 2, iy, cx + CTX_MENU_W - 3, iy + CTX_ITEM_H - 1);
      Canvas.setPenColor(fabgl::Color::Black);
    } else {
      Canvas.setPenColor(fabgl::Color::White);
    }
    Canvas.drawText(cx + 8, iy + 4, CTX_ITEMS[i]);
  }
}

void closeAllMenus() {
  if (!startMenuOpen && !ctxMenuOpen) return;
  startMenuOpen   = false;
  progSubmenuOpen = false;
  ctxMenuOpen     = false;
  redrawDesktopAll();
}

// ============================================================================
// Built-In Windows 95 Dialog Windows
// ============================================================================
void drawWallpaperDialog() {
  drawWindowChrome(winX, winY, winW, winH, "Display Properties - Wallpaper");

  // Left side: Classic Win95 Miniature CRT Monitor Preview!
  int mx = winX + 18, my = winY + 32;
  Canvas.setPenColor(fabgl::Color::White);
  // Monitor outer bezel
  Canvas.drawRectangle(mx, my, mx + 148, my + 86);
  Canvas.drawRectangle(mx + 2, my + 2, mx + 146, my + 84);
  // Inner CRT glass with live 128x64 wallpaper pattern preview!
  Canvas.waitCompletion(false);
  buildWallpaperPreview(wpPreviewIdx);
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(mx + 10, my + 10, mx + 137, my + 73);
  Canvas.drawBitmap(mx + 10, my + 10, &gWallpaperPrevBmp);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(mx + 9, my + 9, mx + 138, my + 74);
  // Mini taskbar inside CRT preview!
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(mx + 10, my + 67, mx + 137, my + 73);
  Canvas.drawLine(mx + 10, my + 67, mx + 137, my + 67);
  Canvas.setBrushColor(fabgl::Color::White);
  Canvas.fillRectangle(mx + 12, my + 69, mx + 26, my + 72);
  // Monitor stand + base
  Canvas.drawRectangle(mx + 58, my + 87, mx + 90, my + 96);
  Canvas.drawRectangle(mx + 38, my + 96, mx + 110, my + 103);

  // Selected wallpaper caption under monitor
  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawText(mx + 4, my + 114, "Pattern:");
  Canvas.drawText(mx + 4, my + 126, WALLPAPER_NAMES[wpPreviewIdx]);
  Canvas.drawText(mx + 4, my + 142, "Saved in ESP32 NVS");

  // Right side: Scrollable Wallpaper Pattern List
  int lx = winX + 182, ly = winY + 32, lw = 216, lh = 136;
  Canvas.drawRectangle(lx, ly, lx + lw - 1, ly + lh - 1);

  if (wpPreviewIdx < wpScrollTop) wpScrollTop = wpPreviewIdx;
  if (wpPreviewIdx >= wpScrollTop + WP_VIS_ROWS) wpScrollTop = wpPreviewIdx - WP_VIS_ROWS + 1;
  wpScrollTop = constrain(wpScrollTop, 0, max(0, NUM_WALLPAPERS - WP_VIS_ROWS));

  for (int r = 0; r < WP_VIS_ROWS; ++r) {
    int idx = wpScrollTop + r;
    if (idx >= NUM_WALLPAPERS) break;
    int ry = ly + 3 + r * 14;
    bool sel = (idx == wpPreviewIdx);
    if (sel) {
      Canvas.setBrushColor(fabgl::Color::White);
      Canvas.fillRectangle(lx + 2, ry, lx + lw - 21, ry + 13);
      Canvas.setPenColor(fabgl::Color::Black);
    } else {
      Canvas.setPenColor(fabgl::Color::White);
    }
    char line[32];
    snprintf(line, sizeof(line), "%2d. %s", idx + 1, WALLPAPER_NAMES[idx]);
    Canvas.drawText(lx + 6, ry + 3, line);
  }

  // Mini scrollbar on list right
  int sbX = lx + lw - 18;
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.drawRectangle(sbX, ly, lx + lw - 1, ly + lh - 1);
  Canvas.drawRectangle(sbX, ly, lx + lw - 1, ly + 15);
  Canvas.drawText(sbX + 6, ly + 4, "^");
  Canvas.drawRectangle(sbX, ly + lh - 16, lx + lw - 1, ly + lh - 1);
  Canvas.drawText(sbX + 6, ly + lh - 12, "v");

  // Bottom Buttons: [ OK ] [ Apply ] [ Cancel ]
  int by = winY + winH - 34;
  drawWin95Button(winX + 124, by, 80, 22, "OK",     true);
  drawWin95Button(winX + 216, by, 80, 22, "Apply",  false);
  drawWin95Button(winX + 308, by, 80, 22, "Cancel", false);
}

void drawMyComputerDialog() {
  drawWindowChrome(winX, winY, winW, winH, "My Computer - System Properties");
  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
  Canvas.setPenColor(fabgl::Color::White);

  const esp_partition_t * runPart = esp_ota_get_running_partition();
  const esp_partition_t * appPart = esp_ota_get_next_update_partition(nullptr);

  int x = winX + 16;
  int y = winY + 32;
  char line[72];

  Canvas.drawText(x, y, "MDA Desktop 95 - Hercules 1-Bit Launcher"); y += 16;
  Canvas.drawLine(x, y - 4, winX + winW - 16, y - 4);

  snprintf(line, sizeof(line), "Display  : 720x350 @ 50Hz TTL MDA (1-bit)");
  Canvas.drawText(x, y, line); y += 13;

  snprintf(line, sizeof(line), "MCU      : ESP32 @ %u MHz | Free Heap: %u KB",
           (unsigned)ESP.getCpuFreqMHz(), (unsigned)(ESP.getFreeHeap() / 1024));
  Canvas.drawText(x, y, line); y += 13;

  snprintf(line, sizeof(line), "Desktop  : %s (%u KB slot)",
           runPart ? runPart->label : "unknown",
           runPart ? (unsigned)(runPart->size / 1024) : 0);
  Canvas.drawText(x, y, line); y += 13;

  if (appPart) {
    snprintf(line, sizeof(line), "App Slot : %s (%u KB max .BIN size)",
             appPart->label, (unsigned)(appPart->size / 1024));
  } else {
    snprintf(line, sizeof(line), "App Slot : MISSING! Select Minimal SPIFFS (OTA)");
  }
  Canvas.drawText(x, y, line); y += 13;

  snprintf(line, sizeof(line), "Cached   : %.40s (%u KB)",
           gCachedAppPath[0] ? gCachedAppPath : "(none)",
           (unsigned)(gCachedAppSize / 1024));
  Canvas.drawText(x, y, line); y += 13;

  snprintf(line, sizeof(line), "SD Card  : %s (CS13 MOSI12 MISO2 CLK14) - %d Apps",
           gSDMounted ? "MOUNTED" : "NOT FOUND", gSDAppCount);
  Canvas.drawText(x, y, line); y += 13;

  snprintf(line, sizeof(line), "PS/2     : Kbd=%s (33/32)  Mouse=%s (26/27) Spd=%d/6",
           Keyboard.isKeyboardAvailable() ? "OK" : "NO",
           Mouse.isMouseAvailable() ? "OK" : "NO",
           mouseSensIdx + 1);
  Canvas.drawText(x, y, line); y += 13;

  snprintf(line, sizeof(line), "Wallpaper: %s", WALLPAPER_NAMES[wallpaperIdx]);
  Canvas.drawText(x, y, line);

  drawWin95Button(winX + (winW - 90) / 2, winY + winH - 34, 90, 22, "OK", true);
}

void drawSDAppsDialog() {
  drawWindowChrome(winX, winY, winW, winH, "SD Apps (/APPS) - Launchable Programs");
  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
  Canvas.setPenColor(fabgl::Color::White);

  int lx = winX + 14, ly = winY + 32, lw = winW - 28, lh = 132;
  Canvas.drawRectangle(lx, ly, lx + lw - 1, ly + lh - 1);

  if (!gSDMounted) {
    Canvas.drawText(lx + 16, ly + 44, "No SD card mounted (CS=13 MOSI=12 MISO=2 CLK=14).");
    Canvas.drawText(lx + 16, ly + 60, "Insert FAT32 microSD card and click [Remount SD].");
  } else if (gSDAppCount == 0) {
    Canvas.drawText(lx + 16, ly + 36, "No .BIN programs found in /APPS/ on SD card.");
    Canvas.drawText(lx + 16, ly + 54, "In Arduino IDE: Sketch -> Export Compiled Binary");
    Canvas.drawText(lx + 16, ly + 68, "Copy MDA_Paint.ino.bin to /APPS/MDAPAINT.BIN");
    Canvas.drawText(lx + 16, ly + 82, "then click [Remount SD] to see it on Desktop!");
  } else {
    sdWinSelIdx = constrain(sdWinSelIdx, 0, gSDAppCount - 1);
    if (sdWinSelIdx < sdWinScrollTop) sdWinScrollTop = sdWinSelIdx;
    if (sdWinSelIdx >= sdWinScrollTop + SDWIN_VIS_ROWS) sdWinScrollTop = sdWinSelIdx - SDWIN_VIS_ROWS + 1;
    sdWinScrollTop = constrain(sdWinScrollTop, 0, max(0, gSDAppCount - SDWIN_VIS_ROWS));

    for (int r = 0; r < SDWIN_VIS_ROWS; ++r) {
      int idx = sdWinScrollTop + r;
      if (idx >= gSDAppCount) break;
      int ry = ly + 4 + r * 15;
      bool sel = (idx == sdWinSelIdx);
      if (sel) {
        Canvas.setBrushColor(fabgl::Color::White);
        Canvas.fillRectangle(lx + 2, ry, lx + lw - 3, ry + 14);
        Canvas.setPenColor(fabgl::Color::Black);
      } else {
        Canvas.setPenColor(fabgl::Color::White);
      }
      bool isCached = (strcmp(gCachedAppPath, gSDApps[idx].path) == 0 && gCachedAppSize == gSDApps[idx].size);
      char line[72];
      snprintf(line, sizeof(line), "%-16.16s  %-22.22s %4uKB %s",
               gSDApps[idx].label, gSDApps[idx].path,
               (unsigned)((gSDApps[idx].size + 1023) / 1024),
               isCached ? "[FAST]" : "");
      Canvas.drawText(lx + 6, ry + 3, line);
    }
  }

  int by = winY + winH - 34;
  drawWin95Button(winX + 16,  by, 108, 22, "Launch App",  gSDAppCount > 0);
  drawWin95Button(winX + 136, by, 114, 22, "Remount SD",  false);
  drawWin95Button(winX + 262, by, 122, 22, "Close",       false);
}

void drawHelpDialog() {
  drawWindowChrome(winX, winY, winW, winH, "MDA Desktop 95 - Help & App Guide");
  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
  Canvas.setPenColor(fabgl::Color::White);

  const char * lines[] = {
    "MOUSE & KEYBOARD CONTROLS:",
    "  Click / Dbl-Click Icon : Select / Launch Program or Tool",
    "  Right-Click Desktop    : Quick Desktop Context Menu",
    "  Drag Window Title Bar  : Move Any Open Window",
    "  Start / Alt / Ctrl+Esc : Open Start Menu (Arrows + Enter)",
    "  - / + Keys             : Adjust Mouse Sensitivity (1..6)",
    "  F5 : Remount SD Card   | F1 : Help  | Esc : Close Window",
    "",
    "ADDING LAUNCHABLE APPS ON SD CARD (/APPS/*.BIN):",
    "  1. Flash MDA_Desktop with an OTA Partition Scheme",
    "     (Tools -> Partition Scheme -> Minimal SPIFFS 1.9MB OTA).",
    "  2. In Arduino IDE, open MDA_Paint (or your own app) and",
    "     choose Sketch -> Export Compiled Binary.",
    "  3. Copy the .bin to /APPS/ on FAT32 SD (e.g. MDAPAINT.BIN).",
    "  4. Optional: Add matching 32x24 1-bit .BMP for custom icon!",
    "  5. Inside any app, File->Exit / Alt+F4 / RST returns here."
  };
  int y = winY + 28;
  for (int i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); ++i) {
    Canvas.drawText(winX + 14, y, lines[i]);
    y += 11;
  }
  drawWin95Button(winX + (winW - 88) / 2, winY + winH - 30, 88, 20, "Close", true);
}

void drawLaunchingDialog() {
  drawWindowChrome(winX, winY, winW, winH, "MDA 95 Program Loader", false);
  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
  Canvas.setPenColor(fabgl::Color::White);

  Canvas.drawText(winX + 18, winY + 34, launchTitle);
  Canvas.drawText(winX + 18, winY + 50, launchStage);

  // Crisp Win95 segmented 1-bit progress bar
  int bx = winX + 18, by = winY + 70, bw = winW - 36, bh = 20;
  Canvas.drawRectangle(bx, by, bx + bw - 1, by + bh - 1);
  int innerW = bw - 6;
  int fillW  = (innerW * constrain(launchPct, 0, 100)) / 100;
  Canvas.setBrushColor(fabgl::Color::White);
  for (int px = 0; px + 6 <= fillW; px += 10) {
    Canvas.fillRectangle(bx + 3 + px, by + 3, bx + 3 + px + 7, by + bh - 4);
  }
  char pctStr[16];
  snprintf(pctStr, sizeof(pctStr), "%d%%", constrain(launchPct, 0, 100));
  Canvas.drawText(winX + (winW - (int)strlen(pctStr) * 6) / 2, by + 28, pctStr);
  Canvas.drawText(winX + 18, winY + winH - 22, "Do not turn off power while writing to ota_1.");
  Canvas.waitCompletion(false);
}

void drawErrorDialog() {
  drawWindowChrome(winX, winY, winW, winH, "Program Launch Error");
  Canvas.selectFont(&fabgl::FONT_6x8);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
  Canvas.setPenColor(fabgl::Color::White);

  Canvas.drawText(winX + 18, winY + 38, "Could not launch program from SD card:");
  Canvas.drawText(winX + 18, winY + 58, errorMsg);
  Canvas.drawText(winX + 18, winY + 82, "Tip: Ensure Arduino Partition Scheme has OTA");
  Canvas.drawText(winX + 18, winY + 95, "     (e.g. Minimal SPIFFS 1.9MB APP with OTA).");
  drawWin95Button(winX + (winW - 88) / 2, winY + winH - 34, 88, 22, "OK", true);
}

void drawNotepadWindow() {
  char title[48];
  gNotepad.getWindowTitle(title, sizeof(title));
  drawWindowChrome(winX, winY, winW, winH, title);
  gNotepad.drawEditorBody(Canvas, winX, winY, winW, winH);
}

void drawActiveWindow() {
  switch (activeWin) {
    case WIN_WALLPAPER:  drawWallpaperDialog(); break;
    case WIN_COMPUTER:   drawMyComputerDialog(); break;
    case WIN_SD_APPS:    drawSDAppsDialog(); break;
    case WIN_NOTEPAD:    drawNotepadWindow(); break;
    case WIN_HELP:       drawHelpDialog(); break;
    case WIN_LAUNCHING:  drawLaunchingDialog(); break;
    case WIN_ERROR:      drawErrorDialog(); break;
    default: break;
  }
}

void redrawDesktopAll() {
  Canvas.waitCompletion(false);
  buildWallpaperStrip(wallpaperIdx);
  drawWallpaperRect(Canvas, 0, 0, SCREEN_W - 1, DESK_H - 1, wallpaperIdx);
  drawAllDesktopIcons();
  if (activeWin != WIN_NONE) drawActiveWindow();
  redrawTaskbar();
  if (startMenuOpen) drawStartMenu();
  if (ctxMenuOpen)   drawContextMenu();
}

// ============================================================================
// Window Management & App Launching
// ============================================================================
void openWindow(ActiveWindow w) {
  startMenuOpen   = false;
  progSubmenuOpen = false;
  ctxMenuOpen     = false;
  activeWin       = w;

  if (w == WIN_WALLPAPER) {
    winW = 414; winH = 224;
    wpPreviewIdx = wallpaperIdx;
  } else if (w == WIN_COMPUTER) {
    winW = 416; winH = 208;
  } else if (w == WIN_SD_APPS) {
    winW = 408; winH = 212;
    sdWinSelIdx = 0;
  } else if (w == WIN_NOTEPAD) {
    winW = 580; winH = 276;
    gNotepad.ensureInit();
    gNotepad.openMenu = -1;
    gNotepad.fbMode   = NPFB_CLOSED;
    gNotepad.caretOn  = true;
    gNotepad.lastBlinkMs = millis();
  } else if (w == WIN_HELP) {
    winW = 448; winH = 250;
  } else if (w == WIN_LAUNCHING) {
    winW = 360; winH = 144;
  } else if (w == WIN_ERROR) {
    winW = 380; winH = 152;
  }

  winX = (SCREEN_W - winW) / 2;
  winY = (DESK_H - winH) / 2;
  redrawDesktopAll();
}

void closeActiveWindow() {
  if (activeWin == WIN_NONE) return;
  activeWin   = WIN_NONE;
  winDragging = false;
  redrawDesktopAll();
}

void onLaunchProgress(int pct, const char * stage) {
  launchPct = pct;
  strncpy(launchStage, stage, sizeof(launchStage) - 1);
  launchStage[sizeof(launchStage) - 1] = '\0';
  drawLaunchingDialog();
}

void triggerLaunchApp(int appIdx) {
  if (appIdx < 0 || appIdx >= gSDAppCount) return;
  const SDAppEntry & app = gSDApps[appIdx];
  snprintf(launchTitle, sizeof(launchTitle), "Launching: %.16s (%.16s)", app.label, app.path);
  snprintf(launchStage, sizeof(launchStage), "Preparing SD card...");
  launchPct = 0;
  openWindow(WIN_LAUNCHING);

  char err[72] = "";
  if (!launchSDApp(app, onLaunchProgress, err, sizeof(err))) {
    strncpy(errorMsg, err, sizeof(errorMsg) - 1);
    errorMsg[sizeof(errorMsg) - 1] = '\0';
    openWindow(WIN_ERROR);
  }
}

void activateDesktopIcon(int idx) {
  if (idx == 0) {
    openWindow(WIN_COMPUTER);
  } else if (idx == 1) {
    openWindow(WIN_WALLPAPER);
  } else if (idx == 2) {
    openWindow(WIN_SD_APPS);
  } else if (idx == 3) {
    openWindow(WIN_NOTEPAD);
  } else if (idx >= BUILTIN_ICONS && idx < totalDesktopIcons()) {
    triggerLaunchApp(idx - BUILTIN_ICONS);
  }
}

void executeStartItem(int idx) {
  switch (idx) {
    case 0: // Programs >
      progSubmenuOpen = !progSubmenuOpen;
      progHoverIdx    = 0;
      if (progSubmenuOpen) drawProgramsSubmenu();
      else                 redrawDesktopAll();
      break;
    case 1: openWindow(WIN_NOTEPAD); break;
    case 2: openWindow(WIN_SD_APPS); break;
    case 3: openWindow(WIN_WALLPAPER); break;
    case 4: // Mouse Speed +
      mouseSensIdx = (mouseSensIdx + 1) % NUM_MOUSE_SENS;
      applyMouseSensitivity(true);
      closeAllMenus();
      break;
    case 5: openWindow(WIN_COMPUTER); break;
    case 6: // Remount SD
      closeAllMenus();
      sdMountAndScanApps(true);
      selectedIconIdx = -1;
      redrawDesktopAll();
      setToast(gSDMounted ? "SD Card Remounted & Rescanned" : "No SD Card Detected", 2800);
      break;
    case 7: openWindow(WIN_HELP); break;
    case 8: ESP.restart(); break;
    default: break;
  }
}

void executeContextItem(int idx) {
  closeAllMenus();
  switch (idx) {
    case 0:
      sdMountAndScanApps(true);
      selectedIconIdx = -1;
      redrawDesktopAll();
      setToast(gSDMounted ? "Desktop Refreshed" : "No SD Card Detected", 2500);
      break;
    case 1: openWindow(WIN_WALLPAPER); break;
    case 2:
      if (mouseSensIdx < NUM_MOUSE_SENS - 1) mouseSensIdx++;
      applyMouseSensitivity(true);
      break;
    case 3:
      if (mouseSensIdx > 0) mouseSensIdx--;
      applyMouseSensitivity(true);
      break;
    case 4: openWindow(WIN_COMPUTER); break;
    case 5: ESP.restart(); break;
    default: break;
  }
}

// ============================================================================
// Mouse Click Handling
// ============================================================================
void handleWindowClick(int sx, int sy) {
  if (activeWin == WIN_LAUNCHING) return;

  // Title bar [X] close button
  if (ptInRect(sx, sy, winX + winW - 22, winY + 5, 16, 15)) {
    closeActiveWindow();
    return;
  }

  // Title bar drag start
  if (ptInRect(sx, sy, winX + 4, winY + 4, winW - 26, 17)) {
    winDragging = true;
    winDragOffX = sx - winX;
    winDragOffY = sy - winY;
    return;
  }

  if (activeWin == WIN_WALLPAPER) {
    int lx = winX + 182, ly = winY + 32, lw = 216, lh = 136;
    // Pattern list click
    if (ptInRect(sx, sy, lx + 2, ly + 3, lw - 22, WP_VIS_ROWS * 14)) {
      int row = (sy - (ly + 3)) / 14;
      int idx = wpScrollTop + row;
      if (idx >= 0 && idx < NUM_WALLPAPERS) {
        wpPreviewIdx = idx;
        drawWallpaperDialog();
      }
      return;
    }
    // Scrollbar arrows
    int sbX = lx + lw - 18;
    if (ptInRect(sx, sy, sbX, ly, 18, 16)) {
      if (wpScrollTop > 0) { wpScrollTop--; drawWallpaperDialog(); }
      return;
    }
    if (ptInRect(sx, sy, sbX, ly + lh - 16, 18, 16)) {
      if (wpScrollTop + WP_VIS_ROWS < NUM_WALLPAPERS) { wpScrollTop++; drawWallpaperDialog(); }
      return;
    }
    // Buttons: OK / Apply / Cancel
    int by = winY + winH - 34;
    if (sy >= by && sy <= by + 22) {
      if (sx >= winX + 124 && sx < winX + 204) {
        wallpaperIdx = wpPreviewIdx;
        mdaSaveWallpaperIdx(wallpaperIdx);
        closeActiveWindow();
        setToast("Wallpaper Saved!", 2200);
        return;
      }
      if (sx >= winX + 216 && sx < winX + 296) {
        wallpaperIdx = wpPreviewIdx;
        mdaSaveWallpaperIdx(wallpaperIdx);
        redrawDesktopAll();
        setToast("Wallpaper Applied!", 2000);
        return;
      }
      if (sx >= winX + 308 && sx < winX + 388) {
        closeActiveWindow();
        return;
      }
    }
  } else if (activeWin == WIN_COMPUTER || activeWin == WIN_ERROR) {
    int by = winY + winH - 34;
    if (ptInRect(sx, sy, winX + (winW - 92) / 2, by, 92, 23)) {
      closeActiveWindow();
    }
  } else if (activeWin == WIN_HELP) {
    int by = winY + winH - 30;
    if (ptInRect(sx, sy, winX + (winW - 90) / 2, by, 90, 21)) {
      closeActiveWindow();
    }
  } else if (activeWin == WIN_NOTEPAD) {
    bool redraw = false;
    bool shouldClose = gNotepad.handleMouseClick(sx, sy, winX, winY, winW, winH, redraw);
    if (shouldClose) closeActiveWindow();
    else if (redraw) drawNotepadWindow();
  } else if (activeWin == WIN_SD_APPS) {
    int lx = winX + 14, ly = winY + 32, lw = winW - 28;
    if (gSDAppCount > 0 && ptInRect(sx, sy, lx + 2, ly + 4, lw - 4, SDWIN_VIS_ROWS * 15)) {
      int row = (sy - (ly + 4)) / 15;
      int idx = sdWinScrollTop + row;
      if (idx >= 0 && idx < gSDAppCount) {
        uint32_t now = millis();
        bool dbl = (idx == sdWinLastClick && (now - sdWinLastClickMs) <= 450);
        sdWinLastClick   = idx;
        sdWinLastClickMs = now;
        sdWinSelIdx      = idx;
        if (dbl) triggerLaunchApp(idx);
        else     drawSDAppsDialog();
      }
      return;
    }
    int by = winY + winH - 34;
    if (sy >= by && sy <= by + 22) {
      if (sx >= winX + 16 && sx < winX + 124 && gSDAppCount > 0) {
        triggerLaunchApp(sdWinSelIdx);
        return;
      }
      if (sx >= winX + 136 && sx < winX + 250) {
        sdMountAndScanApps(true);
        sdWinSelIdx = 0; sdWinScrollTop = 0;
        redrawDesktopAll();
        return;
      }
      if (sx >= winX + 262 && sx < winX + 384) {
        closeActiveWindow();
        return;
      }
    }
  }
}

void handleLeftClick(int sx, int sy) {
  // 1. Context menu hit test
  if (ctxMenuOpen) {
    int ch = CTX_ITEM_CNT * CTX_ITEM_H + 4;
    if (ptInRect(sx, sy, ctxMenuX, ctxMenuY, CTX_MENU_W, ch)) {
      int idx = (sy - (ctxMenuY + 2)) / CTX_ITEM_H;
      if (idx >= 0 && idx < CTX_ITEM_CNT) executeContextItem(idx);
      else closeAllMenus();
      return;
    }
    closeAllMenus();
    return;
  }

  // 2. Start menu + Programs submenu hit test
  if (startMenuOpen) {
    if (progSubmenuOpen && gSDAppCount > 0) {
      int px = START_MENU_X + START_MENU_W - 1;
      int ph = gSDAppCount * START_ITEM_H + 6;
      int py = max(4, min(TASKBAR_Y - ph - 1, TASKBAR_Y - START_ITEM_CNT * START_ITEM_H - 6));
      if (ptInRect(sx, sy, px, py, 170, ph)) {
        int idx = (sy - (py + 3)) / START_ITEM_H;
        if (idx >= 0 && idx < gSDAppCount) {
          closeAllMenus();
          triggerLaunchApp(idx);
        }
        return;
      }
    }
    int mh = START_ITEM_CNT * START_ITEM_H + 6;
    int my = TASKBAR_Y - mh;
    if (ptInRect(sx, sy, START_MENU_X, my, START_MENU_W, mh)) {
      int idx = (sy - (my + 3)) / START_ITEM_H;
      if (idx >= 0 && idx < START_ITEM_CNT) executeStartItem(idx);
      return;
    }
    if (ptInRect(sx, sy, START_X, START_Y, START_W, START_H)) {
      closeAllMenus();
      return;
    }
    closeAllMenus();
  }

  // 3. Taskbar clicks
  if (sy >= TASKBAR_Y) {
    if (ptInRect(sx, sy, START_X, START_Y, START_W, START_H)) {
      startMenuOpen   = true;
      startHoverIdx   = 0;
      progSubmenuOpen = false;
      redrawTaskbar();
      drawStartMenu();
      return;
    }
    // System tray speed indicator click -> cycle sensitivity
    if (ptInRect(sx, sy, 618, TASKBAR_Y + 4, 42, 17)) {
      mouseSensIdx = (mouseSensIdx + 1) % NUM_MOUSE_SENS;
      applyMouseSensitivity(true);
      return;
    }
    return;
  }

  // 4. Active window click
  if (activeWin != WIN_NONE) {
    if (ptInRect(sx, sy, winX, winY, winW, winH)) {
      handleWindowClick(sx, sy);
    }
    return;
  }

  // 5. Desktop Icon hit test
  int total = totalDesktopIcons();
  for (int i = 0; i < total; ++i) {
    int ix, iy, iw, ih;
    getDesktopIconRect(i, ix, iy, iw, ih);
    if (ptInRect(sx, sy, ix, iy, iw, ih)) {
      uint32_t now = millis();
      bool dbl = (i == lastIconClickIdx && (now - lastIconClickMs) <= 450);
      lastIconClickIdx = i;
      lastIconClickMs  = now;
      int oldSel = selectedIconIdx;
      selectedIconIdx = i;
      if (oldSel >= 0 && oldSel != i) drawSingleDesktopIcon(oldSel);
      drawSingleDesktopIcon(i);
      if (dbl) activateDesktopIcon(i);
      return;
    }
  }

  // Click empty desktop clears selection
  if (selectedIconIdx >= 0) {
    int oldSel = selectedIconIdx;
    selectedIconIdx = -1;
    drawSingleDesktopIcon(oldSel);
  }
}

void handleRightClick(int sx, int sy) {
  if (activeWin != WIN_NONE) return;
  if (startMenuOpen || ctxMenuOpen) {
    closeAllMenus();
    return;
  }
  if (sy < TASKBAR_Y) {
    ctxMenuOpen = true;
    ctxMenuX    = sx;
    ctxMenuY    = sy;
    ctxHoverIdx = -1;
    drawContextMenu();
  }
}

// ============================================================================
// PS/2 Mouse & Keyboard Input Loops
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

    // Dragging an open window
    if (winDragging && btnLeft && activeWin != WIN_NONE) {
      int nx = constrain(curX - winDragOffX, 2, SCREEN_W - winW - 4);
      int ny = constrain(curY - winDragOffY, 2, DESK_H - winH - 4);
      if (nx != winX || ny != winY) {
        winX = nx; winY = ny;
        redrawDesktopAll();
      }
    } else if (activeWin == WIN_NOTEPAD && !startMenuOpen && !ctxMenuOpen) {
      if (gNotepad.handleMouseMove(curX, curY, winX, winY, winW, winH)) {
        gNotepad.drawEditorBody(Canvas, winX, winY, winW, winH);
      }
    }

    // Hover highlights in Start menu
    if (startMenuOpen) {
      int mh = START_ITEM_CNT * START_ITEM_H + 6;
      int my = TASKBAR_Y - mh;
      if (ptInRect(curX, curY, START_MENU_X + START_SIDE_W, my + 3, START_MENU_W - START_SIDE_W, START_ITEM_CNT * START_ITEM_H)) {
        int nh = (curY - (my + 3)) / START_ITEM_H;
        if (nh != startHoverIdx) {
          startHoverIdx = nh;
          bool wantSub = (nh == 0);
          if (wantSub != progSubmenuOpen) {
            progSubmenuOpen = wantSub;
            progHoverIdx = 0;
            redrawDesktopAll();
          } else {
            drawStartMenu();
          }
        }
      } else if (progSubmenuOpen && gSDAppCount > 0) {
        int px = START_MENU_X + START_MENU_W - 1;
        int ph = gSDAppCount * START_ITEM_H + 6;
        int py = max(4, min(TASKBAR_Y - ph - 1, TASKBAR_Y - START_ITEM_CNT * START_ITEM_H - 6));
        if (ptInRect(curX, curY, px, py + 3, 170, gSDAppCount * START_ITEM_H)) {
          int nh = (curY - (py + 3)) / START_ITEM_H;
          if (nh != progHoverIdx) {
            progHoverIdx = nh;
            drawProgramsSubmenu();
          }
        }
      }
    }

    // Hover highlights in Context menu
    if (ctxMenuOpen) {
      int nh = -1;
      if (ptInRect(curX, curY, ctxMenuX, ctxMenuY + 2, CTX_MENU_W, CTX_ITEM_CNT * CTX_ITEM_H)) {
        nh = (curY - (ctxMenuY + 2)) / CTX_ITEM_H;
      }
      if (nh != ctxHoverIdx) {
        ctxHoverIdx = nh;
        drawContextMenu();
      }
    }
  }

  // Scroll wheel inside Wallpaper, SD Apps, or Notepad
  if (wheelSum != 0) {
    int dir = (wheelSum > 0) ? -1 : 1;
    if (activeWin == WIN_WALLPAPER) {
      wpPreviewIdx = constrain(wpPreviewIdx + dir, 0, NUM_WALLPAPERS - 1);
      drawWallpaperDialog();
    } else if (activeWin == WIN_SD_APPS && gSDAppCount > 0) {
      sdWinSelIdx = constrain(sdWinSelIdx + dir, 0, gSDAppCount - 1);
      drawSDAppsDialog();
    } else if (activeWin == WIN_NOTEPAD) {
      if (gNotepad.handleWheel(dir)) gNotepad.drawEditorBody(Canvas, winX, winY, winW, winH);
    }
  }

  if (btnLeft && !prevLeft)   handleLeftClick(curX, curY);
  if (btnRight && !prevRight) handleRightClick(curX, curY);
  if (!btnLeft && prevLeft) {
    winDragging = false;
    if (activeWin == WIN_NOTEPAD) gNotepad.handleMouseUp();
  }

  prevLeft  = btnLeft;
  prevRight = btnRight;
  prevMid   = btnMid;
}

void processKeyboard() {
  while (Keyboard.virtualKeyAvailable()) {
    fabgl::VirtualKeyItem item;
    if (!Keyboard.getNextVirtualKey(&item, 0)) break;

    shiftDown = item.SHIFT;
    ctrlDown  = item.CTRL;

    bool isAlt = (item.vk == fabgl::VK_LALT || item.vk == fabgl::VK_RALT);

    if (!item.down) {
      if (isAlt && altHeld && !altUsedAsCombo) {
        if (activeWin == WIN_NOTEPAD && !startMenuOpen && !ctxMenuOpen && gNotepad.fbMode == NPFB_CLOSED) {
          gNotepad.openMenu  = (gNotepad.openMenu >= 0) ? -1 : 0;
          gNotepad.hoverItem = 0;
          drawNotepadWindow();
        } else if (activeWin == WIN_NONE) {
          if (startMenuOpen) closeAllMenus();
          else {
            ctxMenuOpen     = false;
            startMenuOpen   = true;
            startHoverIdx   = 0;
            progSubmenuOpen = false;
            redrawTaskbar();
            drawStartMenu();
          }
        }
      }
      if (isAlt) { altHeld = false; altUsedAsCombo = false; }
      continue;
    }

    if (isAlt) {
      if (!altHeld) { altHeld = true; altUsedAsCombo = false; }
      continue;
    }
    if (altHeld) altUsedAsCombo = true;

    // Ctrl+Esc or Windows key or F10 toggles Start Menu!
    if ((item.CTRL && item.vk == fabgl::VK_ESCAPE) ||
        item.vk == fabgl::VK_LGUI || item.vk == fabgl::VK_RGUI ||
        item.vk == fabgl::VK_F10) {
      if (startMenuOpen) closeAllMenus();
      else {
        ctxMenuOpen     = false;
        startMenuOpen   = true;
        startHoverIdx   = 0;
        progSubmenuOpen = false;
        redrawTaskbar();
        drawStartMenu();
      }
      continue;
    }

    // Route keyboard input directly to Notepad while it is open (and Start Menu is closed)
    if (activeWin == WIN_NOTEPAD && !startMenuOpen && !ctxMenuOpen) {
      // Allow Ctrl+Esc / Win key to still open Start Menu
      if (!((item.CTRL && item.vk == fabgl::VK_ESCAPE) ||
            item.vk == fabgl::VK_LGUI || item.vk == fabgl::VK_RGUI)) {
        // Esc closes Notepad menu/file-browser first; only closes Notepad if none open?
        // Wait: in Notepad, Esc cancels open menu or file browser (use Alt+F4 or [X] to close)
        if (item.vk == fabgl::VK_ESCAPE && gNotepad.openMenu < 0 && gNotepad.fbMode == NPFB_CLOSED) {
          if (gNotepad.hasSelection()) {
            gNotepad.selAnchor = -1;
            gNotepad.drawEditorBody(Canvas, winX, winY, winW, winH);
          }
          continue;
        }
        bool redraw = false;
        bool shouldClose = gNotepad.handleKey(item, winW, winH, redraw);
        if (shouldClose) closeActiveWindow();
        else if (redraw) drawNotepadWindow();
        continue;
      }
    }

    // F1 -> Help
    if (item.vk == fabgl::VK_F1) {
      openWindow(WIN_HELP);
      continue;
    }

    // F5 -> Refresh/Remount SD Card
    if (item.vk == fabgl::VK_F5) {
      sdMountAndScanApps(true);
      selectedIconIdx = -1;
      redrawDesktopAll();
      setToast(gSDMounted ? "SD Card Refreshed" : "No SD Card Detected", 2500);
      continue;
    }

    // Esc closes menus or active window
    if (item.vk == fabgl::VK_ESCAPE) {
      if (progSubmenuOpen) {
        progSubmenuOpen = false;
        redrawDesktopAll();
      } else if (startMenuOpen || ctxMenuOpen) {
        closeAllMenus();
      } else if (activeWin != WIN_NONE && activeWin != WIN_LAUNCHING) {
        closeActiveWindow();
      }
      continue;
    }

    // ---- Start Menu Keyboard Navigation ----
    if (startMenuOpen) {
      if (progSubmenuOpen) {
        if (item.vk == fabgl::VK_LEFT) {
          progSubmenuOpen = false;
          redrawDesktopAll();
        } else if (item.vk == fabgl::VK_UP && gSDAppCount > 0) {
          progHoverIdx = (progHoverIdx + gSDAppCount - 1) % gSDAppCount;
          drawProgramsSubmenu();
        } else if (item.vk == fabgl::VK_DOWN && gSDAppCount > 0) {
          progHoverIdx = (progHoverIdx + 1) % gSDAppCount;
          drawProgramsSubmenu();
        } else if ((item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_KP_ENTER || item.vk == fabgl::VK_SPACE) && gSDAppCount > 0) {
          int chosen = progHoverIdx;
          closeAllMenus();
          triggerLaunchApp(chosen);
        }
      } else {
        if (item.vk == fabgl::VK_UP) {
          startHoverIdx = (startHoverIdx + START_ITEM_CNT - 1) % START_ITEM_CNT;
          drawStartMenu();
        } else if (item.vk == fabgl::VK_DOWN) {
          startHoverIdx = (startHoverIdx + 1) % START_ITEM_CNT;
          drawStartMenu();
        } else if (item.vk == fabgl::VK_RIGHT && startHoverIdx == 0) {
          progSubmenuOpen = true;
          progHoverIdx    = 0;
          drawProgramsSubmenu();
        } else if (item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_KP_ENTER || item.vk == fabgl::VK_SPACE) {
          executeStartItem(startHoverIdx);
        }
      }
      continue;
    }

    // ---- Active Dialog Keyboard Navigation ----
    if (activeWin == WIN_WALLPAPER) {
      if (item.vk == fabgl::VK_UP) {
        if (wpPreviewIdx > 0) { wpPreviewIdx--; drawWallpaperDialog(); }
      } else if (item.vk == fabgl::VK_DOWN) {
        if (wpPreviewIdx + 1 < NUM_WALLPAPERS) { wpPreviewIdx++; drawWallpaperDialog(); }
      } else if (item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_KP_ENTER) {
        wallpaperIdx = wpPreviewIdx;
        mdaSaveWallpaperIdx(wallpaperIdx);
        closeActiveWindow();
        setToast("Wallpaper Saved!", 2200);
      }
      continue;
    }
    if (activeWin == WIN_SD_APPS) {
      if (item.vk == fabgl::VK_UP && gSDAppCount > 0) {
        if (sdWinSelIdx > 0) { sdWinSelIdx--; drawSDAppsDialog(); }
      } else if (item.vk == fabgl::VK_DOWN && gSDAppCount > 0) {
        if (sdWinSelIdx + 1 < gSDAppCount) { sdWinSelIdx++; drawSDAppsDialog(); }
      } else if ((item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_KP_ENTER) && gSDAppCount > 0) {
        triggerLaunchApp(sdWinSelIdx);
      }
      continue;
    }
    if (activeWin == WIN_COMPUTER || activeWin == WIN_HELP || activeWin == WIN_ERROR) {
      if (item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_KP_ENTER || item.vk == fabgl::VK_SPACE) {
        closeActiveWindow();
      }
      continue;
    }

    // ---- Desktop Icon Keyboard Navigation ----
    int total = totalDesktopIcons();
    if (total > 0 && (item.vk == fabgl::VK_UP || item.vk == fabgl::VK_DOWN ||
                      item.vk == fabgl::VK_LEFT || item.vk == fabgl::VK_RIGHT)) {
      int oldSel = selectedIconIdx;
      if (selectedIconIdx < 0) {
        selectedIconIdx = 0;
      } else if (item.vk == fabgl::VK_DOWN) {
        selectedIconIdx = (selectedIconIdx + 1) % total;
      } else if (item.vk == fabgl::VK_UP) {
        selectedIconIdx = (selectedIconIdx + total - 1) % total;
      } else if (item.vk == fabgl::VK_RIGHT) {
        if (selectedIconIdx + ICON_ROWS_COL < total) selectedIconIdx += ICON_ROWS_COL;
      } else if (item.vk == fabgl::VK_LEFT) {
        if (selectedIconIdx - ICON_ROWS_COL >= 0) selectedIconIdx -= ICON_ROWS_COL;
      }
      if (oldSel >= 0 && oldSel != selectedIconIdx) drawSingleDesktopIcon(oldSel);
      drawSingleDesktopIcon(selectedIconIdx);
      continue;
    }

    if ((item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_KP_ENTER) && selectedIconIdx >= 0) {
      activateDesktopIcon(selectedIconIdx);
      continue;
    }

    // Mouse speed shortcuts (- / +)
    if (item.ASCII == '-' || item.ASCII == '_') {
      if (mouseSensIdx > 0) mouseSensIdx--;
      applyMouseSensitivity(true);
      continue;
    }
    if (item.ASCII == '=' || item.ASCII == '+') {
      if (mouseSensIdx < NUM_MOUSE_SENS - 1) mouseSensIdx++;
      applyMouseSensitivity(true);
      continue;
    }
  }
}

// ============================================================================
// Arduino setup() & loop()
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500); // Allow PS/2 peripherals to finish power-on BAT

  // Load persisted wallpaper & mouse sensitivity from ESP32 NVS
  wallpaperIdx = mdaLoadWallpaperIdx(0, NUM_WALLPAPERS - 1);
  mouseSensIdx = mdaLoadMouseSens(2, NUM_MOUSE_SENS - 1);
  loadCachedAppInfo();

  // Explicit LilyGO VGA32 PS/2 GPIO pinout:
  //   Port 0 (Keyboard): CLK = GPIO 33, DATA = GPIO 32
  //   Port 1 (Mouse):    CLK = GPIO 26, DATA = GPIO 27
  PS2Controller.begin(GPIO_NUM_33, GPIO_NUM_32, GPIO_NUM_26, GPIO_NUM_27);
  Keyboard.begin(true, true, 0);
  Keyboard.setLayout(&fabgl::USLayout);
  Mouse.begin(1);
  if (!Mouse.isMouseAvailable()) {
    delay(250);
    Mouse.reset();
  }

  // Mount integrated LilyGO VGA32 SD Card (CS=13, MOSI=12, MISO=2, CLK=14) & scan /APPS/*.BIN
  sdMountAndScanApps(false);

  Serial.println("\n=== MDA Desktop 95 Startup ===");
  Serial.printf("PS/2 Kbd=%d Mouse=%d | SD=%d (%d apps) | Wallpaper=%d | MouseSens=%d\n",
                Keyboard.isKeyboardAvailable() ? 1 : 0,
                Mouse.isMouseAvailable() ? 1 : 0,
                gSDMounted ? 1 : 0, gSDAppCount, wallpaperIdx, mouseSensIdx + 1);

  // LilyGO VGA32 TTL Monochrome MDA Video (720x350 @ 50Hz Hercules)
  // Alternate ESP32 pins: DisplayController.begin(GPIO_NUM_22, GPIO_NUM_22, GPIO_NUM_22, GPIO_NUM_23, GPIO_NUM_15);
  DisplayController.begin(GPIO_NUM_21, GPIO_NUM_21, GPIO_NUM_21, GPIO_NUM_23, GPIO_NUM_15);
  DisplayController.setResolution("\"MDA_50HZ_LOCK\" 16.000 720 736 848 880 350 352 356 364 +HSync -VSync");

  // Strict 1-bit monochrome palette (no grayscale, no intensity)
  DisplayController.setPaletteItem(0, fabgl::RGB888(0, 0, 0));
  DisplayController.setPaletteItem(1, fabgl::RGB888(255, 255, 255));

  // Virtual mouse positioner with shared adjustable sensitivity
  attachVirtualMouse();
  DisplayController.setMouseCursor(fabgl::CursorName::CursorPointerSimple);
  DisplayController.setMouseCursorPos(curX, curY);

  redrawDesktopAll();
  Canvas.waitCompletion(false);
}

void loop() {
  processMouse();
  processKeyboard();

  // Live uptime clock in taskbar system tray (ticks every second)
  uint32_t sec = millis() / 1000;
  if (sec != lastClockSec) {
    lastClockSec = sec;
    drawTaskbarClockOnly();
  }

  // Blinking text cursor inside Notepad
  if (activeWin == WIN_NOTEPAD && !startMenuOpen && !ctxMenuOpen &&
      gNotepad.openMenu < 0 && gNotepad.fbMode == NPFB_CLOSED &&
      (millis() - gNotepad.lastBlinkMs >= 500)) {
    gNotepad.lastBlinkMs = millis();
    gNotepad.caretOn = !gNotepad.caretOn;
    gNotepad.drawEditorBody(Canvas, winX, winY, winW, winH);
  }

  // Expire taskbar toast cleanly
  if (toastMsg[0] && millis() >= toastUntilMs) {
    toastMsg[0] = '\0';
    redrawTaskbar();
  }

  delay(4);
}
