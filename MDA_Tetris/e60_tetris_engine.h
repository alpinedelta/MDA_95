#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ============================================================================
// Elektronika-60 (1984) Character-Only Terminal Tetris Engine
// Strictly pure ASCII 80 x 25 character grid (no graphics primitives!)
//
// Authentic 1984 Elektronika-60 Visual Style:
//   - Playfield walls  : <! ... !>
//   - Playfield floor  : <!====================!>
//   - Playfield base   :   \/\/\/\/\/\/\/\/\/\/
//   - Occupied blocks  : []
//   - Empty grid cells :  .  (toggleable via 0 / G)
// ============================================================================

static constexpr int TERM_COLS = 80;
static constexpr int TERM_ROWS = 25;

static constexpr int WELL_W    = 10;
static constexpr int WELL_H    = 20;

// Centered well coordinates inside 80x25 terminal
static constexpr int WELL_COL  = 30; // Left wall '<!' at 28..29, interior 30..49, right wall '!>' at 50..51
static constexpr int WELL_ROW  = 2;  // Rows 2..21 playable, row 22 floor, row 23 chevrons

enum E60Mode : uint8_t {
  E60_TITLE = 0,
  E60_PLAYING,
  E60_LINE_FLASH,
  E60_PAUSED,
  E60_GAMEOVER
};

// 7 classic tetrominoes: I, O, T, S, Z, J, L
// Stored as 4x4 bitmasks (MSB = col 0, row 0 in high nibble -> bit = 15 - (r*4 + c))
static const uint16_t TET_SHAPES[7][4] = {
  // 0: I
  { 0x0F00, 0x2222, 0x00F0, 0x4444 },
  // 1: O
  { 0x6600, 0x6600, 0x6600, 0x6600 },
  // 2: T
  { 0x4E00, 0x4640, 0x0E40, 0x4C40 },
  // 3: S
  { 0x6C00, 0x4620, 0x06C0, 0x8C40 },
  // 4: Z
  { 0xC600, 0x2640, 0x0C60, 0x4C80 },
  // 5: J
  { 0x8E00, 0x6440, 0x0E20, 0x44C0 },
  // 6: L
  { 0x2E00, 0x4460, 0x0E80, 0xC440 }
};

// Gravity delay in milliseconds per row for Levels 0..15
static const uint16_t LEVEL_DROP_MS[16] = {
  760, 660, 560, 470, 390, 320, 260, 200,
  155, 115,  95,  85,  75,  68,  62,  55
};

class E60TetrisEngine {
public:
  // 80x25 terminal character framebuffer
  char screen[TERM_ROWS][TERM_COLS];

  // Game state
  E60Mode  mode         = E60_TITLE;
  uint8_t  board[WELL_H][WELL_W]; // 0 = empty, 1..7 = occupied piece type
  int      curType      = 0;
  int      curRot       = 0;
  int      curX         = 3;
  int      curY         = 0;
  int      nextType     = 0;

  // Settings (classic Elektronika-60 toggles)
  int      startLevel   = 0;
  int      level        = 0;
  bool     showNext     = true;
  bool     showGrid     = true;

  // Scoring & stats
  uint32_t score        = 0;
  uint32_t highScore    = 0;
  uint32_t lines        = 0;
  uint32_t bestLines    = 0;
  uint32_t statSingles  = 0;
  uint32_t statDoubles  = 0;
  uint32_t statTriples  = 0;
  uint32_t statTetrises = 0;
  bool     newRecord    = false;

  // Timers & animations
  uint32_t lastFallMs   = 0;
  uint32_t flashStartMs = 0;
  uint32_t lastBlinkMs  = 0;
  bool     blinkOn      = true;
  bool     fullRowMask[WELL_H];
  int      pendingCleared = 0;
  char     statusLine[44];

  // 7-bag PRNG
  uint32_t rngState     = 0x1984E601UL;
  uint8_t  bag[7];
  int      bagPos       = 7;

  E60TetrisEngine() {
    memset(screen, ' ', sizeof(screen));
    memset(board, 0, sizeof(board));
    memset(fullRowMask, 0, sizeof(fullRowMask));
    strcpy(statusLine, "READY");
  }

  void seedRng(uint32_t s) {
    rngState = s ? s : 0x1984E601UL;
    bagPos = 7;
  }

  uint32_t nextRand() {
    uint32_t x = rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState = x;
    return x;
  }

  int drawFromBag() {
    if (bagPos >= 7) {
      for (int i = 0; i < 7; ++i) bag[i] = (uint8_t)i;
      for (int i = 6; i > 0; --i) {
        int j = (int)(nextRand() % (uint32_t)(i + 1));
        uint8_t tmp = bag[i];
        bag[i] = bag[j];
        bag[j] = tmp;
      }
      bagPos = 0;
    }
    return (int)bag[bagPos++];
  }

  static inline bool shapeBit(int type, int rot, int r, int c) {
    uint16_t mask = TET_SHAPES[type][rot & 3];
    return ((mask >> (15 - (r * 4 + c))) & 1) != 0;
  }

  bool collides(int type, int rot, int px, int py) const {
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 4; ++c) {
        if (!shapeBit(type, rot, r, c)) continue;
        int bx = px + c;
        int by = py + r;
        if (bx < 0 || bx >= WELL_W || by >= WELL_H) return true;
        if (by >= 0 && board[by][bx] != 0) return true;
      }
    }
    return false;
  }

  uint32_t currentDropIntervalMs() const {
    int idx = level;
    if (idx < 0) idx = 0;
    if (idx > 15) idx = 15;
    return LEVEL_DROP_MS[idx];
  }

  void startGame(uint32_t nowMs) {
    memset(board, 0, sizeof(board));
    memset(fullRowMask, 0, sizeof(fullRowMask));
    score        = 0;
    lines        = 0;
    level        = startLevel;
    statSingles  = 0;
    statDoubles  = 0;
    statTriples  = 0;
    statTetrises = 0;
    newRecord    = false;
    pendingCleared = 0;
    bagPos       = 7;

    nextType     = drawFromBag();
    mode         = E60_PLAYING;
    snprintf(statusLine, sizeof(statusLine), "LEVEL %d - GOOD LUCK", level);
    spawnNextPiece(nowMs);
  }

  void spawnNextPiece(uint32_t nowMs) {
    curType = nextType;
    nextType = drawFromBag();
    curRot = 0;
    curX = 3;
    // Start I-piece at row -1 so its horizontal bar enters on row 0 cleanly
    curY = (curType == 0) ? -1 : 0;
    lastFallMs = nowMs;

    if (collides(curType, curRot, curX, curY)) {
      mode = E60_GAMEOVER;
      checkRecords();
      snprintf(statusLine, sizeof(statusLine), "GAME OVER - PLAY AGAIN? (Y/N)");
    }
  }

  bool checkRecords() {
    bool updated = false;
    if (score > highScore) {
      highScore = score;
      newRecord = true;
      updated = true;
    }
    if (lines > bestLines) {
      bestLines = lines;
      updated = true;
    }
    return updated;
  }

  bool moveHorizontal(int dx) {
    if (mode != E60_PLAYING) return false;
    if (!collides(curType, curRot, curX + dx, curY)) {
      curX += dx;
      return true;
    }
    return false;
  }

  bool rotatePiece(int dir) {
    if (mode != E60_PLAYING) return false;
    int nrot = (curRot + (dir > 0 ? 1 : 3)) & 3;
    static const int kicks[5] = { 0, -1, 1, -2, 2 };
    for (int i = 0; i < 5; ++i) {
      if (!collides(curType, nrot, curX + kicks[i], curY)) {
        curRot = nrot;
        curX += kicks[i];
        return true;
      }
    }
    return false;
  }

  // Returns true if piece locked (and caller should check highScore save)
  bool softDrop(uint32_t nowMs, bool byPlayer) {
    if (mode != E60_PLAYING) return false;
    if (!collides(curType, curRot, curX, curY + 1)) {
      curY++;
      if (byPlayer) score += 1;
      lastFallMs = nowMs;
      return false;
    }
    lockCurrentPiece(nowMs, 0);
    return true;
  }

  void hardDrop(uint32_t nowMs) {
    if (mode != E60_PLAYING) return;
    int dropped = 0;
    while (!collides(curType, curRot, curX, curY + 1)) {
      curY++;
      dropped++;
    }
    lockCurrentPiece(nowMs, dropped);
  }

  void lockCurrentPiece(uint32_t nowMs, int hardDroppedRows) {
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 4; ++c) {
        if (!shapeBit(curType, curRot, r, c)) continue;
        int bx = curX + c;
        int by = curY + r;
        if (by >= 0 && by < WELL_H && bx >= 0 && bx < WELL_W) {
          board[by][bx] = (uint8_t)(curType + 1);
        }
      }
    }

    // Classic Elektronika-60 rewards playing without NEXT preview (+25% drop bonus)
    uint32_t dropPts = (uint32_t)(hardDroppedRows * 2 + 4);
    if (!showNext) dropPts = (dropPts * 5) / 4;
    score += dropPts;

    // Scan for completed rows
    pendingCleared = 0;
    for (int r = 0; r < WELL_H; ++r) {
      bool full = true;
      for (int c = 0; c < WELL_W; ++c) {
        if (board[r][c] == 0) { full = false; break; }
      }
      fullRowMask[r] = full;
      if (full) pendingCleared++;
    }

    if (pendingCleared > 0) {
      mode = E60_LINE_FLASH;
      flashStartMs = nowMs;
      if (pendingCleared == 4) strcpy(statusLine, "*** TETRIS! (+4 LINES) ***");
      else snprintf(statusLine, sizeof(statusLine), "CLEARED %d LINE%s", pendingCleared, pendingCleared > 1 ? "S" : "");
    } else {
      checkRecords();
      spawnNextPiece(nowMs);
    }
  }

  void finishLineClear(uint32_t nowMs) {
    int writeRow = WELL_H - 1;
    for (int r = WELL_H - 1; r >= 0; --r) {
      if (fullRowMask[r]) continue;
      if (writeRow != r) {
        memcpy(board[writeRow], board[r], WELL_W);
      }
      writeRow--;
    }
    while (writeRow >= 0) {
      memset(board[writeRow], 0, WELL_W);
      writeRow--;
    }

    static const uint32_t LINE_PTS[5] = { 0, 100, 300, 500, 800 };
    uint32_t pts = LINE_PTS[pendingCleared <= 4 ? pendingCleared : 4] * (uint32_t)(level + 1);
    if (!showNext) pts = (pts * 5) / 4; // Classic E60 blind-next bonus
    score += pts;
    lines += (uint32_t)pendingCleared;

    if (pendingCleared == 1) statSingles++;
    else if (pendingCleared == 2) statDoubles++;
    else if (pendingCleared == 3) statTriples++;
    else if (pendingCleared >= 4) statTetrises++;

    int calcLevel = startLevel + (int)(lines / 10);
    if (calcLevel > level) {
      level = calcLevel;
      snprintf(statusLine, sizeof(statusLine), "SPEED UP! LEVEL %d", level);
    }

    memset(fullRowMask, 0, sizeof(fullRowMask));
    pendingCleared = 0;
    checkRecords();
    mode = E60_PLAYING;
    spawnNextPiece(nowMs);
  }

  // Periodic tick: returns true if screen needs redraw
  bool tick(uint32_t nowMs) {
    bool changed = false;

    if (nowMs - lastBlinkMs >= 400) {
      lastBlinkMs = nowMs;
      blinkOn = !blinkOn;
      if (mode == E60_TITLE || mode == E60_GAMEOVER || mode == E60_PAUSED) changed = true;
    }

    if (mode == E60_LINE_FLASH) {
      uint32_t elapsed = nowMs - flashStartMs;
      if (elapsed >= 260) {
        finishLineClear(nowMs);
      }
      changed = true;
    } else if (mode == E60_PLAYING) {
      if (nowMs - lastFallMs >= currentDropIntervalMs()) {
        softDrop(nowMs, false);
        changed = true;
      }
    }

    return changed;
  }

  // ==========================================================================
  // Pure Character-Only 80x25 Screen Composition
  // ==========================================================================
  void clearScreen() {
    memset(screen, ' ', sizeof(screen));
  }

  void putChar(int row, int col, char ch) {
    if (row >= 0 && row < TERM_ROWS && col >= 0 && col < TERM_COLS) {
      screen[row][col] = ch;
    }
  }

  void putStr(int row, int col, const char * s) {
    if (row < 0 || row >= TERM_ROWS || !s) return;
    while (*s && col < TERM_COLS) {
      if (col >= 0) screen[row][col] = *s;
      col++;
      s++;
    }
  }

  void renderTitleScreen() {
    clearScreen();
    putStr(0, 1,  "ELEKTRONIKA-60  TERM 80x25");
    putStr(0, 61, "[F10: EXIT TO OS]");

    // Classic Elektronika-60 style ASCII banner drawn purely with '[]' blocks!
    static const char * BANNER[5] = {
      "[][][] [][][] [][][] [][]   []  [][]",
      "  []   []       []   [] []  [] []   ",
      "  []   [][]     []   [][]   []  [][] ",
      "  []   []       []   [] []  []     []",
      "  []   [][][]   []   []  [] []  [][]"
    };
    for (int i = 0; i < 5; ++i) {
      putStr(3 + i, 21, BANNER[i]);
    }

    putStr(9,  19, "ORIGINAL CONCEPT: ALEXEY PAJITNOV (1984)");
    putStr(10, 21, "HERCULES MDA CHARACTER TERMINAL PORT");

    putStr(12, 16, "------------------------------------------------");
    char recBuf[56];
    snprintf(recBuf, sizeof(recBuf), "HIGH SCORE: %-8lu      BEST LINES: %-5lu",
             (unsigned long)highScore, (unsigned long)bestLines);
    putStr(13, 18, recBuf);
    putStr(14, 16, "------------------------------------------------");

    char lvlBuf[48];
    snprintf(lvlBuf, sizeof(lvlBuf), "YOUR LEVEL? (0-9): %d%c", startLevel, blinkOn ? '_' : ' ');
    putStr(16, 28, lvlBuf);

    putStr(18, 17, "PRESS 0-9 TO CHOOSE LEVEL, ENTER/SPACE TO START");
    putStr(20, 16, "KEYS: ARROWS OR 7/8/9/4/5  |  SPACE: HARD DROP");
    putStr(21, 16, "      1/N: NEXT  |  0/G: GRID  |  P/ESC: PAUSE");
    putStr(22, 16, "      F10 (OR ALT+F4): RETURN TO MDA DESKTOP");

    putStr(24, 1,  "READY>");
    putStr(24, 65, "ELEKTRONIKA-60");
  }

  void renderGameScreen(uint32_t nowMs) {
    clearScreen();

    // Top terminal line
    putStr(0, 1,  "ELEKTRONIKA-60 TETRIS (1984)");
    putStr(0, 61, "[F10: EXIT TO OS]");

    // ---- Left HUD Panel (Authentic Elektronika-60 stats) ----
    char buf[32];
    snprintf(buf, sizeof(buf), "FULL LINES: %8lu", (unsigned long)lines);
    putStr(2, 2, buf);
    snprintf(buf, sizeof(buf), "LEVEL:      %8d", level);
    putStr(3, 2, buf);
    snprintf(buf, sizeof(buf), "SCORE:      %8lu", (unsigned long)score);
    putStr(4, 2, buf);
    snprintf(buf, sizeof(buf), "HIGH SCORE: %8lu", (unsigned long)highScore);
    putStr(5, 2, buf);

    // Next piece preview (pure '[]' blocks, toggleable like original E60!)
    if (showNext) {
      putStr(7, 2, "NEXT PIECE:");
      for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
          if (shapeBit(nextType, 0, r, c)) {
            putStr(9 + r, 6 + c * 2, "[]");
          }
        }
      }
    } else {
      putStr(7, 2, "NEXT: OFF (+25% PTS)");
    }

    // Line-clear breakdown
    putStr(14, 2, "STATISTICS:");
    snprintf(buf, sizeof(buf), "  SINGLES:  %6lu", (unsigned long)statSingles);
    putStr(15, 2, buf);
    snprintf(buf, sizeof(buf), "  DOUBLES:  %6lu", (unsigned long)statDoubles);
    putStr(16, 2, buf);
    snprintf(buf, sizeof(buf), "  TRIPLES:  %6lu", (unsigned long)statTriples);
    putStr(17, 2, buf);
    snprintf(buf, sizeof(buf), "  TETRIS:   %6lu", (unsigned long)statTetrises);
    putStr(18, 2, buf);

    // ---- Center Playfield (Authentic <! . . . [] !> Well) ----
    bool flashToggle = ((nowMs - flashStartMs) / 65) % 2 == 0;
    for (int r = 0; r < WELL_H; ++r) {
      int tr = WELL_ROW + r;
      putStr(tr, WELL_COL - 2, "<!");
      putStr(tr, WELL_COL + WELL_W * 2, "!>");

      if (mode == E60_PAUSED) {
        for (int c = 0; c < WELL_W; ++c) putStr(tr, WELL_COL + c * 2, "  ");
      } else if (mode == E60_LINE_FLASH && fullRowMask[r]) {
        const char * fpat = flashToggle ? "==" : "  ";
        for (int c = 0; c < WELL_W; ++c) putStr(tr, WELL_COL + c * 2, fpat);
      } else {
        for (int c = 0; c < WELL_W; ++c) {
          if (board[r][c] != 0) {
            putStr(tr, WELL_COL + c * 2, "[]");
          } else {
            putStr(tr, WELL_COL + c * 2, showGrid ? " ." : "  ");
          }
        }
      }
    }

    // Active falling tetromino overlay
    if (mode == E60_PLAYING) {
      for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
          if (!shapeBit(curType, curRot, r, c)) continue;
          int bx = curX + c;
          int by = curY + r;
          if (by >= 0 && by < WELL_H && bx >= 0 && bx < WELL_W) {
            putStr(WELL_ROW + by, WELL_COL + bx * 2, "[]");
          }
        }
      }
    }

    // Pause / Game Over pure-ASCII overlays centered inside the well
    if (mode == E60_PAUSED) {
      putStr(WELL_ROW + 8,  WELL_COL + 2, "================");
      putStr(WELL_ROW + 9,  WELL_COL + 2, "  *** PAUSED ***");
      putStr(WELL_ROW + 10, WELL_COL + 2, "  P/ESC: RESUME ");
      putStr(WELL_ROW + 11, WELL_COL + 2, "  F10  : EXIT OS");
      putStr(WELL_ROW + 12, WELL_COL + 2, "================");
    } else if (mode == E60_GAMEOVER) {
      putStr(WELL_ROW + 7,  WELL_COL + 1, "==================");
      putStr(WELL_ROW + 8,  WELL_COL + 1, "    GAME  OVER    ");
      putStr(WELL_ROW + 9,  WELL_COL + 1, newRecord ? "  * NEW RECORD *  " : "                  ");
      putStr(WELL_ROW + 10, WELL_COL + 1, " PLAY AGAIN? Y/N  ");
      putStr(WELL_ROW + 11, WELL_COL + 1, "  F10: EXIT TO OS ");
      putStr(WELL_ROW + 12, WELL_COL + 1, "==================");
    }

    // Authentic Elektronika-60 floor & chevron base!
    putStr(WELL_ROW + WELL_H,     WELL_COL - 2, "<!====================!>");
    putStr(WELL_ROW + WELL_H + 1, WELL_COL,     "\\/\\/\\/\\/\\/\\/\\/\\/\\/\\/");

    // ---- Right Panel (Classic Elektronika-60 Controls Legend) ----
    const int rx = 56;
    putStr(2,  rx, "TETRIS / MDA-80x25");
    putStr(3,  rx, "---------------------");
    putStr(4,  rx, "7 / LEFT  : LEFT");
    putStr(5,  rx, "9 / RIGHT : RIGHT");
    putStr(6,  rx, "8 / UP    : ROTATE");
    putStr(7,  rx, "Z         : ROT CCW");
    putStr(8,  rx, "4 / DOWN  : SOFT DROP");
    putStr(9,  rx, "5 / SPACE : HARD DROP");
    putStr(11, rx, "1 / N     : SHOW NEXT");
    putStr(12, rx, "0 / G     : SHOW GRID");
    putStr(13, rx, "P / ESC   : PAUSE");
    putStr(14, rx, "R         : TITLE/NEW");
    putStr(15, rx, "F10       : EXIT OS");
    putStr(17, rx, "---------------------");
    snprintf(buf, sizeof(buf), "NEXT PREVIEW : %s", showNext ? "ON " : "OFF");
    putStr(18, rx, buf);
    snprintf(buf, sizeof(buf), "WELL GRID    : %s", showGrid ? "ON " : "OFF");
    putStr(19, rx, buf);
    snprintf(buf, sizeof(buf), "BEST LINES   : %lu", (unsigned long)bestLines);
    putStr(20, rx, buf);

    // Bottom terminal status prompt
    char st[60];
    snprintf(st, sizeof(st), "> %.42s%c", statusLine, (mode == E60_GAMEOVER && blinkOn) ? '_' : ' ');
    putStr(24, 1, st);
    putStr(24, 65, "ELEKTRONIKA-60");
  }

  void render(uint32_t nowMs) {
    if (mode == E60_TITLE) renderTitleScreen();
    else                   renderGameScreen(nowMs);
  }
};
