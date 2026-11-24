# MDA 95 — Monochrome Hercules Desktop & App Suite for ESP32

**MDA 95** is a lightweight, Windows 95–inspired desktop launcher and application suite engineered strictly for **1-bit monochrome Hercules / MDA CRT monitors** (`720×350 @ 50Hz` TTL monochrome — no grayscale, no intensity levels) running on the **ESP32 / LilyGO VGA32** using [FabGL](https://github.com/fdivitto/FabGL).

The main **MDA Desktop 95** launcher stays permanently in ESP32 flash (`ota_0`), includes built-in windowed utilities (such as **Notepad** and **Wallpaper Properties**), and dynamically loads standalone compiled ESP32 applications (`.BIN`) directly from a FAT32 MicroSD card into `ota_1` on demand.

---

## Project Layout

| Directory / File | Role | Launch Mode |
|---|---|---|
| [`MDA_Desktop/`](MDA_Desktop/README.md) | **MDA Desktop 95** — Main launcher, taskbar, wallpaper manager, SD app loader, and built-in **Notepad** window | Flashed once to `ota_0` |
| [`MDA_Paint/`](MDA_Paint/README.md) | **MDA Paint** — Full 1-bit monochrome MS Paint editor with SD `.BMP` Open/Save | Standalone `/APPS/MDAPAINT.BIN` |
| [`MDA_Solitaire/`](MDA_Solitaire/README.md) | **MDA Solitaire** — Win95 Klondike Solitaire with zero-flicker VSync full-card dragging | Standalone `/APPS/SOLITAIR.BIN` |
| [`MDA_Tetris/`](MDA_Tetris/README.md) | **MDA Tetris** — Authentic 1984 Elektronika-60 character-only 80×25 terminal Tetris | Standalone `/APPS/TETRIS.BIN` |
| [`NetworkTerminal_MDA.ino`](NetworkTerminal_MDA.ino) | Original VT/ANSI Wi-Fi & Telnet MDA terminal (hardware & modeline reference) | Standalone sketch |

---

## Hardware Requirements & LilyGO VGA32 Wiring

All applications share the same verified **LilyGO VGA32** pinout and Hercules MDA video timing (`"MDA_50HZ_LOCK" 16.000 720 736 848 880 350 352 356 364 +HSync -VSync`):

| Peripheral | GPIO Pins | Notes |
|---|---|---|
| **MDA TTL Video** | Video `GPIO 21`, HSync `GPIO 23`, VSync `GPIO 15` | Strict 1-bit (`Black` / `White`), `720×350 @ 50Hz` |
| **PS/2 Keyboard (Port 0)** | CLK `GPIO 33`, DATA `GPIO 32` | Standard PS/2 keyboard (`USLayout`) |
| **PS/2 Mouse (Port 1)** | CLK `GPIO 26`, DATA `GPIO 27` | 2/3-button + scroll wheel; 6-level NVS sensitivity |
| **MicroSD Card Slot** | CS `GPIO 13`, MOSI `GPIO 12`, MISO `GPIO 2`, CLK `GPIO 14` | FAT32 formatted microSD card |

> **Flashing Tip (LilyGO VGA32):** Because SD `MISO` shares **`GPIO 2`** (an ESP32 boot-strapping pin), **eject the MicroSD card before uploading firmware over USB** to avoid `Wrong boot mode detected (0xa)` errors, then reinsert the card afterward.

---

## Architecture: How the Dual-OTA SD Launcher Works

ESP32 OTA partition tables provide two application partitions (`ota_0` and `ota_1`):

1. **Permanent Desktop (`ota_0`)**: `MDA_Desktop` is flashed once over USB into `ota_0`.
2. **On-Demand App Loading (`ota_1`)**: Double-clicking any `.BIN` app icon on the desktop (or selecting it from **`Start → Programs`**) streams `/APPS/<NAME>.BIN` from the MicroSD card into `ota_1`, records its fingerprint in ESP32 NVS, sets `ota_1` as the boot target, and restarts directly into the application.
3. **Instant Re-Launch Cache**: Launching the same unmodified `.BIN` again skips flashing completely and boots in **< 0.5 seconds** with zero extra flash wear.
4. **Guaranteed Safe Return**: Every app includes `mda_launcher_sdk.h` and calls `mdaArmDesktopOnReset()` during `setup()`. Exiting an app (e.g., `Alt+F4`, `File → Exit`, or `F10` in Tetris) — or even pressing the hardware **RST** button — returns straight back to **MDA Desktop 95**.
5. **Shared NVS Preferences (`mda_sys`)**: PS/2 mouse sensitivity (Levels 1–6) and desktop wallpaper choice persist across reboots and are shared seamlessly between `MDA_Desktop`, `MDA_Paint`, and `MDA_Solitaire`.

---

## Main Launcher & Built-In Tools

### 1. MDA Desktop 95 (`MDA_Desktop/`)
A crisp 1-bit monochrome desktop shell designed for both PS/2 mouse and mouseless keyboard operation:
- **Desktop & Taskbar**: Icon grid with single-click selection and double-click launch, Win95 `[ Start ]` button with cascading `Programs >` submenu, active window taskbar tab, `SD:Y/N` mount status, clickable mouse speed badge (`S:1`–`S:6`), and live uptime clock.
- **16 Monochrome Wallpaper Patterns**: Open **Wallpaper** from the desktop, Start menu, or right-click menu to preview all 16 classic 1-bit dither/hatch patterns on a miniature Win95 CRT monitor preview; saved in NVS.
- **Built-In Draggable Dialogs**: **My Computer** (heap, OTA slot sizes, cached app, SD & PS/2 status), **SD Apps** (`/APPS/*.BIN` browser with `[FAST]` cache indicators and Remount), and **Help / Guide**.
- **Global Shortcuts**: `Alt` / `F10` / `Ctrl+Esc` / `Win` (Start Menu), `Arrows + Enter` (navigation), `-` / `+` (mouse speed 1–6), `F5` (remount SD & rescan apps), `F1` (Help), `Esc` (close menu/window).

### 2. Integrated Notepad Window (Built Into `MDA_Desktop`)
Runs directly in a draggable `580×276` monochrome window over your desktop wallpaper with instant launch and no reboot:
- **Full Plain-Text Editing**: Lazily allocated 16 KB text buffer, blinking caret, mouse click-to-place and drag-to-select, `Shift+Arrows`/`Home`/`End`/`PgUp`/`PgDn`, and 4 KB clipboard (`Ctrl+A`, `Ctrl+X`, `Ctrl+C`, `Ctrl+V`).
- **Formatting & Menus**: `Alt` or `Alt+F/E/O` pull-down menus with **Word Wrap** toggle, **Font Size** toggle (`6×8` compact ~91 cols vs `8×8` large ~68 cols), scrollbar, and mouse wheel support.
- **SD Card File Browser**: `File → Open...` (`Ctrl+O`), `Save` (`Ctrl+S`), and `Save As...` browse folders on the MicroSD card, automatically append `.TXT`, and cleanly normalize Windows (`CRLF`) and Linux (`LF`) text files.

---

## Standalone SD Card Applications (`/APPS/*.BIN`)

### 3. MDA Paint (`MDA_Paint/`)
An MS Paint–style bitmap graphics editor designed specifically for strict 1-bit Hercules displays (`640×296` canvas):
- **14 Drawing Tools**: Select (move, `Ctrl+Drag` clone, cut/copy/paste, opaque/transparent, invert, flip H/V), Pencil, 4-nib Brush, Eraser, pattern-safe Flood Fill, Airbrush/Spray, Line (`Shift` 0°/45°/90°), 3-point Quadratic Bezier Curve, Rectangle, Rounded Rect, Ellipse, Polygon, Text (`6×8`/`8×8`/`8×14` + Bold/2×), and **4× FatBits Zoom**.
- **16 Monochrome Patterns & Modes**: Classic 8×8 dither/hatch patterns usable across brushes, fills, and shapes; Dark CRT / Light Paper toggle (`I`); fullscreen preview (`Tab`); up to 4-level Undo/Redo.
- **Windows/Linux `.BMP` SD Browser**: Saves native 1-bit uncompressed `.BMP` (`640×296`) and loads 1-bit, 4-bit, 8-bit, and 24-bit `.BMP` files (automatically applying 4×4 Bayer ordered dithering to color/grayscale images). Also supports USB Serial Netpbm `.PBM` export.
- **Exit to Desktop**: `File → Exit to Desk` or `Alt+F4`.

### 4. MDA Solitaire (`MDA_Solitaire/`)
Classic Windows 95 Klondike Solitaire built around a dedicated **zero-flicker, zero-tearing** rendering pipeline:
- **Smooth Full-Card Dragging**: Combines `VGA2Controller` hardware VSync double-buffering (`Canvas.swapBuffers()`) with two 30.8 KB offscreen 1-bit RAM compositing buffers (`bgFB` + `outFB`) so dragging single cards or full 13-card stacks renders with drop shadows and a grabbing cursor at exact 50 Hz VSync lockstep.
- **1-Bit Monochrome Suit Clarity**: Solid filled pips & bold corner ranks for Spades/Clubs; crisp hollow outlined pips & underlined ranks for Hearts/Diamonds.
- **Features**: Draw-1 / Draw-3 modes (`F3`), 32-level Undo (`Ctrl+Z`), Hints (`H`), Auto-Complete (`A`), double-click / right-click auto-foundation, 4 selectable 1-bit card backs (`B`), and live score/timer.
- **Exit to Desktop**: `Game → Exit` or `Alt+F4`.

### 5. MDA Tetris — Elektronika-60 Edition (`MDA_Tetris/`)
A full-screen, strictly **character-only** tribute to Alexey Pajitnov's original 1984 Elektronika-60 text-terminal Tetris:
- **Pure 80×25 MDA Terminal**: Renders exclusively with `FONT_8x14` glyphs at IBM MDA `9×14` cell pitch (`720×350` exact) using a zero-flicker dirty-cell diff renderer — zero pixel/graphics primitives.
- **Authentic 1984 Look**: Iconic `<! . . . [] !>` well, `<!====================!>` floor, `\/\/\/\/\/\/\/\/\/\/` base, `YOUR LEVEL? (0-9)` startup prompt, and pure-ASCII line-clear flash animation.
- **Controls & Scoring**: Supports both original Elektronika-60 numeric keys (`7/8/9/4/5`, `1` = Next, `0` = Grid) and modern Arrow keys + `Space` + `Z/X`; includes persistent ESP32 NVS High Score / Best Lines and the classic **+25% score bonus** when playing with Next Preview OFF.
- **Exit to Desktop**: Press **`F10`** (or `Alt+F4`) from any screen.

---

## Quick Start: Building & Installing Everything

### Prerequisites
- **Arduino IDE** with **ESP32 Board Support** installed.
- **FabGL** library installed via Arduino Library Manager.

### Step 1 — Flash MDA Desktop 95 (Once)
1. **Eject the MicroSD card** from the LilyGO VGA32.
2. Open [`MDA_Desktop/MDA_Desktop.ino`](MDA_Desktop/MDA_Desktop.ino) in Arduino IDE.
3. Select **Tools → Partition Scheme → `Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)`** *(gives every SD app up to 1.9 MB)*.
4. Upload to your ESP32 / LilyGO VGA32.

### Step 2 — Export & Copy Standalone Apps to MicroSD
For each standalone app (`MDA_Paint`, `MDA_Solitaire`, `MDA_Tetris`):
1. Open its `.ino` sketch in Arduino IDE using the **same OTA Partition Scheme**.
2. Click **Sketch → Export Compiled Binary**.
3. Copy the exported `.ino.bin` files into a `/APPS` folder on your FAT32 MicroSD card:

```text
SD Card Root (/)
├── APPS/
│   ├── MDAPAINT.BIN      <- Exported from MDA_Paint/MDA_Paint.ino
│   ├── SOLITAIR.BIN      <- Exported from MDA_Solitaire/MDA_Solitaire.ino
│   ├── TETRIS.BIN        <- Exported from MDA_Tetris/MDA_Tetris.ino
│   └── TETRIS.BMP        <- Optional custom 32x24 1-bit icon (included in MDA_Tetris/)
├── NOTES01.TXT           <- Text files saved/opened by Desktop Notepad
└── ART01.BMP             <- Artwork saved/opened by MDA Paint
```

*(Any `/APPS/<NAME>.BIN` can optionally pair with a matching `32×24` 1-bit `.BMP` custom icon; otherwise `MDA_Desktop` automatically selects a built-in icon from the filename.)*

4. Insert the MicroSD card into the LilyGO VGA32, power on (or press **`F5`** in `MDA_Desktop`), and double-click any app to launch!

---

## Writing Your Own Launchable MDA 95 App

1. Copy [`MDA_Desktop/mda_launcher_sdk.h`](MDA_Desktop/mda_launcher_sdk.h) into your new Arduino sketch folder.
2. Initialize the MDA display (`GPIO 21/23/15`, `720×350 @ 50Hz`) and PS/2 ports (`33/32`, `26/27`), and hook into the launcher SDK:

```cpp
#include "mda_launcher_sdk.h"

void setup() {
  mdaArmDesktopOnReset();               // Hardware RST or crash returns safely to MDA Desktop
  int mouseSens = mdaLoadMouseSens();   // Optional: sync mouse sensitivity with Desktop
  // ... initialize VGA2Controller, PS2Controller, Keyboard/Mouse ...
}

// Call whenever the user chooses Exit (e.g. Alt+F4 or F10):
void exitApp() {
  mdaReturnToDesktop();
}
```

3. Export the compiled binary with the same OTA partition scheme and drop the `.BIN` into `/APPS/` on your SD card.

> **Arduino IDE Note:** Place any custom `struct` or `enum` types used in function parameters inside a `.h` header file (rather than directly in the `.ino`) so Arduino's auto-prototype generator always sees them cleanly.
