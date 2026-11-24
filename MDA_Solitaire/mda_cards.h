#pragma once
#include <stdint.h>
#include "mda_fb1bit.h"

// ============================================================================
// MDA Solitaire - Crisp 1-Bit Monochrome Playing Card Artwork (88 x 78 px)
// Proportioned for Hercules 720x350 (~1.54:1 pixel aspect ratio).
//
// Strict 1-Bit Red vs Black Suit Distinction (Zero Grayscale):
//   - BLACK SUITS (Spades / Clubs)   : SOLID FILLED black pips + bold solid rank
//   - RED SUITS   (Hearts / Diamonds): HOLLOW OUTLINED pips (crisp border, open center)
//     + clean light rank with subtle corner underline so Red vs Black is instant!
// ============================================================================

static constexpr int CARD_W = 88;
static constexpr int CARD_H = 78;

enum Suit : uint8_t {
  SUIT_SPADES = 0,   // Black (solid)
  SUIT_HEARTS = 1,   // Red   (hollow)
  SUIT_CLUBS  = 2,   // Black (solid)
  SUIT_DIAMONDS = 3  // Red   (hollow)
};

inline bool isRedSuit(uint8_t suit) {
  return (suit == SUIT_HEARTS || suit == SUIT_DIAMONDS);
}

struct Card {
  uint8_t rank;   // 1 (Ace) .. 13 (King)
  uint8_t suit;   // 0..3
  bool    faceUp;
};

struct Pile {
  Card cards[24];
  int  count = 0;
};

struct GameState {
  Pile     stock;
  Pile     waste;
  Pile     found[4];
  Pile     tab[7];
  int      score    = 0;
  int      moves    = 0;
  uint8_t  wasteVis = 0; // How many waste cards currently fanned (1..3 in Draw-3)
};

enum PileSource : uint8_t {
  SRC_NONE = 0,
  SRC_WASTE,
  SRC_FOUND,
  SRC_TAB
};

static const char * RANK_STR[14] = {
  "", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
};

// ---- Small Corner Suit Pips (10 x 9, 2 bytes/row) ----
// Spades (SOLID black)
static const uint8_t PIP_SM_SPADE[18] = {
  0x0C, 0x00, // ....##....
  0x1E, 0x00, // ...####...
  0x3F, 0x00, // ..######..
  0x7F, 0x80, // .########.
  0xFF, 0xC0, // ##########
  0xFF, 0xC0, // ##########
  0x6C, 0xC0, // .##.##.##.
  0x0C, 0x00, // ....##....
  0x3F, 0x00  // ..######..
};

// Hearts (HOLLOW outlined for unmistakable 1-bit Red suit!)
static const uint8_t PIP_SM_HEART[18] = {
  0x33, 0x00, // ..##..##..
  0x4C, 0x80, // .#..##..#.
  0x80, 0x40, // #........#
  0x80, 0x40, // #........#
  0x80, 0x40, // #........#
  0x40, 0x80, // .#......#.
  0x21, 0x00, // ..#....#..
  0x12, 0x00, // ...#..#...
  0x0C, 0x00  // ....##....
};

// Clubs (SOLID black)
static const uint8_t PIP_SM_CLUB[18] = {
  0x1E, 0x00, // ...####...
  0x3F, 0x00, // ..######..
  0x1E, 0x00, // ...####...
  0x6C, 0xC0, // .##.##.##.
  0xFF, 0xC0, // ##########
  0xFF, 0xC0, // ##########
  0x6C, 0xC0, // .##.##.##.
  0x0C, 0x00, // ....##....
  0x3F, 0x00  // ..######..
};

// Diamonds (HOLLOW outlined for unmistakable 1-bit Red suit!)
static const uint8_t PIP_SM_DIAMOND[18] = {
  0x0C, 0x00, // ....##....
  0x12, 0x00, // ...#..#...
  0x21, 0x00, // ..#....#..
  0x40, 0x80, // .#......#.
  0x80, 0x40, // #........#
  0x40, 0x80, // .#......#.
  0x21, 0x00, // ..#....#..
  0x12, 0x00, // ...#..#...
  0x0C, 0x00  // ....##....
};

// ---- Large Center Suit Pips (16 x 13, 2 bytes/row) ----
static const uint8_t PIP_LG_SPADE[26] = {
  0x01, 0x80,
  0x03, 0xC0,
  0x07, 0xE0,
  0x0F, 0xF0,
  0x1F, 0xF8,
  0x3F, 0xFC,
  0x7F, 0xFE,
  0x7F, 0xFE,
  0x7F, 0xFE,
  0x39, 0x9C,
  0x01, 0x80,
  0x03, 0xC0,
  0x0F, 0xF0
};

static const uint8_t PIP_LG_HEART[26] = {
  0x0E, 0x70,
  0x19, 0x98,
  0x30, 0x0C,
  0x60, 0x06,
  0x60, 0x06,
  0x60, 0x06,
  0x30, 0x0C,
  0x18, 0x18,
  0x0C, 0x30,
  0x06, 0x60,
  0x03, 0xC0,
  0x01, 0x80,
  0x00, 0x00
};

static const uint8_t PIP_LG_CLUB[26] = {
  0x03, 0xC0,
  0x07, 0xE0,
  0x07, 0xE0,
  0x03, 0xC0,
  0x1C, 0x38,
  0x3F, 0xFC,
  0x7F, 0xFE,
  0x7F, 0xFE,
  0x3F, 0xFC,
  0x1D, 0xB8,
  0x01, 0x80,
  0x03, 0xC0,
  0x0F, 0xF0
};

static const uint8_t PIP_LG_DIAMOND[26] = {
  0x01, 0x80,
  0x03, 0xC0,
  0x06, 0x60,
  0x0C, 0x30,
  0x18, 0x18,
  0x30, 0x0C,
  0x60, 0x06,
  0x30, 0x0C,
  0x18, 0x18,
  0x0C, 0x30,
  0x06, 0x60,
  0x03, 0xC0,
  0x01, 0x80
};

static const uint8_t * const SM_PIPS[4] = {
  PIP_SM_SPADE, PIP_SM_HEART, PIP_SM_CLUB, PIP_SM_DIAMOND
};
static const uint8_t * const LG_PIPS[4] = {
  PIP_LG_SPADE, PIP_LG_HEART, PIP_LG_CLUB, PIP_LG_DIAMOND
};

// ---- Table Felt & 4 Selectable Classic 1-Bit Card Backs ----
static const uint8_t FELT_PATTERN[8] = {
  0x80, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00 // 6% subtle CRT-friendly felt dots
};

static constexpr int NUM_CARD_BACKS = 4;
static const char * CARD_BACK_NAMES[NUM_CARD_BACKS] = {
  "Diamond Lattice",
  "Basket Weave",
  "Classic Stripes",
  "Starburst"
};

static const uint8_t CARD_BACK_PATS[NUM_CARD_BACKS][8] = {
  { 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81 }, // Diamond Lattice
  { 0xE7, 0x42, 0x42, 0xE7, 0x18, 0xBD, 0xBD, 0x18 }, // Basket Weave
  { 0x88, 0x44, 0x22, 0x11, 0x88, 0x44, 0x22, 0x11 }, // Classic Stripes
  { 0xFF, 0x81, 0xBD, 0xA5, 0xA5, 0xBD, 0x81, 0xFF }  // Starburst Tiles
};

// Draw empty pile placeholder on the dark felt
inline void drawEmptySlot(FrameBuf1Bit & fb, int x, int y, char glyph) {
  fb.drawDashedRoundRect(x, y, CARD_W, CARD_H, true);
  if (glyph == 'O') {
    // Stock recycle ring
    fb.drawRoundRect(x + 30, y + 26, 28, 26, true);
    fb.drawRoundRect(x + 32, y + 28, 24, 22, true);
  } else if (glyph == 'A') {
    // Foundation 'A' badge
    fb.drawRoundRect(x + 28, y + 24, 32, 30, true);
    fb.drawChar(x + 40, y + 35, 'A', &fabgl::FONT_8x8, true, true);
  } else if (glyph == 'K') {
    // Empty Tableau 'K' hint
    fb.drawChar(x + 40, y + 35, 'K', &fabgl::FONT_8x8, true, false);
  }
}

// Draw face-down playing card
inline void drawCardBack(FrameBuf1Bit & fb, int x, int y, int backIdx, bool shadow = false) {
  if (shadow) {
    fb.fillRoundRect(x + 2, y + 2, CARD_W, CARD_H, false);
  }
  // White outer card rim
  fb.fillRoundRect(x, y, CARD_W, CARD_H, true);
  fb.drawRoundRect(x, y, CARD_W, CARD_H, false);
  // Crisp black frame + decorative monochrome back pattern inside
  fb.fillRect(x + 4, y + 4, CARD_W - 8, CARD_H - 8, false);
  fb.fillPatternRect(x + 6, y + 6, CARD_W - 12, CARD_H - 12, CARD_BACK_PATS[backIdx % NUM_CARD_BACKS]);
  fb.drawRect(x + 5, y + 5, CARD_W - 10, CARD_H - 10, false);
}

// Draw face-up playing card with crisp corner rank/suit + center pips/court frame
inline void drawCardFace(FrameBuf1Bit & fb, int x, int y, const Card & c, bool shadow = false, bool highlight = false) {
  if (shadow) {
    // Crisp 2px dark drop shadow + 1px offset outline so dragged cards pop over white cards too!
    fb.fillRoundRect(x + 3, y + 3, CARD_W, CARD_H, false);
  }

  // Pure phosphor-white card body with crisp black rounded border
  fb.fillRoundRect(x, y, CARD_W, CARD_H, true);
  fb.drawRoundRect(x, y, CARD_W, CARD_H, false);
  if (highlight) {
    fb.drawRoundRect(x + 1, y + 1, CARD_W - 2, CARD_H - 2, false);
    fb.drawRoundRect(x + 2, y + 2, CARD_W - 4, CARD_H - 4, false);
  }

  bool red = isRedSuit(c.suit);
  const char * rStr = RANK_STR[c.rank];
  int rLen = (int)strlen(rStr);

  // ---- Top-Left Corner (Rank + Suit side-by-side so ALWAYS visible in cascaded tableau!) ----
  // Black suits use bold solid font; Red suits use crisp normal font + tiny underline accent
  fb.drawText(x + 5, y + 4, rStr, &fabgl::FONT_8x8, false, !red);
  if (red) {
    fb.drawHLine(x + 5, x + 5 + rLen * 8 - 2, y + 13, false);
  }
  fb.drawMonoSprite(x + 6 + rLen * 9, y + 4, 10, 9, SM_PIPS[c.suit], false);

  // ---- Top-Right Corner Mini Suit (extra readability while fanned/cascaded!) ----
  fb.drawMonoSprite(x + CARD_W - 15, y + 4, 10, 9, SM_PIPS[c.suit], false);

  // ---- Bottom-Right Corner (Rank + Suit) ----
  int brX = x + CARD_W - 6 - rLen * (red ? 8 : 9);
  fb.drawText(brX, y + CARD_H - 12, rStr, &fabgl::FONT_8x8, false, !red);
  fb.drawMonoSprite(brX - 12, y + CARD_H - 13, 10, 9, SM_PIPS[c.suit], false);

  // ---- Center Artwork (y + 17 .. y + 61) ----
  if (c.rank == 1) {
    // Classic bold Ace centerpiece
    fb.drawMonoSprite(x + (CARD_W - 16) / 2, y + (CARD_H - 13) / 2, 16, 13, LG_PIPS[c.suit], false);
    fb.drawRect(x + 20, y + 18, CARD_W - 40, CARD_H - 36, false);
  } else if (c.rank >= 11) {
    // Court Card (Jack, Queen, King) ornate 1-bit monochrome portrait frame
    int fx = x + 14, fy = y + 17, fw = CARD_W - 28, fh = CARD_H - 34;
    fb.drawRect(fx, fy, fw, fh, false);
    fb.drawRect(fx + 2, fy + 2, fw - 4, fh - 4, false);
    // Decorative corner dots inside court frame
    fb.drawMonoSprite(fx + 5, fy + 5, 10, 9, SM_PIPS[c.suit], false);
    fb.drawMonoSprite(fx + fw - 15, fy + fh - 14, 10, 9, SM_PIPS[c.suit], false);
    // Crown/Emblem + large bold rank letter in center
    int cx = x + CARD_W / 2;
    int cy = y + CARD_H / 2;
    if (c.rank == 13) {
      // King crown above letter
      fb.drawHLine(cx - 10, cx + 10, cy - 10, false);
      fb.drawLine(cx - 10, cy - 10, cx - 12, cy - 16, false);
      fb.drawLine(cx - 12, cy - 16, cx - 5,  cy - 12, false);
      fb.drawLine(cx - 5,  cy - 12, cx,      cy - 17, false);
      fb.drawLine(cx,      cy - 17, cx + 5,  cy - 12, false);
      fb.drawLine(cx + 5,  cy - 12, cx + 12, cy - 16, false);
      fb.drawLine(cx + 12, cy - 16, cx + 10, cy - 10, false);
    } else if (c.rank == 12) {
      // Queen tiara
      fb.drawHLine(cx - 9, cx + 9, cy - 10, false);
      fb.drawLine(cx - 9, cy - 10, cx - 4, cy - 15, false);
      fb.drawLine(cx - 4, cy - 15, cx,     cy - 11, false);
      fb.drawLine(cx,     cy - 11, cx + 4, cy - 15, false);
      fb.drawLine(cx + 4, cy - 15, cx + 9, cy - 10, false);
    } else {
      // Jack heraldic diamond crest
      fb.drawLine(cx, cy - 16, cx + 7, cy - 11, false);
      fb.drawLine(cx + 7, cy - 11, cx, cy - 6, false);
      fb.drawLine(cx, cy - 6, cx - 7, cy - 11, false);
      fb.drawLine(cx - 7, cy - 11, cx, cy - 16, false);
    }
    const char * courtTitle = (c.rank == 11) ? "JACK" : (c.rank == 12 ? "QUEEN" : "KING");
    int tw = (int)strlen(courtTitle) * 6;
    fb.drawText(cx - tw / 2, cy - 2, courtTitle, &fabgl::FONT_6x8, false, false);
    fb.drawMonoSprite(cx - 8, cy + 7, 16, 13, LG_PIPS[c.suit], false);
  } else {
    // Number Cards (2..10): Clean classic symmetric pip layouts!
    auto pip = [&](int px, int py) {
      fb.drawMonoSprite(x + px - 5, y + py - 4, 10, 9, SM_PIPS[c.suit], false);
    };
    int L = 24, C = 44, R = 64;
    int T = 23, M = 39, B = 55;
    int TM = 31, BM = 47;

    switch (c.rank) {
      case 2: pip(C, T); pip(C, B); break;
      case 3: pip(C, T); pip(C, M); pip(C, B); break;
      case 4: pip(L, T); pip(R, T); pip(L, B); pip(R, B); break;
      case 5: pip(L, T); pip(R, T); pip(C, M); pip(L, B); pip(R, B); break;
      case 6: pip(L, T); pip(R, T); pip(L, M); pip(R, M); pip(L, B); pip(R, B); break;
      case 7: pip(L, T); pip(R, T); pip(C, TM); pip(L, M); pip(R, M); pip(L, B); pip(R, B); break;
      case 8: pip(L, T); pip(R, T); pip(C, TM); pip(L, M); pip(R, M); pip(C, BM); pip(L, B); pip(R, B); break;
      case 9:
        pip(L, T); pip(R, T); pip(L, TM); pip(R, TM); pip(C, M);
        pip(L, BM); pip(R, BM); pip(L, B); pip(R, B);
        break;
      case 10:
        pip(L, T); pip(R, T); pip(C, 27); pip(L, TM); pip(R, TM);
        pip(L, BM); pip(R, BM); pip(C, 51); pip(L, B); pip(R, B);
        break;
    }
  }
}
