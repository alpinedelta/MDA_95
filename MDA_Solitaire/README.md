# MDA Solitaire

Classic Windows 95–style **Klondike Solitaire** engineered strictly for **1-bit monochrome Hercules / MDA CRT monitors** (`720×350 @ 50Hz`, no grayscale, no intensity) on **ESP32 / LilyGO VGA32** — designed as a launchable `.BIN` app for **MDA Desktop 95** (and also playable standalone!).

## Zero-Flicker, Zero-Tearing Full-Card Dragging

To solve flickering and tearing when dragging cards on a 1-bit MDA display, **MDA Solitaire** uses a dedicated two-stage compositor:
1. **`VGA2Controller` Hardware Double-Buffering**: Enabled via `setResolution(..., -1, -1, true)` so frames swap atomically at exact **50 Hz VSync** (`Canvas.swapBuffers()`).
2. **Offscreen 1-Bit RAM Compositing (`bgFB` + `outFB`, 30.8 KB each)**: Static table cards are rendered once into `bgFB`. While dragging a card (or an entire stack of up to 13 cards!), each frame copies `bgFB` in `< 0.2 ms`, draws the full dragged card face(s) with crisp drop shadows + grabbing-hand cursor into `outFB`, and flips atomically at VSync.

## 1-Bit Monochrome Suit Design (No Grayscale)

- **Black Suits (Spades ♠ & Clubs ♣)**: **Solid filled** black pips + bold solid corner rank.
- **Red Suits (Hearts ♥ & Diamonds ♦)**: **Crisp hollow outlined** pips + underlined corner rank.

## Controls

### Mouse (Primary)
- **Left Drag**: Smoothly drag any single card or legal face-up stack (snaps cleanly to best legal pile!).
- **Click Stock (Top-Left)**: Draw 1 or 3 cards (or recycle empty stock).
- **Double-Click or Right-Click** on any top card: Auto-send directly to its Foundation pile.
- **Click Top Menu**: `Game`, `Options`, `Help`.

### Keyboard Shortcuts
| Key | Action |
|---|---|
| `F2` / `Ctrl+N` | New Deal |
| `Ctrl+Z` | Undo Move (up to 32 levels) |
| `Space` / `D` | Draw from Stock |
| `H` | Highlight a valid Hint move |
| `A` | Auto-Complete (when all cards are revealed & stock empty) |
| `F3` | Toggle Draw-1 / Draw-3 mode |
| `B` | Cycle 4 classic 1-bit Card Back designs |
| `Alt` / `F10` | Open top Menu Bar (`Alt+G/O/H`) |
| `-` / `+` (`=`) | Adjust PS/2 mouse sensitivity (synced with MDA Desktop via NVS) |
| `F1` | Rules & Controls overlay |
| `Alt+F4` | Return immediately to **MDA Desktop** |

## Installing on MDA Desktop SD Card

1. Open `MDA_Solitaire/MDA_Solitaire.ino` in Arduino IDE (using the same OTA partition scheme as `MDA_Desktop`, e.g. *Minimal SPIFFS 1.9MB APP with OTA*).
2. Click **Sketch → Export Compiled Binary**.
3. Copy `MDA_Solitaire.ino.bin` to **`/APPS/SOLITAIR.BIN`** (or `/APPS/SOLITAIRE.BIN`) on your FAT32 microSD card.
4. Insert the SD card and press **`F5`** in MDA Desktop — **Solitaire** appears on your desktop with a playing-card icon!
