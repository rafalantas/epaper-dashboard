// ─── epaper_dashboard.ino ────────────────────────────────────────────────────
// XIAO ESP32-C3 + Seeed 7.5" ePaper (UC8179, 800x480)
// Deep sleep co 5 minut, cisza nocna 23:00-06:00
// Biblioteki: Seeed GFX (ZIP), ArduinoJson, ElegantOTA (opcjonalne)
// ─────────────────────────────────────────────────────────────────────────────

#include "driver.h"
#include <TFT_eSPI.h>

namespace fs { class FS; }
using fs::FS;

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include "ota_update.h"

// ─── Konfiguracja — edytuj config.h ──────────────────────────────────────────
#include "config.h"

#ifndef SETTINGS_RESET_TOKEN
#define SETTINGS_RESET_TOKEN ""
#endif

const char* NTP_SERVER   = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";
const char* TZ_INFO      = "CET-1CEST,M3.5.0,M10.5.0/3";

// ─── Struktury ────────────────────────────────────────────────────────────────
struct State {
  float pvTotal     = 0;
  float soc         = 0;
  float battPower   = 0;
  int   chgDsgState = 0;
  float sysLoad     = 0;
  float feedPower   = 0;
  float fromGrid    = 0;
  float loadFromPv  = 0;
  float loadFromBat = 0;
  float loadFromGrid= 0;
  // Efficiency
  float solarKwh    = 0;
  float forecastKwh = 0;
  float independence= 0;
  int   chgRemTime  = 0;
  int   dsgRemTime  = 0;
  bool  connected   = false;
};

struct PriceSlot {
  int   hour       = 0;
  float price      = 0;
  bool  isCheap    = false;
  bool  isExpensive= false;
};

// ─── Globalne ─────────────────────────────────────────────────────────────────
State     state;
PriceSlot prices[48];
int       priceCount      = 0;
int       currentPriceIdx = -1;
long      g_utcOffsetSec  = 7200;

struct DeviceSettings {
  String wifiSsid;
  String wifiPassword;
  String apiHost;
  uint32_t sleepSeconds;
  int nightHourStart;
  int nightHourEnd;
  float pvMaxW;
};
DeviceSettings deviceSettings;
String latestGithubVersion;

EPaper epd;
RTC_DATA_ATTR int   rtc_bootCount        = 0;
RTC_DATA_ATTR float rtc_independence      = 0;
RTC_DATA_ATTR float rtc_forecastKwh       = 0;
RTC_DATA_ATTR float rtc_solarKwh          = 0;
RTC_DATA_ATTR int   rtc_effFetchHour      = -1;
RTC_DATA_ATTR bool  rtc_nightUpdateChecked = false;

// ─── Forward declarations ──────────────────────────────────────────────────────
void drawMsg(const String& l1, const String& l2 = "");
bool fetchPrices(int currentLocalHour);

void loadDeviceSettings() {
  Preferences prefs;
  if (!prefs.begin("epaper", false)) {
    deviceSettings.wifiSsid = WIFI_SSID;
    deviceSettings.wifiPassword = WIFI_PASSWORD;
    deviceSettings.apiHost = API_HOST;
    deviceSettings.sleepSeconds = SLEEP_SECONDS;
    deviceSettings.nightHourStart = NIGHT_HOUR_START;
    deviceSettings.nightHourEnd = NIGHT_HOUR_END;
    deviceSettings.pvMaxW = PV_MAX_W;
    return;
  }

  const String resetToken = SETTINGS_RESET_TOKEN;
  const bool resetRequested = !resetToken.isEmpty() &&
    prefs.getString("resetToken", "") != resetToken;
  if (resetRequested || !prefs.getBool("configured", false)) {
    prefs.putString("ssid", WIFI_SSID);
    prefs.putString("wifiPass", WIFI_PASSWORD);
    prefs.putString("apiHost", API_HOST);
    prefs.putUInt("sleepSec", SLEEP_SECONDS);
    prefs.putInt("nightStart", NIGHT_HOUR_START);
    prefs.putInt("nightEnd", NIGHT_HOUR_END);
    prefs.putFloat("pvMaxW", PV_MAX_W);
    prefs.putBool("configured", true);
    prefs.putString("resetToken", resetToken);
  }

  deviceSettings.wifiSsid = prefs.getString("ssid", WIFI_SSID);
  deviceSettings.wifiPassword = prefs.getString("wifiPass", WIFI_PASSWORD);
  deviceSettings.apiHost = prefs.getString("apiHost", API_HOST);
  deviceSettings.sleepSeconds = prefs.getUInt("sleepSec", SLEEP_SECONDS);
  deviceSettings.nightHourStart = prefs.getInt("nightStart", NIGHT_HOUR_START);
  deviceSettings.nightHourEnd = prefs.getInt("nightEnd", NIGHT_HOUR_END);
  deviceSettings.pvMaxW = prefs.getFloat("pvMaxW", PV_MAX_W);
  prefs.end();

  if (deviceSettings.apiHost.isEmpty()) deviceSettings.apiHost = API_HOST;
  if (deviceSettings.sleepSeconds < 30) deviceSettings.sleepSeconds = SLEEP_SECONDS;
  if (deviceSettings.nightHourStart < 0 || deviceSettings.nightHourStart > 23)
    deviceSettings.nightHourStart = NIGHT_HOUR_START;
  if (deviceSettings.nightHourEnd < 0 || deviceSettings.nightHourEnd > 23)
    deviceSettings.nightHourEnd = NIGHT_HOUR_END;
  if (deviceSettings.pvMaxW <= 0) deviceSettings.pvMaxW = PV_MAX_W;
}

// ─── Sleep calculator ─────────────────────────────────────────────────────────
bool isNightHours(int localHour) {
  return (deviceSettings.nightHourStart > deviceSettings.nightHourEnd)
    ? (localHour >= deviceSettings.nightHourStart || localHour < deviceSettings.nightHourEnd)
    : (localHour >= deviceSettings.nightHourStart && localHour < deviceSettings.nightHourEnd);
}

unsigned long calcSleepSeconds(int localHour, int localMin) {
  bool isNight = isNightHours(localHour);
  if (!isNight) return deviceSettings.sleepSeconds;
  int hoursLeft;
  if (deviceSettings.nightHourStart > deviceSettings.nightHourEnd) {
    hoursLeft = (localHour >= deviceSettings.nightHourStart)
      ? (24 - localHour + deviceSettings.nightHourEnd)
      : (deviceSettings.nightHourEnd - localHour);
  } else {
    hoursLeft = deviceSettings.nightHourEnd - localHour;
  }
  unsigned long secs = (unsigned long)hoursLeft * 3600UL - (unsigned long)localMin * 60UL;
  return max(secs, 60UL);
}

// ─── UTC offset ───────────────────────────────────────────────────────────────
// Pobieramy offset przez porownanie tego samego time_t przez localtime i gmtime
// Uzywamy minut i sekund zeby byc precyzyjnym (DST moze miec pol-godzinne offsety)
void calcUtcOffset() {
  time_t t = time(nullptr);
  struct tm local_tm = {}, utc_tm = {};
  localtime_r(&t, &local_tm);
  gmtime_r(&t, &utc_tm);
  // Przelicz oba na minuty od poczatku doby (uwzgledniaj przekroczenie polnocy)
  int localMins = local_tm.tm_hour * 60 + local_tm.tm_min;
  int utcMins   = utc_tm.tm_hour   * 60 + utc_tm.tm_min;
  int diffMins  = localMins - utcMins;
  if (diffMins >  720) diffMins -= 1440;
  if (diffMins < -720) diffMins += 1440;
  g_utcOffsetSec = (long)diffMins * 60L;
  Serial.printf("UTC offset: %ld min (%ldh) local=%02d:%02d utc=%02d:%02d\n",
    diffMins, g_utcOffsetSec/3600,
    local_tm.tm_hour, local_tm.tm_min,
    utc_tm.tm_hour, utc_tm.tm_min);
}

// ─── Helpers ──────────────────────────────────────────────────────────────────
void drawCard(int x, int y, int w, int h) {
  epd.fillRect(x, y, w, h, TFT_WHITE);
  epd.drawRect(x, y, w, h, TFT_BLACK);
  epd.drawRect(x+1, y+1, w-2, h-2, TFT_BLACK);
}

void drawBar(int x, int y, int w, int h, float pct) {
  pct = max(0.0f, min(100.0f, pct));
  epd.fillRect(x, y, w, h, TFT_WHITE);
  epd.drawRect(x, y, w, h, TFT_BLACK);
  int fill = (int)(w * pct / 100.0f);
  if (fill > 2) epd.fillRect(x+1, y+1, fill-2, h-2, TFT_BLACK);
}

void drawBadge(int x, int y, int w, int h, const char* text, bool filled) {
  if (filled) {
    epd.fillRect(x, y, w, h, TFT_BLACK);
    // Podwójna ramka dla efektu grubości
    epd.drawRect(x+1, y+1, w-2, h-2, TFT_BLACK);
    epd.setTextColor(TFT_WHITE, TFT_BLACK);
  } else {
    epd.fillRect(x, y, w, h, TFT_WHITE);
    epd.drawRect(x, y, w, h, TFT_BLACK);
    epd.drawRect(x+1, y+1, w-2, h-2, TFT_BLACK);
    epd.setTextColor(TFT_BLACK, TFT_WHITE);
  }
  epd.setTextSize(1);
  int tw = strlen(text) * 6;
  epd.setCursor(x + (w - tw) / 2, y + (h - 8) / 2);
  epd.print(text);
  epd.setTextColor(TFT_BLACK, TFT_WHITE);
}

// ─── Ikony bitmap 16x16 ───────────────────────────────────────────────────────
const uint8_t ICON_SUN[] = {
  0b00010000,0b00001000,
  0b00001000,0b00010000,
  0b00000001,0b10000000,
  0b00100110,0b01100100,
  0b01000100,0b00100010,
  0b00011000,0b00011000,
  0b11110000,0b00001111,
  0b11110000,0b00001111,
  0b00011000,0b00011000,
  0b01000100,0b00100010,
  0b00100110,0b01100100,
  0b00000001,0b10000000,
  0b00001000,0b00010000,
  0b00010000,0b00001000,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
};
const uint8_t ICON_BAT[] = {
  0b00000001,0b11000000,
  0b00000001,0b11000000,
  0b01111111,0b11111110,
  0b01000000,0b00000010,
  0b01011111,0b11111010,
  0b01011111,0b11111010,
  0b01011111,0b11111010,
  0b01011111,0b11111010,
  0b01011111,0b11111010,
  0b01011111,0b11111010,
  0b01000000,0b00000010,
  0b01111111,0b11111110,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
};
const uint8_t ICON_HOME[] = {
  0b00000001,0b10000000,
  0b00000011,0b11000000,
  0b00000111,0b11100000,
  0b00001111,0b11110000,
  0b00011111,0b11111000,
  0b00111111,0b11111100,
  0b01111111,0b11111110,
  0b00011111,0b11111000,
  0b00011001,0b10011000,
  0b00011001,0b10011000,
  0b00011001,0b10011000,
  0b00011111,0b11111000,
  0b00011111,0b11111000,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
};
const uint8_t ICON_GRID[] = {
  0b00000111,0b10000000,
  0b00001111,0b00000000,
  0b00011110,0b00000000,
  0b00111100,0b00000000,
  0b01111111,0b11000000,
  0b00001111,0b10000000,
  0b00011111,0b00000000,
  0b00111110,0b00000000,
  0b01111100,0b00000000,
  0b00111111,0b11100000,
  0b00000111,0b10000000,
  0b00001111,0b00000000,
  0b00000110,0b00000000,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
  0b00000000,0b00000000,
};



void drawIcon(int x, int y, const uint8_t* icon) {
  for (int row = 0; row < 16; row++) {
    uint16_t bits = ((uint16_t)icon[row*2] << 8) | icon[row*2+1];
    for (int col = 0; col < 16; col++) {
      if (bits & (0x8000 >> col)) epd.drawPixel(x+col, y+row, TFT_BLACK);
    }
  }
}

void drawPriceTrend(int x, int y) {
  if (currentPriceIdx <= 0 || currentPriceIdx >= priceCount) return;
  float diff = prices[currentPriceIdx].price - prices[currentPriceIdx-1].price;
  if (fabs(diff) < 0.05f) {
    epd.drawLine(x, y+4, x+10, y+4, TFT_BLACK);
  } else if (diff > 0) {
    epd.drawLine(x, y+8, x+5, y,   TFT_BLACK);
    epd.drawLine(x+5, y, x+10, y+8, TFT_BLACK);
  } else {
    epd.drawLine(x, y,   x+5, y+8,  TFT_BLACK);
    epd.drawLine(x+5, y+8, x+10, y, TFT_BLACK);
  }
}

void drawPriceMeter(int x, int y, int w, float price, float minP, float maxP) {
  epd.drawRect(x, y, w, 7, TFT_BLACK);
  float range = max(maxP - minP, 0.01f);
  int fill = (int)((price - minP) / range * (w-2));
  fill = max(0, min(fill, w-2));
  epd.fillRect(x+1, y+1, fill, 5, TFT_BLACK);
}

// ─── WiFi ─────────────────────────────────────────────────────────────────────
bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("sloneczny-panel");
  WiFi.begin(deviceSettings.wifiSsid.c_str(), deviceSettings.wifiPassword.c_str());
  // Pierwsza proba - 15 sekund
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  if (WiFi.status() == WL_CONNECTED) return true;
  // Druga proba - disconnect i reconnect
  WiFi.disconnect();
  delay(500);
  WiFi.begin(deviceSettings.wifiSsid.c_str(), deviceSettings.wifiPassword.c_str());
  for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  return WiFi.status() == WL_CONNECTED;
}

// ─── HTTP GET ─────────────────────────────────────────────────────────────────
String httpGet(const String& url) {
  HTTPClient http;
  http.begin(url);
  http.setTimeout(15000);
  String body = "";
  if (http.GET() == HTTP_CODE_OK) body = http.getString();
  http.end();
  return body;
}

// ─── Fetch /api/state ─────────────────────────────────────────────────────────
bool fetchState() {
  String body = httpGet(deviceSettings.apiHost + "/api/state");
  if (body.isEmpty()) return false;
  // The server returns many fields (including a second device). Keep only the
  // values used by this display so the response fits in the ESP32's JSON pool.
  StaticJsonDocument<512> filter;
  filter["pvTotal"] = true;
  filter["soc"] = true;
  filter["battPower"] = true;
  filter["chgDsgState"] = true;
  filter["sysLoad"] = true;
  filter["feedPower"] = true;
  filter["fromGrid"] = true;
  filter["loadFromPv"] = true;
  filter["loadFromBat"] = true;
  filter["loadFromGrid"] = true;
  filter["chgRemTime"] = true;
  filter["dsgRemTime"] = true;
  filter["connected"] = true;
  StaticJsonDocument<768> doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) != DeserializationError::Ok)
    return false;
  state.pvTotal     = doc["pvTotal"]    | 0.0f;
  state.soc         = doc["soc"]        | 0.0f;
  state.battPower   = doc["battPower"]  | 0.0f;
  state.chgDsgState = doc["chgDsgState"]| 0;
  state.sysLoad     = doc["sysLoad"]    | 0.0f;
  state.feedPower   = doc["feedPower"]  | 0.0f;
  state.fromGrid    = doc["fromGrid"]   | 0.0f;
  state.loadFromPv  = doc["loadFromPv"]  | 0.0f;
  state.loadFromBat = doc["loadFromBat"] | 0.0f;
  state.loadFromGrid= doc["loadFromGrid"]| 0.0f;
  state.chgRemTime  = doc["chgRemTime"] | 0;
  state.dsgRemTime  = doc["dsgRemTime"] | 0;
  state.connected   = doc["connected"]  | false;
  return true;
}

// ─── Fetch /api/efficiency ───────────────────────────────────────────────────
bool fetchEfficiency() {
  bool ok = false;

  // 1. /api/energy?period=day — efficiency (83.8%) i totalKwh (produkcja dzienna)
  {
    String body = httpGet(deviceSettings.apiHost + "/api/energy?period=day");
    if (!body.isEmpty()) {
      DynamicJsonDocument doc(2048);
      if (deserializeJson(doc, body) == DeserializationError::Ok) {
        state.solarKwh    = doc["totalKwh"]   | 0.0f;
        state.independence= doc["efficiency"] | 0.0f;  // 83.8%
        ok = true;
        Serial.printf("energy OK: solar=%.1fkWh eff=%.1f%%\n",
          state.solarKwh, state.independence);
      }
    }
  }

  // 2. /api/efficiency — forecastKwh (prognoza)
  {
    HTTPClient http;
    http.begin(deviceSettings.apiHost + "/api/efficiency");
    http.setTimeout(25000);
    String body = "";
    if (http.GET() == HTTP_CODE_OK) body = http.getString();
    http.end();
    if (!body.isEmpty()) {
      DynamicJsonDocument doc(4096);
      if (deserializeJson(doc, body) == DeserializationError::Ok) {
        state.forecastKwh = doc["forecastKwh"] | 0.0f;
        Serial.printf("forecast OK: %.1f kWh\n", state.forecastKwh);
        ok = true;
      }
    }
  }

  return ok;
}

// ─── Fetch /api/prices ────────────────────────────────────────────────────────
bool fetchPrices(int currentLocalHour) {
  String body = httpGet(deviceSettings.apiHost + "/api/prices");
  // Jedna ponowna proba jesli pusta odpowiedz
  if (body.isEmpty()) {
    delay(2000);
    body = httpGet(deviceSettings.apiHost + "/api/prices");
  }
  if (body.isEmpty()) {
    drawMsg("prices: brak odpowiedzi", deviceSettings.apiHost);
    delay(3000); return false;
  }
  DynamicJsonDocument doc(10240);
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    drawMsg("prices: JSON error"); delay(3000); return false;
  }
  JsonArray arr = doc["prices"].as<JsonArray>();
  if (arr.isNull()) { drawMsg("prices: brak tablicy"); delay(3000); return false; }

  priceCount = 0; currentPriceIdx = -1;

  for (JsonObject slot : arr) {
    if (priceCount >= 48) break;
    if (slot["price"].isNull()) continue;
    float price = slot["price"] | 0.0f;

    prices[priceCount].price       = price;
    prices[priceCount].isCheap     = slot["isCheap"]     | false;
    prices[priceCount].isExpensive = slot["isExpensive"] | false;

    String startStr = slot["start"].as<String>();
    String endStr   = slot["end"].as<String>();
    int Y=0,Mo=0,D=0,h=0,m=0,s=0;
    sscanf(startStr.c_str(), "%d-%d-%dT%d:%d:%d", &Y,&Mo,&D,&h,&m,&s);
    int localHour = (h + (int)(g_utcOffsetSec / 3600)) % 24;
    if (localHour < 0) localHour += 24;
    prices[priceCount].hour = localHour;

    // Porownaj godziny bezposrednio - bez mktime, bez time_t
    // localHour juz jest przeliczony na czas lokalny (+2h)
    // Aktualny czas lokalny mamy z NTP
    // Slot jest aktualny jesli localHour == aktualnaGodzinaLokalna
    // (i data sie zgadza - bierzemy pierwszy pasujacy slot)
    if (localHour == currentLocalHour && currentPriceIdx < 0) {
      currentPriceIdx = priceCount;
    }

    priceCount++;
  }
  Serial.printf("Prices: %d slots, idx=%d, offset=%ldh\n",
    priceCount, currentPriceIdx, g_utcOffsetSec/3600);
  // Debug: wypisz kilka slotow wokol aktualnego
  int dbgStart = max(0, currentPriceIdx - 2);
  int dbgEnd   = min(priceCount-1, currentPriceIdx + 2);
  for (int i = dbgStart; i <= dbgEnd; i++) {
    Serial.printf("  [%d] hour=%02d price=%.2f cheap=%d exp=%d %s\n",
      i, prices[i].hour, prices[i].price,
      prices[i].isCheap, prices[i].isExpensive,
      i == currentPriceIdx ? "<-- CURRENT" : "");
  }
  return priceCount > 0;
}

// ─── Tanie godziny ────────────────────────────────────────────────────────────
int minutesToNextCheap() {
  if (currentPriceIdx < 0) return -1;
  if (prices[currentPriceIdx].isCheap) return 0;
  for (int i = currentPriceIdx + 1; i < priceCount; i++)
    if (prices[i].isCheap) return (i - currentPriceIdx) * 60;
  return -1;
}

// ─── Rekomendacja ─────────────────────────────────────────────────────────────
String getRecommendation(String& desc) {
  if (currentPriceIdx < 0) { desc = "Brak danych cenowych"; return "?"; }
  bool  cheap     = prices[currentPriceIdx].isCheap;
  bool  expensive = prices[currentPriceIdx].isExpensive;
  bool  normal    = !cheap && !expensive;
  bool  nextCheap = (currentPriceIdx + 1 < priceCount) && prices[currentPriceIdx+1].isCheap;
  float pv        = state.pvTotal;
  float soc       = state.soc;
  float load      = state.sysLoad;
  bool  pvSurplus = (pv > load + 200);
  bool  pvGood    = (pv > 1000);

  if (cheap && pvGood)       { desc = "Tania cena + duze PV";      return "UZYWAJ TERAZ"; }
  if (cheap && soc > 50)     { desc = "Tania cena + bateria OK";   return "UZYWAJ TERAZ"; }
  if (cheap)                 { desc = "Tania cena";                return "UZYWAJ TERAZ"; }
  if (pvGood && pvSurplus)   { desc = "Duze PV pokrywa koszty";    return "UZYWAJ TERAZ"; }
  if (pvGood && normal)      { desc = "Duze PV, cena normalna";    return "UZYWAJ TERAZ"; }
  if (expensive && pvGood)   { desc = "PV ratuje - droga cena";    return "UZYWAJ Z GLOWA"; }
  if (expensive && pvGood)   { desc = "PV chroni przed wysoka cena"; return "UZYWAJ Z GLOWA"; }
  if (expensive)             { desc = "Drogo - ogranicz zuzycie";  return "OSZCZEDZAJ"; }
  if (nextCheap)             { desc = "Wkrotce bedzie taniej";     return "POCZEKAJ"; }
  if (pvSurplus)             { desc = "Nadwyzka PV";               return "UZYWAJ TERAZ"; }
  desc = "Cena normalna, PV slabie"; return "OK";
}

// ─── Rysowanie ────────────────────────────────────────────────────────────────
void drawScreen() {
  epd.fillScreen(TFT_WHITE);
  epd.setTextColor(TFT_BLACK, TFT_WHITE);

  float allMinP = 9999, allMaxP = -9999;
  for (int i = 0; i < min(priceCount, 24); i++) {
    allMinP = min(allMinP, prices[i].price);
    allMaxP = max(allMaxP, prices[i].price);
  }

  // ── 4 karty górne (y:10-134) ──────────────────────────────────────────────

  // PV
  drawCard(10, 10, 190, 125);
  drawIcon(18, 18, ICON_SUN);
  epd.setTextSize(1); epd.setCursor(38, 24); epd.print("PV");
  epd.setTextSize(3); epd.setCursor(18, 48); epd.print(String((int)state.pvTotal));
  epd.setTextSize(2); epd.setCursor(18 + String((int)state.pvTotal).length()*18+2, 55); epd.print("W");
  int pvPct = (int)(state.pvTotal / deviceSettings.pvMaxW * 100.0f);
  pvPct = min(pvPct, 100);
  // Linia 1: dziś kWh + efektywność LUB produkcja %
  epd.setTextSize(1); epd.setCursor(18, 90);
  if (state.solarKwh > 0 && state.independence > 0) {
    char pLine[28];
    snprintf(pLine, sizeof(pLine), "%.1fkWh  eff:%.0f%%",
      state.solarKwh, state.independence);
    epd.print(pLine);
  } else if (state.solarKwh > 0) {
    char pLine[20];
    snprintf(pLine, sizeof(pLine), "dzis: %.1f kWh", state.solarKwh);
    epd.print(pLine);
  } else if (state.pvTotal > 50) {
    epd.print("produkcja " + String(pvPct) + "%");
  } else {
    epd.print("brak slonca");
  }
  // Pasek efektywnosci (lub mocy szczytowej jako fallback)
  float barVal = state.independence > 0 ? state.independence : (float)pvPct;
  drawBar(18, 104, 168, 8, barVal);

  // Bateria
  drawCard(208, 10, 190, 125);
  drawIcon(216, 18, ICON_BAT);
  epd.setTextSize(1); epd.setCursor(236, 24); epd.print("Bateria EcoFlow");
  epd.setTextSize(3); epd.setCursor(216, 48); epd.print(String((int)state.soc));
  epd.setTextSize(2); epd.setCursor(216+String((int)state.soc).length()*18+2, 55); epd.print("%");
  // Strzalka kierunku + moc + czas
  epd.setTextSize(1);
  // Strzalka — ta sama wysokosc co cyfry % (textSize 3 = ~24px)
  // Pozycja: za cyframi SOC + znak % + maly odstep
  // Strzalka prawa krawedz kafelka (karta: x=208, w=190, prawa=398)
  int arrowX   = 372;  // prawa krawedz
  int arrowTop = 42;   // gora cyfr SOC
  int arrowBot = 64;   // dol cyfr SOC
  int arrowMid = (arrowTop + arrowBot) / 2;

  if (state.chgDsgState == 2) {
    // Strzalka w gore (ladowanie)
    epd.fillTriangle(arrowX, arrowBot, arrowX+14, arrowBot, arrowX+7, arrowTop, TFT_BLACK);
    epd.fillRect(arrowX+4, arrowBot-2, 6, 6, TFT_BLACK);
    epd.setTextSize(1); epd.setCursor(216, 90);
    if (state.chgRemTime > 0) {
      char tBuf[20]; snprintf(tBuf, sizeof(tBuf), "pelna za %dh %02dmin", state.chgRemTime/60, state.chgRemTime%60);
      epd.print(tBuf);
    } else { epd.print("laduje " + String((int)fabs(state.battPower)) + "W"); }
  } else if (state.chgDsgState == 1) {
    // Strzalka w dol (rozladowanie)
    epd.fillTriangle(arrowX, arrowTop, arrowX+14, arrowTop, arrowX+7, arrowBot, TFT_BLACK);
    epd.fillRect(arrowX+4, arrowTop-4, 6, 6, TFT_BLACK);
    epd.setTextSize(1); epd.setCursor(216, 90);
    if (state.dsgRemTime > 0) {
      char tBuf[20]; snprintf(tBuf, sizeof(tBuf), "zostalo %dh %02dmin", state.dsgRemTime/60, state.dsgRemTime%60);
      epd.print(tBuf);
    } else { epd.print("rozlad " + String((int)fabs(state.battPower)) + "W"); }
  } else {
    epd.fillRect(arrowX, arrowMid, 14, 3, TFT_BLACK);
    epd.setTextSize(1); epd.setCursor(216, 90); epd.print("idle");
  }
  drawBar(216, 104, 168, 8, state.soc);

  // Dom
  drawCard(406, 10, 190, 125);
  drawIcon(414, 18, ICON_HOME);
  epd.setTextSize(1); epd.setCursor(434, 24); epd.print("Zuzycie");
  epd.setTextSize(3); epd.setCursor(414, 48); epd.print(String((int)state.sysLoad));
  epd.setTextSize(2); epd.setCursor(414+String((int)state.sysLoad).length()*18+2, 55); epd.print("W");
  // Jedna linia: PV + Bat + Siec
  epd.setTextSize(1); epd.setCursor(414, 90);
  char zLine[24];
  snprintf(zLine, sizeof(zLine), "PV:%dW  Bat:%dW",
    (int)state.loadFromPv, (int)state.loadFromBat);
  epd.print(zLine);
  int pvCover = (state.sysLoad > 0) ? (int)(state.loadFromPv / state.sysLoad * 100.0f) : 0;
  pvCover = min(pvCover, 100);
  drawBar(414, 104, 168, 8, (float)pvCover);

  // Grid
  drawCard(604, 10, 186, 125);
  drawIcon(612, 18, ICON_GRID);
  epd.setTextSize(1); epd.setCursor(632, 24); epd.print("Siec");
  if (state.feedPower > 20) {
    String val = "+" + String((int)state.feedPower);
    epd.setTextSize(3); epd.setCursor(612, 48); epd.print(val);
    epd.setTextSize(2); epd.setCursor(612+val.length()*18+2, 55); epd.print("W");
    epd.setTextSize(1); epd.setCursor(612, 90); epd.print("eksport");
    drawBar(612, 104, 164, 8, min(state.feedPower / deviceSettings.pvMaxW * 100.0f, 100.0f));
  } else if (state.fromGrid > 20) {
    String val = String((int)state.fromGrid);
    epd.setTextSize(3); epd.setCursor(612, 48); epd.print(val);
    epd.setTextSize(2); epd.setCursor(612+val.length()*18+2, 55); epd.print("W");
    epd.setTextSize(1); epd.setCursor(612, 90); epd.print("pobor");
    drawBar(612, 104, 164, 8, min(state.fromGrid / deviceSettings.pvMaxW * 100.0f, 100.0f));
  } else {
    epd.setTextSize(3); epd.setCursor(612, 48); epd.print("~0");
    epd.setTextSize(2); epd.setCursor(648, 55); epd.print("W");
    epd.setTextSize(1); epd.setCursor(612, 90); epd.print("bilans zerowy");
    drawBar(612, 104, 164, 8, 0);
  }

  // Separator
  epd.fillRect(10, 147, 780, 2, TFT_BLACK);

  // ── Cena teraz (y:157-286) ────────────────────────────────────────────────
  drawCard(10, 157, 185, 130);
  epd.setTextSize(1); epd.setCursor(24, 170); epd.print("Cena teraz");
  drawPriceTrend(150, 170);
  if (currentPriceIdx >= 0) {

    char buf[10]; dtostrf(prices[currentPriceIdx].price, 4, 2, buf);
    epd.setTextSize(3); epd.setCursor(24, 196); epd.print(buf);
    epd.setTextSize(1); epd.setCursor(24, 226); epd.print("zl/kWh");
    drawPriceMeter(24, 236, 157, prices[currentPriceIdx].price, allMinP, allMaxP);
    // badge na pelna szerokosc karty z marginesem 8px, wyśrodkowany
    if      (prices[currentPriceIdx].isCheap)     drawBadge(18, 248, 161, 24, "TANIA",    true);
    else if (prices[currentPriceIdx].isExpensive) drawBadge(18, 248, 161, 24, "DROGA",    false);
    else                                           drawBadge(18, 248, 161, 24, "normalna", false);
  } else {
    epd.setTextSize(1); epd.setCursor(24, 196); epd.print("brak danych");
  }

  // Za godzinę — przerywana ramka
  epd.fillRect(203, 157, 185, 130, TFT_WHITE);
  for (int i = 203; i < 388; i += 8) {
    epd.drawLine(i, 157, min(i+5, 387), 157, TFT_BLACK);
    epd.drawLine(i, 286, min(i+5, 387), 286, TFT_BLACK);
  }
  for (int i = 157; i < 287; i += 8) {
    epd.drawLine(203, i, 203, min(i+5, 286), TFT_BLACK);
    epd.drawLine(387, i, 387, min(i+5, 286), TFT_BLACK);
  }
  epd.setTextSize(1); epd.setCursor(217, 170); epd.print("Za godzine");
  if (currentPriceIdx >= 0 && currentPriceIdx + 1 < priceCount) {
    char buf[10]; dtostrf(prices[currentPriceIdx+1].price, 4, 2, buf);
    epd.setTextSize(3); epd.setCursor(217, 196); epd.print(buf);
    epd.setTextSize(1); epd.setCursor(217, 226); epd.print("zl/kWh");
    drawPriceMeter(217, 236, 157, prices[currentPriceIdx+1].price, allMinP, allMaxP);
    if      (prices[currentPriceIdx+1].isCheap)     drawBadge(212, 248, 161, 24, "TANIA",    true);
    else if (prices[currentPriceIdx+1].isExpensive) drawBadge(212, 248, 161, 24, "DROGA",    false);
    else                                              drawBadge(212, 248, 161, 24, "normalna", false);
  }

  // Wykres 24h
  drawCard(396, 157, 394, 130);
  epd.setTextSize(1); epd.setCursor(410, 168); epd.print("Ceny 24h");

  if (priceCount > 0) {
    int chartX = 438, chartY = 268, chartW = 342, chartH = 76;  // zmniejszone zeby etykiety miescily sie
    int startIdx = max(0, currentPriceIdx - 2);
    int endIdx   = min(priceCount - 1, startIdx + 23);
    int slotW    = chartW / max(endIdx - startIdx + 1, 1);

    float minP = 9999, maxP = -9999;
    for (int i = startIdx; i <= endIdx; i++) {
      minP = min(minP, prices[i].price);
      maxP = max(maxP, prices[i].price);
    }
    // scaleMin moze byc ujemny — obslugujemy ceny ponizej zera
    float margin   = (maxP - minP) * 0.1f;
    float scaleMin = minP - margin;  // moze byc ujemny
    float range    = max(maxP - scaleMin, 0.01f);
    // Oblicz pozycje osi Y=0 na wykresie
    int zeroY = chartY - (int)((0.0f - scaleMin) / range * (chartH - 4));
    zeroY = max(zeroY, chartY - chartH + 4);
    zeroY = min(zeroY, chartY);

    for (int i = startIdx; i <= endIdx; i++) {
      int x = chartX + (i - startIdx) * slotW;
      int h, y;
      if (prices[i].price >= 0) {
        // Dodatnia — slupek w gore od zeroY
        h = max((int)((prices[i].price - max(scaleMin, 0.0f)) / range * (chartH-4)), 3);
        y = zeroY - h;
        // Clamp — nie wychodz poza gorna krawedz karty
        int topLimit = chartY - chartH + 2;
        if (y < topLimit) { h -= (topLimit - y); y = topLimit; }
      } else {
        // Ujemna — slupek w dol od zeroY
        h = max((int)((-prices[i].price) / range * (chartH-4)), 3);
        y = zeroY;
        // Clamp — nie wychodz poza dolna krawedz karty
        int botLimit = chartY - 2;
        if (y + h > botLimit) h = botLimit - y;
      }
      if (h < 2) continue;  // za maly - pomin

      if (i == currentPriceIdx) {
        // Aktualny slot — normalny slupek ale z bialymi przerwami po bokach
        // Biala przerwa z lewej
        epd.fillRect(x-3, y, 3, h, TFT_WHITE);
        // Biala przerwa z prawej
        epd.fillRect(x+slotW, y, 3, h, TFT_WHITE);
        // Wypelniony slupek
        epd.fillRect(x+1, y, slotW-2, h, TFT_BLACK);
        // Ramka
        epd.drawRect(x, y-2, slotW, h+3, TFT_BLACK);
      } else if (prices[i].isCheap) {
        // Tania — pelne czarne
        epd.fillRect(x+1, y, slotW-2, h, TFT_BLACK);
      } else if (prices[i].isExpensive) {
        // Droga — pusty obrys
        epd.drawRect(x+1, y, slotW-2, h, TFT_BLACK);
      } else {
        // Normalna — obrys + poziome kreski co 3px (zamiast szarego)
        epd.drawRect(x+1, y, slotW-2, h, TFT_BLACK);
        for (int ly = y+3; ly < y+h-1; ly += 3) {
          epd.drawLine(x+2, ly, x+slotW-3, ly, TFT_BLACK);
        }
      }
    }
    // Linia bazowa na poziomie 0 (moze byc wyzej niz dol wykresu gdy sa ujemne ceny)
    epd.drawLine(chartX, zeroY, chartX+chartW, zeroY, TFT_BLACK);
    // Jesli sa ceny ujemne — cienka linia na dnie
    if (zeroY < chartY) epd.drawLine(chartX, chartY, chartX+chartW, chartY, TFT_BLACK);

    // Legenda Y — min, max, i 0 jesli sa ujemne
    epd.setTextSize(1);
    char maxBuf[8], minBuf[8];
    dtostrf(maxP, 4, 2, maxBuf);
    dtostrf(minP, 4, 2, minBuf);
    // Max — gora
    epd.setCursor(chartX - 30, chartY - (chartH-4) - 2);
    epd.print(maxBuf);
    // Min — dol
    epd.setCursor(chartX - 30, chartY + 2);
    epd.print(minBuf);
    // Srodek lub 0.00 jesli sa ujemne
    if (minP < 0) {
      epd.setCursor(chartX - 30, zeroY + 2);
      epd.print("0.00");
      epd.drawLine(chartX - 3, zeroY, chartX - 1, zeroY, TFT_BLACK);
    } else {
      float midP = (minP + maxP) / 2.0f;
      char midBuf[8]; dtostrf(midP, 4, 2, midBuf);
      epd.setCursor(chartX - 30, chartY - (chartH-4)/2 - 4);
      epd.print(midBuf);
      epd.drawLine(chartX - 3, chartY - (chartH-4)/2, chartX - 1, chartY - (chartH-4)/2, TFT_BLACK);
    }
    // Pionowa linia osi Y
    epd.drawLine(chartX - 1, chartY - (chartH-4), chartX - 1, chartY, TFT_BLACK);
    epd.drawLine(chartX - 3, chartY - (chartH-4), chartX - 1, chartY - (chartH-4), TFT_BLACK);
    epd.drawLine(chartX - 3, chartY, chartX - 1, chartY, TFT_BLACK);

    // Etykiety godzin co 6h
    epd.setTextSize(1);
    for (int i = startIdx; i <= endIdx; i++) {
      if (prices[i].hour % 6 == 0) {
        int x = chartX + (i - startIdx) * slotW;
        char lb[4]; snprintf(lb, sizeof(lb), "%02d", prices[i].hour);
        epd.setCursor(x, zeroY+4); epd.print(lb);
      }
    }

    // Tanie zakresy — gruba linia pod słupkami
    int rs = -1;
    for (int i = startIdx; i <= endIdx; i++) {
      if (prices[i].isCheap && rs < 0) rs = i;
      bool last = (i == endIdx);
      if (rs >= 0 && (last || !prices[i+1].isCheap)) {
        int lx = chartX + (rs - startIdx) * slotW;
        int lw = (i - rs + 1) * slotW;
        epd.fillRect(lx, zeroY+1, lw-1, 2, TFT_BLACK);
        rs = -1;
      }
    }
  }

  // Separator
  epd.fillRect(10, 299, 780, 2, TFT_BLACK);

  // ── Pasek tanich godzin (y:305-330) ───────────────────────────────────────
  epd.fillRect(10, 305, 780, 26, TFT_WHITE);
  epd.drawRect(10, 305, 780, 26, TFT_BLACK);
  epd.setTextSize(1); epd.setCursor(18, 315); epd.print("Najtaniej dzis:");

  int bx = 115, rs2 = -1;
  int maxSlot = min(priceCount, 24);
  for (int i = 0; i < maxSlot; i++) {
    if (prices[i].isCheap && rs2 < 0) rs2 = i;
    bool last = (i == maxSlot-1);
    if (rs2 >= 0 && (last || !prices[i+1].isCheap)) {
      char lb[10];
      snprintf(lb, sizeof(lb), "%02d-%02dh", prices[rs2].hour, prices[i].hour+1);
      int bw = strlen(lb)*6+8;
      if (bx+bw < 570) {
        epd.fillRect(bx, 308, bw, 18, TFT_BLACK);
        epd.setTextColor(TFT_WHITE, TFT_BLACK);
        epd.setCursor(bx+4, 315); epd.print(lb);
        epd.setTextColor(TFT_BLACK, TFT_WHITE);
        bx += bw+5;
      }
      rs2 = -1;
    }
  }

  int mins = minutesToNextCheap();
  if (mins == 0) {
    epd.setCursor(580, 315); epd.print(">> teraz tania! <<");
  } else if (mins > 0) {
    char tb[20];
    snprintf(tb, sizeof(tb), "tania za %dh", mins/60 + (mins%60 > 0 ? 1 : 0));
    epd.setCursor(620, 315); epd.print(tb);
  }

  // ── Rekomendacja (y:335-415) ──────────────────────────────────────────────
  epd.fillRect(10, 335, 780, 82, TFT_BLACK);
  String recDesc;
  String rec = getRecommendation(recDesc);
  epd.setTextColor(TFT_WHITE, TFT_BLACK);
  // Glowna rekomendacja — textSize 4, wycentrowana pionowo i poziomo
  // textSize 4: kazdy znak ~24px szeroki, ~32px wysoki
  int recW = rec.length() * 24;
  int recX = max(16, (800 - recW) / 2);
  epd.setTextSize(4); epd.setCursor(recX, 348); epd.print(rec);
  // Opis — textSize 2, wycentrowany na dole
  int descW = recDesc.length() * 12;
  int descX = max(16, (800 - descW) / 2);
  epd.setTextSize(2); epd.setCursor(descX, 400); epd.print(recDesc);
  epd.setTextColor(TFT_BLACK, TFT_WHITE);

  // ── Stopka ────────────────────────────────────────────────────────────────
  epd.fillRect(10, 423, 780, 2, TFT_BLACK);
  time_t now2 = time(nullptr);
  struct tm ti2 = {};
  localtime_r(&now2, &ti2);
  char timeBuf[24];
  strftime(timeBuf, sizeof(timeBuf), "%H:%M  %d.%m.%Y", &ti2);
  epd.setTextSize(1);
  epd.setCursor(16, 436); epd.print("Akt.: "); epd.print(timeBuf);
  epd.setCursor(520, 436); epd.print("budzenie #"); epd.print(rtc_bootCount);
  epd.setCursor(680, 436); epd.print(state.connected ? "EcoFlow: OK" : "EcoFlow: brak");
  epd.setCursor(16, 452); epd.print("Firmware: v"); epd.print(FIRMWARE_VERSION);
  epd.print("  GitHub: ");
  epd.print(latestGithubVersion.isEmpty() ? "sprawdzanie nocne" : "v" + latestGithubVersion);

  epd.update();
}

void drawMsg(const String& l1, const String& l2) {
  epd.fillScreen(TFT_WHITE);
  epd.setTextColor(TFT_BLACK, TFT_WHITE);
  epd.setTextSize(2);
  epd.setCursor(20, 20); epd.print(l1);
  if (l2.length()) { epd.setCursor(20, 60); epd.print(l2); }
  epd.update();
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  rtc_bootCount++;
  delay(3000);  // okno serwisowe na Upload

  epd.begin();
  loadDeviceSettings();
  {
    Preferences prefs;
    if (prefs.begin("epaper", true)) {
      latestGithubVersion = prefs.getString("ghVersion", "");
      prefs.end();
    }
  }

  // Zaladuj efficiency z RTC cache
  state.independence = rtc_independence;
  state.forecastKwh  = rtc_forecastKwh;
  state.solarKwh     = rtc_solarKwh;

  if (!connectWiFi()) {
    drawMsg("Brak WiFi!", "Spi 5 min...");
    WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
    esp_deep_sleep((uint64_t)deviceSettings.sleepSeconds * 1000000ULL);
    return;
  }

  // NTP sync
  configTzTime(TZ_INFO, NTP_SERVER, NTP_SERVER_2);
  {
    time_t t = 0; struct tm tmp = {};
    for (int i = 0; i < 30; i++) {
      delay(500); t = time(nullptr); localtime_r(&t, &tmp);
      if (tmp.tm_year > 100) break;
    }
    if (tmp.tm_year <= 100) {
      WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
      esp_deep_sleep((uint64_t)deviceSettings.sleepSeconds * 1000000ULL);
      return;
    }
  }

  calcUtcOffset();

  time_t now = time(nullptr);
  struct tm nowTm = {};
  localtime_r(&now, &nowTm);
  int currentHour = nowTm.tm_hour;

  bool s = fetchState();
  bool p = fetchPrices(currentHour);

  const bool nightMode = isNightHours(nowTm.tm_hour);
  const unsigned long sleepSec = calcSleepSeconds(nowTm.tm_hour, nowTm.tm_min);

  if (s && p) {
    drawScreen();
  } else {
    drawMsg("Blad pobierania!", !s ? "/api/state" : "/api/prices");
  }

  if (nightMode) {
    epd.fillScreen(TFT_WHITE);
    epd.setTextColor(TFT_BLACK, TFT_WHITE);
    epd.setTextSize(2); epd.setCursor(20, 20); epd.print("Dobranoc :)");
    epd.setTextSize(1); epd.setCursor(20, 60);
    epd.print("Nastepne odswiezenie o " + String(deviceSettings.nightHourEnd) + ":00");
    char tb[20]; strftime(tb, sizeof(tb), "%H:%M  %d.%m.%Y", &nowTm);
    epd.setCursor(20, 80); epd.print(tb);
    epd.update();
  }

  bool otaUpdated = false;
  // Check once as the device enters its long night sleep. The e-paper is not
  // refreshed by OTA; it keeps its existing night image while firmware downloads.
  if (!nightMode) {
    rtc_nightUpdateChecked = false;
  } else if (!rtc_nightUpdateChecked) {
    rtc_nightUpdateChecked = true;
    String checkedVersion;
    otaUpdated = checkAndInstallFirmwareUpdate(&checkedVersion);
    if (!checkedVersion.isEmpty()) {
      Preferences prefs;
      if (prefs.begin("epaper", false)) {
        prefs.putString("ghVersion", checkedVersion);
        prefs.end();
      }
    }
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  if (otaUpdated) {
    Serial.println("OTA: wersja zapisana, restartuje urzadzenie");
    ESP.restart();
  }

  Serial.printf("Sleep %lus. Boot #%d\n", sleepSec, rtc_bootCount);
  esp_deep_sleep((uint64_t)sleepSec * 1000000ULL);
}

void loop() {}

