#pragma once
#include <Arduino.h>
#include <fabgl.h>
#include <SD.h>
#include <SPI.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "mda_sd_launcher.h"

// ============================================================================
// MDA Desktop - Integrated Win95-Style Monochrome Notepad Window
// Features:
//   - Runs directly in a draggable window under MDA Desktop (no reboot!)
//   - 16 KB plain-text buffer (ASCII), full selection, Cut/Copy/Paste (4 KB)
//   - Word Wrap toggle + 6x8 / 8x8 font size toggle
//   - Mouse click-to-place cursor, mouse drag-to-select, scroll wheel & bar
//   - Built-in SD Card File Browser for Open / Save / Save As (.TXT/.MD/etc.)
//   - Cleanly reads Windows (CRLF) and Linux (LF) text files
// ============================================================================

static constexpr int NP_MAX_BYTES = 16384;
static constexpr int NP_CLIP_MAX  = 4096;
static constexpr int NP_MAX_VLINES = 1024;
static constexpr int NP_MAX_FILES  = 64;

enum NotepadFBMode : uint8_t { NPFB_CLOSED = 0, NPFB_OPEN, NPFB_SAVE };

struct NotepadFileEntry {
  char     name[32];
  bool     isDir;
  uint32_t size;
};

struct NotepadVLine {
  uint16_t start;
  uint16_t len;
};

class NotepadApp {
public:
  // Text & clipboard buffers (lazily allocated)
  char * text       = nullptr;
  char * clip       = nullptr;
  int    len        = 0;
  int    clipLen    = 0;
  int    cursor     = 0;
  int    selAnchor  = -1; // -1 = no active selection
  bool   modified   = false;
  char   filePath[96] = "";

  // View & formatting
  bool   wordWrap   = true;
  bool   largeFont  = false; // false = 6x8 (86 cols), true = 8x8 (64 cols)
  int    scrollLine = 0;
  int    scrollCol  = 0;
  bool   mouseSelecting = false;
  bool   caretOn    = true;
  uint32_t lastBlinkMs = 0;

  // Visual line layout cache
  NotepadVLine * vlines = nullptr;
  int            vlineCount = 0;
  int            visCols    = 84;
  int            visRows    = 19;

  // Internal Notepad menu bar (0=File, 1=Edit, 2=Format)
  int    openMenu   = -1;
  int    hoverItem  = -1;

  // Internal SD Card File Browser state
  NotepadFBMode    fbMode = NPFB_CLOSED;
  char             fbDir[64] = "/";
  NotepadFileEntry fbEntries[NP_MAX_FILES];
  int              fbCount = 0;
  int              fbSel   = 0;
  int              fbTop   = 0;
  char             fbName[32] = "NOTES01.TXT";
  int              fbNameLen  = 11;
  bool             fbTyping   = false;
  bool             fbConfirmDel = false;
  char             fbStatus[56] = "";
  int              fbLastClick  = -1;
  uint32_t         fbLastClickMs = 0;

  // Status message inside Notepad bottom bar
  char             msg[48]   = "";
  uint32_t         msgUntil  = 0;

  bool ensureInit() {
    if (!text)   text   = (char *)calloc(NP_MAX_BYTES + 1, 1);
    if (!clip)   clip   = (char *)calloc(NP_CLIP_MAX + 1, 1);
    if (!vlines) vlines = (NotepadVLine *)malloc(sizeof(NotepadVLine) * NP_MAX_VLINES);
    return (text && clip && vlines);
  }

  void setMsg(const char * s, uint32_t ms = 2400) {
    strncpy(msg, s, sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = '\0';
    msgUntil = millis() + ms;
  }

  void clearDoc() {
    if (!ensureInit()) return;
    len = 0;
    text[0] = '\0';
    cursor = 0;
    selAnchor = -1;
    scrollLine = 0;
    scrollCol = 0;
    modified = false;
    filePath[0] = '\0';
    setMsg("New Document", 1800);
  }

  inline bool hasSelection() const {
    return (selAnchor >= 0 && selAnchor != cursor);
  }

  inline void getSelRange(int & a, int & b) const {
    if (!hasSelection()) { a = b = cursor; return; }
    a = min(selAnchor, cursor);
    b = max(selAnchor, cursor);
  }

  void deleteSelection() {
    if (!hasSelection()) return;
    int a, b;
    getSelRange(a, b);
    memmove(text + a, text + b, (size_t)(len - b + 1));
    len -= (b - a);
    cursor = a;
    selAnchor = -1;
    modified = true;
  }

  void insertChar(char c) {
    if (hasSelection()) deleteSelection();
    if (len >= NP_MAX_BYTES) {
      setMsg("Buffer full (16 KB max)", 2000);
      return;
    }
    memmove(text + cursor + 1, text + cursor, (size_t)(len - cursor + 1));
    text[cursor++] = c;
    len++;
    modified = true;
  }

  void insertText(const char * s, int slen) {
    if (hasSelection()) deleteSelection();
    int avail = NP_MAX_BYTES - len;
    if (slen > avail) slen = avail;
    if (slen <= 0) return;
    memmove(text + cursor + slen, text + cursor, (size_t)(len - cursor + 1));
    memcpy(text + cursor, s, (size_t)slen);
    cursor += slen;
    len += slen;
    modified = true;
  }

  void copySelection() {
    if (!hasSelection()) {
      setMsg("Select text first (Shift+Arrows)", 2000);
      return;
    }
    int a, b;
    getSelRange(a, b);
    int n = min(b - a, NP_CLIP_MAX);
    memcpy(clip, text + a, (size_t)n);
    clip[n] = '\0';
    clipLen = n;
    setMsg("Copied to Clipboard", 1600);
  }

  void cutSelection() {
    if (!hasSelection()) return;
    copySelection();
    deleteSelection();
    setMsg("Cut to Clipboard", 1600);
  }

  void pasteClipboard() {
    if (clipLen <= 0) {
      setMsg("Clipboard Empty", 1600);
      return;
    }
    insertText(clip, clipLen);
    setMsg("Pasted", 1400);
  }

  // ---- Visual Line Wrapping & Cursor Mapping ----
  int charW() const { return largeFont ? 8 : 6; }
  int rowH()  const { return 10; }

  void layoutMetrics(int winW, int winH) {
    int textAreaW = winW - 30;
    int textAreaH = winH - 58;
    visCols = max(16, textAreaW / charW());
    visRows = max(6, textAreaH / rowH());
  }

  void computeVLines() {
    vlineCount = 0;
    if (!text || !vlines) return;
    int pos = 0;
    while (pos <= len && vlineCount < NP_MAX_VLINES) {
      int lineStart = pos;
      // Find next newline or end
      int nl = pos;
      while (nl < len && text[nl] != '\n') nl++;
      int segLen = nl - pos;

      if (!wordWrap || segLen <= visCols) {
        vlines[vlineCount++] = { (uint16_t)lineStart, (uint16_t)segLen };
        pos = (nl < len) ? (nl + 1) : (len + 1);
      } else {
        // Wrap cleanly at last space within visCols if possible
        int wrapLen = visCols;
        for (int k = visCols; k > visCols / 2; --k) {
          if (text[pos + k - 1] == ' ') { wrapLen = k; break; }
        }
        vlines[vlineCount++] = { (uint16_t)lineStart, (uint16_t)wrapLen };
        pos += wrapLen;
      }
    }
    if (vlineCount == 0) {
      vlines[vlineCount++] = { 0, 0 };
    }
  }

  void findCursorRC(int & outRow, int & outCol) const {
    outRow = 0; outCol = 0;
    for (int r = 0; r < vlineCount; ++r) {
      int s = vlines[r].start;
      int e = s + vlines[r].len;
      bool isLast = (r == vlineCount - 1);
      // If cursor lies strictly inside [s..e], or at end of a hard-terminated line
      if (cursor >= s && cursor <= e) {
        // If wrapped line and cursor == e and next line starts at e, prefer next line start
        if (cursor == e && !isLast && vlines[r + 1].start == e) {
          outRow = r + 1;
          outCol = 0;
        } else {
          outRow = r;
          outCol = cursor - s;
        }
        return;
      }
    }
    outRow = max(0, vlineCount - 1);
    outCol = vlines[outRow].len;
  }

  void ensureCursorVisible() {
    computeVLines();
    int r, c;
    findCursorRC(r, c);
    if (r < scrollLine) scrollLine = r;
    if (r >= scrollLine + visRows) scrollLine = r - visRows + 1;
    int maxScroll = max(0, vlineCount - visRows);
    scrollLine = constrain(scrollLine, 0, maxScroll);

    if (wordWrap) {
      scrollCol = 0;
    } else {
      if (c < scrollCol) scrollCol = c;
      if (c >= scrollCol + visCols) scrollCol = c - visCols + 1;
      if (scrollCol < 0) scrollCol = 0;
    }
  }

  bool isSoftWrappedRow(int row) const {
    if (row < 0 || row + 1 >= vlineCount) return false;
    return (vlines[row + 1].start == vlines[row].start + vlines[row].len) && (vlines[row].len > 0);
  }

  int rcToOffset(int row, int col) const {
    if (vlineCount <= 0) return 0;
    row = constrain(row, 0, vlineCount - 1);
    int maxCol = isSoftWrappedRow(row) ? (int)vlines[row].len - 1 : (int)vlines[row].len;
    col = constrain(col, 0, max(0, maxCol));
    return vlines[row].start + col;
  }

  // ---- SD Card File I/O & Directory Browser ----
  static void buildPath(char * out, size_t outSz, const char * dir, const char * name) {
    if (strcmp(dir, "/") == 0) snprintf(out, outSz, "/%s", name);
    else                       snprintf(out, outSz, "%s/%s", dir, name);
  }

  static bool isBinaryExt(const char * name) {
    size_t n = strlen(name);
    if (n < 4) return false;
    const char * e = name + n - 4;
    if (e[0] != '.') return false;
    return (strcasecmp(e, ".BIN") == 0 || strcasecmp(e, ".BMP") == 0);
  }

  bool fbRefreshDir() {
    fbCount = 0;
    if (!gSDMounted && !sdMountAndScanApps(false)) return false;

    File dir = SD.open(fbDir);
    if (!dir || !dir.isDirectory()) {
      strcpy(fbDir, "/");
      dir = SD.open("/");
      if (!dir || !dir.isDirectory()) {
        gSDMounted = false;
        return false;
      }
    }

    if (strcmp(fbDir, "/") != 0) {
      strcpy(fbEntries[fbCount].name, "..");
      fbEntries[fbCount].isDir = true;
      fbEntries[fbCount].size  = 0;
      fbCount++;
    }

    File entry = dir.openNextFile();
    while (entry && fbCount < NP_MAX_FILES) {
      const char * raw = entry.name();
      const char * base = strrchr(raw, '/');
      base = base ? (base + 1) : raw;
      bool isD = entry.isDirectory();
      if (base[0] != '\0' && base[0] != '.' && (isD || !isBinaryExt(base))) {
        strncpy(fbEntries[fbCount].name, base, sizeof(fbEntries[0].name) - 1);
        fbEntries[fbCount].name[sizeof(fbEntries[0].name) - 1] = '\0';
        fbEntries[fbCount].isDir = isD;
        fbEntries[fbCount].size  = isD ? 0 : (uint32_t)entry.size();
        fbCount++;
      }
      entry.close();
      entry = dir.openNextFile();
    }
    dir.close();

    // Sort: ".." first, then directories, then files alphabetically
    for (int i = 0; i < fbCount - 1; ++i) {
      for (int j = i + 1; j < fbCount; ++j) {
        bool sw = false;
        const NotepadFileEntry & a = fbEntries[i];
        const NotepadFileEntry & b = fbEntries[j];
        if (strcmp(b.name, "..") == 0) sw = true;
        else if (strcmp(a.name, "..") != 0) {
          if (a.isDir != b.isDir) sw = (!a.isDir && b.isDir);
          else                    sw = (strcasecmp(a.name, b.name) > 0);
        }
        if (sw) {
          NotepadFileEntry tmp = fbEntries[i];
          fbEntries[i] = fbEntries[j];
          fbEntries[j] = tmp;
        }
      }
    }
    return true;
  }

  void fbGoParent() {
    size_t l = strlen(fbDir);
    if (l <= 1) { strcpy(fbDir, "/"); return; }
    if (fbDir[l - 1] == '/') { fbDir[l - 1] = '\0'; l--; }
    char * s = strrchr(fbDir, '/');
    if (!s || s == fbDir) strcpy(fbDir, "/");
    else *s = '\0';
  }

  void suggestNextTxtName() {
    if (!gSDMounted) { strcpy(fbName, "NOTES01.TXT"); fbNameLen = 11; return; }
    char path[96];
    for (int i = 1; i <= 99; ++i) {
      char cand[20];
      snprintf(cand, sizeof(cand), "NOTES%02d.TXT", i);
      buildPath(path, sizeof(path), fbDir, cand);
      if (!SD.exists(path)) {
        strncpy(fbName, cand, sizeof(fbName) - 1);
        fbName[sizeof(fbName) - 1] = '\0';
        fbNameLen = (int)strlen(fbName);
        return;
      }
    }
    strcpy(fbName, "NOTES99.TXT");
    fbNameLen = 11;
  }

  bool loadFromSD(const char * path, char * errOut, size_t errSz) {
    if (!ensureInit()) { snprintf(errOut, errSz, "Out of memory"); return false; }
    if (!gSDMounted && !sdMountAndScanApps(false)) {
      snprintf(errOut, errSz, "SD card not mounted");
      return false;
    }
    File f = SD.open(path, FILE_READ);
    if (!f) { snprintf(errOut, errSz, "Cannot open file"); return false; }

    int outIdx = 0;
    bool truncated = false;
    uint8_t chunk[256];
    while (true) {
      int n = f.read(chunk, sizeof(chunk));
      if (n <= 0) break;
      for (int i = 0; i < n; ++i) {
        uint8_t ch = chunk[i];
        if (ch == '\r') continue; // Normalize Windows CRLF -> LF cleanly
        if (ch == '\t') {
          for (int t = 0; t < 2 && outIdx < NP_MAX_BYTES; ++t) text[outIdx++] = ' ';
          continue;
        }
        if (ch == '\n' || (ch >= 32 && ch <= 126)) {
          if (outIdx < NP_MAX_BYTES) text[outIdx++] = (char)ch;
          else { truncated = true; break; }
        }
      }
      if (truncated) break;
    }
    f.close();
    text[outIdx] = '\0';
    len        = outIdx;
    cursor     = 0;
    selAnchor  = -1;
    scrollLine = 0;
    scrollCol  = 0;
    modified   = false;
    strncpy(filePath, path, sizeof(filePath) - 1);
    filePath[sizeof(filePath) - 1] = '\0';
    setMsg(truncated ? "Loaded (truncated at 16 KB)" : "File Loaded OK", 2400);
    return true;
  }

  bool saveToSD(const char * path, char * errOut, size_t errSz) {
    if (!ensureInit()) { snprintf(errOut, errSz, "Out of memory"); return false; }
    if (!gSDMounted && !sdMountAndScanApps(false)) {
      snprintf(errOut, errSz, "SD card not mounted");
      return false;
    }
    if (SD.exists(path)) SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) { snprintf(errOut, errSz, "Cannot create file"); return false; }

    size_t written = f.write((const uint8_t *)text, (size_t)len);
    f.flush();
    f.close();
    if ((int)written != len) {
      snprintf(errOut, errSz, "Write incomplete (%u/%d B)", (unsigned)written, len);
      return false;
    }
    modified = false;
    strncpy(filePath, path, sizeof(filePath) - 1);
    filePath[sizeof(filePath) - 1] = '\0';
    const char * base = strrchr(path, '/');
    char m[48];
    snprintf(m, sizeof(m), "Saved: %.34s", base ? (base + 1) : path);
    setMsg(m, 2400);
    return true;
  }

  void openFileBrowser(NotepadFBMode mode) {
    openMenu     = -1;
    fbMode       = mode;
    fbSel        = 0;
    fbTop        = 0;
    fbConfirmDel = false;
    fbTyping     = (mode == NPFB_SAVE);
    fbLastClick  = -1;
    fbStatus[0]  = '\0';

    if (!fbRefreshDir()) {
      snprintf(fbStatus, sizeof(fbStatus), "SD Not Found - Insert Card & Click REMOUNT");
    }
    if (mode == NPFB_SAVE) {
      if (filePath[0]) {
        const char * base = strrchr(filePath, '/');
        base = base ? (base + 1) : filePath;
        strncpy(fbName, base, sizeof(fbName) - 1);
        fbName[sizeof(fbName) - 1] = '\0';
        fbNameLen = (int)strlen(fbName);
      } else {
        suggestNextTxtName();
      }
    }
  }

  void fbDoPrimary() {
    fbConfirmDel = false;
    if (!gSDMounted) {
      snprintf(fbStatus, sizeof(fbStatus), "No SD card mounted! Click REMOUNT.");
      return;
    }
    bool selIsDir = (fbCount > 0 && fbSel >= 0 && fbSel < fbCount && fbEntries[fbSel].isDir);
    if (selIsDir && (fbMode == NPFB_OPEN || !fbTyping)) {
      if (strcmp(fbEntries[fbSel].name, "..") == 0) fbGoParent();
      else {
        char nxt[64];
        buildPath(nxt, sizeof(nxt), fbDir, fbEntries[fbSel].name);
        strncpy(fbDir, nxt, sizeof(fbDir) - 1);
        fbDir[sizeof(fbDir) - 1] = '\0';
      }
      fbSel = 0; fbTop = 0; fbStatus[0] = '\0';
      fbRefreshDir();
      return;
    }

    if (fbMode == NPFB_OPEN) {
      if (fbCount <= 0 || fbSel < 0 || fbSel >= fbCount) {
        snprintf(fbStatus, sizeof(fbStatus), "Select a text file to open");
        return;
      }
      char full[96];
      buildPath(full, sizeof(full), fbDir, fbEntries[fbSel].name);
      char err[48] = "";
      if (loadFromSD(full, err, sizeof(err))) {
        fbMode = NPFB_CLOSED;
      } else {
        snprintf(fbStatus, sizeof(fbStatus), "Open failed: %.40s", err);
      }
    } else if (fbMode == NPFB_SAVE) {
      if (fbNameLen <= 0) {
        snprintf(fbStatus, sizeof(fbStatus), "Enter a filename first!");
        return;
      }
      char nm[32];
      strncpy(nm, fbName, sizeof(nm) - 1);
      nm[sizeof(nm) - 1] = '\0';
      if (!strchr(nm, '.') && strlen(nm) + 4 < sizeof(nm)) strcat(nm, ".TXT");
      char full[96];
      buildPath(full, sizeof(full), fbDir, nm);
      char err[48] = "";
      if (saveToSD(full, err, sizeof(err))) {
        fbMode = NPFB_CLOSED;
      } else {
        snprintf(fbStatus, sizeof(fbStatus), "Save failed: %.40s", err);
      }
    }
  }

  void fbDoDelete() {
    if (!gSDMounted || fbCount <= 0 || fbSel < 0 || fbSel >= fbCount) return;
    if (fbEntries[fbSel].isDir) {
      snprintf(fbStatus, sizeof(fbStatus), "Cannot delete folders here");
      fbConfirmDel = false;
      return;
    }
    if (!fbConfirmDel) {
      fbConfirmDel = true;
      snprintf(fbStatus, sizeof(fbStatus), "Delete %.18s? Click CONFIRM! or Del", fbEntries[fbSel].name);
      return;
    }
    char full[96];
    buildPath(full, sizeof(full), fbDir, fbEntries[fbSel].name);
    if (SD.remove(full)) {
      snprintf(fbStatus, sizeof(fbStatus), "Deleted %s", fbEntries[fbSel].name);
      if (strcmp(filePath, full) == 0) filePath[0] = '\0';
    } else {
      snprintf(fbStatus, sizeof(fbStatus), "Delete failed!");
    }
    fbConfirmDel = false;
    fbRefreshDir();
    fbSel = constrain(fbSel, 0, max(0, fbCount - 1));
  }

  // ---- Menu Definitions ----
  static constexpr int NP_MENU_COUNT = 3;
  static const char * menuTitle(int i) {
    static const char * t[3] = { "File", "Edit", "Format" };
    return (i >= 0 && i < 3) ? t[i] : "";
  }
  static int menuXOff(int i) {
    static const int o[3] = { 6, 48, 90 };
    return (i >= 0 && i < 3) ? o[i] : 6;
  }
  static int menuW(int i) {
    static const int w[3] = { 40, 40, 52 };
    return (i >= 0 && i < 3) ? w[i] : 40;
  }
  static int menuItemCount(int m) {
    if (m == 0) return 5;
    if (m == 1) return 6;
    if (m == 2) return 2;
    return 0;
  }
  const char * menuItemLabel(int m, int idx, const char *& shortcut) const {
    shortcut = "";
    if (m == 0) {
      static const char * L[5] = { "New", "Open...", "Save", "Save As...", "Close" };
      static const char * S[5] = { "Ctrl+N", "Ctrl+O", "Ctrl+S", "", "Alt+F4" };
      shortcut = S[idx]; return L[idx];
    }
    if (m == 1) {
      static const char * L[6] = { "Cut", "Copy", "Paste", "Delete", "Select All", "Clear All" };
      static const char * S[6] = { "Ctrl+X", "Ctrl+C", "Ctrl+V", "Del", "Ctrl+A", "" };
      shortcut = S[idx]; return L[idx];
    }
    if (m == 2) {
      if (idx == 0) return wordWrap ? "[x] Word Wrap" : "[ ] Word Wrap";
      return largeFont ? "Font: 8x8 (Large)" : "Font: 6x8 (Compact)";
    }
    return "";
  }

  // Returns true if Notepad requested closing itself!
  bool runMenuItem(int m, int idx) {
    openMenu = -1;
    if (m == 0) {
      if (idx == 0) { clearDoc(); return false; }
      if (idx == 1) { openFileBrowser(NPFB_OPEN); return false; }
      if (idx == 2) {
        if (filePath[0]) {
          char err[48] = "";
          if (!saveToSD(filePath, err, sizeof(err))) setMsg(err, 2600);
        } else {
          openFileBrowser(NPFB_SAVE);
        }
        return false;
      }
      if (idx == 3) { openFileBrowser(NPFB_SAVE); return false; }
      if (idx == 4) { return true; } // Close window
    } else if (m == 1) {
      if (idx == 0) cutSelection();
      else if (idx == 1) copySelection();
      else if (idx == 2) pasteClipboard();
      else if (idx == 3) {
        if (hasSelection()) deleteSelection();
        else if (cursor < len) {
          memmove(text + cursor, text + cursor + 1, (size_t)(len - cursor));
          len--; modified = true;
        }
      } else if (idx == 4) {
        selAnchor = 0; cursor = len;
      } else if (idx == 5) {
        len = 0; text[0] = '\0'; cursor = 0; selAnchor = -1; modified = true;
      }
    } else if (m == 2) {
      if (idx == 0) { wordWrap = !wordWrap; scrollCol = 0; }
      else if (idx == 1) { largeFont = !largeFont; }
    }
    ensureCursorVisible();
    return false;
  }

  // ---- Rendering (Draws directly into Canvas at Desktop's winX/winY/winW/winH) ----
  void getWindowTitle(char * out, size_t outSz) const {
    const char * base = "Untitled";
    if (filePath[0]) {
      const char * s = strrchr(filePath, '/');
      base = s ? (s + 1) : filePath;
    }
    snprintf(out, outSz, "Notepad - %.28s%s", base, modified ? "*" : "");
  }

  void drawEditorBody(fabgl::Canvas & canvas, int wx, int wy, int ww, int wh) {
    if (!ensureInit()) return;
    layoutMetrics(ww, wh);
    computeVLines();

    // 1. Notepad Menu Bar (y = wy + 22 .. wy + 34)
    int my = wy + 22;
    canvas.setBrushColor(fabgl::Color::Black);
    canvas.fillRectangle(wx + 4, my, wx + ww - 5, my + 13);
    canvas.setPenColor(fabgl::Color::White);
    canvas.drawLine(wx + 4, my + 13, wx + ww - 5, my + 13);
    canvas.selectFont(&fabgl::FONT_6x8);
    canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));

    for (int i = 0; i < NP_MENU_COUNT; ++i) {
      int mx = wx + menuXOff(i);
      int mw = menuW(i);
      if (openMenu == i) {
        canvas.setBrushColor(fabgl::Color::White);
        canvas.fillRectangle(mx, my + 1, mx + mw - 1, my + 12);
        canvas.setPenColor(fabgl::Color::Black);
      } else {
        canvas.setPenColor(fabgl::Color::White);
      }
      canvas.drawText(mx + 4, my + 3, menuTitle(i));
      // Underline accelerator: _F_ile, _E_dit, F_o_rmat
      int ulOff = (i == 2) ? 10 : 4;
      canvas.drawLine(mx + ulOff, my + 11, mx + ulOff + 5, my + 11);
    }

    // 2. Text Client Area (wx + 6 .. wx + ww - 22, wy + 37 .. wy + wh - 19)
    int tx = wx + 6;
    int ty = wy + 37;
    int tw = ww - 28;
    int th = wh - 56;
    canvas.setBrushColor(fabgl::Color::Black);
    canvas.fillRectangle(tx, ty, tx + tw - 1, ty + th - 1);

    int curRow, curCol;
    findCursorRC(curRow, curCol);
    int selA, selB;
    getSelRange(selA, selB);
    bool hasSel = hasSelection();

    int cw = charW();
    int rh = rowH();
    canvas.selectFont(largeFont ? &fabgl::FONT_8x8 : &fabgl::FONT_6x8);

    char rowStr[100];
    for (int vr = 0; vr < visRows; ++vr) {
      int lidx = scrollLine + vr;
      if (lidx >= vlineCount) break;
      int ry = ty + 2 + vr * rh;
      int start = vlines[lidx].start;
      int llen  = vlines[lidx].len;

      int c0 = wordWrap ? 0 : scrollCol;
      int drawChars = max(0, min(visCols, llen - c0));
      if (drawChars > 0) {
        int copyN = min(drawChars, (int)sizeof(rowStr) - 1);
        memcpy(rowStr, text + start + c0, (size_t)copyN);
        rowStr[copyN] = '\0';

        // Draw normal white text
        canvas.setPenColor(fabgl::Color::White);
        canvas.drawText(tx + 2, ry, rowStr);

        // Overlay inverted highlight for any selected span on this line
        if (hasSel) {
          int lineVisA = start + c0;
          int lineVisB = lineVisA + copyN;
          int ovA = max(selA, lineVisA);
          int ovB = min(selB, lineVisB);
          if (ovA < ovB) {
            int sx0 = tx + 2 + (ovA - lineVisA) * cw;
            int sx1 = tx + 2 + (ovB - lineVisA) * cw - 1;
            canvas.setBrushColor(fabgl::Color::White);
            canvas.fillRectangle(sx0, ry - 1, sx1, ry + 8);
            char selSub[100];
            int sn = min(ovB - ovA, (int)sizeof(selSub) - 1);
            memcpy(selSub, text + ovA, (size_t)sn);
            selSub[sn] = '\0';
            canvas.setPenColor(fabgl::Color::Black);
            canvas.drawText(sx0, ry, selSub);
          }
        }
      }

      // Draw crisp blinking 2px caret if on this visible row
      if (lidx == curRow && caretOn && openMenu < 0 && fbMode == NPFB_CLOSED) {
        int colVis = curCol - c0;
        if (colVis >= 0 && colVis <= visCols) {
          int cx = tx + 2 + colVis * cw;
          canvas.setBrushColor(fabgl::Color::White);
          canvas.fillRectangle(cx, ry - 1, cx + 1, ry + 8);
        }
      }
    }

    // 3. Vertical Scrollbar on right of editor
    int sbX = tx + tw + 1;
    int sbW = 16;
    canvas.selectFont(&fabgl::FONT_6x8);
    canvas.setBrushColor(fabgl::Color::Black);
    canvas.fillRectangle(sbX, ty, sbX + sbW - 1, ty + th - 1);
    canvas.setPenColor(fabgl::Color::White);
    canvas.drawRectangle(sbX, ty, sbX + sbW - 1, ty + th - 1);
    canvas.drawRectangle(sbX, ty, sbX + sbW - 1, ty + 14);
    canvas.drawText(sbX + 5, ty + 3, "^");
    canvas.drawRectangle(sbX, ty + th - 15, sbX + sbW - 1, ty + th - 1);
    canvas.drawText(sbX + 5, ty + th - 11, "v");

    int trackY = ty + 16;
    int trackH = max(8, th - 32);
    if (vlineCount > visRows) {
      int thumbH = max(12, (trackH * visRows) / vlineCount);
      int maxTop = max(1, vlineCount - visRows);
      int thumbY = trackY + ((trackH - thumbH) * scrollLine) / maxTop;
      canvas.setBrushColor(fabgl::Color::White);
      canvas.fillRectangle(sbX + 3, thumbY, sbX + sbW - 4, thumbY + thumbH - 1);
    } else {
      canvas.setBrushColor(fabgl::Color::White);
      canvas.fillRectangle(sbX + 3, trackY + 1, sbX + sbW - 4, trackY + trackH - 2);
    }

    // 4. Bottom Window Status Strip (wy + wh - 16 .. wy + wh - 5)
    int sy = wy + wh - 17;
    canvas.setBrushColor(fabgl::Color::Black);
    canvas.fillRectangle(wx + 4, sy, wx + ww - 5, wy + wh - 4);
    canvas.setPenColor(fabgl::Color::White);
    canvas.drawLine(wx + 4, sy, wx + ww - 5, sy);

    if (msg[0] && millis() < msgUntil) {
      canvas.setBrushColor(fabgl::Color::White);
      canvas.fillRectangle(wx + 6, sy + 2, wx + 290, wy + wh - 5);
      canvas.setPenColor(fabgl::Color::Black);
      canvas.drawText(wx + 10, sy + 4, msg);
    } else {
      msg[0] = '\0';
      char st[96];
      snprintf(st, sizeof(st), "Ln %d, Col %d | %d B | %s | SD:%c | Ctrl+S:Save Ctrl+O:Open",
               curRow + 1, curCol + 1, len,
               wordWrap ? "Wrap" : "NoWrap",
               gSDMounted ? 'Y' : 'N');
      canvas.drawText(wx + 8, sy + 4, st);
    }

    // 5. Open Pull-Down Menu Popup
    if (openMenu >= 0) {
      int px = wx + menuXOff(openMenu);
      int py = my + 14;
      int pw = 140;
      int cnt = menuItemCount(openMenu);
      int ph = cnt * 14 + 4;
      canvas.setBrushColor(fabgl::Color::Black);
      canvas.fillRectangle(px, py, px + pw - 1, py + ph - 1);
      canvas.setPenColor(fabgl::Color::White);
      canvas.drawRectangle(px, py, px + pw - 1, py + ph - 1);
      canvas.drawLine(px + 2, py + ph, px + pw, py + ph);
      canvas.drawLine(px + pw, py + 2, px + pw, py + ph);

      for (int i = 0; i < cnt; ++i) {
        int iy = py + 2 + i * 14;
        bool hov = (i == hoverItem);
        if (hov) {
          canvas.setBrushColor(fabgl::Color::White);
          canvas.fillRectangle(px + 2, iy, px + pw - 3, iy + 13);
          canvas.setPenColor(fabgl::Color::Black);
        } else {
          canvas.setPenColor(fabgl::Color::White);
        }
        const char * sc = "";
        const char * lbl = menuItemLabel(openMenu, i, sc);
        canvas.drawText(px + 6, iy + 3, lbl);
        if (sc[0]) {
          canvas.drawText(px + pw - (int)strlen(sc) * 6 - 6, iy + 3, sc);
        }
      }
    }

    // 6. Integrated SD File Browser Overlay (centered inside Notepad window!)
    if (fbMode != NPFB_CLOSED) {
      drawFileBrowserOverlay(canvas, wx, wy, ww, wh);
    }
  }

  void drawFileBrowserOverlay(fabgl::Canvas & canvas, int wx, int wy, int ww, int wh) {
    int bx = wx + 36, by = wy + 26, bw = ww - 72, bh = wh - 42;
    canvas.setBrushColor(fabgl::Color::Black);
    canvas.fillRectangle(bx, by, bx + bw - 1, by + bh - 1);
    canvas.setPenColor(fabgl::Color::White);
    canvas.drawRectangle(bx, by, bx + bw - 1, by + bh - 1);
    canvas.drawRectangle(bx + 2, by + 2, bx + bw - 3, by + bh - 3);

    // Title
    canvas.setBrushColor(fabgl::Color::White);
    canvas.fillRectangle(bx + 4, by + 4, bx + bw - 5, by + 17);
    canvas.selectFont(&fabgl::FONT_8x8);
    canvas.setGlyphOptions(fabgl::GlyphOptions().FillBackground(false));
    canvas.setPenColor(fabgl::Color::Black);
    const char * ttl = (fbMode == NPFB_OPEN) ? "OPEN TEXT FILE (SD)" : "SAVE TEXT FILE (SD)";
    canvas.drawText(bx + (bw - (int)strlen(ttl) * 8) / 2, by + 7, ttl);

    canvas.selectFont(&fabgl::FONT_6x8);
    canvas.setPenColor(fabgl::Color::White);
    char dline[64];
    snprintf(dline, sizeof(dline), "DIR: %.42s", fbDir);
    canvas.drawText(bx + 10, by + 23, dline);

    int lx = bx + 10, ly = by + 35, lw = bw - 38, lh = 120;
    static constexpr int FB_VIS = 8;
    canvas.drawRectangle(lx, ly, lx + lw - 1, ly + lh - 1);

    if (!gSDMounted) {
      canvas.drawText(lx + 14, ly + 44, "No SD card mounted (CS13 MOSI12 MISO2 CLK14).");
      canvas.drawText(lx + 14, ly + 58, "Insert FAT32 microSD and click [REMOUNT].");
    } else if (fbCount == 0) {
      canvas.drawText(lx + 14, ly + 48, "(No text files or folders in this directory)");
    } else {
      fbSel = constrain(fbSel, 0, fbCount - 1);
      if (fbSel < fbTop) fbTop = fbSel;
      if (fbSel >= fbTop + FB_VIS) fbTop = fbSel - FB_VIS + 1;
      fbTop = constrain(fbTop, 0, max(0, fbCount - FB_VIS));

      for (int r = 0; r < FB_VIS; ++r) {
        int idx = fbTop + r;
        if (idx >= fbCount) break;
        int ry = ly + 3 + r * 14;
        bool sel = (idx == fbSel);
        if (sel) {
          canvas.setBrushColor(fabgl::Color::White);
          canvas.fillRectangle(lx + 2, ry, lx + lw - 3, ry + 13);
          canvas.setPenColor(fabgl::Color::Black);
        } else {
          canvas.setPenColor(fabgl::Color::White);
        }
        char row[72];
        if (fbEntries[idx].isDir) {
          snprintf(row, sizeof(row), "[DIR]  %-32.32s", fbEntries[idx].name);
        } else {
          snprintf(row, sizeof(row), "       %-32.32s %5u B", fbEntries[idx].name, (unsigned)fbEntries[idx].size);
        }
        canvas.drawText(lx + 6, ry + 3, row);
      }
    }

    // Scrollbar
    int sbX = lx + lw + 2;
    canvas.setPenColor(fabgl::Color::White);
    canvas.drawRectangle(sbX, ly, sbX + 16, ly + lh - 1);
    canvas.drawRectangle(sbX, ly, sbX + 16, ly + 15);
    canvas.drawText(sbX + 5, ly + 4, "^");
    canvas.drawRectangle(sbX, ly + lh - 16, sbX + 16, ly + lh - 1);
    canvas.drawText(sbX + 5, ly + lh - 12, "v");

    // Filename input (Save) or hint (Open)
    int fy = ly + lh + 6;
    if (fbMode == NPFB_SAVE) {
      canvas.drawText(lx, fy + 4, "FILE:");
      canvas.drawRectangle(lx + 36, fy, bx + bw - 10, fy + 16);
      char disp[40];
      snprintf(disp, sizeof(disp), "%s_", fbName);
      canvas.drawText(lx + 42, fy + 4, disp);
    } else {
      canvas.drawText(lx, fy + 4, "DblClick/Enter:Open  Bksp:Parent  Del:Delete  Esc:Cancel");
    }

    // Status banner
    int sty = fy + 20;
    if (fbStatus[0]) {
      canvas.setBrushColor(fabgl::Color::White);
      canvas.fillRectangle(lx, sty, bx + bw - 10, sty + 12);
      canvas.setPenColor(fabgl::Color::Black);
      canvas.drawText(lx + 4, sty + 2, fbStatus);
    }

    // Bottom buttons
    int bty = by + bh - 28;
    struct Btn { int x; int w; const char * t; bool pri; };
    Btn btns[4] = {
      { bx + 10,  104, (fbMode == NPFB_OPEN) ? "OPEN" : "SAVE", true },
      { bx + 122, 104, fbConfirmDel ? "CONFIRM!" : "DELETE", fbConfirmDel },
      { bx + 234, 114, "REMOUNT", false },
      { bx + 356, 142, "CANCEL",  false }
    };
    for (int i = 0; i < 4; ++i) {
      canvas.setBrushColor(btns[i].pri ? fabgl::Color::White : fabgl::Color::Black);
      canvas.fillRectangle(btns[i].x, bty, btns[i].x + btns[i].w - 1, bty + 20);
      canvas.setPenColor(btns[i].pri ? fabgl::Color::Black : fabgl::Color::White);
      if (!btns[i].pri) canvas.drawRectangle(btns[i].x, bty, btns[i].x + btns[i].w - 1, bty + 20);
      int tw = (int)strlen(btns[i].t) * 6;
      canvas.drawText(btns[i].x + (btns[i].w - tw) / 2, bty + 6, btns[i].t);
    }
  }

  // ---- Mouse Input Handlers (returns true if window should close) ----
  bool handleMouseClick(int sx, int sy, int wx, int wy, int ww, int wh, bool & needRedraw) {
    needRedraw = true;
    // File browser captures clicks when open
    if (fbMode != NPFB_CLOSED) {
      int bx = wx + 36, by = wy + 26, bw = ww - 72, bh = wh - 42;
      int lx = bx + 10, ly = by + 35, lw = bw - 38, lh = 120;
      static constexpr int FB_VIS = 8;

      if (sx >= lx && sx < lx + lw && sy >= ly && sy < ly + lh) {
        int row = (sy - (ly + 3)) / 14;
        int idx = fbTop + row;
        if (row >= 0 && row < FB_VIS && idx >= 0 && idx < fbCount) {
          uint32_t now = millis();
          bool dbl = (idx == fbLastClick && (now - fbLastClickMs) <= 450);
          fbLastClick   = idx;
          fbLastClickMs = now;
          fbSel         = idx;
          fbConfirmDel  = false;
          fbStatus[0]   = '\0';
          fbTyping      = !fbEntries[idx].isDir;
          if (fbMode == NPFB_SAVE && !fbEntries[idx].isDir) {
            strncpy(fbName, fbEntries[idx].name, sizeof(fbName) - 1);
            fbName[sizeof(fbName) - 1] = '\0';
            fbNameLen = (int)strlen(fbName);
          }
          if (dbl) fbDoPrimary();
        }
        return false;
      }
      int sbX = lx + lw + 2;
      if (sx >= sbX && sx <= sbX + 16 && sy >= ly && sy <= ly + 15) {
        if (fbTop > 0) fbTop--;
        return false;
      }
      if (sx >= sbX && sx <= sbX + 16 && sy >= ly + lh - 16 && sy <= ly + lh) {
        if (fbTop + FB_VIS < fbCount) fbTop++;
        return false;
      }
      int bty = by + bh - 28;
      if (sy >= bty && sy <= bty + 20) {
        if (sx >= bx + 10 && sx < bx + 114) { fbDoPrimary(); return false; }
        if (sx >= bx + 122 && sx < bx + 226) { fbDoDelete(); return false; }
        if (sx >= bx + 234 && sx < bx + 348) {
          fbConfirmDel = false;
          if (sdMountAndScanApps(true) && fbRefreshDir()) snprintf(fbStatus, sizeof(fbStatus), "SD Card Remounted OK");
          else snprintf(fbStatus, sizeof(fbStatus), "SD Mount Failed");
          fbSel = 0; fbTop = 0;
          return false;
        }
        if (sx >= bx + 356 && sx < bx + 498) { fbMode = NPFB_CLOSED; return false; }
      }
      return false;
    }

    int my = wy + 22;
    // Open pull-down menu hit test
    if (openMenu >= 0) {
      int px = wx + menuXOff(openMenu);
      int py = my + 14;
      int cnt = menuItemCount(openMenu);
      if (sx >= px && sx < px + 140 && sy >= py && sy < py + cnt * 14 + 4) {
        int idx = (sy - (py + 2)) / 14;
        if (idx >= 0 && idx < cnt) return runMenuItem(openMenu, idx);
      }
      openMenu = -1;
      if (sy < my || sy > my + 13) return false;
    }

    // Menu bar click
    if (sy >= my && sy <= my + 13) {
      for (int i = 0; i < NP_MENU_COUNT; ++i) {
        int mx = wx + menuXOff(i);
        if (sx >= mx && sx < mx + menuW(i)) {
          openMenu  = (openMenu == i) ? -1 : i;
          hoverItem = 0;
          return false;
        }
      }
      return false;
    }

    // Text client area click -> position cursor (and begin drag selection!)
    int tx = wx + 6, ty = wy + 37, tw = ww - 28, th = wh - 56;
    if (sx >= tx && sx < tx + tw && sy >= ty && sy < ty + th) {
      layoutMetrics(ww, wh);
      computeVLines();
      int r = scrollLine + max(0, (sy - (ty + 2)) / rowH());
      int c = (wordWrap ? 0 : scrollCol) + max(0, (sx - (tx + 2) + charW() / 2) / charW());
      cursor = rcToOffset(r, c);
      selAnchor = cursor;
      mouseSelecting = true;
      caretOn = true;
      lastBlinkMs = millis();
      return false;
    }

    // Scrollbar clicks
    int sbX = tx + tw + 1;
    if (sx >= sbX && sx < sbX + 16 && sy >= ty && sy < ty + th) {
      if (sy < ty + 15) {
        if (scrollLine > 0) scrollLine--;
      } else if (sy > ty + th - 16) {
        if (scrollLine + visRows < vlineCount) scrollLine++;
      } else {
        // Page jump based on click above/below middle
        if (sy < ty + th / 2) scrollLine = max(0, scrollLine - visRows / 2);
        else                  scrollLine = min(max(0, vlineCount - visRows), scrollLine + visRows / 2);
      }
      return false;
    }

    needRedraw = false;
    return false;
  }

  bool handleMouseMove(int sx, int sy, int wx, int wy, int ww, int wh) {
    (void)ww; (void)wh;
    if (openMenu >= 0) {
      int px = wx + menuXOff(openMenu);
      int py = wy + 36;
      int cnt = menuItemCount(openMenu);
      int nh = -1;
      if (sx >= px && sx < px + 140 && sy >= py + 2 && sy < py + 2 + cnt * 14) {
        nh = (sy - (py + 2)) / 14;
      }
      if (nh != hoverItem) { hoverItem = nh; return true; }
    }
    if (mouseSelecting && fbMode == NPFB_CLOSED) {
      int tx = wx + 6, ty = wy + 37;
      int r = scrollLine + max(0, (sy - (ty + 2)) / rowH());
      int c = (wordWrap ? 0 : scrollCol) + max(0, (sx - (tx + 2) + charW() / 2) / charW());
      int newOff = rcToOffset(r, c);
      if (newOff != cursor) {
        cursor = newOff;
        return true;
      }
    }
    return false;
  }

  void handleMouseUp() {
    if (mouseSelecting) {
      mouseSelecting = false;
      if (selAnchor == cursor) selAnchor = -1;
    }
  }

  bool handleWheel(int dir) {
    if (fbMode != NPFB_CLOSED) {
      fbTop = constrain(fbTop + dir * 2, 0, max(0, fbCount - 8));
      return true;
    }
    int maxTop = max(0, vlineCount - visRows);
    int nxt = constrain(scrollLine + dir * 3, 0, maxTop);
    if (nxt != scrollLine) { scrollLine = nxt; return true; }
    return false;
  }

  // ---- Keyboard Input Handler (returns true if window should close) ----
  bool handleKey(const fabgl::VirtualKeyItem & item, int ww, int wh, bool & needRedraw) {
    needRedraw = true;
    if (!ensureInit()) return false;
    layoutMetrics(ww, wh);
    computeVLines();

    // 1. File Browser keyboard control
    if (fbMode != NPFB_CLOSED) {
      if (item.vk == fabgl::VK_ESCAPE) { fbMode = NPFB_CLOSED; return false; }
      if (item.vk == fabgl::VK_UP) {
        if (fbSel > 0) fbSel--;
        fbConfirmDel = false;
        if (fbCount > 0 && fbEntries[fbSel].isDir) fbTyping = false;
        return false;
      }
      if (item.vk == fabgl::VK_DOWN) {
        if (fbSel + 1 < fbCount) fbSel++;
        fbConfirmDel = false;
        if (fbCount > 0 && fbEntries[fbSel].isDir) fbTyping = false;
        return false;
      }
      if (item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_KP_ENTER) {
        fbDoPrimary();
        return false;
      }
      if (item.vk == fabgl::VK_DELETE) {
        fbDoDelete();
        return false;
      }
      if (item.vk == fabgl::VK_TAB && fbMode == NPFB_SAVE) {
        if (fbCount > 0 && fbSel < fbCount && !fbEntries[fbSel].isDir) {
          strncpy(fbName, fbEntries[fbSel].name, sizeof(fbName) - 1);
          fbName[sizeof(fbName) - 1] = '\0';
          fbNameLen = (int)strlen(fbName);
          fbTyping = true;
        }
        return false;
      }
      if (item.vk == fabgl::VK_BACKSPACE) {
        fbConfirmDel = false;
        if (fbMode == NPFB_SAVE) {
          fbTyping = true;
          if (fbNameLen > 0) fbName[--fbNameLen] = '\0';
        } else if (strcmp(fbDir, "/") != 0) {
          fbGoParent();
          fbSel = 0; fbTop = 0;
          fbRefreshDir();
        }
        return false;
      }
      if (fbMode == NPFB_SAVE && !item.CTRL && item.ASCII >= 32 && item.ASCII <= 126) {
        char c = (char)item.ASCII;
        if ((isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.') && fbNameLen < 24) {
          fbTyping = true;
          fbConfirmDel = false;
          fbStatus[0] = '\0';
          fbName[fbNameLen++] = (char)toupper((unsigned char)c);
          fbName[fbNameLen] = '\0';
        }
      }
      return false;
    }

    // 2. Alt shortcuts (Alt+F4 closes Notepad; Alt+F/E/O opens Notepad menus)
    if (item.LALT || item.RALT) {
      if (item.vk == fabgl::VK_F4) return true;
      char c = (char)toupper((unsigned char)item.ASCII);
      if (item.vk == fabgl::VK_f || item.vk == fabgl::VK_F || c == 'F') { openMenu = 0; hoverItem = 0; return false; }
      if (item.vk == fabgl::VK_e || item.vk == fabgl::VK_E || c == 'E') { openMenu = 1; hoverItem = 0; return false; }
      if (item.vk == fabgl::VK_o || item.vk == fabgl::VK_O || c == 'O') { openMenu = 2; hoverItem = 0; return false; }
      needRedraw = false;
      return false;
    }

    // 3. Open pull-down menu keyboard navigation
    if (openMenu >= 0) {
      if (item.vk == fabgl::VK_ESCAPE) { openMenu = -1; return false; }
      if (item.vk == fabgl::VK_LEFT)  { openMenu = (openMenu + NP_MENU_COUNT - 1) % NP_MENU_COUNT; hoverItem = 0; return false; }
      if (item.vk == fabgl::VK_RIGHT) { openMenu = (openMenu + 1) % NP_MENU_COUNT; hoverItem = 0; return false; }
      if (item.vk == fabgl::VK_UP) {
        int cnt = menuItemCount(openMenu);
        hoverItem = (hoverItem <= 0) ? (cnt - 1) : (hoverItem - 1);
        return false;
      }
      if (item.vk == fabgl::VK_DOWN) {
        hoverItem = (hoverItem + 1) % menuItemCount(openMenu);
        return false;
      }
      if (item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_KP_ENTER || item.vk == fabgl::VK_SPACE) {
        return runMenuItem(openMenu, max(0, hoverItem));
      }
      openMenu = -1;
      return false;
    }

    // 4. Ctrl shortcuts
    if (item.CTRL) {
      char c = (char)tolower(item.ASCII);
      if (item.vk == fabgl::VK_n || item.vk == fabgl::VK_N || c == 'n' || item.ASCII == 14) { clearDoc(); return false; }
      if (item.vk == fabgl::VK_o || item.vk == fabgl::VK_O || c == 'o' || item.ASCII == 15) { openFileBrowser(NPFB_OPEN); return false; }
      if (item.vk == fabgl::VK_s || item.vk == fabgl::VK_S || c == 's' || item.ASCII == 19) { return runMenuItem(0, 2); }
      if (item.vk == fabgl::VK_a || item.vk == fabgl::VK_A || c == 'a' || item.ASCII == 1)  { selAnchor = 0; cursor = len; return false; }
      if (item.vk == fabgl::VK_c || item.vk == fabgl::VK_C || c == 'c' || item.ASCII == 3)  { copySelection(); return false; }
      if (item.vk == fabgl::VK_x || item.vk == fabgl::VK_X || c == 'x' || item.ASCII == 24) { cutSelection(); ensureCursorVisible(); return false; }
      if (item.vk == fabgl::VK_v || item.vk == fabgl::VK_V || c == 'v' || item.ASCII == 22) { pasteClipboard(); ensureCursorVisible(); return false; }
      needRedraw = false;
      return false;
    }

    // 5. Navigation keys (with Shift selection support!)
    auto updateSelForMove = [&]() {
      if (item.SHIFT) {
        if (selAnchor < 0) selAnchor = cursor;
      } else {
        selAnchor = -1;
      }
    };

    if (item.vk == fabgl::VK_LEFT) {
      updateSelForMove();
      if (cursor > 0) cursor--;
      caretOn = true; lastBlinkMs = millis();
      ensureCursorVisible();
      return false;
    }
    if (item.vk == fabgl::VK_RIGHT) {
      updateSelForMove();
      if (cursor < len) cursor++;
      caretOn = true; lastBlinkMs = millis();
      ensureCursorVisible();
      return false;
    }
    if (item.vk == fabgl::VK_UP) {
      updateSelForMove();
      int r, c; findCursorRC(r, c);
      if (r > 0) cursor = rcToOffset(r - 1, c);
      caretOn = true; lastBlinkMs = millis();
      ensureCursorVisible();
      return false;
    }
    if (item.vk == fabgl::VK_DOWN) {
      updateSelForMove();
      int r, c; findCursorRC(r, c);
      if (r + 1 < vlineCount) cursor = rcToOffset(r + 1, c);
      caretOn = true; lastBlinkMs = millis();
      ensureCursorVisible();
      return false;
    }
    if (item.vk == fabgl::VK_HOME) {
      updateSelForMove();
      int r, c; findCursorRC(r, c);
      cursor = vlines[r].start;
      caretOn = true; lastBlinkMs = millis();
      ensureCursorVisible();
      return false;
    }
    if (item.vk == fabgl::VK_END) {
      updateSelForMove();
      int r, c; findCursorRC(r, c);
      cursor = rcToOffset(r, (int)vlines[r].len);
      caretOn = true; lastBlinkMs = millis();
      ensureCursorVisible();
      return false;
    }
    if (item.vk == fabgl::VK_PAGEUP) {
      updateSelForMove();
      int r, c; findCursorRC(r, c);
      cursor = rcToOffset(max(0, r - visRows), c);
      caretOn = true; lastBlinkMs = millis();
      ensureCursorVisible();
      return false;
    }
    if (item.vk == fabgl::VK_PAGEDOWN) {
      updateSelForMove();
      int r, c; findCursorRC(r, c);
      cursor = rcToOffset(min(vlineCount - 1, r + visRows), c);
      caretOn = true; lastBlinkMs = millis();
      ensureCursorVisible();
      return false;
    }

    // 6. Text editing keys
    if (item.vk == fabgl::VK_BACKSPACE) {
      if (hasSelection()) {
        deleteSelection();
      } else if (cursor > 0) {
        memmove(text + cursor - 1, text + cursor, (size_t)(len - cursor + 1));
        cursor--; len--; modified = true;
      }
      caretOn = true; lastBlinkMs = millis();
      ensureCursorVisible();
      return false;
    }
    if (item.vk == fabgl::VK_DELETE) {
      if (hasSelection()) {
        deleteSelection();
      } else if (cursor < len) {
        memmove(text + cursor, text + cursor + 1, (size_t)(len - cursor));
        len--; modified = true;
      }
      caretOn = true; lastBlinkMs = millis();
      ensureCursorVisible();
      return false;
    }
    if (item.vk == fabgl::VK_RETURN || item.vk == fabgl::VK_KP_ENTER) {
      insertChar('\n');
      caretOn = true; lastBlinkMs = millis();
      ensureCursorVisible();
      return false;
    }
    if (item.vk == fabgl::VK_TAB) {
      insertText("  ", 2);
      caretOn = true; lastBlinkMs = millis();
      ensureCursorVisible();
      return false;
    }
    if (item.ASCII >= 32 && item.ASCII <= 126) {
      insertChar((char)item.ASCII);
      caretOn = true; lastBlinkMs = millis();
      ensureCursorVisible();
      return false;
    }

    needRedraw = false;
    return false;
  }
};
