// ============================================================================
// MDA Tetris - Authentic Elektronika-60 (1984) Character-Only Terminal Clone
// Standalone Full-Screen SD Card App for MDA Desktop (/APPS/TETRIS.BIN)
//
// Target Hardware: ESP32 / LilyGO VGA32 + Hercules / MDA Monochrome Monitor
// Display        : 720 x 350 @ 50Hz TTL Monochrome (Strict 1-Bit, 80x25 Text)
// Keyboard       : PS/2 Port 0 (CLK = GPIO 33, DATA = GPIO 32)
// Exit to OS     : Press F10 (or Alt+F4) at any time to return to MDA Desktop
// ============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include <fabgl.h>
#include "mda_launcher_sdk.h"
#include "e60_tetris_engine.h"

// ============================================================================
// Hardware Controllers (LilyGO VGA32 MDA TTL + Explicit PS/2 Keyboard)
// ============================================================================
fabgl::VGA2Controller DisplayController;
fabgl::PS2Controller  PS2Controller;
fabgl::Keyboard       Keyboard;
fabgl::Canvas         Canvas(&DisplayController);

// Authentic IBM MDA 80x25 cell pitch: 8x14 glyph + 1px column spacing = 9x14
// 80 * 9 = 720 px, 25 * 14 = 350 px (exact 1:1 Hercules MDA full screen!)
static constexpr int CELL_W = 9;
static constexpr int CELL_H = 14;

static constexpr const char * TET_NVS_NS = "mda_tet";

E60TetrisEngine gGame;
char            gPrevScreen[TERM_ROWS][TERM_COLS];
bool            gFullRedrawNeeded = true;
uint32_t        gSavedHighScore   = 0;
uint32_t        gSavedBestLines   = 0;

// ============================================================================
// Persistent NVS High Score Storage
// ============================================================================
void loadHighScores() {
  Preferences prefs;
  if (prefs.begin(TET_NVS_NS, true)) {
    gGame.highScore = prefs.getUInt("hiscore", 0);
    gGame.bestLines = prefs.getUInt("blines", 0);
    prefs.end();
  }
  gSavedHighScore = gGame.highScore;
  gSavedBestLines = gGame.bestLines;
}

void saveHighScoresIfChanged() {
  if (gGame.highScore == gSavedHighScore && gGame.bestLines == gSavedBestLines) return;
  Preferences prefs;
  if (prefs.begin(TET_NVS_NS, false)) {
    prefs.putUInt("hiscore", gGame.highScore);
    prefs.putUInt("blines", gGame.bestLines);
    prefs.end();
  }
  gSavedHighScore = gGame.highScore;
  gSavedBestLines = gGame.bestLines;
}

// ============================================================================
// Zero-Flicker 80x25 Character-Only Diff Renderer
// Only redraws character cells that actually changed since the previous frame!
// ============================================================================
void flushTerminalScreen() {
  if (gFullRedrawNeeded) {
    Canvas.setBrushColor(fabgl::Color::Black);
    Canvas.clear();
    memset(gPrevScreen, 0, sizeof(gPrevScreen)); // Force every cell to paint
    gFullRedrawNeeded = false;
  }

  Canvas.selectFont(&fabgl::FONT_8x14);
  Canvas.setPenColor(fabgl::Color::White);
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(true));

  char chBuf[2] = { 0, 0 };
  bool anyDrawn = false;

  for (int r = 0; r < TERM_ROWS; ++r) {
    int py = r * CELL_H;
    for (int c = 0; c < TERM_COLS; ++c) {
      char ch = gGame.screen[r][c];
      if (ch != gPrevScreen[r][c]) {
        gPrevScreen[r][c] = ch;
        chBuf[0] = ch;
        Canvas.drawText(c * CELL_W, py, chBuf);
        anyDrawn = true;
      }
    }
  }

  if (anyDrawn) {
    Canvas.waitCompletion(false);
  }
}

void redrawNow() {
  gGame.render(millis());
  flushTerminalScreen();
}

// ============================================================================
// PS/2 Keyboard Handling
// ============================================================================
void processKeyboard() {
  bool dirty = false;

  while (Keyboard.virtualKeyAvailable()) {
    fabgl::VirtualKeyItem item;
    if (!Keyboard.getNextVirtualKey(&item, 0)) break;
    if (!item.down) continue;

    // Function Key F10 (or Alt+F4) -> Exit cleanly back to MDA Desktop OS!
    if (item.vk == fabgl::VK_F10 ||
        ((item.LALT || item.RALT) && item.vk == fabgl::VK_F4)) {
      saveHighScoresIfChanged();
      mdaReturnToDesktop();
      return;
    }

    uint32_t now = millis();
    char asciiUpper = (char)toupper((unsigned char)item.ASCII);

    // ---- 1. Title / Level Select Screen ----
    if (gGame.mode == E60_TITLE) {
      if (item.ASCII >= '0' && item.ASCII <= '9') {
        gGame.startLevel = item.ASCII - '0';
        dirty = true;
      } else if (item.vk == fabgl::VK_LEFT || item.vk == fabgl::VK_DOWN) {
        gGame.startLevel = (gGame.startLevel + 9) % 10;
        dirty = true;
      } else if (item.vk == fabgl::VK_RIGHT || item.vk == fabgl::VK_UP) {
        gGame.startLevel = (gGame.startLevel + 1) % 10;
        dirty = true;
      } else if (item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_KP_ENTER ||
                 item.vk == fabgl::VK_SPACE) {
        gGame.seedRng(now ^ (uint32_t)micros() ^ 0x1984E601UL);
        gGame.startGame(now);
        dirty = true;
      }
      continue;
    }

    // ---- 2. Game Over Screen ----
    if (gGame.mode == E60_GAMEOVER) {
      if (asciiUpper == 'Y' || item.vk == fabgl::VK_RETURN ||
          item.vk == fabgl::VK_KP_ENTER || item.vk == fabgl::VK_SPACE) {
        gGame.seedRng(now ^ (uint32_t)micros());
        gGame.startGame(now);
        dirty = true;
      } else if (asciiUpper == 'N' || asciiUpper == 'R' || item.vk == fabgl::VK_ESCAPE) {
        gGame.mode = E60_TITLE;
        dirty = true;
      }
      continue;
    }

    // ---- 3. Paused Screen ----
    if (gGame.mode == E60_PAUSED) {
      if (asciiUpper == 'P' || item.vk == fabgl::VK_ESCAPE || item.vk == fabgl::VK_SPACE) {
        gGame.mode = E60_PLAYING;
        gGame.lastFallMs = now;
        snprintf(gGame.statusLine, sizeof(gGame.statusLine), "RESUMED");
        dirty = true;
      } else if (asciiUpper == 'R') {
        gGame.mode = E60_TITLE;
        dirty = true;
      }
      continue;
    }

    // ---- 4. Active Gameplay Controls ----
    if (gGame.mode == E60_PLAYING) {
      // Move Left: Left Arrow, Numpad/Top '7', or 'A'
      if (item.vk == fabgl::VK_LEFT || item.vk == fabgl::VK_KP_7 ||
          item.ASCII == '7' || asciiUpper == 'A') {
        if (gGame.moveHorizontal(-1)) dirty = true;
      }
      // Move Right: Right Arrow, Numpad/Top '9', or 'D'
      else if (item.vk == fabgl::VK_RIGHT || item.vk == fabgl::VK_KP_9 ||
               item.ASCII == '9' || asciiUpper == 'D') {
        if (gGame.moveHorizontal(1)) dirty = true;
      }
      // Rotate CW: Up Arrow, Numpad/Top '8', 'W', or 'X'
      else if (item.vk == fabgl::VK_UP || item.vk == fabgl::VK_KP_8 ||
               item.ASCII == '8' || asciiUpper == 'W' || asciiUpper == 'X') {
        if (gGame.rotatePiece(1)) dirty = true;
      }
      // Rotate CCW: 'Z'
      else if (asciiUpper == 'Z') {
        if (gGame.rotatePiece(-1)) dirty = true;
      }
      // Soft Drop: Down Arrow, Numpad/Top '4', or 'S'
      else if (item.vk == fabgl::VK_DOWN || item.vk == fabgl::VK_KP_4 ||
               item.ASCII == '4' || asciiUpper == 'S') {
        gGame.softDrop(now, true);
        saveHighScoresIfChanged();
        dirty = true;
      }
      // Hard Drop (Slam): Space, Numpad/Top '5', or Enter
      else if (item.vk == fabgl::VK_SPACE || item.vk == fabgl::VK_KP_5 ||
               item.ASCII == '5' || item.vk == fabgl::VK_RETURN ||
               item.vk == fabgl::VK_KP_ENTER) {
        gGame.hardDrop(now);
        saveHighScoresIfChanged();
        dirty = true;
      }
      // Toggle Next Piece Preview (classic E60 '1' key or 'N')
      else if (item.ASCII == '1' || item.vk == fabgl::VK_KP_1 || asciiUpper == 'N') {
        gGame.showNext = !gGame.showNext;
        snprintf(gGame.statusLine, sizeof(gGame.statusLine), "%s",
                 gGame.showNext ? "NEXT PREVIEW: ON" : "NEXT PREVIEW: OFF (+25% PTS)");
        dirty = true;
      }
      // Toggle Well Grid Dots (classic E60 '0' key or 'G')
      else if (item.ASCII == '0' || item.vk == fabgl::VK_KP_0 || asciiUpper == 'G') {
        gGame.showGrid = !gGame.showGrid;
        snprintf(gGame.statusLine, sizeof(gGame.statusLine), "%s",
                 gGame.showGrid ? "WELL GRID: ON" : "WELL GRID: OFF");
        dirty = true;
      }
      // Pause
      else if (asciiUpper == 'P' || item.vk == fabgl::VK_ESCAPE) {
        gGame.mode = E60_PAUSED;
        snprintf(gGame.statusLine, sizeof(gGame.statusLine), "PAUSED - PRESS P OR ESC");
        dirty = true;
      }
      // Return to Title / Level Select
      else if (asciiUpper == 'R') {
        saveHighScoresIfChanged();
        gGame.mode = E60_TITLE;
        dirty = true;
      }
    }
  }

  if (dirty) {
    redrawNow();
  }
}

// ============================================================================
// Arduino setup() & loop()
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(500); // Allow PS/2 keyboard to complete power-on BAT

  // Arm ota_0 so hardware RST or power-cycle always recovers to MDA Desktop
  mdaArmDesktopOnReset();

  // Load persisted High Score & Best Lines from ESP32 NVS
  loadHighScores();
  gGame.seedRng((uint32_t)micros() ^ (uint32_t)millis() ^ 0x1984E601UL);

  // Explicit LilyGO VGA32 PS/2 GPIO pinout (Port 0 Keyboard: CLK=33, DATA=32)
  PS2Controller.begin(GPIO_NUM_33, GPIO_NUM_32, GPIO_NUM_26, GPIO_NUM_27);
  Keyboard.begin(true, true, 0);
  Keyboard.setLayout(&fabgl::USLayout);

  // LilyGO VGA32 TTL Monochrome MDA Video (720x350 @ 50Hz Hercules)
  // Alternate ESP32 pins: DisplayController.begin(GPIO_NUM_22, GPIO_NUM_22, GPIO_NUM_22, GPIO_NUM_23, GPIO_NUM_15);
  DisplayController.begin(GPIO_NUM_21, GPIO_NUM_21, GPIO_NUM_21, GPIO_NUM_23, GPIO_NUM_15);
  DisplayController.setResolution("\"MDA_50HZ_LOCK\" 16.000 720 736 848 880 350 352 356 364 +HSync -VSync");

  // Strict 1-bit monochrome palette (no grayscale, no intensity)
  DisplayController.setPaletteItem(0, fabgl::RGB888(0, 0, 0));
  DisplayController.setPaletteItem(1, fabgl::RGB888(255, 255, 255));

  gFullRedrawNeeded = true;
  redrawNow();
}

void loop() {
  processKeyboard();

  if (gGame.tick(millis())) {
    saveHighScoresIfChanged();
    redrawNow();
  }

  delay(4);
}
