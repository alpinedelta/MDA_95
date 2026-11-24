# MDA Tetris — Elektronika-60 (1984) Terminal Edition

A full-screen, strictly character-only **Tetris** clone styled after Alexey Pajitnov's original 1984 **Elektronika-60** text-terminal version, built as a standalone SD-card launchable application for **MDA Desktop** on the **LilyGO VGA32** (`720×350 @ 50Hz` 1-bit monochrome Hercules/MDA).

## Authentic Elektronika-60 Terminal Aesthetic
- Native **80 × 25** IBM MDA character cell grid (`8×14` glyphs at `9×14` cell pitch = exact `720×350`, zero graphics primitives).
- Iconic ASCII playfield: `<!` / `!>` side walls, `[]` tetromino blocks, toggleable ` .` interior matrix grid, `<!====================!>` floor, and `\/\/\/\/\/\/\/\/\/\/` base.
- Zero-flicker dirty-cell diff renderer (only changed character cells are redrawn).
- Classic terminal startup level prompt (`YOUR LEVEL? (0-9)`) and persistent ESP32 NVS High Score / Best Lines storage.
- Classic Elektronika-60 blind-play bonus: turning **NEXT PREVIEW** off (`1` or `N`) grants **+25% score bonus**.

## Controls
Both original **Elektronika-60 numeric keys** and modern **Arrow keys** work simultaneously:

| Key(s) | Action |
|---|---|
| **`F10`** (or `Alt+F4`) | **Exit back to MDA Desktop OS** (works from any screen) |
| `Left` / `7` / `A` | Move piece left |
| `Right` / `9` / `D` | Move piece right |
| `Up` / `8` / `W` / `X` | Rotate clockwise |
| `Z` | Rotate counter-clockwise |
| `Down` / `4` / `S` | Soft drop (+1 pt/row) |
| `Space` / `5` / `Enter` | Hard drop / slam |
| `1` / `N` | Toggle **Next Piece** preview (OFF = +25% points) |
| `0` / `G` | Toggle dotted well grid (` .`) |
| `P` / `Esc` | Pause / Resume |
| `R` | Return to Title / Level Select |

## Building & Deploying to SD Card (`/APPS/TETRIS.BIN`)
1. Open `MDA_Tetris/MDA_Tetris.ino` in **Arduino IDE** (ESP32 board + FabGL library).
2. Select the same OTA partition scheme used for `MDA_Desktop` (e.g., **Minimal SPIFFS (1.9MB APP with OTA)**).
3. Choose **Sketch → Export Compiled Binary**.
4. Copy the generated `.ino.bin` file to your FAT32 MicroSD card as:
   - `/APPS/TETRIS.BIN`
   - *(Optional)* Copy `TETRIS.BMP` to `/APPS/TETRIS.BMP` for a custom 32×24 monochrome desktop icon!
5. Insert the SD card into the LilyGO VGA32, press **F5** (or **Remount SD**) in `MDA_Desktop`, and double-click **TETRIS** to play!
