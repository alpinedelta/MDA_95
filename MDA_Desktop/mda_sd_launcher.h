#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "mda_desktop_icons.h"
#include "mda_launcher_sdk.h"

// ============================================================================
// MDA Desktop - SD Card .BIN App Scanner & Smart Dual-OTA Launcher
// LilyGO VGA32 SD Card Pins: CS = 13, MOSI = 12, MISO = 2, CLK = 14
//
// Scans /APPS/*.BIN (or root /*.BIN if /APPS doesn't exist).
// Supports optional matching 32x24 1-bit .BMP custom icon next to each .BIN!
// Caches the last flashed app signature in NVS so re-launching the same app
// boots instantly (<0.5s) from ota_1 with ZERO flash re-writing!
// ============================================================================

static constexpr int SD_PIN_CS   = 13;
static constexpr int SD_PIN_MOSI = 12;
static constexpr int SD_PIN_MISO = 2;
static constexpr int SD_PIN_CLK  = 14;

static constexpr int MAX_SD_APPS = 20;

struct SDAppEntry {
  char            path[64];       // e.g. "/APPS/MDAPAINT.BIN"
  char            label[18];      // Friendly desktop icon label, e.g. "MDA Paint"
  uint32_t        size;           // File size in bytes
  DesktopIconKind iconKind;       // Auto-matched built-in icon
  bool            hasCustomIcon;  // True if matching 32x24 .BMP was loaded
  uint8_t         customIcon[96]; // Optional custom 32x24 1-bit icon data
};

static bool       gSDMounted   = false;
static SDAppEntry gSDApps[MAX_SD_APPS];
static int        gSDAppCount  = 0;
static char       gCachedAppPath[64] = "";
static uint32_t   gCachedAppSize     = 0;
static uint32_t   gCachedAppHash     = 0;

// Persistent Bitmap wrappers for custom SD-loaded icons
static fabgl::Bitmap gCustomIconWhite[MAX_SD_APPS];
static fabgl::Bitmap gCustomIconBlack[MAX_SD_APPS];

inline bool hasBinExtension(const char * name) {
  size_t len = strlen(name);
  if (len < 4) return false;
  const char * ext = name + len - 4;
  return (ext[0] == '.' &&
          toupper((unsigned char)ext[1]) == 'B' &&
          toupper((unsigned char)ext[2]) == 'I' &&
          toupper((unsigned char)ext[3]) == 'N');
}

inline bool strContainsCI(const char * haystack, const char * needle) {
  size_t nlen = strlen(needle);
  if (nlen == 0) return true;
  for (const char * p = haystack; *p; ++p) {
    size_t i = 0;
    while (i < nlen && p[i] && toupper((unsigned char)p[i]) == toupper((unsigned char)needle[i])) {
      i++;
    }
    if (i == nlen) return true;
  }
  return false;
}

inline DesktopIconKind guessIconForApp(const char * filename) {
  if (strContainsCI(filename, "PAINT") || strContainsCI(filename, "DRAW")) return ICON_PAINT;
  if (strContainsCI(filename, "SOLIT") || strContainsCI(filename, "CARD") || strContainsCI(filename, "KLOND")) return ICON_CARDS;
  if (strContainsCI(filename, "TERM")  || strContainsCI(filename, "NET") || strContainsCI(filename, "SSH")) return ICON_TERMINAL;
  if (strContainsCI(filename, "NOTE")  || strContainsCI(filename, "EDIT") || strContainsCI(filename, "TEXT")) return ICON_NOTEPAD;
  if (strContainsCI(filename, "CALC")) return ICON_CALC;
  if (strContainsCI(filename, "GAME")  || strContainsCI(filename, "TETRIS") || strContainsCI(filename, "PONG") || strContainsCI(filename, "DOOM")) return ICON_GAME;
  return ICON_APP_GENERIC;
}

inline void makeFriendlyLabel(const char * filename, char * out, size_t outSize) {
  // Strip .BIN and turn underscores into spaces, Title Case nicely
  char tmp[32];
  strncpy(tmp, filename, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';
  size_t len = strlen(tmp);
  if (len >= 4 && hasBinExtension(tmp)) tmp[len - 4] = '\0';
  // Also strip Arduino ".ino" suffix if user kept "MDA_Paint.ino.bin"
  len = strlen(tmp);
  if (len >= 4 && strcasecmp(tmp + len - 4, ".ino") == 0) tmp[len - 4] = '\0';

  if (strcasecmp(tmp, "MDAPAINT") == 0 || strcasecmp(tmp, "MDA_PAINT") == 0) {
    strncpy(out, "MDA Paint", outSize - 1);
    out[outSize - 1] = '\0';
    return;
  }

  size_t oi = 0;
  bool capNext = true;
  for (size_t i = 0; tmp[i] && oi + 1 < outSize; ++i) {
    char c = tmp[i];
    if (c == '_' || c == '-') {
      out[oi++] = ' ';
      capNext = true;
    } else {
      out[oi++] = capNext ? (char)toupper((unsigned char)c) : (char)tolower((unsigned char)c);
      capNext = false;
    }
  }
  out[oi] = '\0';
}

// Try loading an optional matching 32x24 1-bit BMP icon (e.g. /APPS/MDAPAINT.BMP)
inline bool tryLoadCustomIconBMP(const char * binPath, uint8_t out96[96]) {
  char bmpPath[72];
  strncpy(bmpPath, binPath, sizeof(bmpPath) - 1);
  bmpPath[sizeof(bmpPath) - 1] = '\0';
  size_t len = strlen(bmpPath);
  if (len < 4) return false;
  strcpy(bmpPath + len - 4, ".BMP");
  if (!SD.exists(bmpPath)) {
    strcpy(bmpPath + len - 4, ".bmp");
    if (!SD.exists(bmpPath)) return false;
  }

  File f = SD.open(bmpPath, FILE_READ);
  if (!f) return false;
  uint8_t hdr[62];
  if (f.read(hdr, 62) != 62 || hdr[0] != 'B' || hdr[1] != 'M') {
    f.close();
    return false;
  }
  uint32_t dataOff = (uint32_t)hdr[10] | ((uint32_t)hdr[11] << 8) | ((uint32_t)hdr[12] << 16) | ((uint32_t)hdr[13] << 24);
  int32_t  w       = (int32_t)((uint32_t)hdr[18] | ((uint32_t)hdr[19] << 8) | ((uint32_t)hdr[20] << 16) | ((uint32_t)hdr[21] << 24));
  int32_t  hSigned = (int32_t)((uint32_t)hdr[22] | ((uint32_t)hdr[23] << 8) | ((uint32_t)hdr[24] << 16) | ((uint32_t)hdr[25] << 24));
  uint16_t bpp     = (uint16_t)hdr[28] | ((uint16_t)hdr[29] << 8);
  bool topDown = (hSigned < 0);
  int32_t h = topDown ? -hSigned : hSigned;
  if (w != 32 || h != 24 || bpp != 1) {
    f.close();
    return false;
  }

  f.seek(dataOff);
  for (int r = 0; r < 24; ++r) {
    int dstRow = topDown ? r : (23 - r);
    if (f.read(out96 + dstRow * 4, 4) != 4) {
      f.close();
      return false;
    }
  }
  f.close();
  return true;
}

inline void loadCachedAppInfo() {
  Preferences prefs;
  gCachedAppPath[0] = '\0';
  gCachedAppSize    = 0;
  gCachedAppHash    = 0;
  if (prefs.begin(MDA_NVS_NS, true)) {
    String p = prefs.getString("c_path", "");
    strncpy(gCachedAppPath, p.c_str(), sizeof(gCachedAppPath) - 1);
    gCachedAppPath[sizeof(gCachedAppPath) - 1] = '\0';
    gCachedAppSize = prefs.getUInt("c_size", 0);
    gCachedAppHash = prefs.getUInt("c_hash", 0);
    prefs.end();
  }
}

inline void saveCachedAppInfo(const char * path, uint32_t sz, uint32_t hash) {
  Preferences prefs;
  if (prefs.begin(MDA_NVS_NS, false)) {
    prefs.putString("c_path", path);
    prefs.putUInt("c_size", sz);
    prefs.putUInt("c_hash", hash);
    prefs.end();
  }
  strncpy(gCachedAppPath, path, sizeof(gCachedAppPath) - 1);
  gCachedAppPath[sizeof(gCachedAppPath) - 1] = '\0';
  gCachedAppSize = sz;
  gCachedAppHash = hash;
}

// Fast FNV-1a fingerprint over first 1KB + middle 1KB + last 1KB of a .BIN file
inline uint32_t computeBinFingerprint(File & f, uint32_t sz) {
  uint32_t h = 2166136261UL ^ sz;
  uint8_t tmp[512];
  uint32_t offsets[3] = { 0, sz / 2, (sz > 512) ? (sz - 512) : 0 };
  for (int k = 0; k < 3; ++k) {
    f.seek(offsets[k]);
    int n = f.read(tmp, sizeof(tmp));
    for (int i = 0; i < n; ++i) {
      h ^= tmp[i];
      h *= 16777619UL;
    }
  }
  f.seek(0);
  return h;
}

inline bool sdMountAndScanApps(bool forceRemount = false) {
  gSDAppCount = 0;
  if (forceRemount && gSDMounted) {
    SD.end();
    gSDMounted = false;
  }
  if (!gSDMounted) {
    SPI.begin(SD_PIN_CLK, SD_PIN_MISO, SD_PIN_MOSI, SD_PIN_CS);
    if (SD.begin(SD_PIN_CS, SPI, 4000000) || SD.begin(SD_PIN_CS, SPI, 1000000)) {
      gSDMounted = true;
    } else {
      return false;
    }
  }

  // Ensure /APPS directory exists if SD is writable, and scan /APPS first (fallback to /)
  const char * scanDir = "/APPS";
  if (!SD.exists("/APPS")) {
    SD.mkdir("/APPS");
  }

  auto scanFolder = [&](const char * folder) {
    File dir = SD.open(folder);
    if (!dir || !dir.isDirectory()) return;
    File entry = dir.openNextFile();
    while (entry && gSDAppCount < MAX_SD_APPS) {
      if (!entry.isDirectory()) {
        const char * rawName = entry.name();
        const char * base = strrchr(rawName, '/');
        base = base ? (base + 1) : rawName;
        if (base[0] != '.' && hasBinExtension(base) && !strContainsCI(base, "DESKTOP")) {
          SDAppEntry & app = gSDApps[gSDAppCount];
          if (strcmp(folder, "/") == 0) snprintf(app.path, sizeof(app.path), "/%s", base);
          else                           snprintf(app.path, sizeof(app.path), "%s/%s", folder, base);
          makeFriendlyLabel(base, app.label, sizeof(app.label));
          app.size          = (uint32_t)entry.size();
          app.iconKind      = guessIconForApp(base);
          app.hasCustomIcon = tryLoadCustomIconBMP(app.path, app.customIcon);
          if (app.hasCustomIcon) {
            gCustomIconWhite[gSDAppCount] = fabgl::Bitmap(DICON_W, DICON_H, app.customIcon, fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255));
            gCustomIconBlack[gSDAppCount] = fabgl::Bitmap(DICON_W, DICON_H, app.customIcon, fabgl::PixelFormat::Mask, fabgl::RGB888(0, 0, 0));
          }
          gSDAppCount++;
        }
      }
      entry.close();
      entry = dir.openNextFile();
    }
    dir.close();
  };

  scanFolder(scanDir);
  // If /APPS is empty, also check root "/" for convenience
  if (gSDAppCount == 0) {
    scanFolder("/");
  }

  // Sort apps alphabetically by label
  for (int i = 0; i < gSDAppCount - 1; ++i) {
    for (int j = i + 1; j < gSDAppCount; ++j) {
      if (strcasecmp(gSDApps[i].label, gSDApps[j].label) > 0) {
        SDAppEntry tmp = gSDApps[i];
        gSDApps[i] = gSDApps[j];
        gSDApps[j] = tmp;
        // Rebind bitmap pointers if custom icons swapped
        if (gSDApps[i].hasCustomIcon) {
          gCustomIconWhite[i] = fabgl::Bitmap(DICON_W, DICON_H, gSDApps[i].customIcon, fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255));
          gCustomIconBlack[i] = fabgl::Bitmap(DICON_W, DICON_H, gSDApps[i].customIcon, fabgl::PixelFormat::Mask, fabgl::RGB888(0, 0, 0));
        }
        if (gSDApps[j].hasCustomIcon) {
          gCustomIconWhite[j] = fabgl::Bitmap(DICON_W, DICON_H, gSDApps[j].customIcon, fabgl::PixelFormat::Mask, fabgl::RGB888(255, 255, 255));
          gCustomIconBlack[j] = fabgl::Bitmap(DICON_W, DICON_H, gSDApps[j].customIcon, fabgl::PixelFormat::Mask, fabgl::RGB888(0, 0, 0));
        }
      }
    }
  }
  return true;
}

typedef void (*LaunchProgressCB)(int percent, const char * stage);

// Launches an SD-card .BIN into ota_1 (leaving MDA Desktop intact in ota_0!)
inline bool launchSDApp(const SDAppEntry & app, LaunchProgressCB progressCB, char * errOut, size_t errSize) {
  if (!gSDMounted && !sdMountAndScanApps(false)) {
    snprintf(errOut, errSize, "SD card not mounted");
    return false;
  }

  // Verify we have a second OTA partition (ota_1) so we NEVER overwrite Desktop (ota_0)
  const esp_partition_t * running = esp_ota_get_running_partition();
  const esp_partition_t * target  = esp_ota_get_next_update_partition(nullptr);
  if (!running || !target) {
    snprintf(errOut, errSize, "No OTA partition! Use Minimal SPIFFS (OTA)");
    return false;
  }
  if (target->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
    snprintf(errOut, errSize, "Desktop must be flashed to ota_0!");
    return false;
  }

  File f = SD.open(app.path, FILE_READ);
  if (!f) {
    snprintf(errOut, errSize, "Cannot open %s", app.path);
    return false;
  }

  uint32_t fileSize = (uint32_t)f.size();
  if (fileSize < 4096 || fileSize > target->size) {
    f.close();
    snprintf(errOut, errSize, "Bad size: %u KB (max %u KB)", (unsigned)(fileSize / 1024), (unsigned)(target->size / 1024));
    return false;
  }

  // Check ESP32 firmware magic byte (0xE9)
  uint8_t magic = 0;
  f.read(&magic, 1);
  if (magic != 0xE9) {
    f.close();
    snprintf(errOut, errSize, "Not a valid ESP32 .BIN image (0x%02X)", magic);
    return false;
  }

  // Check if ota_1 already holds this exact binary -> instant zero-flash launch!
  uint32_t fpHash = computeBinFingerprint(f, fileSize);
  if (strcmp(gCachedAppPath, app.path) == 0 && gCachedAppSize == fileSize && gCachedAppHash == fpHash) {
    f.close();
    if (progressCB) progressCB(100, "Cached in flash - launching instantly...");
    delay(180);
    esp_err_t err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
      snprintf(errOut, errSize, "Boot switch failed (%d)", (int)err);
      return false;
    }
    ESP.restart();
    return true;
  }

  if (progressCB) progressCB(0, "Erasing app slot (ota_1)...");
  esp_ota_handle_t handle = 0;
  esp_err_t err = esp_ota_begin(target, fileSize, &handle);
  if (err != ESP_OK) {
    f.close();
    snprintf(errOut, errSize, "OTA begin failed (%d)", (int)err);
    return false;
  }

  uint8_t chunk[4096];
  uint32_t written = 0;
  int lastPct = -1;
  f.seek(0);

  while (written < fileSize) {
    int n = f.read(chunk, sizeof(chunk));
    if (n <= 0) break;
    err = esp_ota_write(handle, chunk, (size_t)n);
    if (err != ESP_OK) {
      esp_ota_end(handle);
      f.close();
      snprintf(errOut, errSize, "Flash write error (%d)", (int)err);
      return false;
    }
    written += (uint32_t)n;
    int pct = (int)((uint64_t)written * 100ULL / fileSize);
    if (pct != lastPct && (pct - lastPct >= 2 || pct == 100)) {
      lastPct = pct;
      if (progressCB) progressCB(pct, "Loading program from SD card...");
    }
  }
  f.close();

  if (written != fileSize) {
    esp_ota_end(handle);
    snprintf(errOut, errSize, "Short read (%u / %u bytes)", (unsigned)written, (unsigned)fileSize);
    return false;
  }

  if (progressCB) progressCB(100, "Verifying & booting...");
  err = esp_ota_end(handle);
  if (err != ESP_OK) {
    snprintf(errOut, errSize, "Invalid ESP32 app image (%d)", (int)err);
    return false;
  }

  err = esp_ota_set_boot_partition(target);
  if (err != ESP_OK) {
    snprintf(errOut, errSize, "Set boot partition failed (%d)", (int)err);
    return false;
  }

  saveCachedAppInfo(app.path, fileSize, fpHash);
  delay(200);
  ESP.restart();
  return true;
}
