# MDA Paint

An MS Paint–style graphics editor built specifically for **strict 1-bit monochrome Hercules / MDA CRT monitors** (720×350 @ 50Hz TTL monochrome — no grayscale, no intensity levels) on ESP32 / LilyGO VGA32 using [FabGL](https://github.com/fdivitto/FabGL).

## Hardware Setup

Uses the same MDA TTL video pinout and modeline as `NetworkTerminal_MDA.ino`, expanded to initialize both PS/2 ports:

- **Video (`fabgl::VGA2Controller`)**:
  - Video (R/G/B tied): `GPIO 21` (LilyGO VGA32 TTL pinout; alternate ESP32 `GPIO 22` commented in `setup()`)
  - HSync: `GPIO 23`
  - VSync: `GPIO 15`
  - Modeline: `"MDA_50HZ_LOCK" 16.000 720 736 848 880 350 352 356 364 +HSync -VSync` (720×350 @ 50Hz)
- **PS/2 Input (`PS2Preset::KeyboardPort0_MousePort1`)**:
  - Port 0 (Keyboard): `GPIO 33` (CLK), `GPIO 32` (DAT)
  - Port 1 (Mouse): `GPIO 26` (CLK), `GPIO 27` (DAT)
- **MicroSD Card (LilyGO VGA32 Integrated Slot)**:
  - `CS`: `GPIO 13`, `MOSI`: `GPIO 12`, `MISO`: `GPIO 2`, `CLK`: `GPIO 14`

## Features Designed for 1-Bit Monochrome

- **640×296 1-bit Offscreen Canvas** (23.1 KB RAM) with up to **4-level Undo + Redo** (auto-scaled to free heap; no PSRAM required).
- **SD Card BMP File Browser (`File -> Open BMP...` / `Save BMP` / `Save BMP As...`, `Ctrl+O` / `Ctrl+S`)**:
  - Saves standard **1-bit uncompressed Windows/Linux `.BMP`** (`640×296`, `23,742` bytes) readable natively in Windows Paint/Photos, GIMP, Linux viewers, and browsers.
  - Opens **1-bit** (lossless), **4-bit, 8-bit, and 24-bit** uncompressed `.BMP` files from Windows/Linux (color/grayscale BMPs are automatically converted using crisp 4×4 Bayer ordered dithering!).
  - Full mouse + keyboard file browser with folder navigation, scrolling, filename typing, file deletion, and live SD remounting.
- **16 Classic 8×8 Monochrome Dither & Hatch Patterns** (Solid, Clear, 50% Checker, 25% Light, 75% Heavy, 12% Sparse, Horizontal/Vertical Lines & Bars, Diagonals, Crosshatch, Diamond Grid, Brick, Weave) — usable with Brush, Flood Fill, Spray, and all Filled Shapes.
- **14 Tools** with custom 1-bit pixel-art icons proportioned for MDA's non-square CRT pixel aspect ratio:
  - `S` **Select** (marquee, move, `Ctrl+Drag` clone, Copy/Cut/Paste with Opaque/Transparent mode, Invert, Flip H/V, Delete)
  - `P` **Pencil** (1px precision)
  - `B` **Brush** (4 sizes × 4 nibs: Round, Square, `/` & `\` Calligraphy, pattern-capable)
  - `E` **Eraser** (4 sizes)
  - `F` **Fill Bucket** (pattern-safe scanline flood fill)
  - `A` **Spray / Airbrush** (stippled monochrome scatter)
  - `L` **Line** (rubber-band preview, `Shift` snaps to 0°/45°/90°)
  - `C` **Curve** (classic 3-point Quadratic Bezier: drag endpoints, then bend)
  - `R` **Rectangle** / `U` **Rounded Rect** / `O` **Ellipse** (`Shift` constrains to square/circle; Outline, Outline+Pattern, or Pattern-Only fill modes)
  - `Y` **Polygon** (multi-vertex, right-click or `Enter` to close with pattern fill)
  - `T` **Text** (direct PS/2 typing onto canvas; `6x8`, `8x8`, `8x14` fonts + `Bold` and `2X` toggles)
  - `Z` **4× FatBits Zoom** (draw individual pixels at 4× magnification; pan with Arrow keys)
- **Dark CRT / Light Paper Toggle (`I`)**: Defaults to phosphor-on-black to protect CRT phosphor; switches cleanly to black-on-phosphor paper mode anytime.
- **Fullscreen Preview (`Tab`)**: View artwork unobstructed at 720×350.
- **USB Serial PBM Export (`File -> Export PBM USB`)**: Dumps a standard Netpbm `P1` `.pbm` file over USB Serial (115200 baud) for saving/viewing on a PC.

## Controls

### Mouse (Primary)
- **Left Click / Drag**: Draw with Foreground Ink & Active Pattern (or click UI/menus)
- **Right Click / Drag**: Draw with Background Ink / Close Polygon / Cancel Curve
- **Middle Click**: Toggle 4× FatBits Zoom centered at cursor
- **Scroll Wheel**: Adjust brush/line thickness (`Shift + Wheel` cycles patterns)

### Keyboard Shortcuts
| Key | Action |
|---|---|
| `S P B E F A L C R U O Y T Z` | Select Tool |
| `[` / `]` | Decrease / Increase brush & line size |
| `,` / `.` or `1`–`8` (`Shift+1`–`8`) | Select monochrome pattern |
| `X` | Swap Foreground / Background ink |
| `M` | Cycle shape Fill Mode (Outline / Both / Pattern) |
| `G` | Toggle pixel grid overlay |
| `I` | Toggle Dark CRT / Light Paper mode |
| `Tab` | Fullscreen artwork preview |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo |
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy / Cut / Paste selection |
| `Ctrl+A` / `Del` | Select All / Clear Selection |
| `Ctrl+I` / `Ctrl+N` | Invert / New Canvas |
| `Arrow Keys` | 1px cursor nudge (`Shift` = 8px; pans viewport in 4× Zoom) |
| `Space` | Left-click / draw with keyboard |
| `Alt` / `F10` | Highlight top menu bar (use Arrows + `Enter`, or `Alt+F/E/I/V/H`) |
| `-` / `+` (`=`) | Decrease / Increase PS/2 mouse sensitivity (Levels 1–6: 0.5× to 6.0×) |
| `F5` | Reset / re-initialize PS/2 mouse |
| `Shift + Drag` | Constrain lines (0°/45°/90°) and shapes (square/circle) |
| `F1` | Toggle in-app Help & Shortcuts overlay |
| `Esc` | Cancel active tool / close menu / exit zoom |

## Building in Arduino IDE

1. Install **ESP32 Board Support** and the **FabGL** library in Arduino IDE (same setup as `NetworkTerminal_MDA`).
2. Open `MDA_Paint/MDA_Paint.ino`.
3. Select your ESP32 board (e.g. *ESP32 Dev Module*) and upload.
