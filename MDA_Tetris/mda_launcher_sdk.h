#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

// ============================================================================
// MDA Desktop Launcher SDK (Drop-In Header for Any Launchable ESP32 MDA App)
//
// How the Dual-OTA Architecture Works:
//   - Partition ota_0 : Permanently holds MDA Desktop (flashed via Arduino IDE
//                       using an OTA Partition Scheme such as
//                       "Minimal SPIFFS (1.9MB APP with OTA)" or
//                       "Default 4MB with spiffs").
//   - Partition ota_1 : Holds the currently launched SD-card app (.BIN).
//   - Calling mdaArmDesktopOnReset() in your app's setup() automatically sets
//     the next boot target back to ota_0 so hardware RST or power-cycle always
//     recovers cleanly to MDA Desktop.
//   - Calling mdaReturnToDesktop() (e.g. F10 or Alt+F4) jumps right back to
//     MDA Desktop in <0.5s with zero flash wear.
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
