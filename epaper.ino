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
  float systemSoc   = -1;
  float battPower   = 0;
  float battPower2  = 0;
  int   chgDsgState = 0;
  int   chgDsgState2= 0;
  float sysLoad     = 0;
  float feedPower   = 0;
  float fromGrid    = 0;
  float loadFromPv  = 0;
  float loadFromBat = 0;
  float loadFromGrid= 0;
  // Efficiency
  float solarKwh    = 0;
  float forecastKwh = 0;
  float forecastTomorrowKwh = -1;
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

// powGetSysLoad is signed (negative means export). Household consumption is
// the sum of the positive PV, battery and grid contributions instead.
float houseLoadW() {
  float fromSources = max(0.0f, state.loadFromPv) +
                      max(0.0f, state.loadFromBat) +
                      max(0.0f, state.loadFromGrid);
  if (fromSources > 0) return fromSources;
  // EcoFlow's sysLoad is signed: a negative value means export to the grid.
  return state.sysLoad;
}

// EcoFlow can report 5939 minutes while a battery is idle. Treat implausible
// countdowns as unavailable instead of showing nearly 99 hours on the screen.
bool validBatteryRuntime(int minutes) {
  return minutes > 0 && minutes <= 24 * 60;
}

float batteryFlowAmount(float power, int chgDsgState, bool charging) {
  if (chgDsgState == 2) return charging ? fabsf(power) : 0;
  if (chgDsgState == 1) return charging ? 0 : fabsf(power);
  if (power > 20) return charging ? power : 0;
  if (power < -20) return charging ? 0 : fabsf(power);
  return 0;
}

float totalBatteryFlow(bool charging) {
  return batteryFlowAmount(state.battPower, state.chgDsgState, charging) +
         batteryFlowAmount(state.battPower2, state.chgDsgState2, charging);
}

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
RTC_DATA_ATTR float rtc_forecastTomorrowKwh = -1;
RTC_DATA_ATTR int   rtc_effFetchHour      = -1;
RTC_DATA_ATTR bool  rtc_nightUpdateChecked = false;
RTC_DATA_ATTR bool  rtc_dashboardDrawn    = false;  // na ekranie jest dashboard (nie komunikat)

// ─── Forward declarations ──────────────────────────────────────────────────────
void drawMsg(const String& l1, const String& l2 = "");

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
// Kanal i BSSID routera z poprzedniego polaczenia - pomijamy skanowanie sieci.
RTC_DATA_ATTR bool    rtc_wifiKnown = false;
RTC_DATA_ATTR uint8_t rtc_wifiBssid[6];
RTC_DATA_ATTR int32_t rtc_wifiChannel = 0;

static bool waitForWiFi(int halfSeconds) {
  for (int i = 0; i < halfSeconds && WiFi.status() != WL_CONNECTED; i++) delay(500);
  return WiFi.status() == WL_CONNECTED;
}

static void rememberWiFi() {
  uint8_t* bssid = WiFi.BSSID();
  if (bssid == nullptr) return;
  memcpy(rtc_wifiBssid, bssid, 6);
  rtc_wifiChannel = WiFi.channel();
  rtc_wifiKnown = true;
}

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("sloneczny-panel");

  // Szybka sciezka: znany kanal + BSSID (zwykle 1-2 s zamiast 3-6 s).
  if (rtc_wifiKnown) {
    WiFi.begin(deviceSettings.wifiSsid.c_str(), deviceSettings.wifiPassword.c_str(),
               rtc_wifiChannel, rtc_wifiBssid);
    if (waitForWiFi(10)) { rememberWiFi(); return true; }  // 5 s
    Serial.println("WiFi: szybkie laczenie nieudane, pelne skanowanie");
    rtc_wifiKnown = false;  // router mogl zmienic kanal/AP
    WiFi.disconnect();
    delay(200);
  }

  // Zwykle laczenie - pierwsza proba 15 s
  WiFi.begin(deviceSettings.wifiSsid.c_str(), deviceSettings.wifiPassword.c_str());
  if (waitForWiFi(30)) { rememberWiFi(); return true; }
  // Druga proba - disconnect i reconnect
  WiFi.disconnect();
  delay(500);
  WiFi.begin(deviceSettings.wifiSsid.c_str(), deviceSettings.wifiPassword.c_str());
  if (waitForWiFi(20)) { rememberWiFi(); return true; }
  return false;
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
  StaticJsonDocument<768> filter;
  filter["pvTotal"] = true;
  filter["soc"] = true;
  filter["cmsBattSoc"] = true;
  filter["battPower"] = true;
  filter["chgDsgState"] = true;
  filter["device2"]["battPower"] = true;
  filter["device2"]["chgDsgState"] = true;
  filter["sysLoad"] = true;
  filter["feedPower"] = true;
  filter["fromGrid"] = true;
  filter["loadFromPv"] = true;
  filter["loadFromBat"] = true;
  filter["loadFromGrid"] = true;
  filter["chgRemTime"] = true;
  filter["dsgRemTime"] = true;
  filter["connected"] = true;
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) != DeserializationError::Ok)
    return false;
  state.pvTotal     = doc["pvTotal"]    | 0.0f;
  state.soc         = doc["soc"]        | 0.0f;
  state.systemSoc   = doc["cmsBattSoc"] | -1.0f;
  state.battPower   = doc["battPower"]  | 0.0f;
  state.chgDsgState = doc["chgDsgState"]| 0;
  state.battPower2  = doc["device2"]["battPower"] | 0.0f;
  state.chgDsgState2= doc["device2"]["chgDsgState"] | 0;
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
        state.forecastTomorrowKwh = doc["forecastTomorrow"] | -1.0f;
        Serial.printf("forecast OK: today=%.1f tomorrow=%.1f kWh\n",
          state.forecastKwh, state.forecastTomorrowKwh);
        ok = true;
      }
    }
  }

  return ok;
}

// ─── Ceny: pobieranie + cache w RTC ──────────────────────────────────────────
// Klucz czasu (minuty, UTC) - monotoniczny, do porownywania slotow z "teraz".
long utcTimeKey(int Y, int Mo, int D, int h, int m) {
  return (((long)Y * 12 + (Mo - 1)) * 31 + (D - 1)) * 1440L + h * 60 + m;
}

long nowUtcKey() {
  time_t t = time(nullptr);
  struct tm u = {};
  gmtime_r(&t, &u);
  return utcTimeKey(u.tm_year + 1900, u.tm_mon + 1, u.tm_mday, u.tm_hour, u.tm_min);
}

long priceKey[48];  // czas startu kazdego slotu (UTC), rownolegle do prices[]

struct CachedPrice {
  long    key;
  float   price;
  int8_t  hour;
  uint8_t flags;  // bit0 = tania, bit1 = droga
};
RTC_DATA_ATTR CachedPrice rtc_prices[48];
RTC_DATA_ATTR int         rtc_priceCount = 0;
RTC_DATA_ATTR int         rtc_priceAttemptSlot = -1;  // ostatnia godzina proby pobrania

void savePricesToRtc() {
  rtc_priceCount = priceCount;
  for (int i = 0; i < priceCount; i++) {
    rtc_prices[i].key   = priceKey[i];
    rtc_prices[i].price = prices[i].price;
    rtc_prices[i].hour  = (int8_t)prices[i].hour;
    rtc_prices[i].flags = (prices[i].isCheap ? 1 : 0) | (prices[i].isExpensive ? 2 : 0);
  }
}

void loadPricesFromRtc() {
  priceCount = min(rtc_priceCount, 48);
  for (int i = 0; i < priceCount; i++) {
    priceKey[i]              = rtc_prices[i].key;
    prices[i].price          = rtc_prices[i].price;
    prices[i].hour           = rtc_prices[i].hour;
    prices[i].isCheap        = rtc_prices[i].flags & 1;
    prices[i].isExpensive    = rtc_prices[i].flags & 2;
  }
}

// Biezacy slot = ostatni, ktory zaczal sie nie pozniej niz teraz i nie dawniej niz
// godzine temu. Porownanie z data chroni przed wczorajszym slotem o tej samej godzinie.
void selectCurrentPrice() {
  const long nowKey = nowUtcKey();
  currentPriceIdx = -1;
  for (int i = 0; i < priceCount; i++) {
    if (priceKey[i] <= nowKey && nowKey - priceKey[i] < 60) currentPriceIdx = i;
  }
  Serial.printf("Prices: %d slots, idx=%d, offset=%ldh\n",
    priceCount, currentPriceIdx, g_utcOffsetSec/3600);
}

// Pobiera /api/prices i zapisuje do prices[] + RTC. Bledy tylko w logu -
// nie nadpisujemy dashboardu na e-papierze.
bool fetchPricesFromApi() {
  String body = httpGet(deviceSettings.apiHost + "/api/prices");
  if (body.isEmpty()) {  // jedna ponowna proba
    delay(2000);
    body = httpGet(deviceSettings.apiHost + "/api/prices");
  }
  if (body.isEmpty()) { Serial.println("prices: brak odpowiedzi"); return false; }
  DynamicJsonDocument doc(10240);
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    Serial.println("prices: JSON error");
    return false;
  }
  JsonArray arr = doc["prices"].as<JsonArray>();
  if (arr.isNull()) { Serial.println("prices: brak tablicy"); return false; }

  int n = 0;
  for (JsonObject slot : arr) {
    if (n >= 48) break;
    if (slot["price"].isNull()) continue;
    prices[n].price       = slot["price"] | 0.0f;
    prices[n].isCheap     = slot["isCheap"]     | false;
    prices[n].isExpensive = slot["isExpensive"] | false;

    String startStr = slot["start"].as<String>();
    int Y=0,Mo=0,D=0,h=0,m=0,s=0;
    sscanf(startStr.c_str(), "%d-%d-%dT%d:%d:%d", &Y,&Mo,&D,&h,&m,&s);
    int localHour = (h + (int)(g_utcOffsetSec / 3600)) % 24;
    if (localHour < 0) localHour += 24;
    prices[n].hour = localHour;
    priceKey[n] = utcTimeKey(Y, Mo, D, h, m);
    n++;
  }
  if (n == 0) return false;
  priceCount = n;
  savePricesToRtc();
  return true;
}

// Ceny zmieniaja sie raz na dobe (jutrzejsze ok. 13:00), wiec cache w RTC wystarcza.
// Pobieramy gdy: cache pusty/nieaktualny, albo pokrywa mniej niz 12 h do przodu
// (czekamy na jutrzejsze) - ale nie czesciej niz raz na godzine.
bool loadPrices(int currentLocalHour, int yday) {
  loadPricesFromRtc();
  selectCurrentPrice();

  const long nowKey = nowUtcKey();
  const long lastKey = priceCount > 0 ? priceKey[priceCount - 1] : 0;
  const bool needFetch = currentPriceIdx < 0 || (lastKey - nowKey) < 12 * 60;
  const int attemptSlot = yday * 24 + currentLocalHour;

  if (needFetch && rtc_priceAttemptSlot != attemptSlot) {
    rtc_priceAttemptSlot = attemptSlot;
    if (fetchPricesFromApi()) selectCurrentPrice();
    else if (currentPriceIdx < 0) priceCount = 0;  // cache bezuzyteczny
  } else if (needFetch) {
    Serial.println("Prices: uzywam cache (kolejna proba za godzine)");
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
  float soc       = state.systemSoc >= 0 && state.systemSoc <= 100 ? state.systemSoc : state.soc;
  float load      = houseLoadW();
  bool  pvSurplus = (load >= 0 && pv > load + 200);
  bool  pvGood    = (pv > 1000);

  if (cheap) {
    desc = pvGood ? "Tania cena + duze PV" : (soc > 50 ? "Tania cena + bateria OK" : "Tania cena");
    return "UZYWAJ TERAZ";
  }
  if (pvGood && pvSurplus)   { desc = "Duze PV pokrywa koszty";    return "UZYWAJ TERAZ"; }
  if (pvGood && normal)      { desc = "Duze PV, cena normalna";    return "UZYWAJ TERAZ"; }
  if (expensive && pvGood)   { desc = "PV ratuje - droga cena";    return "UZYWAJ Z GLOWA"; }
  if (expensive)             { desc = "Drogo - ogranicz zuzycie";  return "OSZCZEDZAJ"; }
  if (nextCheap)             { desc = "Wkrotce bedzie taniej";     return "POCZEKAJ"; }
  if (pvSurplus)             { desc = "Nadwyzka PV";               return "UZYWAJ TERAZ"; }
  desc = "Cena normalna, PV slabie"; return "OK";
}

// ─── Rysowanie ────────────────────────────────────────────────────────────────
void drawScreen() {
  epd.fillScreen(TFT_WHITE);
  epd.setTextColor(TFT_BLACK, TFT_WHITE);

  // ── 4 karty górne (y:10-134) ──────────────────────────────────────────────

  // PV
  drawCard(10, 10, 190, 125);
  drawIcon(18, 18, ICON_SUN);
  epd.setTextSize(1); epd.setCursor(38, 24); epd.print("PV");
  epd.setTextSize(3); epd.setCursor(18, 48); epd.print(String((int)state.pvTotal));
  epd.setTextSize(2); epd.setCursor(18 + String((int)state.pvTotal).length()*18+2, 55); epd.print("W");
  int pvPct = (int)(state.pvTotal / deviceSettings.pvMaxW * 100.0f);
  pvPct = min(pvPct, 100);
  // Dwie krótkie linie mieszczą dzisiejszą produkcję i prognozę na jutro.
  epd.setTextSize(1); epd.setCursor(18, 82);
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
  epd.setCursor(18, 94);
  if (state.forecastTomorrowKwh > 0) {
    char tomorrowLine[28];
    snprintf(tomorrowLine, sizeof(tomorrowLine), "Jutro PV: %.1f kWh", state.forecastTomorrowKwh);
    epd.print(tomorrowLine);
  } else {
    epd.print("Jutro PV: brak prognozy");
  }
  // Pasek efektywnosci (lub mocy szczytowej jako fallback)
  float barVal = state.independence > 0 ? state.independence : (float)pvPct;
  drawBar(18, 112, 168, 8, barVal);

  // Bateria
  drawCard(208, 10, 190, 125);
  drawIcon(216, 18, ICON_BAT);
  epd.setTextSize(1); epd.setCursor(236, 24); epd.print("Bateria EcoFlow");
  float batterySoc = state.systemSoc >= 0 && state.systemSoc <= 100 ? state.systemSoc : state.soc;
  epd.setTextSize(3); epd.setCursor(216, 48); epd.print(String((int)batterySoc));
  epd.setTextSize(2); epd.setCursor(216+String((int)batterySoc).length()*18+2, 55); epd.print("%");
  // Strzalka kierunku + moc + czas
  epd.setTextSize(1);
  // Strzalka — ta sama wysokosc co cyfry % (textSize 3 = ~24px)
  // Pozycja: za cyframi SOC + znak % + maly odstep
  // Strzalka prawa krawedz kafelka (karta: x=208, w=190, prawa=398)
  int arrowX   = 372;  // prawa krawedz
  int arrowTop = 42;   // gora cyfr SOC
  int arrowBot = 64;   // dol cyfr SOC
  int arrowMid = (arrowTop + arrowBot) / 2;

  float batteryChargeW = totalBatteryFlow(true);
  float batteryDischargeW = totalBatteryFlow(false);
  bool batteryCharging = batteryChargeW > 20 && batteryChargeW >= batteryDischargeW;
  bool batteryDischarging = batteryDischargeW > 20 && batteryDischargeW > batteryChargeW;
  int batteryNetPower = (int)fabsf(batteryChargeW - batteryDischargeW);

  if (batteryCharging) {
    // Strzalka w gore (ladowanie)
    epd.fillTriangle(arrowX, arrowBot, arrowX+14, arrowBot, arrowX+7, arrowTop, TFT_BLACK);
    epd.fillRect(arrowX+4, arrowBot-2, 6, 6, TFT_BLACK);
    epd.setTextSize(1); epd.setCursor(216, 90);
    if (validBatteryRuntime(state.chgRemTime)) {
      char tBuf[20]; snprintf(tBuf, sizeof(tBuf), "pelna za %dh %02dmin", state.chgRemTime/60, state.chgRemTime%60);
      epd.print(tBuf);
    } else { epd.print("laduje " + String(batteryNetPower) + "W"); }
  } else if (batteryDischarging) {
    // Strzalka w dol (rozladowanie)
    epd.fillTriangle(arrowX, arrowTop, arrowX+14, arrowTop, arrowX+7, arrowBot, TFT_BLACK);
    epd.fillRect(arrowX+4, arrowTop-4, 6, 6, TFT_BLACK);
    epd.setTextSize(1); epd.setCursor(216, 90);
    if (validBatteryRuntime(state.dsgRemTime)) {
      char tBuf[20]; snprintf(tBuf, sizeof(tBuf), "zostalo %dh %02dmin", state.dsgRemTime/60, state.dsgRemTime%60);
      epd.print(tBuf);
    } else { epd.print("rozlad " + String(batteryNetPower) + "W"); }
  } else {
    epd.fillRect(arrowX, arrowMid, 14, 3, TFT_BLACK);
    epd.setTextSize(1); epd.setCursor(216, 90); epd.print("idle");
  }
  drawBar(216, 104, 168, 8, batterySoc);

  // Dom
  drawCard(406, 10, 190, 125);
  drawIcon(414, 18, ICON_HOME);
  epd.setTextSize(1); epd.setCursor(434, 24); epd.print("Dom");
  float loadW = houseLoadW();
  if (loadW != 0 || state.connected) {
    String loadText = String((int)loadW);
    epd.setTextSize(3); epd.setCursor(414, 48); epd.print(loadText);
    epd.setTextSize(2); epd.setCursor(414+loadText.length()*18+2, 55); epd.print("W");
  } else {
    epd.setTextSize(2); epd.setCursor(414, 52); epd.print("BRAK DANYCH");
  }
  // Zrodla zuzycia: PV + bateria + siec
  epd.setTextSize(1); epd.setCursor(414, 90);
  if (loadW != 0 || state.connected) {
    char zLine[30];
    snprintf(zLine, sizeof(zLine), "PV:%d Bat:%d Siec:%d",
      (int)max(0.0f, state.loadFromPv),
      (int)max(0.0f, state.loadFromBat),
      (int)max(0.0f, state.loadFromGrid));
    epd.print(zLine);
  } else {
    epd.print("brak danych zuzycia");
  }
  int pvCover = (loadW > 0) ? (int)(max(0.0f, state.loadFromPv) / loadW * 100.0f) : 0;
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

  // ── Ceny i wykres (y:157-287) ────────────────────────────────────────────
  // Biezaca cena jest jedynym wypelnionym polem; reszta sekcji pozostaje lekka.
  epd.fillRect(10, 157, 185, 130, TFT_BLACK);
  epd.setTextColor(TFT_WHITE, TFT_BLACK);
  epd.setTextSize(1); epd.setCursor(24, 174); epd.print("CENA TERAZ");
  if (currentPriceIdx >= 0) {
    char buf[10]; dtostrf(prices[currentPriceIdx].price, 4, 2, buf);
    epd.setTextSize(4); epd.setCursor(24, 196); epd.print(buf);
    epd.setTextSize(1); epd.setCursor(24, 232); epd.print("zl/kWh");
    epd.setTextSize(2); epd.setCursor(24, 259);
    if      (prices[currentPriceIdx].isCheap)     epd.print("TANIA");
    else if (prices[currentPriceIdx].isExpensive) epd.print("DROGA");
    else                                           epd.print("NORMALNA");
  } else {
    epd.setTextSize(2); epd.setCursor(24, 214); epd.print("BRAK DANYCH");
  }
  epd.setTextColor(TFT_BLACK, TFT_WHITE);

  // Cena kolejnej godziny: prosty podzial pionowy zamiast przerywanej ramki.
  epd.drawLine(203, 157, 203, 287, TFT_BLACK);
  epd.setTextSize(1); epd.setCursor(218, 174); epd.print("ZA GODZINE");
  if (currentPriceIdx >= 0 && currentPriceIdx + 1 < priceCount) {
    char buf[10]; dtostrf(prices[currentPriceIdx+1].price, 4, 2, buf);
    epd.setTextSize(4); epd.setCursor(218, 196); epd.print(buf);
    epd.setTextSize(1); epd.setCursor(218, 232); epd.print("zl/kWh");
    epd.setTextSize(2); epd.setCursor(218, 259);
    if      (prices[currentPriceIdx+1].isCheap)     epd.print("TANIA");
    else if (prices[currentPriceIdx+1].isExpensive) epd.print("DROGA");
    else                                              epd.print("NORMALNA");
  } else {
    epd.setTextSize(1); epd.setCursor(218, 214); epd.print("brak danych");
  }

  // Wykres 24h
  epd.drawLine(396, 157, 790, 157, TFT_BLACK);
  epd.setTextSize(1); epd.setCursor(410, 174); epd.print("CENY ENERGII - 24H");

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

  // ── Rekomendacja i najtansze godziny ──────────────────────────────────────
  epd.fillRect(10, 299, 780, 2, TFT_BLACK);
  epd.fillRect(10, 305, 780, 74, TFT_BLACK);
  String recDesc;
  String rec = getRecommendation(recDesc);
  epd.setTextColor(TFT_WHITE, TFT_BLACK);
  int recW = rec.length() * 18;
  int recX = max(16, (800 - recW) / 2);
  epd.setTextSize(3); epd.setCursor(recX, 317); epd.print(rec);
  int descW = recDesc.length() * 6;
  int descX = max(16, (800 - descW) / 2);
  epd.setTextSize(1); epd.setCursor(descX, 358); epd.print(recDesc);
  epd.setTextColor(TFT_BLACK, TFT_WHITE);

  epd.drawLine(10, 386, 790, 386, TFT_BLACK);
  epd.setTextSize(1); epd.setCursor(18, 401); epd.print("NAJTANIEJ DZIS:");
  int bx = 128, rs2 = -1, cheapRanges = 0;
  int maxSlot = min(priceCount, 24);
  for (int i = 0; i < maxSlot; i++) {
    if (prices[i].isCheap && rs2 < 0) rs2 = i;
    bool last = (i == maxSlot-1);
    if (rs2 >= 0 && (last || !prices[i+1].isCheap)) {
      char lb[10];
      snprintf(lb, sizeof(lb), "%02d-%02dh", prices[rs2].hour, prices[i].hour+1);
      int bw = strlen(lb)*6+8;
      if (bx+bw < 600) {
        epd.fillRect(bx, 392, bw, 18, TFT_BLACK);
        epd.setTextColor(TFT_WHITE, TFT_BLACK);
        epd.setCursor(bx+4, 399); epd.print(lb);
        epd.setTextColor(TFT_BLACK, TFT_WHITE);
        bx += bw+5;
        cheapRanges++;
      }
      rs2 = -1;
    }
  }
  if (cheapRanges == 0) {
    epd.setCursor(128, 401);
    epd.print(priceCount > 0 ? "brak tanszych godzin w danych" : "brak danych cenowych");
  }

  int mins = minutesToNextCheap();
  if (mins == 0) {
    epd.setCursor(620, 401); epd.print("TERAZ TANIA");
  } else if (mins > 0) {
    char tb[20];
    snprintf(tb, sizeof(tb), "tania za %dh", mins/60 + (mins%60 > 0 ? 1 : 0));
    epd.setCursor(650, 401); epd.print(tb);
  }

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

// ─── Ekran nocny ──────────────────────────────────────────────────────────────
void drawStar(int x, int y, int r) {
  epd.drawLine(x - r, y, x + r, y, TFT_BLACK);
  epd.drawLine(x, y - r, x, y + r, TFT_BLACK);
  int d = r / 2;
  if (d > 0) {
    epd.drawLine(x - d, y - d, x + d, y + d, TFT_BLACK);
    epd.drawLine(x - d, y + d, x + d, y - d, TFT_BLACK);
  }
}

void drawCenteredText(const String& text, int y, int size) {
  int w = text.length() * 6 * size;
  epd.setTextSize(size);
  epd.setCursor(max(0, (800 - w) / 2), y);
  epd.print(text);
}

void drawNightScreen(const struct tm& nowTm) {
  epd.fillScreen(TFT_WHITE);
  epd.setTextColor(TFT_BLACK, TFT_WHITE);

  // Ksiezyc: czarne kolo przyciete bialym kolem z przesunieciem = sierp.
  epd.fillCircle(400, 150, 80, TFT_BLACK);
  epd.fillCircle(432, 128, 68, TFT_WHITE);

  // Gwiazdki dookola ksiezyca.
  drawStar(250,  70, 10);
  drawStar(180, 180,  6);
  drawStar(300, 240,  4);
  drawStar(560,  60,  6);
  drawStar(620, 170, 10);
  drawStar(540, 250,  4);
  drawStar(120,  90,  4);
  drawStar(690, 100,  4);
  drawStar(700, 240,  6);

  // "Zzz" przy ksiezycu.
  epd.setTextSize(3); epd.setCursor(490, 130); epd.print("z");
  epd.setTextSize(2); epd.setCursor(520, 108); epd.print("z");
  epd.setTextSize(1); epd.setCursor(540, 92);  epd.print("z");

  drawCenteredText("Dobranoc", 300, 6);
  drawCenteredText("Nastepne odswiezenie o " + String(deviceSettings.nightHourEnd) + ":00", 372, 2);
  char tb[24]; strftime(tb, sizeof(tb), "%H:%M  %d.%m.%Y", &nowTm);
  drawCenteredText(tb, 410, 1);
  epd.update();
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  rtc_bootCount++;
  // Okno serwisowe na Upload tylko po zasilaniu/resecie, nie po kazdym budzeniu ze snu.
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED) delay(3000);

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
  state.forecastTomorrowKwh = rtc_forecastTomorrowKwh;

  if (!connectWiFi()) {
    // Nie nadpisuj dashboardu - e-papier trzyma ostatni obraz.
    if (!rtc_dashboardDrawn) {
      drawMsg("Brak WiFi!", "Spi " + String(deviceSettings.sleepSeconds / 60) + " min...");
    }
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

  // Prognoza i statystyki zmieniają się wolno; pobieraj je najwyżej raz na godzinę.
  const int efficiencyFetchSlot = nowTm.tm_yday * 24 + currentHour;
  if (!isNightHours(currentHour) && rtc_effFetchHour != efficiencyFetchSlot) {
    if (fetchEfficiency()) {
      rtc_independence = state.independence;
      rtc_forecastKwh = state.forecastKwh;
      rtc_solarKwh = state.solarKwh;
      rtc_forecastTomorrowKwh = state.forecastTomorrowKwh;
    }
    rtc_effFetchHour = efficiencyFetchSlot;
  }

  const bool nightMode = isNightHours(nowTm.tm_hour);
  const unsigned long sleepSec = calcSleepSeconds(nowTm.tm_hour, nowTm.tm_min);

  // W nocy nie pobieramy danych ani nie rysujemy dashboardu - tylko "Dobranoc".
  if (!nightMode) {
    bool s = fetchState();
    bool p = loadPrices(currentHour, nowTm.tm_yday);
    if (s && p) {
      drawScreen();
      rtc_dashboardDrawn = true;
    } else if (!rtc_dashboardDrawn) {
      // Brak poprzedniego dashboardu na ekranie - pokaz blad. W innym wypadku
      // zostaje ostatni obraz (e-papier go trzyma).
      drawMsg("Blad pobierania!", !s ? "/api/state" : "/api/prices");
    } else {
      Serial.printf("Fetch failed (state=%d prices=%d) - zostawiam poprzedni obraz\n", s, p);
    }
  }

  if (nightMode) {
    rtc_dashboardDrawn = false;
    drawNightScreen(nowTm);
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

