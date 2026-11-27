#pragma once

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

#include "firmware_version.h"
#include "ota_certs.h"

static const char* const RELEASE_API_URL =
  "https://api.github.com/repos/rafalantas/epaper-dashboard/releases/latest";
static const char* const RELEASE_ASSET_NAME = "epaper-dashboard-xiao-esp32c3.bin";

enum class OtaResult : uint8_t {
  NO_UPDATE,
  UPDATED,
  FAILED
};

static bool parseFirmwareVersion(const String& value, uint32_t parts[3]) {
  String version = value;
  if (version.startsWith("v")) version.remove(0, 1);
  unsigned int major = 0, minor = 0, patch = 0;
  char extra = 0;
  if (sscanf(version.c_str(), "%u.%u.%u%c", &major, &minor, &patch, &extra) != 3) {
    return false;
  }
  parts[0] = major;
  parts[1] = minor;
  parts[2] = patch;
  return true;
}

static bool isNewerFirmware(const String& latest, const String& current) {
  uint32_t latestParts[3], currentParts[3];
  if (!parseFirmwareVersion(latest, latestParts) ||
      !parseFirmwareVersion(current, currentParts)) return false;
  for (int i = 0; i < 3; ++i) {
    if (latestParts[i] != currentParts[i]) return latestParts[i] > currentParts[i];
  }
  return false;
}

static OtaResult checkAndInstallFirmwareUpdate(String* checkedVersion = nullptr) {
  WiFiClientSecure secureClient;
  secureClient.setCACert(GITHUB_OTA_ROOTS);

  HTTPClient http;
  if (!http.begin(secureClient, RELEASE_API_URL)) {
    Serial.println("OTA: nie mozna otworzyc API GitHub");
    return OtaResult::FAILED;
  }
  http.setTimeout(15000);
  http.setUserAgent("epaper-dashboard-ota");
  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("X-GitHub-Api-Version", "2022-11-28");

  StaticJsonDocument<256> filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  StaticJsonDocument<1536> release;
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("OTA: GitHub API HTTP %d\n", status);
    http.end();
    return OtaResult::FAILED;
  }

  const DeserializationError jsonError =
    deserializeJson(release, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (jsonError != DeserializationError::Ok) {
    Serial.printf("OTA: blad JSON GitHub: %s\n", jsonError.c_str());
    return OtaResult::FAILED;
  }

  const String latestVersion = release["tag_name"] | "";
  uint32_t latestParts[3];
  if (!parseFirmwareVersion(latestVersion, latestParts)) {
    Serial.printf("OTA: nieprawidlowy tag wydania: '%s'\n", latestVersion.c_str());
    return OtaResult::FAILED;
  }
  if (checkedVersion != nullptr) *checkedVersion = latestVersion;

  if (!isNewerFirmware(latestVersion, FIRMWARE_VERSION)) {
    Serial.printf("OTA: firmware aktualny (%s)\n", FIRMWARE_VERSION);
    return OtaResult::NO_UPDATE;
  }

  String firmwareUrl;
  for (JsonObject asset : release["assets"].as<JsonArray>()) {
    if (asset["name"] == RELEASE_ASSET_NAME) {
      firmwareUrl = asset["browser_download_url"] | "";
      break;
    }
  }
  if (firmwareUrl.isEmpty() ||
      !firmwareUrl.startsWith("https://github.com/rafalantas/epaper-dashboard/releases/")) {
    Serial.println("OTA: brak prawidlowej binarki w najnowszym wydaniu");
    return OtaResult::FAILED;
  }

  Serial.printf("OTA: aktualizacja %s -> %s\n", FIRMWARE_VERSION, latestVersion.c_str());
  HTTPUpdate updater(90000);
  updater.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  // Let setup() save the discovered version before restarting.
  updater.rebootOnUpdate(false);
  const auto result = updater.update(secureClient, firmwareUrl, FIRMWARE_VERSION);
  if (result == HTTP_UPDATE_FAILED) {
    Serial.printf("OTA: niepowodzenie: %s\n", updater.getLastErrorString().c_str());
    return OtaResult::FAILED;
  }
  if (result != HTTP_UPDATE_OK) {
    Serial.printf("OTA: nieoczekiwany wynik instalacji: %d\n", (int)result);
    return OtaResult::FAILED;
  }

  // On success HTTPUpdate may restart the ESP32. No e-paper update is performed.
  return OtaResult::UPDATED;
}
