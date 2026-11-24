// ============================================================================
// MDA Solitaire - Classic Windows 95 Klondike Solitaire for Hercules / MDA
// Built for ESP32 / LilyGO VGA32 using FabGL VGA2Controller + PS/2 Mouse & Kbd
// Display: 720 x 350 @ 50Hz TTL Monochrome (Strict 1-Bit, No Grayscale)
//
// Zero-Flicker / Zero-Tearing Full-Card Dragging Engine:
//   Uses VGA2Controller hardware VSync double-buffering (Canvas.swapBuffers)
//   paired with two 30.8 KB 1-bit offscreen compositing buffers (bgFB + outFB).
//   Dragged cards (and entire dragged stacks!) render in full detail with drop
//   shadows and grabbing cursor in exact VSync lockstep — zero flashing!
// ============================================================================

#include <Arduino.h>
#include <fabgl.h>
#include "mda_launcher_sdk.h"
#include "mda_fb1bit.h"
#include "mda_cards.h"

// ============================================================================
// Hardware Controllers (LilyGO VGA32 MDA TTL + Explicit PS/2 Keyboard & Mouse)
// ============================================================================
fabgl::VGA2Controller DisplayController;
fabgl::PS2Controller  PS2Controller;
fabgl::Keyboard       Keyboard;
fabgl::Mouse          Mouse;
fabgl::Canvas         Canvas(&DisplayController);

// Offscreen 1-bit compositing buffers
FrameBuf1Bit    bgFB;        // Static table (felt + non-dragged cards + chrome)
FrameBuf1Bit    outFB;       // Final composed frame (bgFB + dragged stack + menus + cursor)
fabgl::Bitmap * gOutBmp = nullptr;

// ============================================================================
// Table Layout Constants (720 x 350 Hercules MDA)
// ============================================================================
static constexpr int MENUBAR_H   = 15;
static constexpr int STATUS_Y    = 336;
static constexpr int TOP_ROW_Y   = 21;
static constexpr int TAB_ROW_Y   = 107;
static constexpr int COL_X0      = 16;
static constexpr int COL_PITCH   = 100; // 7 * 88 + 6 * 12 = 688, centered with 16px margins
static constexpr int DRAG_STEP_Y = 16;

inline int colX(int col) { return COL_X0 + col * COL_PITCH; }

// ============================================================================
// Adjustable Mouse Sensitivity (Shared with MDA Desktop & MDA Paint via NVS)
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
// Klondike Solitaire Game State & Undo Stack (Types defined in mda_cards.h)
// ============================================================================
GameState gState;
bool      drawThreeMode = false; // false = Draw-1, true = Draw-3
int       cardBackIdx   = 0;
uint32_t  gameStartMs   = 0;
uint32_t  elapsedSec    = 0;
bool      gameStarted   = false;
uint32_t  rngSeed       = 0x51A7F3C9UL;

// 32-level Undo Stack (each GameState is ~1.1 KB -> 32 levels = ~35 KB)
static constexpr int MAX_UNDO = 32;
GameState * undoRing  = nullptr;
int         undoHead  = 0;
int         undoCount = 0;

// Dragging State
PileSource dragSrc      = SRC_NONE;
int        dragSrcIdx   = 0; // foundation 0..3 or tableau 0..6
int        dragStartRow = 0; // index in source pile where stack begins
Card       dragCards[14];
int        dragCount    = 0;
int        dragX        = 0;
int        dragY        = 0;
int        dragOffX     = 0;
int        dragOffY     = 0;

// Click / Double-click tracking
PileSource lastClickSrc = SRC_NONE;
int        lastClickIdx = -1;
uint32_t   lastClickMs  = 0;

// Hint highlight state
bool       hintActive   = false;
PileSource hintSrc      = SRC_NONE;
int        hintSrcIdx   = 0;
int        hintSrcRow   = 0;
PileSource hintDst      = SRC_NONE;
int        hintDstIdx   = 0;
uint32_t   hintUntilMs  = 0;

// Auto-complete & Victory Cascade state
bool     autoCompleteRunning = false;
uint32_t lastAutoMoveMs      = 0;

bool     victoryMode    = false;
int      vicCardSuit    = 0;
int      vicCardRank    = 13;
float    vicX           = 0, vicY = 0, vicVX = 0, vicVY = 0;
bool     vicBallActive  = false;
uint32_t lastVicFrameMs = 0;

// Input & UI State
int  curX = FB_W / 2;
int  curY = FB_H / 2;
bool btnLeft = false, btnRight = false, btnMid = false;
bool prevLeft = false, prevRight = false, prevMid = false;
bool shiftDown = false, ctrlDown = false;
bool altHeld = false, altUsedAsCombo = false;

bool showHelpModal  = false;
bool bgDirty        = true;
bool needPresent    = true;
char toastMsg[56]   = "";
uint32_t toastUntil = 0;

// ============================================================================
// Top Menu Bar (Mouse + Full Alt / F10 Keyboard Navigation)
// ============================================================================
struct MenuItem {
  const char * label;
  const char * shortcut;
  uint8_t      actionId;
};

enum MenuAction : uint8_t {
  ACT_NONE = 0,
  ACT_NEW_DEAL,
  ACT_UNDO,
  ACT_HINT,
  ACT_AUTOCOMPLETE,
  ACT_TOGGLE_DRAW,
  ACT_NEXT_BACK,
  ACT_MOUSE_SLOWER,
  ACT_MOUSE_FASTER,
  ACT_HELP,
  ACT_EXIT_DESKTOP
};

static const MenuItem MENU_GAME[] = {
  { "New Deal",        "F2",     ACT_NEW_DEAL },
  { "Undo Move",       "Ctrl+Z", ACT_UNDO },
  { "Hint",            "H",      ACT_HINT },
  { "Auto-Complete",   "A",      ACT_AUTOCOMPLETE },
  { "Exit to Desktop", "Alt+F4", ACT_EXIT_DESKTOP }
};

static const MenuItem MENU_OPT[] = {
  { "Toggle Draw 1/3", "F3",     ACT_TOGGLE_DRAW },
  { "Next Card Back",  "B",      ACT_NEXT_BACK },
  { "Mouse Slower",    "-",      ACT_MOUSE_SLOWER },
  { "Mouse Faster",    "+",      ACT_MOUSE_FASTER }
};

static const MenuItem MENU_HELP[] = {
  { "Rules & Keys",    "F1",     ACT_HELP }
};

struct MenuDef {
  const char *     title;
  int              x;
  int              w;
  const MenuItem * items;
  int              count;
};

static constexpr int NUM_MENUS = 3;
static const MenuDef MENUS[NUM_MENUS] = {
  { "Game",    106, 42, MENU_GAME, 5 },
  { "Options", 150, 58, MENU_OPT,  4 },
  { "Help",    210, 42, MENU_HELP, 1 }
};

int  openMenuIdx    = -1;
int  hoverMenuIdx   = -1;
bool menuBarFocused = false;
int  menuFocusIdx   = 0;
static constexpr int MENU_POP_W  = 156;
static constexpr int MENU_ITEM_H = 14;

// ============================================================================
// Forward Declarations
// ============================================================================
void dealNewGame();
void rebuildBackground();
void presentFrame();
void executeMenuAction(uint8_t act);
bool tryAutoFoundation(PileSource src, int srcIdx);

// ============================================================================
// Helpers & Mouse Sensitivity
// ============================================================================
inline bool ptInRect(int x, int y, int rx, int ry, int rw, int rh) {
  return (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
}

void setToast(const char * msg, uint32_t ms = 2400) {
  strncpy(toastMsg, msg, sizeof(toastMsg) - 1);
  toastMsg[sizeof(toastMsg) - 1] = '\0';
  toastUntil = millis() + ms;
  bgDirty = true;
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
// Klondike Rules, Layout Math & Undo
// ============================================================================
void pushUndo() {
  if (!undoRing) return;
  undoRing[undoHead] = gState;
  undoHead = (undoHead + 1) % MAX_UNDO;
  if (undoCount < MAX_UNDO) undoCount++;
}

bool performUndo() {
  if (!undoRing || undoCount <= 0) return false;
  undoHead = (undoHead - 1 + MAX_UNDO) % MAX_UNDO;
  undoCount--;
  gState = undoRing[undoHead];
  hintActive = false;
  autoCompleteRunning = false;
  bgDirty = true;
  return true;
}

// Computes adaptive Y coordinates for cards in a tableau column so tall stacks never clip off-screen!
void computeTableauCardYs(const Pile & p, int outY[24]) {
  if (p.count <= 0) return;
  int downCnt = 0, upCnt = 0;
  for (int i = 0; i < p.count - 1; ++i) {
    if (p.cards[i].faceUp) upCnt++;
    else                   downCnt++;
  }
  int downStep = 9;
  int upStep   = 16;
  int maxBottom = STATUS_Y - 4;
  int maxStartY = maxBottom - CARD_H;
  // Compress gracefully if column would exceed bottom status bar
  while (upStep > 10 && (TAB_ROW_Y + downCnt * downStep + upCnt * upStep) > maxStartY) {
    upStep--;
  }
  while (downStep > 5 && (TAB_ROW_Y + downCnt * downStep + upCnt * upStep) > maxStartY) {
    downStep--;
  }
  int y = TAB_ROW_Y;
  for (int i = 0; i < p.count; ++i) {
    outY[i] = y;
    y += p.cards[i].faceUp ? upStep : downStep;
  }
}

bool canPlaceOnFoundation(const Card & card, int fIdx) {
  const Pile & f = gState.found[fIdx];
  if (f.count == 0) return (card.rank == 1);
  const Card & top = f.cards[f.count - 1];
  return (top.suit == card.suit && card.rank == top.rank + 1);
}

bool canPlaceOnTableau(const Card & card, int tIdx) {
  const Pile & t = gState.tab[tIdx];
  if (t.count == 0) return (card.rank == 13);
  const Card & top = t.cards[t.count - 1];
  if (!top.faceUp) return false;
  return (isRedSuit(card.suit) != isRedSuit(top.suit)) && (card.rank + 1 == top.rank);
}

bool isVictory() {
  int total = 0;
  for (int i = 0; i < 4; ++i) total += gState.found[i].count;
  return (total == 52);
}

bool canAutoComplete() {
  if (gState.stock.count > 0 || gState.waste.count > 0) return false;
  for (int col = 0; col < 7; ++col) {
    for (int i = 0; i < gState.tab[col].count; ++i) {
      if (!gState.tab[col].cards[i].faceUp) return false;
    }
  }
  return !isVictory();
}

void startVictoryCascade() {
  victoryMode         = true;
  autoCompleteRunning = false;
  vicCardSuit         = 0;
  vicCardRank         = 13;
  vicBallActive       = false;
  rebuildBackground();
  outFB.copyFrom(bgFB);
  lastVicFrameMs = millis();
  needPresent = true;
}

void checkPostMoveState() {
  if (!gameStarted) {
    gameStarted = true;
    gameStartMs = millis();
  }
  // Flip any newly exposed face-down tableau top card
  for (int col = 0; col < 7; ++col) {
    Pile & t = gState.tab[col];
    if (t.count > 0 && !t.cards[t.count - 1].faceUp) {
      t.cards[t.count - 1].faceUp = true;
      gState.score += 5;
    }
  }
  if (isVictory()) {
    startVictoryCascade();
  }
}

void dealNewGame() {
  gState = GameState{};
  undoHead = undoCount = 0;
  dragSrc = SRC_NONE; dragCount = 0;
  hintActive = false;
  autoCompleteRunning = false;
  victoryMode = false;
  gameStarted = false;
  elapsedSec  = 0;

  // Build & Fisher-Yates shuffle 52-card deck
  Card deck[52];
  int k = 0;
  for (uint8_t s = 0; s < 4; ++s) {
    for (uint8_t r = 1; r <= 13; ++r) {
      deck[k++] = { r, s, false };
    }
  }
  rngSeed ^= (uint32_t)micros() ^ ((uint32_t)esp_random());
  for (int i = 51; i > 0; --i) {
    rngSeed = rngSeed * 1664525UL + 1013904223UL;
    int j = (int)((rngSeed >> 12) % (i + 1));
    Card tmp = deck[i]; deck[i] = deck[j]; deck[j] = tmp;
  }

  // Deal 7 tableau columns (1..7 cards, top card face-up)
  int idx = 0;
  for (int col = 0; col < 7; ++col) {
    for (int row = 0; row <= col; ++row) {
      Card c = deck[idx++];
      c.faceUp = (row == col);
      gState.tab[col].cards[gState.tab[col].count++] = c;
    }
  }
  // Remaining 24 cards go to Stock (face-down)
  while (idx < 52) {
    Card c = deck[idx++];
    c.faceUp = false;
    gState.stock.cards[gState.stock.count++] = c;
  }

  bgDirty = true;
  setToast(drawThreeMode ? "New Deal (Draw-3)" : "New Deal (Draw-1)", 2000);
}

void drawFromStock() {
  hintActive = false;
  if (gState.stock.count > 0) {
    pushUndo();
    int n = drawThreeMode ? min(3, gState.stock.count) : 1;
    gState.wasteVis = (uint8_t)n;
    for (int i = 0; i < n; ++i) {
      Card c = gState.stock.cards[--gState.stock.count];
      c.faceUp = true;
      gState.waste.cards[gState.waste.count++] = c;
    }
    gState.moves++;
    if (!gameStarted) { gameStarted = true; gameStartMs = millis(); }
    bgDirty = true;
  } else if (gState.waste.count > 0) {
    pushUndo();
    while (gState.waste.count > 0) {
      Card c = gState.waste.cards[--gState.waste.count];
      c.faceUp = false;
      gState.stock.cards[gState.stock.count++] = c;
    }
    gState.wasteVis = 0;
    if (!drawThreeMode) gState.score = max(0, gState.score - 20);
    gState.moves++;
    bgDirty = true;
  }
}

bool tryAutoFoundation(PileSource src, int srcIdx) {
  Card c;
  if (src == SRC_WASTE) {
    if (gState.waste.count <= 0) return false;
    c = gState.waste.cards[gState.waste.count - 1];
  } else if (src == SRC_TAB) {
    if (gState.tab[srcIdx].count <= 0) return false;
    c = gState.tab[srcIdx].cards[gState.tab[srcIdx].count - 1];
    if (!c.faceUp) return false;
  } else {
    return false;
  }

  for (int f = 0; f < 4; ++f) {
    if (canPlaceOnFoundation(c, f)) {
      pushUndo();
      if (src == SRC_WASTE) {
        gState.waste.count--;
        if (gState.wasteVis > 1) gState.wasteVis--;
        else gState.wasteVis = min(1, gState.waste.count);
      } else {
        gState.tab[srcIdx].count--;
      }
      gState.found[f].cards[gState.found[f].count++] = c;
      gState.score += 10;
      gState.moves++;
      hintActive = false;
      checkPostMoveState();
      bgDirty = true;
      return true;
    }
  }
  return false;
}

bool findHint() {
  // 1. Any top tableau or waste card to Foundation
  if (gState.waste.count > 0) {
    Card c = gState.waste.cards[gState.waste.count - 1];
    for (int f = 0; f < 4; ++f) {
      if (canPlaceOnFoundation(c, f)) {
        hintSrc = SRC_WASTE; hintSrcIdx = 0; hintSrcRow = gState.waste.count - 1;
        hintDst = SRC_FOUND; hintDstIdx = f;
        return true;
      }
    }
  }
  for (int col = 0; col < 7; ++col) {
    Pile & t = gState.tab[col];
    if (t.count > 0 && t.cards[t.count - 1].faceUp) {
      Card c = t.cards[t.count - 1];
      for (int f = 0; f < 4; ++f) {
        if (canPlaceOnFoundation(c, f)) {
          hintSrc = SRC_TAB;   hintSrcIdx = col; hintSrcRow = t.count - 1;
          hintDst = SRC_FOUND; hintDstIdx = f;
          return true;
        }
      }
    }
  }
  // 2. Tableau stack move that reveals a face-down card or frees an empty column
  for (int col = 0; col < 7; ++col) {
    Pile & t = gState.tab[col];
    int firstUp = -1;
    for (int i = 0; i < t.count; ++i) {
      if (t.cards[i].faceUp) { firstUp = i; break; }
    }
    if (firstUp < 0) continue;
    Card lead = t.cards[firstUp];
    // Don't suggest moving a King from row 0 onto another empty column!
    for (int dst = 0; dst < 7; ++dst) {
      if (dst == col) continue;
      if (lead.rank == 13 && firstUp == 0 && gState.tab[dst].count == 0) continue;
      if (canPlaceOnTableau(lead, dst)) {
        hintSrc = SRC_TAB; hintSrcIdx = col; hintSrcRow = firstUp;
        hintDst = SRC_TAB; hintDstIdx = dst;
        return true;
      }
    }
  }
  // 3. Waste card to Tableau
  if (gState.waste.count > 0) {
    Card c = gState.waste.cards[gState.waste.count - 1];
    for (int dst = 0; dst < 7; ++dst) {
      if (canPlaceOnTableau(c, dst)) {
        hintSrc = SRC_WASTE; hintSrcIdx = 0; hintSrcRow = gState.waste.count - 1;
        hintDst = SRC_TAB;   hintDstIdx = dst;
        return true;
      }
    }
  }
  return false;
}

// ============================================================================
// Offscreen Rendering into bgFB (Table + Chrome) & outFB (Dragged + Cursor)
// ============================================================================
void drawTopMenuBar(FrameBuf1Bit & fb) {
  fb.fillRect(0, 0, FB_W, MENUBAR_H, false);
  // Title badge
  fb.fillRect(2, 1, 98, 13, true);
  fb.drawText(6, 4, " MDA SOLITAIRE ", &fabgl::FONT_6x8, false);

  for (int i = 0; i < NUM_MENUS; ++i) {
    bool active = (openMenuIdx == i) || (menuBarFocused && openMenuIdx < 0 && menuFocusIdx == i);
    if (active) {
      fb.fillRect(MENUS[i].x, 1, MENUS[i].w, 13, true);
      fb.drawText(MENUS[i].x + 5, 4, MENUS[i].title, &fabgl::FONT_6x8, false);
      fb.drawHLine(MENUS[i].x + 5, MENUS[i].x + 10, 12, false);
    } else {
      fb.drawText(MENUS[i].x + 5, 4, MENUS[i].title, &fabgl::FONT_6x8, true);
      fb.drawHLine(MENUS[i].x + 5, MENUS[i].x + 10, 12, true);
    }
  }

  // Right-side Score, Moves, Time
  char stats[64];
  snprintf(stats, sizeof(stats), "SCORE:%-4d  MOVES:%-3d  TIME:%02u:%02u",
           gState.score, gState.moves,
           (unsigned)(elapsedSec / 60), (unsigned)(elapsedSec % 60));
  fb.drawText(FB_W - (int)strlen(stats) * 6 - 8, 4, stats, &fabgl::FONT_6x8, true);
  fb.drawHLine(0, FB_W - 1, MENUBAR_H, true);
}

void drawStatusBar(FrameBuf1Bit & fb) {
  fb.fillRect(0, STATUS_Y, FB_W, FB_H - STATUS_Y, false);
  fb.drawHLine(0, FB_W - 1, STATUS_Y, true);

  if (toastMsg[0] && millis() < toastUntil) {
    fb.fillRect(4, STATUS_Y + 2, 340, 11, true);
    fb.drawText(10, STATUS_Y + 4, toastMsg, &fabgl::FONT_6x8, false);
  } else {
    toastMsg[0] = '\0';
    char left[64];
    snprintf(left, sizeof(left), "%s | Undo:%d | Spd:%d/6 %s",
             drawThreeMode ? "Draw-3" : "Draw-1",
             undoCount, mouseSensIdx + 1,
             canAutoComplete() ? "| [A] AUTO-COMPLETE!" : "");
    fb.drawText(6, STATUS_Y + 4, left, &fabgl::FONT_6x8, true);
  }

  const char * hint = "Drag:Move | Dbl/R-Clk:Auto | Ctrl+Z:Undo | F1:Help | Alt+F4:Desk";
  if (openMenuIdx >= 0) hint = "Up/Dn:Item  L/R:Menu  Enter:Run  Esc:Close";
  else if (menuBarFocused) hint = "L/R:Menu  Dn/Enter:Open  G/O/H  Esc:Close";
  fb.drawText(FB_W - (int)strlen(hint) * 6 - 6, STATUS_Y + 4, hint, &fabgl::FONT_6x8, true);
}

void rebuildBackground() {
  // 1. Subtle CRT-friendly dark felt table
  bgFB.fillPatternRect(0, MENUBAR_H + 1, FB_W, STATUS_Y - MENUBAR_H - 1, FELT_PATTERN);

  // 2. Top Menu Bar & Bottom Status Bar
  drawTopMenuBar(bgFB);
  drawStatusBar(bgFB);

  // 3. Stock Pile (Col 0)
  int sx = colX(0);
  if (gState.stock.count > 0) {
    // Subtle 3D deck thickness edge
    if (gState.stock.count > 8) {
      bgFB.drawRoundRect(sx + 2, TOP_ROW_Y + 2, CARD_W, CARD_H, true);
    }
    drawCardBack(bgFB, sx, TOP_ROW_Y, cardBackIdx, true);
    // Crisp card count pill in bottom-center of stock
    char cntStr[8];
    snprintf(cntStr, sizeof(cntStr), "%d", gState.stock.count);
    int cw = (int)strlen(cntStr) * 6 + 6;
    bgFB.fillRect(sx + (CARD_W - cw) / 2, TOP_ROW_Y + CARD_H - 14, cw, 11, false);
    bgFB.drawRect(sx + (CARD_W - cw) / 2, TOP_ROW_Y + CARD_H - 14, cw, 11, true);
    bgFB.drawText(sx + (CARD_W - cw) / 2 + 3, TOP_ROW_Y + CARD_H - 12, cntStr, &fabgl::FONT_6x8, true);
  } else {
    drawEmptySlot(bgFB, sx, TOP_ROW_Y, 'O');
  }

  // 4. Waste Pile (Col 1 — fans up to 3 cards horizontally in Draw-3 mode!)
  int wx = colX(1);
  int wasteShow = gState.waste.count;
  if (dragSrc == SRC_WASTE && dragCount > 0) wasteShow--;
  if (wasteShow <= 0) {
    drawEmptySlot(bgFB, wx, TOP_ROW_Y, ' ');
  } else {
    int fan = drawThreeMode ? min((int)max((int)gState.wasteVis, 1), wasteShow) : 1;
    fan = min(fan, 3);
    int first = wasteShow - fan;
    for (int i = 0; i < fan; ++i) {
      int cardIdx = first + i;
      bool hl = (hintActive && hintSrc == SRC_WASTE && i == fan - 1);
      drawCardFace(bgFB, wx + i * 20, TOP_ROW_Y, gState.waste.cards[cardIdx], true, hl);
    }
  }

  // 5. 4 Foundation Piles (Cols 3..6)
  for (int f = 0; f < 4; ++f) {
    int fx = colX(3 + f);
    int cnt = gState.found[f].count;
    if (dragSrc == SRC_FOUND && dragSrcIdx == f && dragCount > 0) cnt--;
    bool hl = (hintActive && hintDst == SRC_FOUND && hintDstIdx == f);
    if (cnt <= 0) {
      drawEmptySlot(bgFB, fx, TOP_ROW_Y, 'A');
      if (hl) bgFB.drawRoundRect(fx + 1, TOP_ROW_Y + 1, CARD_W - 2, CARD_H - 2, true);
    } else {
      drawCardFace(bgFB, fx, TOP_ROW_Y, gState.found[f].cards[cnt - 1], true, hl);
    }
  }

  // 6. 7 Tableau Columns (Cols 0..6)
  for (int col = 0; col < 7; ++col) {
    int tx = colX(col);
    const Pile & t = gState.tab[col];
    int showCnt = t.count;
    if (dragSrc == SRC_TAB && dragSrcIdx == col && dragCount > 0) {
      showCnt = dragStartRow;
    }
    if (showCnt <= 0) {
      drawEmptySlot(bgFB, tx, TAB_ROW_Y, 'K');
      if (hintActive && hintDst == SRC_TAB && hintDstIdx == col) {
        bgFB.drawRoundRect(tx + 1, TAB_ROW_Y + 1, CARD_W - 2, CARD_H - 2, true);
      }
    } else {
      int ys[24];
      computeTableauCardYs(t, ys);
      for (int i = 0; i < showCnt; ++i) {
        bool hl = (hintActive &&
                   ((hintSrc == SRC_TAB && hintSrcIdx == col && i == hintSrcRow) ||
                    (hintDst == SRC_TAB && hintDstIdx == col && i == showCnt - 1)));
        if (t.cards[i].faceUp) {
          drawCardFace(bgFB, tx, ys[i], t.cards[i], false, hl);
        } else {
          drawCardBack(bgFB, tx, ys[i], cardBackIdx, false);
        }
      }
    }
  }

  bgDirty     = false;
  needPresent = true;
}

void drawMenuPopupOnFB(FrameBuf1Bit & fb) {
  if (openMenuIdx < 0 || openMenuIdx >= NUM_MENUS) return;
  const MenuDef & m = MENUS[openMenuIdx];
  int px = m.x;
  int py = MENUBAR_H + 1;
  int ph = m.count * MENU_ITEM_H + 4;

  fb.fillRect(px + 2, py + 2, MENU_POP_W, ph, false);
  fb.fillRect(px, py, MENU_POP_W, ph, false);
  fb.drawRect(px, py, MENU_POP_W, ph, true);
  fb.drawHLine(px + 2, px + MENU_POP_W, py + ph, true);
  fb.drawVLine(px + MENU_POP_W, py + 2, py + ph, true);

  for (int i = 0; i < m.count; ++i) {
    int iy = py + 2 + i * MENU_ITEM_H;
    bool hov = (i == hoverMenuIdx);
    if (hov) {
      fb.fillRect(px + 2, iy, MENU_POP_W - 4, MENU_ITEM_H, true);
      fb.drawText(px + 6, iy + 3, m.items[i].label, &fabgl::FONT_6x8, false);
      if (m.items[i].shortcut[0]) {
        int sw = (int)strlen(m.items[i].shortcut) * 6;
        fb.drawText(px + MENU_POP_W - sw - 6, iy + 3, m.items[i].shortcut, &fabgl::FONT_6x8, false);
      }
    } else {
      fb.drawText(px + 6, iy + 3, m.items[i].label, &fabgl::FONT_6x8, true);
      if (m.items[i].shortcut[0]) {
        int sw = (int)strlen(m.items[i].shortcut) * 6;
        fb.drawText(px + MENU_POP_W - sw - 6, iy + 3, m.items[i].shortcut, &fabgl::FONT_6x8, true);
      }
    }
  }
}

void drawHelpModalOnFB(FrameBuf1Bit & fb) {
  int mx = 110, my = 38, mw = 500, mh = 270;
  fb.fillRect(mx + 3, my + 3, mw, mh, false);
  fb.fillRect(mx, my, mw, mh, false);
  fb.drawRect(mx, my, mw, mh, true);
  fb.drawRect(mx + 2, my + 2, mw - 4, mh - 4, true);

  fb.fillRect(mx + 4, my + 4, mw - 8, 16, true);
  fb.drawText(mx + 128, my + 8, "MDA SOLITAIRE - RULES & KEYS", &fabgl::FONT_8x8, false);

  const char * lines[] = {
    "1-BIT MONOCHROME SUIT GUIDE (No Grayscale):",
    "  BLACK Suits (Spades & Clubs)   : SOLID FILLED black pips + Bold rank",
    "  RED   Suits (Hearts & Diamonds): HOLLOW OUTLINED pips + Underlined rank",
    "",
    "MOUSE CONTROLS (Zero-Flicker Full-Card Dragging):",
    "  Left Drag           : Smoothly drag single card or face-up stack",
    "  Click Stock (Top-L) : Draw 1 or 3 cards / Recycle empty stock",
    "  Dbl-Click / R-Click : Auto-send card straight to Foundation",
    "",
    "KEYBOARD SHORTCUTS:",
    "  F2 / Ctrl+N : New Deal           Ctrl+Z : Undo Move (32 levels)",
    "  Space / D   : Draw from Stock    H      : Highlight Hint Move",
    "  A           : Auto-Complete (when all cards are face-up)",
    "  F3          : Toggle Draw-1 / Draw-3   B : Cycle Card Back",
    "  Alt / F10   : Top Menu Bar             - / + : Mouse Sensitivity",
    "  Alt+F4      : Return to MDA Desktop    F1 / Esc : Close Help",
    "",
    "             Click Anywhere or Press Esc / F1 to Resume"
  };
  int y = my + 28;
  for (int i = 0; i < (int)(sizeof(lines) / sizeof(lines[0])); ++i) {
    fb.drawText(mx + 14, y, lines[i], &fabgl::FONT_6x8, true);
    y += 12;
  }
}

// Compute best legal drop pile for dragged stack (maximizes overlap area!)
bool findBestDropTarget(int rx, int ry, PileSource & outDst, int & outIdx) {
  if (dragCount <= 0) return false;
  const Card & lead = dragCards[0];
  int bestArea = 0;
  outDst = SRC_NONE;
  outIdx = -1;

  auto overlapArea = [](int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) -> int {
    int ox = min(ax + aw, bx + bw) - max(ax, bx);
    int oy = min(ay + ah, by + bh) - max(ay, by);
    return (ox > 0 && oy > 0) ? (ox * oy) : 0;
  };

  // Check 4 Foundations (only single cards can drop on Foundations)
  if (dragCount == 1) {
    for (int f = 0; f < 4; ++f) {
      if (dragSrc == SRC_FOUND && dragSrcIdx == f) continue;
      if (!canPlaceOnFoundation(lead, f)) continue;
      int area = overlapArea(rx, ry, CARD_W, CARD_H, colX(3 + f), TOP_ROW_Y, CARD_W, CARD_H);
      if (area > bestArea) {
        bestArea = area; outDst = SRC_FOUND; outIdx = f;
      }
    }
  }

  // Check 7 Tableau columns
  for (int col = 0; col < 7; ++col) {
    if (dragSrc == SRC_TAB && dragSrcIdx == col) continue;
    if (!canPlaceOnTableau(lead, col)) continue;
    int tx = colX(col);
    int ty = TAB_ROW_Y;
    if (gState.tab[col].count > 0) {
      int ys[24];
      computeTableauCardYs(gState.tab[col], ys);
      ty = ys[gState.tab[col].count - 1];
    }
    int area = overlapArea(rx, ry, CARD_W, CARD_H, tx, ty, CARD_W, CARD_H);
    if (area > bestArea) {
      bestArea = area; outDst = SRC_TAB; outIdx = col;
    }
  }
  return (bestArea > 200); // Require meaningful card overlap
}

// Atomic VSync Double-Buffered Present — ZERO Flicker, ZERO Tearing!
void presentFrame() {
  if (!victoryMode) {
    if (bgDirty) rebuildBackground();
    outFB.copyFrom(bgFB);

    // Draw dragged full card(s) with shadow & legal-target snap ring
    if (dragCount > 0) {
      PileSource dstType;
      int dstIdx;
      if (findBestDropTarget(dragX, dragY, dstType, dstIdx)) {
        int hx = (dstType == SRC_FOUND) ? colX(3 + dstIdx) : colX(dstIdx);
        int hy = TOP_ROW_Y;
        if (dstType == SRC_TAB) {
          if (gState.tab[dstIdx].count > 0) {
            int ys[24];
            computeTableauCardYs(gState.tab[dstIdx], ys);
            hy = ys[gState.tab[dstIdx].count - 1];
          } else {
            hy = TAB_ROW_Y;
          }
        }
        outFB.drawDashedRoundRect(hx - 2, hy - 2, CARD_W + 4, CARD_H + 4, true);
      }

      for (int i = 0; i < dragCount; ++i) {
        drawCardFace(outFB, dragX, dragY + i * DRAG_STEP_Y, dragCards[i], true, false);
      }
    }

    if (openMenuIdx >= 0) drawMenuPopupOnFB(outFB);
    if (showHelpModal)    drawHelpModalOnFB(outFB);

    // Draw crisp 1-bit cursor in exact lockstep with dragged cards!
    outFB.drawCursor(curX, curY, dragCount > 0);
  }

  // Push complete offscreen frame to invisible back buffer & flip at VSync!
  Canvas.setBrushColor(fabgl::Color::Black);
  Canvas.fillRectangle(0, 0, FB_W - 1, FB_H - 1);
  Canvas.drawBitmap(0, 0, gOutBmp);
  Canvas.swapBuffers();
  needPresent = false;
}

// ============================================================================
// Victory Bouncing Card Cascade Animation (Classic Windows 95 Finale!)
// ============================================================================
void stepVictoryCascade() {
  if (millis() - lastVicFrameMs < 20) return;
  lastVicFrameMs = millis();

  if (!vicBallActive) {
    if (vicCardRank < 1) {
      vicCardRank = 13;
      vicCardSuit = 0;
    }
    vicX  = (float)colX(3 + vicCardSuit);
    vicY  = (float)TOP_ROW_Y;
    rngSeed = rngSeed * 1664525UL + 1013904223UL;
    float spd = 2.6f + (float)((rngSeed >> 16) % 40) * 0.1f;
    vicVX = ((rngSeed & 1) ? 1.0f : -1.0f) * spd;
    vicVY = -2.0f - (float)((rngSeed >> 8) % 35) * 0.1f;
    vicBallActive = true;
  }

  vicX  += vicVX;
  vicVY += 0.55f;
  vicY  += vicVY;

  float floorY = (float)(STATUS_Y - CARD_H - 2);
  if (vicY >= floorY) {
    vicY  = floorY;
    vicVY = -vicVY * 0.80f;
    if (fabsf(vicVY) < 2.2f) vicVY = -4.5f;
  }

  Card c = { (uint8_t)vicCardRank, (uint8_t)vicCardSuit, true };
  drawCardFace(outFB, (int)vicX, (int)vicY, c, false, false);

  // Crisp victory banner centered over cascade
  int bx = 200, by = 142, bw = 320, bh = 58;
  outFB.fillRect(bx, by, bw, bh, false);
  outFB.drawRect(bx, by, bw, bh, true);
  outFB.drawRect(bx + 2, by + 2, bw - 4, bh - 4, true);
  outFB.drawText(bx + 92, by + 11, "YOU WIN!", &fabgl::FONT_8x8, true, true);
  char sLine[48];
  snprintf(sLine, sizeof(sLine), "Score: %d | Moves: %d | %02u:%02u",
           gState.score, gState.moves, (unsigned)(elapsedSec / 60), (unsigned)(elapsedSec % 60));
  outFB.drawText(bx + (bw - (int)strlen(sLine) * 6) / 2, by + 27, sLine, &fabgl::FONT_6x8, true);
  outFB.drawText(bx + 42, by + 42, "Click or Press Any Key for New Deal", &fabgl::FONT_6x8, true);

  if (vicX < -CARD_W || vicX > FB_W) {
    vicBallActive = false;
    vicCardSuit++;
    if (vicCardSuit >= 4) {
      vicCardSuit = 0;
      vicCardRank--;
    }
  }
  needPresent = true;
}

// ============================================================================
// Menu Actions & Mouse / Keyboard Interaction
// ============================================================================
void closeMenus() {
  if (openMenuIdx < 0 && !menuBarFocused) return;
  openMenuIdx    = -1;
  hoverMenuIdx   = -1;
  menuBarFocused = false;
  bgDirty = true;
}

void openMenuByIdx(int idx) {
  idx = (idx + NUM_MENUS) % NUM_MENUS;
  menuBarFocused = true;
  menuFocusIdx   = idx;
  openMenuIdx    = idx;
  hoverMenuIdx   = 0;
  bgDirty        = true;
}

void executeMenuAction(uint8_t act) {
  closeMenus();
  switch (act) {
    case ACT_NEW_DEAL:
      dealNewGame();
      break;
    case ACT_UNDO:
      if (!performUndo()) setToast("Nothing to Undo", 1600);
      break;
    case ACT_HINT:
      if (findHint()) {
        hintActive  = true;
        hintUntilMs = millis() + 2600;
        bgDirty     = true;
        setToast("Hint Highlighted!", 2200);
      } else {
        setToast("No Direct Moves Found - Draw from Stock", 2200);
      }
      break;
    case ACT_AUTOCOMPLETE:
      if (canAutoComplete()) {
        autoCompleteRunning = true;
        lastAutoMoveMs = 0;
        setToast("Auto-Completing...", 2000);
      } else {
        setToast("Reveal all cards & empty stock first", 2200);
      }
      break;
    case ACT_TOGGLE_DRAW:
      drawThreeMode = !drawThreeMode;
      dealNewGame();
      break;
    case ACT_NEXT_BACK:
      cardBackIdx = (cardBackIdx + 1) % NUM_CARD_BACKS;
      bgDirty = true;
      {
        char msg[44];
        snprintf(msg, sizeof(msg), "Card Back: %s", CARD_BACK_NAMES[cardBackIdx]);
        setToast(msg, 2000);
      }
      break;
    case ACT_MOUSE_SLOWER:
      if (mouseSensIdx > 0) mouseSensIdx--;
      applyMouseSensitivity(true);
      break;
    case ACT_MOUSE_FASTER:
      if (mouseSensIdx < NUM_MOUSE_SENS - 1) mouseSensIdx++;
      applyMouseSensitivity(true);
      break;
    case ACT_HELP:
      showHelpModal = !showHelpModal;
      needPresent = true;
      break;
    case ACT_EXIT_DESKTOP:
      setToast("Returning to MDA Desktop...", 1200);
      presentFrame();
      mdaReturnToDesktop();
      break;
    default:
      break;
  }
}

void onTableLeftDown(int sx, int sy) {
  if (victoryMode) { dealNewGame(); return; }
  if (showHelpModal) { showHelpModal = false; needPresent = true; return; }

  // 1. Open pull-down menu hit test
  if (openMenuIdx >= 0) {
    const MenuDef & m = MENUS[openMenuIdx];
    int py = MENUBAR_H + 1;
    int ph = m.count * MENU_ITEM_H + 4;
    if (ptInRect(sx, sy, m.x, py, MENU_POP_W, ph)) {
      int idx = (sy - (py + 2)) / MENU_ITEM_H;
      if (idx >= 0 && idx < m.count) executeMenuAction(m.items[idx].actionId);
      else closeMenus();
      return;
    }
    if (sy < MENUBAR_H) {
      for (int i = 0; i < NUM_MENUS; ++i) {
        if (ptInRect(sx, sy, MENUS[i].x, 0, MENUS[i].w, MENUBAR_H)) {
          if (i == openMenuIdx) closeMenus();
          else openMenuByIdx(i);
          return;
        }
      }
    }
    closeMenus();
    return;
  }

  // 2. Top Menu Bar click
  if (sy < MENUBAR_H) {
    for (int i = 0; i < NUM_MENUS; ++i) {
      if (ptInRect(sx, sy, MENUS[i].x, 0, MENUS[i].w, MENUBAR_H)) {
        openMenuByIdx(i);
        return;
      }
    }
    return;
  }

  if (sy >= STATUS_Y) return;
  hintActive = false;

  // 3. Stock Pile click (Col 0)
  if (ptInRect(sx, sy, colX(0), TOP_ROW_Y, CARD_W, CARD_H)) {
    drawFromStock();
    return;
  }

  // 4. Waste Pile pick-up / double-click (Col 1)
  if (gState.waste.count > 0) {
    int fan = drawThreeMode ? min((int)max((int)gState.wasteVis, 1), gState.waste.count) : 1;
    fan = min(fan, 3);
    int topX = colX(1) + (fan - 1) * 20;
    if (ptInRect(sx, sy, topX, TOP_ROW_Y, CARD_W, CARD_H)) {
      uint32_t now = millis();
      bool dbl = (lastClickSrc == SRC_WASTE && (now - lastClickMs) <= 400);
      lastClickSrc = SRC_WASTE; lastClickIdx = 0; lastClickMs = now;
      if (dbl && tryAutoFoundation(SRC_WASTE, 0)) return;

      dragSrc      = SRC_WASTE;
      dragSrcIdx   = 0;
      dragStartRow = gState.waste.count - 1;
      dragCards[0] = gState.waste.cards[gState.waste.count - 1];
      dragCount    = 1;
      dragX        = topX;
      dragY        = TOP_ROW_Y;
      dragOffX     = sx - topX;
      dragOffY     = sy - TOP_ROW_Y;
      bgDirty      = true;
      return;
    }
  }

  // 5. Foundation Pile pick-up (Cols 3..6)
  for (int f = 0; f < 4; ++f) {
    if (gState.found[f].count > 0 && ptInRect(sx, sy, colX(3 + f), TOP_ROW_Y, CARD_W, CARD_H)) {
      dragSrc      = SRC_FOUND;
      dragSrcIdx   = f;
      dragStartRow = gState.found[f].count - 1;
      dragCards[0] = gState.found[f].cards[gState.found[f].count - 1];
      dragCount    = 1;
      dragX        = colX(3 + f);
      dragY        = TOP_ROW_Y;
      dragOffX     = sx - dragX;
      dragOffY     = sy - dragY;
      bgDirty      = true;
      return;
    }
  }

  // 6. Tableau Columns pick-up / double-click (check top-most card first!)
  for (int col = 0; col < 7; ++col) {
    int tx = colX(col);
    const Pile & t = gState.tab[col];
    if (t.count <= 0 || sx < tx || sx >= tx + CARD_W) continue;
    int ys[24];
    computeTableauCardYs(t, ys);
    for (int i = t.count - 1; i >= 0; --i) {
      int cardBot = (i == t.count - 1) ? (ys[i] + CARD_H) : ys[i + 1];
      if (sy >= ys[i] && sy < cardBot) {
        if (!t.cards[i].faceUp) return;
        // Double-click on top card -> auto-send to Foundation
        if (i == t.count - 1) {
          uint32_t now = millis();
          bool dbl = (lastClickSrc == SRC_TAB && lastClickIdx == col && (now - lastClickMs) <= 400);
          lastClickSrc = SRC_TAB; lastClickIdx = col; lastClickMs = now;
          if (dbl && tryAutoFoundation(SRC_TAB, col)) return;
        }
        // Pick up entire sub-stack from i..t.count-1!
        dragSrc      = SRC_TAB;
        dragSrcIdx   = col;
        dragStartRow = i;
        dragCount    = t.count - i;
        for (int k = 0; k < dragCount; ++k) dragCards[k] = t.cards[i + k];
        dragX        = tx;
        dragY        = ys[i];
        dragOffX     = sx - tx;
        dragOffY     = sy - ys[i];
        bgDirty      = true;
        return;
      }
    }
  }
}

void onTableRightDown(int sx, int sy) {
  if (victoryMode) { dealNewGame(); return; }
  if (openMenuIdx >= 0) { closeMenus(); return; }
  if (showHelpModal) { showHelpModal = false; needPresent = true; return; }
  if (dragCount > 0) return;

  // Right-click on Waste top card -> auto-foundation
  if (gState.waste.count > 0) {
    int fan = drawThreeMode ? min((int)max((int)gState.wasteVis, 1), gState.waste.count) : 1;
    fan = min(fan, 3);
    int topX = colX(1) + (fan - 1) * 20;
    if (ptInRect(sx, sy, topX, TOP_ROW_Y, CARD_W, CARD_H)) {
      tryAutoFoundation(SRC_WASTE, 0);
      return;
    }
  }
  // Right-click on any Tableau top card -> auto-foundation
  for (int col = 0; col < 7; ++col) {
    int tx = colX(col);
    const Pile & t = gState.tab[col];
    if (t.count <= 0 || sx < tx || sx >= tx + CARD_W) continue;
    int ys[24];
    computeTableauCardYs(t, ys);
    if (sy >= ys[t.count - 1] && sy < ys[t.count - 1] + CARD_H) {
      tryAutoFoundation(SRC_TAB, col);
      return;
    }
  }
}

void onTableLeftUp() {
  if (dragCount <= 0) return;

  PileSource dstType;
  int dstIdx;
  if (findBestDropTarget(dragX, dragY, dstType, dstIdx)) {
    pushUndo();
    // Remove from source
    if (dragSrc == SRC_WASTE) {
      gState.waste.count--;
      if (gState.wasteVis > 1) gState.wasteVis--;
      else gState.wasteVis = min(1, gState.waste.count);
    } else if (dragSrc == SRC_FOUND) {
      gState.found[dragSrcIdx].count--;
    } else if (dragSrc == SRC_TAB) {
      gState.tab[dragSrcIdx].count = dragStartRow;
    }

    // Place onto target & update score
    if (dstType == SRC_FOUND) {
      gState.found[dstIdx].cards[gState.found[dstIdx].count++] = dragCards[0];
      if (dragSrc != SRC_FOUND) gState.score += 10;
    } else if (dstType == SRC_TAB) {
      for (int i = 0; i < dragCount; ++i) {
        gState.tab[dstIdx].cards[gState.tab[dstIdx].count++] = dragCards[i];
      }
      if (dragSrc == SRC_WASTE) gState.score += 5;
      else if (dragSrc == SRC_FOUND) gState.score = max(0, gState.score - 15);
    }
    gState.moves++;
    dragSrc   = SRC_NONE;
    dragCount = 0;
    checkPostMoveState();
    bgDirty = true;
  } else {
    // Dropped on invalid spot -> smoothly snap back to source pile
    dragSrc   = SRC_NONE;
    dragCount = 0;
    bgDirty   = true;
  }
}

// ============================================================================
// PS/2 Mouse & Keyboard Processing
// ============================================================================
void processMouse() {
  bool moved    = false;
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

      int nx = constrain(curX + stepX, 0, FB_W - 1);
      int ny = constrain(curY + stepY, 0, FB_H - 1);
      if (nx != curX || ny != curY) {
        curX  = nx;
        curY  = ny;
        moved = true;
      }
    }
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
    needPresent = true;
    if (dragCount > 0) {
      dragX = constrain(curX - dragOffX, 2, FB_W - CARD_W - 4);
      dragY = constrain(curY - dragOffY, MENUBAR_H + 2, STATUS_Y - CARD_H - 2);
    }
    if (openMenuIdx >= 0) {
      const MenuDef & m = MENUS[openMenuIdx];
      int py = MENUBAR_H + 1;
      int nh = -1;
      if (ptInRect(curX, curY, m.x, py + 2, MENU_POP_W, m.count * MENU_ITEM_H)) {
        nh = (curY - (py + 2)) / MENU_ITEM_H;
      }
      hoverMenuIdx = nh;
    }
  }

  if (btnLeft && !prevLeft)   onTableLeftDown(curX, curY);
  if (btnRight && !prevRight) onTableRightDown(curX, curY);
  if (!btnLeft && prevLeft)   onTableLeftUp();

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
      if (isAlt && altHeld && !altUsedAsCombo && !showHelpModal && !victoryMode) {
        if (openMenuIdx >= 0 || menuBarFocused) closeMenus();
        else { menuBarFocused = true; menuFocusIdx = 0; bgDirty = true; }
      }
      if (isAlt) { altHeld = false; altUsedAsCombo = false; }
      continue;
    }

    if (isAlt) {
      if (!altHeld) { altHeld = true; altUsedAsCombo = false; }
      continue;
    }

    if (victoryMode) { dealNewGame(); continue; }
    if (showHelpModal) {
      if (item.vk == fabgl::VK_ESCAPE || item.vk == fabgl::VK_F1 || item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_SPACE) {
        showHelpModal = false;
        needPresent = true;
      }
      continue;
    }

    // Alt combos: Alt+F4 exits to MDA Desktop; Alt+G/O/H opens top menus
    if (item.LALT || item.RALT || altHeld) {
      altUsedAsCombo = true;
      if (item.vk == fabgl::VK_F4) { executeMenuAction(ACT_EXIT_DESKTOP); continue; }
      char c = (char)toupper(item.ASCII);
      if (c == 'G') { openMenuByIdx(0); continue; }
      if (c == 'O') { openMenuByIdx(1); continue; }
      if (c == 'H') { openMenuByIdx(2); continue; }
    }

    if (item.vk == fabgl::VK_F10) {
      if (openMenuIdx >= 0 || menuBarFocused) closeMenus();
      else { menuBarFocused = true; menuFocusIdx = 0; bgDirty = true; }
      continue;
    }
    if (item.vk == fabgl::VK_F1) { executeMenuAction(ACT_HELP); continue; }
    if (item.vk == fabgl::VK_F2) { executeMenuAction(ACT_NEW_DEAL); continue; }
    if (item.vk == fabgl::VK_F3) { executeMenuAction(ACT_TOGGLE_DRAW); continue; }
    if (item.vk == fabgl::VK_ESCAPE) {
      if (openMenuIdx >= 0 || menuBarFocused) closeMenus();
      else if (dragCount > 0) { dragSrc = SRC_NONE; dragCount = 0; bgDirty = true; }
      continue;
    }

    // Menu Bar keyboard navigation
    if (menuBarFocused || openMenuIdx >= 0) {
      if (item.vk == fabgl::VK_LEFT) {
        int nxt = ((openMenuIdx >= 0 ? openMenuIdx : menuFocusIdx) + NUM_MENUS - 1) % NUM_MENUS;
        if (openMenuIdx >= 0) openMenuByIdx(nxt);
        else { menuFocusIdx = nxt; bgDirty = true; }
      } else if (item.vk == fabgl::VK_RIGHT) {
        int nxt = ((openMenuIdx >= 0 ? openMenuIdx : menuFocusIdx) + 1) % NUM_MENUS;
        if (openMenuIdx >= 0) openMenuByIdx(nxt);
        else { menuFocusIdx = nxt; bgDirty = true; }
      } else if (item.vk == fabgl::VK_DOWN) {
        if (openMenuIdx < 0) openMenuByIdx(menuFocusIdx);
        else { hoverMenuIdx = (hoverMenuIdx + 1) % MENUS[openMenuIdx].count; needPresent = true; }
      } else if (item.vk == fabgl::VK_UP && openMenuIdx >= 0) {
        const MenuDef & m = MENUS[openMenuIdx];
        hoverMenuIdx = (hoverMenuIdx <= 0) ? (m.count - 1) : (hoverMenuIdx - 1);
        needPresent = true;
      } else if (item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_KP_ENTER || item.vk == fabgl::VK_SPACE) {
        if (openMenuIdx < 0) openMenuByIdx(menuFocusIdx);
        else executeMenuAction(MENUS[openMenuIdx].items[max(0, hoverMenuIdx)].actionId);
      } else {
        closeMenus();
      }
      continue;
    }

    // Ctrl shortcuts
    if (item.CTRL) {
      char c = (char)tolower(item.ASCII);
      if (item.vk == fabgl::VK_z || item.vk == fabgl::VK_Z || c == 'z' || item.ASCII == 26) {
        executeMenuAction(ACT_UNDO); continue;
      }
      if (item.vk == fabgl::VK_n || item.vk == fabgl::VK_N || c == 'n' || item.ASCII == 14) {
        executeMenuAction(ACT_NEW_DEAL); continue;
      }
      continue;
    }

    // Single-key gameplay shortcuts
    char ch = (char)toupper(item.ASCII);
    if (item.vk == fabgl::VK_SPACE || ch == 'D') { drawFromStock(); continue; }
    if (ch == 'H') { executeMenuAction(ACT_HINT); continue; }
    if (ch == 'A') { executeMenuAction(ACT_AUTOCOMPLETE); continue; }
    if (ch == 'B') { executeMenuAction(ACT_NEXT_BACK); continue; }
    if (ch == '-' || ch == '_') { executeMenuAction(ACT_MOUSE_SLOWER); continue; }
    if (ch == '=' || ch == '+') { executeMenuAction(ACT_MOUSE_FASTER); continue; }
  }
}

// ============================================================================
// Arduino setup() & loop()
// ============================================================================
void setup() {
  Serial.begin(115200);

  // If launched from MDA Desktop (ota_1), arm ota_0 so Reset/Alt+F4 returns to Desktop!
  mdaArmDesktopOnReset();
  mouseSensIdx = mdaLoadMouseSens(2, NUM_MOUSE_SENS - 1);

  delay(500);

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

  // LilyGO VGA32 TTL Monochrome MDA Video (720x350 @ 50Hz Hercules)
  // ENABLE HARDWARE VSYNC DOUBLE-BUFFERING (doubleBuffered = true) FOR ZERO FLICKER!
  DisplayController.begin(GPIO_NUM_21, GPIO_NUM_21, GPIO_NUM_21, GPIO_NUM_23, GPIO_NUM_15);
  DisplayController.setResolution("\"MDA_50HZ_LOCK\" 16.000 720 736 848 880 350 352 356 364 +HSync -VSync",
                                  -1, -1, true);
  DisplayController.setPaletteItem(0, fabgl::RGB888(0, 0, 0));
  DisplayController.setPaletteItem(1, fabgl::RGB888(255, 255, 255));

  // Allocate two 30.8 KB offscreen 1-bit buffers + Undo ring
  if (!bgFB.alloc() || !outFB.alloc()) {
    Canvas.selectFont(&fabgl::FONT_8x14);
    Canvas.setBrushColor(fabgl::Color::Black);
    Canvas.setPenColor(fabgl::Color::White);
    Canvas.clear();
    Canvas.drawText(20, 20, "MDA SOLITAIRE ERROR: Out of memory!");
    Canvas.swapBuffers();
    while (true) delay(1000);
  }
  undoRing = (GameState *)malloc(sizeof(GameState) * MAX_UNDO);

  gOutBmp = new fabgl::Bitmap(FB_W, FB_H, outFB.data,
                              fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255));

  attachVirtualMouse();
  // We draw our own VSync-locked 1-bit cursor inside outFB so card+cursor move in unison!
  DisplayController.setMouseCursor(nullptr);

  Serial.printf("\n=== MDA Solitaire Ready | Free Heap: %u KB ===\n",
                (unsigned)(ESP.getFreeHeap() / 1024));

  dealNewGame();
  presentFrame();
}

void loop() {
  processMouse();
  processKeyboard();

  // Auto-complete animation step (moves one card every 90 ms)
  if (autoCompleteRunning && !victoryMode && (millis() - lastAutoMoveMs >= 90)) {
    lastAutoMoveMs = millis();
    bool moved = false;
    for (int col = 0; col < 7; ++col) {
      if (tryAutoFoundation(SRC_TAB, col)) { moved = true; break; }
    }
    if (!moved) autoCompleteRunning = false;
  }

  // Victory bouncing card cascade
  if (victoryMode) {
    stepVictoryCascade();
  }

  // Expire Hint highlight or Status Toast
  if (hintActive && millis() >= hintUntilMs) {
    hintActive = false;
    bgDirty = true;
  }
  if (toastMsg[0] && millis() >= toastUntil) {
    toastMsg[0] = '\0';
    bgDirty = true;
  }

  // Live game timer (1 Hz)
  if (gameStarted && !victoryMode) {
    uint32_t sec = (millis() - gameStartMs) / 1000;
    if (sec != elapsedSec) {
      elapsedSec = sec;
      bgDirty = true;
    }
  }

  // Present cleanly at VSync whenever anything changed!
  if (bgDirty || needPresent) {
    presentFrame();
  } else {
    delay(4);
  }
}
