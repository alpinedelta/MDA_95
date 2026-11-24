#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

// ============================================================================
// MDA Desktop Launcher SDK (Drop-In Header for Any Launchable ESP32 MDA App)
// ============================================================================

static constexpr const char * MDA_NVS_NS = "mda_sys";

inline bool mdaIsRunningAsLaunchedApp() {
  const esp_partition_t * running = esp_ota_get_running_partition();
  return (running != nullptr && running->subtype != ESP_PARTITION_SUBTYPE_APP_OTA_0);
}

inline void mdaArmDesktopOnReset() {
  const esp_partition_t * running = esp_ota_get_running_partition();
  if (!running || running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) return;
  const esp_partition_t * desktopPart = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  if (desktopPart) {
    esp_ota_set_boot_partition(desktopPart);
  }
}

inline void mdaReturnToDesktop() {
  const esp_partition_t * desktopPart = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  if (desktopPart) {
    esp_ota_set_boot_partition(desktopPart);
  }
  delay(80);
  ESP.restart();
}

inline int mdaLoadMouseSens(int defaultIdx = 2, int maxIdx = 5) {
  Preferences prefs;
  int val = defaultIdx;
  if (prefs.begin(MDA_NVS_NS, true)) {
    val = prefs.getInt("msens", defaultIdx);
    prefs.end();
  }
  if (val < 0) val = 0;
  if (val > maxIdx) val = maxIdx;
  return val;
}

inline void mdaSaveMouseSens(int idx) {
  Preferences prefs;
  if (prefs.begin(MDA_NVS_NS, false)) {
    prefs.putInt("msens", idx);
    prefs.end();
  }
}
