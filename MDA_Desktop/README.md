# MDA Desktop 95

A minimal **Windows 95–style Desktop Launcher** engineered strictly for **1-bit monochrome Hercules / MDA CRT monitors** (`720×350 @ 50Hz`, no grayscale, no intensity) on **ESP32 / LilyGO VGA32**.

Stores compiled ESP32 applications (such as **MDA Paint**, **Network Terminal**, or your own future apps) as `.BIN` files on the MicroSD card and launches them on demand when you double-click their desktop icon or select them from **`Start -> Programs`**.

## How the Dual-OTA SD Launcher Works

ESP32 OTA partition tables contain two application slots: `ota_0` and `ota_1`.

1. **MDA Desktop** is flashed once via Arduino IDE into **`ota_0`** and **stays there permanently**.
2. When you click an app on the SD card (e.g. `/APPS/MDAPAINT.BIN`), **MDA Desktop** streams the `.BIN` into **`ota_1`**, saves its fingerprint in NVS, switches the boot pointer to `ota_1`, and restarts directly into that app.
3. **Instant Re-launch Cache**: If you launch the same app again without changing its `.BIN` file, MDA Desktop detects that `ota_1` already holds it and boots it in **< 0.5 seconds** with **zero flash wear**.
4. **Safe Return to Desktop**: Apps that include `mda_launcher_sdk.h` call `mdaArmDesktopOnReset()` in `setup()` and `mdaReturnToDesktop()` on `File -> Exit to Desktop` (`Alt+F4`). That means exiting the app — **or even pressing the ESP32 Reset button if an experimental app hangs** — returns straight back to **MDA Desktop**!

## Hardware Wiring (LilyGO VGA32)

| Peripheral | GPIO Pins |
|---|---|
| MDA TTL Video | Video `21`, HSync `23`, VSync `15` (`720×350 @ 50Hz`) |
| PS/2 Keyboard (Port 0) | CLK `33`, DATA `32` |
| PS/2 Mouse (Port 1) | CLK `26`, DATA `27` |
| MicroSD Card | CS `13`, MOSI `12`, MISO `2`, CLK `14` |

## Setup Guide (Step-by-Step)

### 1. Flash MDA Desktop (Once)
1. Open `MDA_Desktop/MDA_Desktop.ino` in Arduino IDE.
2. Under **Tools -> Partition Scheme**, select an OTA partition scheme:
   - **`Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)`** *(recommended — gives every launchable app up to 1.9 MB!)*
   - or `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)`
3. Upload `MDA_Desktop` to your LilyGO VGA32.

### 2. Export MDA Paint (or Any App) to Your SD Card
1. Open `MDA_Paint/MDA_Paint.ino` in Arduino IDE (using the same board/partition scheme).
2. Click **Sketch -> Export Compiled Binary**.
3. On a FAT32 microSD card, create a folder named **`/APPS`**.
4. Copy the exported `MDA_Paint.ino.bin` into `/APPS/` (e.g. `/APPS/MDAPAINT.BIN`).
5. *(Optional)* To give any app a custom icon, place a **`32×24` 1-bit monochrome `.BMP`** with the same base name next to it (e.g. `/APPS/MDAPAINT.BMP`). Otherwise MDA Desktop automatically chooses a crisp built-in icon based on the filename (`*PAINT*`, `*TERM*`/`*NET*`, `*NOTE*`/`*EDIT*`, `*CALC*`, `*GAME*`, or generic app window).
6. Insert the SD card into the LilyGO VGA32 (or press **`F5`** / `Start -> Remount SD` if inserted after boot). **MDA Paint** appears right on your desktop!

### 3. Making New Launchable Apps
Copy `mda_launcher_sdk.h` into any new Arduino sketch and add 2 lines:

```cpp
#include "mda_launcher_sdk.h"

void setup() {
  mdaArmDesktopOnReset(); // Hardware RST/crash safely returns to MDA Desktop
  int mouseSens = mdaLoadMouseSens(2, 5); // Share mouse speed with Desktop
  ...
}

// When user clicks Exit or presses Alt+F4:
mdaReturnToDesktop();
```

## Features & Controls

- **Built-In Notepad Window (`Notepad` desktop icon or `Start -> Notepad`)**:
  - Runs directly inside a draggable Win95 monochrome window on top of your desktop (no reboot required!).
  - Full 16 KB plain-text editor with mouse click/drag selection, `Shift+Arrows`, Cut/Copy/Paste (`Ctrl+X/C/V`), Select All (`Ctrl+A`), Word Wrap toggle, and compact `6×8` / large `8×8` font toggle.
  - Integrated SD Card File Browser (`File -> Open...` / `Ctrl+O`, `File -> Save` / `Ctrl+S`, `File -> Save As...`) that reads & writes standard `.TXT` (and `.MD`/`.LOG`/`.INI`/`.C`/`.H`) files seamlessly across Windows and Linux.
- **16 Settable Monochrome Wallpaper Patterns** (`Wallpaper` desktop icon, `Start -> Wallpaper...`, or Right-Click Desktop): Includes a classic Win95 miniature CRT preview screen; selection is saved permanently in NVS.
- **Taskbar & Start Menu**: `[ Start ]` button with cascading `Programs >` submenu, active window tab, `SD:Y/N` indicator, clickable mouse speed badge `S:1..6`, and live uptime clock.
- **Draggable Built-In Windows**: Grab any dialog's title bar to drag it around the desktop.
- **Keyboard Shortcuts**:
  - `Alt` / `F10` / `Ctrl+Esc` / `Win`: Open Start Menu
  - `Arrow Keys` + `Enter`: Navigate desktop icons, menus, and dialogs
  - `-` / `+` (`=`): Adjust PS/2 mouse sensitivity (Levels 1–6, persisted in NVS and shared with MDA Paint)
  - `F5`: Remount & rescan SD card
  - `F1`: Help window
  - `Esc`: Close menu or active window
