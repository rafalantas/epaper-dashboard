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
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <esp_timer.h>
#include <Adafruit_MAX1704X.h>
#include "ota_update.h"

// ─── Konfiguracja — edytuj config.h ──────────────────────────────────────────
#include "config.h"

#ifndef SETTINGS_RESET_TOKEN
#define SETTINGS_RESET_TOKEN ""
#endif

const char* NTP_SERVER   = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.google.com";

// Open-Meteo uses Let's Encrypt; validate the chain with ISRG Root X1.
static const char OPEN_METEO_ROOT_CA[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)PEM";
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
  // Bateria samego ekranu (MAX17048 na I2C: D4=SDA, D5=SCL). -1 = brak miernika.
  float devBatPct   = -1;
  float devBatV     = 0;
  float devBatRate  = 0;   // %/h, dodatnie = ladowanie
};

struct PriceSlot {
  int   hour       = 0;
  float price      = 0;
  bool  isCheap    = false;
  bool  isExpensive= false;
};

// Outdoor weather cached in RTC memory so the last valid reading survives deep sleep.
#ifndef WEATHER_LATITUDE
#define WEATHER_LATITUDE 52.743280f
#endif
#ifndef WEATHER_LONGITUDE
#define WEATHER_LONGITUDE 23.581220f
#endif
#ifndef WEATHER_REFRESH_SECONDS
#define WEATHER_REFRESH_SECONDS 3600
#endif
RTC_DATA_ATTR float rtc_weatherTemp = -999.0f;
RTC_DATA_ATTR float rtc_weatherFeels = -999.0f;
RTC_DATA_ATTR float rtc_weatherWind = -999.0f;
RTC_DATA_ATTR int   rtc_weatherCode = -1;
RTC_DATA_ATTR bool  rtc_weatherValid = false;
RTC_DATA_ATTR time_t rtc_weatherLastFetch = 0;

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

// ─── Bateria ekranu (MAX17048) ────────────────────────────────────────────────
#ifndef BATTERY_LOW_PCT
#define BATTERY_LOW_PCT 15      // ponizej: ostrzezenie na ekranie
#endif
#ifndef BATTERY_SAVE_PCT
#define BATTERY_SAVE_PCT 10     // ponizej: rzadsze odswiezanie
#endif
#ifndef BATTERY_SAVE_SLEEP_SECONDS
#define BATTERY_SAVE_SLEEP_SECONDS 1800
#endif

Adafruit_MAX17048 maxLipo;
bool maxPresent = false;

// Odczyt zaraz po wybudzeniu, przed wlaczeniem Wi-Fi (napiecie bez obciazenia radiem).
void readDeviceBattery(bool firstBoot) {
  if (!maxLipo.begin()) {
    return;
  }
  maxPresent = true;
  maxLipo.wake();
  if (firstBoot) maxLipo.quickStart();  // pierwsze oszacowanie po podlaczeniu zasilania
  delay(200);
  float v = maxLipo.cellVoltage();
  float pct = maxLipo.cellPercent();
  if (isnan(v) || isnan(pct)) {
    return;
  }
  state.devBatV = v;
  state.devBatPct = max(0.0f, min(100.0f, pct));
  float rate = maxLipo.chargeRate();
  state.devBatRate = isnan(rate) ? 0.0f : rate;
}

// Uspienie z miernikiem w hibernacji (ok. 3 uA zamiast ok. 23 uA).
RTC_DATA_ATTR uint64_t rtc_totalAwakeMs = 0;
RTC_DATA_ATTR uint64_t rtc_totalWifiMs = 0;
RTC_DATA_ATTR uint32_t rtc_lastAwakeMs = 0;
RTC_DATA_ATTR uint32_t rtc_lastWifiMs = 0;
uint64_t g_awakeStartUs = 0;
uint64_t g_wifiStartUs = 0;

void recordWifiActivity() {
  if (g_wifiStartUs == 0) return;
  const uint64_t nowUs = esp_timer_get_time();
  rtc_lastWifiMs = (uint32_t)((nowUs - g_wifiStartUs) / 1000ULL);
  rtc_totalWifiMs += rtc_lastWifiMs;
  g_wifiStartUs = 0;
}

void goToSleep(uint64_t seconds) {
  if (g_wifiStartUs != 0 && WiFi.status() == WL_CONNECTED) recordWifiActivity();
  if (g_awakeStartUs != 0) {
    rtc_lastAwakeMs = (uint32_t)((esp_timer_get_time() - g_awakeStartUs) / 1000ULL);
    rtc_totalAwakeMs += rtc_lastAwakeMs;
    Serial.printf("DUTY: awake=%lu ms wifi=%lu ms totals awake=%llu ms wifi=%llu ms (no current measurement)\n",
      (unsigned long)rtc_lastAwakeMs, (unsigned long)rtc_lastWifiMs,
      (unsigned long long)rtc_totalAwakeMs, (unsigned long long)rtc_totalWifiMs);
    g_awakeStartUs = 0;
  }
  if (maxPresent) maxLipo.hibernate();
  esp_deep_sleep(seconds * 1000000ULL);
}
RTC_DATA_ATTR int   rtc_bootCount        = 0;
RTC_DATA_ATTR float rtc_independence      = 0;
RTC_DATA_ATTR float rtc_forecastKwh       = 0;
RTC_DATA_ATTR float rtc_solarKwh          = 0;
RTC_DATA_ATTR float rtc_forecastTomorrowKwh = -1;
RTC_DATA_ATTR int   rtc_effFetchHour      = -1;
RTC_DATA_ATTR bool  rtc_nightUpdateChecked = false;
RTC_DATA_ATTR uint8_t rtc_otaFailureCount = 0;
RTC_DATA_ATTR time_t rtc_otaRetryAfter = 0;
RTC_DATA_ATTR time_t rtc_ntpLastStart = 0;
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
  http.setConnectTimeout(5000);
  http.setTimeout(10000);
  http.setReuse(false);
  if (!http.begin(url)) {
    Serial.println("API: nie mozna otworzyc polaczenia");
    return "";
  }

  const int status = http.GET();
  String body;
  if (status == HTTP_CODE_OK) {
    body = http.getString();
  } else {
    Serial.printf("API: HTTP %d (%s) dla endpointu\n",
      status, HTTPClient::errorToString(status).c_str());
  }
  http.end();
  return body;
}

// Open-Meteo is a public API and does not require a key. Keep failures visible
// in Serial so a missing footer value can be diagnosed on the next USB session.
bool fetchWeather() {
  WiFiClientSecure client;
  client.setCACert(OPEN_METEO_ROOT_CA);
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(12000);
  http.setReuse(false);

  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") +
    String(WEATHER_LATITUDE, 4) + "&longitude=" + String(WEATHER_LONGITUDE, 4) +
    "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m&timezone=auto";

  if (!http.begin(client, url)) {
    Serial.println("Open-Meteo: http.begin failed");
    return false;
  }

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("Open-Meteo: HTTP status %d (%s)\n",
      status, HTTPClient::errorToString(status).c_str());
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();
  if (payload.isEmpty()) {
    Serial.println("Open-Meteo: empty response body");
    return false;
  }

  // Open-Meteo returns metadata as well as current values. 768 bytes is too
  // tight for the complete response on some API responses; leave headroom.
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("Open-Meteo: JSON parse failed: %s (payload=%u)\n",
      err.c_str(), (unsigned)payload.length());
    return false;
  }

  JsonObject current = doc["current"];
  if (current.isNull()) {
    Serial.println("Open-Meteo: response has no current object");
    return false;
  }

  const float temp = current["temperature_2m"] | NAN;
  const float feels = current["apparent_temperature"] | NAN;
  const float wind = current["wind_speed_10m"] | NAN;
  const int code = current["weather_code"] | -1;
  if (!isfinite(temp) || !isfinite(feels) || !isfinite(wind) ||
      temp < -100.0f || temp > 70.0f || feels < -100.0f ||
      wind < 0.0f || code < 0 || code > 99) {
    Serial.printf("Open-Meteo: invalid values temp=%.1f feels=%.1f wind=%.1f code=%d\n",
      temp, feels, wind, code);
    return false;
  }

  rtc_weatherTemp = temp;
  rtc_weatherFeels = feels;
  rtc_weatherWind = wind;
  rtc_weatherCode = code;
  rtc_weatherValid = true;
  rtc_weatherLastFetch = time(nullptr);
  return true;
}

// Compact monochrome weather pictograms sized for the existing 800x480 footer.
void drawWeatherIcon(int x, int y, int code) {
  const bool clear = code == 0;
  const bool partly = code == 1 || code == 2;
  const bool fog = code == 45 || code == 48;
  const bool drizzle = code == 51 || code == 53 || code == 55 || code == 56 || code == 57;
  const bool rain = code == 61 || code == 63 || code == 65 || code == 66 || code == 67 || code == 80 || code == 81 || code == 82;
  const bool snow = code == 71 || code == 73 || code == 75 || code == 77 || code == 85 || code == 86;
  const bool storm = code == 95 || code == 96 || code == 99;
  if (clear || partly) {
    epd.drawCircle(x + 17, y + 7, 5, TFT_BLACK);
    epd.drawLine(x + 17, y, x + 17, y - 3, TFT_BLACK);
    epd.drawLine(x + 17, y + 14, x + 17, y + 17, TFT_BLACK);
    epd.drawLine(x + 10, y + 7, x + 7, y + 7, TFT_BLACK);
    epd.drawLine(x + 24, y + 7, x + 27, y + 7, TFT_BLACK);
    if (clear) return;
  }
  if (fog) {
    epd.drawLine(x + 2, y + 7, x + 25, y + 7, TFT_BLACK);
    epd.drawLine(x, y + 12, x + 22, y + 12, TFT_BLACK);
    epd.drawLine(x + 3, y + 17, x + 25, y + 17, TFT_BLACK);
    return;
  }
  epd.drawCircle(x + 8, y + 14, 5, TFT_BLACK);
  epd.drawCircle(x + 15, y + 10, 7, TFT_BLACK);
  epd.drawCircle(x + 22, y + 14, 5, TFT_BLACK);
  epd.drawLine(x + 4, y + 19, x + 26, y + 19, TFT_BLACK);
  if (storm) {
    epd.drawLine(x + 15, y + 19, x + 11, y + 25, TFT_BLACK);
    epd.drawLine(x + 11, y + 25, x + 17, y + 25, TFT_BLACK);
    epd.drawLine(x + 17, y + 25, x + 13, y + 30, TFT_BLACK);
  } else if (snow) {
    epd.drawLine(x + 9, y + 22, x + 9, y + 28, TFT_BLACK);
    epd.drawLine(x + 6, y + 25, x + 12, y + 25, TFT_BLACK);
    epd.drawLine(x + 19, y + 22, x + 19, y + 28, TFT_BLACK);
    epd.drawLine(x + 16, y + 25, x + 22, y + 25, TFT_BLACK);
  } else if (rain || drizzle) {
    epd.drawLine(x + 8, y + 22, x + 5, y + 27, TFT_BLACK);
    epd.drawLine(x + 17, y + 22, x + 14, y + 27, TFT_BLACK);
    if (rain) epd.drawLine(x + 24, y + 22, x + 21, y + 27, TFT_BLACK);
  }
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
      StaticJsonDocument<128> filter;
      filter["totalKwh"] = true;
      filter["efficiency"] = true;
      DynamicJsonDocument doc(256);
      if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) == DeserializationError::Ok) {
        state.solarKwh    = doc["totalKwh"]   | 0.0f;
        state.independence= doc["efficiency"] | 0.0f;  // 83.8%
        ok = true;
      }
    }
  }

  // 2. /api/efficiency — forecastKwh (prognoza)
  {
    String body = httpGet(deviceSettings.apiHost + "/api/efficiency");
    if (!body.isEmpty()) {
      StaticJsonDocument<128> filter;
      filter["forecastKwh"] = true;
      filter["forecastTomorrow"] = true;
      DynamicJsonDocument doc(256);
      if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) == DeserializationError::Ok) {
        state.forecastKwh = doc["forecastKwh"] | 0.0f;
        state.forecastTomorrowKwh = doc["forecastTomorrow"] | -1.0f;
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
}

// Pobiera /api/prices i zapisuje do prices[] + RTC. Bledy tylko w logu -
// nie nadpisujemy dashboardu na e-papierze.
static int daysInMonth(int year, int month) {
  static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (month < 1 || month > 12) return 0;
  if (month == 2) {
    const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    return leap ? 29 : 28;
  }
  return days[month - 1];
}

// Pstryk returns ISO-8601 timestamps. Accept UTC (Z), no suffix,
// fractional seconds, and explicit numeric offsets; normalize all to UTC
// before building price keys so the displayed hour is never shifted.
static bool parsePriceStartUtc(const char* value, int& year, int& month,
                               int& day, int& hour, int& minute, int& second) {
  if (value == nullptr) return false;
  int consumed = 0;
  const int fields = sscanf(value, "%4d-%2d-%2dT%2d:%2d:%2d%n",
                            &year, &month, &day, &hour, &minute, &second, &consumed);
  if (fields != 6 || year < 2024 || year > 2100 ||
      month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month) ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59) return false;

  const char* suffix = value + consumed;
  if (*suffix == '.') {
    suffix++;
    if (*suffix < '0' || *suffix > '9') return false;
    while (*suffix >= '0' && *suffix <= '9') suffix++;
  }

  int offsetMinutes = 0;
  if (*suffix == 'Z' && suffix[1] == '\0') {
    // UTC.
  } else if (*suffix == '+' || *suffix == '-') {
    const int sign = (*suffix == '+') ? 1 : -1;
    int offsetHour = 0, offsetMinute = 0, offsetConsumed = 0;
    if (sscanf(suffix + 1, "%2d:%2d%n", &offsetHour, &offsetMinute, &offsetConsumed) != 2 ||
        suffix[1 + offsetConsumed] != '\0' ||
        offsetHour > 14 || offsetMinute > 59 ||
        (offsetHour == 14 && offsetMinute != 0)) return false;
    offsetMinutes = sign * (offsetHour * 60 + offsetMinute);
  } else if (*suffix != '\0') {
    return false;
  }

  // Convert the local timestamp represented by the offset into UTC.
  int totalMinutes = hour * 60 + minute - offsetMinutes;
  while (totalMinutes < 0) {
    totalMinutes += 1440;
    if (--day < 1) {
      if (--month < 1) { month = 12; --year; }
      day = daysInMonth(year, month);
    }
  }
  while (totalMinutes >= 1440) {
    totalMinutes -= 1440;
    if (++day > daysInMonth(year, month)) {
      day = 1;
      if (++month > 12) { month = 1; ++year; }
    }
  }
  hour = totalMinutes / 60;
  minute = totalMinutes % 60;
  return year >= 2024 && year <= 2100;
}

static long long priceValidationMinute(int year, int month, int day,
                                       int hour, int minute) {
  // Gregorian date -> minutes since Unix epoch. Used only for sequence checks.
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = (unsigned)(year - era * 400);
  const unsigned doy = (153U * (month + (month > 2 ? -3 : 9)) + 2U) / 5U + day - 1U;
  const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
  const long long days = (long long)era * 146097LL + (long long)doe - 719468LL;
  return days * 1440LL + hour * 60LL + minute;
}

bool fetchPricesFromApi() {
  // One request per hourly attempt slot; loadPrices() handles a later retry.
  String body = httpGet(deviceSettings.apiHost + "/api/prices");
  if (body.isEmpty()) { Serial.println("prices: brak odpowiedzi"); return false; }

  StaticJsonDocument<256> filter;
  filter["prices"][0]["price"] = true;
  filter["prices"][0]["isCheap"] = true;
  filter["prices"][0]["isExpensive"] = true;
  filter["prices"][0]["start"] = true;
  DynamicJsonDocument doc(6144);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) != DeserializationError::Ok) {
    Serial.println("prices: JSON error");
    return false;
  }

  JsonArray arr = doc["prices"].as<JsonArray>();
  if (arr.isNull() || arr.size() < 12 || arr.size() > 48) {
    Serial.printf("prices: nieprawidlowa liczba slotow (%u)\n",
                  arr.isNull() ? 0U : (unsigned)arr.size());
    return false;
  }

  // Stage the full response first. Never partially overwrite a good RTC cache.
  float newPrice[48];
  int8_t newHour[48];
  long newKey[48];
  bool newCheap[48], newExpensive[48];
  long long previousMinute = -1;
  bool unpublishedTailStarted = false;
  int n = 0;

  for (JsonObject slot : arr) {
    if (!slot["start"].is<const char*>()) {
      Serial.println("prices: brak czasu poczatku slotu");
      return false;
    }

    int Y=0, Mo=0, D=0, h=0, m=0, sec=0;
    if (!parsePriceStartUtc(slot["start"].as<const char*>(), Y, Mo, D, h, m, sec)) {
      Serial.println("prices: nieprawidlowy format daty/czasu (oczekiwane UTC ISO-8601)");
      return false;
    }

    const long long minuteKey = priceValidationMinute(Y, Mo, D, h, m);
    if (previousMinute >= 0 && minuteKey - previousMinute != 60) {
      Serial.println("prices: sloty nie sa kolejnymi godzinami");
      return false;
    }
    previousMinute = minuteKey;

    // Pstryk publishes tomorrow's hours later. Keep the valid prefix and
    // ignore only trailing slots whose price is explicitly null.
    if (slot["price"].isNull()) {
      unpublishedTailStarted = true;
      continue;
    }
    if (!slot["price"].is<float>() || unpublishedTailStarted) {
      Serial.println("prices: nieprawidlowa cena lub luka przed kolejnym slotem");
      return false;
    }

    const float value = slot["price"].as<float>();
    if (!isfinite(value) || value < -10000.0f || value > 100000.0f) {
      Serial.printf("prices: cena poza zakresem (%.3f)\\n", value);
      return false;
    }

    int localHour = (h + (int)(g_utcOffsetSec / 3600)) % 24;
    if (localHour < 0) localHour += 24;
    newPrice[n] = value;
    newHour[n] = (int8_t)localHour;
    newKey[n] = utcTimeKey(Y, Mo, D, h, m);
    newCheap[n] = slot["isCheap"] | false;
    newExpensive[n] = slot["isExpensive"] | false;
    if (newCheap[n] && newExpensive[n]) {
      Serial.println("prices: slot nie moze byc jednoczesnie tani i drogi");
      return false;
    }
    n++;
  }

  if (n < 12) {
    Serial.printf("prices: za malo opublikowanych cen (%d)\\n", n);
    return false;
  }

  for (int i = 0; i < n; i++) {
    prices[i].price = newPrice[i];
    prices[i].hour = newHour[i];
    prices[i].isCheap = newCheap[i];
    prices[i].isExpensive = newExpensive[i];
    priceKey[i] = newKey[i];
  }
  priceCount = n;
  savePricesToRtc();
  return true;
}

// Ceny na dzis sa dostepne od rana, a ceny na jutro pojawiaja sie
// zwykle miedzy 13:00 a 15:00. Nie odpytujemy API co 5 minut ani przed
// publikacja jutra: probujemy raz na godzine od 13:00 do 23:00.
// Gdy cache jest pusty lub nie zawiera biezacej godziny, wykonujemy probe
// od razu. Nie kasujemy ostatniego poprawnego cache po bledzie API.
bool loadPrices(int currentLocalHour, int yday) {
  loadPricesFromRtc();
  selectCurrentPrice();

  const long nowKey = nowUtcKey();
  const long lastKey = priceCount > 0 ? priceKey[priceCount - 1] : 0;
  const bool cacheHasCurrentPrice = currentPriceIdx >= 0;
  // Today's final slot starts at midnight tomorrow. Require a full next-day
  // horizon so that this boundary is not mistaken for published tomorrow.
  const bool cacheHasTomorrow = priceCount > 0 && (lastKey - nowKey) >= 24 * 60;

  const bool initialFetchNeeded = !cacheHasCurrentPrice;
  const bool tomorrowRefreshDue = !cacheHasTomorrow &&
                                  currentLocalHour >= 13 &&
                                  currentLocalHour <= 23;
  const int attemptSlot = yday * 24 + currentLocalHour;

  if ((initialFetchNeeded || tomorrowRefreshDue) &&
      rtc_priceAttemptSlot != attemptSlot) {
    rtc_priceAttemptSlot = attemptSlot;
    if (fetchPricesFromApi()) {
      selectCurrentPrice();
    } else {
      // Zachowaj cache: dzisiejsze ceny nadal sa przydatne, nawet gdy
      // publikacja jutrzejszych danych jeszcze sie opoznia.
      Serial.println("prices: pozostawiam ostatni poprawny cache");
    }
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

  // ── Stopka: jeden panel, cztery sekcje i wspolna siatka ────────────────
  // Panel ma stale marginesy; wszystkie elementy mieszcza sie w obszarze
  // y=418..476, bez nachodzenia na sekcje "NAJTANIEJ DZIS".
  const int footerX = 10, footerY = 418, footerW = 780, footerH = 58;
  epd.fillRoundRect(footerX, footerY, footerW, footerH, 8, TFT_WHITE);
  epd.drawRoundRect(footerX, footerY, footerW, footerH, 8, TFT_BLACK);

  // Separatory dziela panel na: aktualizacja, pogoda, status i bateria.
  epd.drawLine(210, 426, 210, 468, TFT_BLACK);
  epd.drawLine(435, 426, 435, 468, TFT_BLACK);
  epd.drawLine(610, 426, 610, 468, TFT_BLACK);

  time_t footerNow = time(nullptr);
  struct tm footerTm = {};
  localtime_r(&footerNow, &footerTm);
  char footerTime[8], footerDate[12];
  strftime(footerTime, sizeof(footerTime), "%H:%M", &footerTm);
  strftime(footerDate, sizeof(footerDate), "%d.%m.%Y", &footerTm);

  // 1. Aktualizacja: mocny akcent na godzinie, wersje w jednej linii.
  epd.setTextSize(1);
  epd.setCursor(22, 429);
  epd.print("AKTUALIZACJA");
  epd.setTextSize(2);
  epd.setCursor(22, 442);
  epd.print(footerTime);
  epd.setTextSize(1);
  epd.setCursor(88, 450);
  epd.print(footerDate);
  epd.setCursor(22, 466);
  epd.print("FW ");
  epd.print(FIRMWARE_VERSION);
  epd.print(" | GH ");
  epd.print(latestGithubVersion.isEmpty() ? "--" : latestGithubVersion);

  // 2. Pogoda: ikona i temperatura na pierwszym planie, szczegoly pod spodem.
  epd.setTextSize(1);
  epd.setCursor(222, 429);
  epd.print("POGODA");
  if (rtc_weatherValid) {
    drawWeatherIcon(222, 439, rtc_weatherCode);
    char footerTemp[12], footerFeels[18], footerWind[18];
    snprintf(footerTemp, sizeof(footerTemp), "%.1fC", rtc_weatherTemp);
    snprintf(footerFeels, sizeof(footerFeels), "Odcz. %.0fC", rtc_weatherFeels);
    snprintf(footerWind, sizeof(footerWind), "Wiatr %.0f km/h", rtc_weatherWind);
    epd.setTextSize(2);
    epd.setCursor(260, 443);
    epd.print(footerTemp);
    epd.setTextSize(1);
    epd.setCursor(330, 447);
    epd.print(footerFeels);
    epd.setCursor(330, 464);
    epd.print(footerWind);
  } else {
    epd.setCursor(222, 453);
    epd.print("Brak danych");
    epd.setCursor(222, 466);
    epd.print("oczekiwanie na odczyt");
  }

  // 3. Status urzadzenia i polaczenia EcoFlow.
  epd.setTextSize(1);
  epd.setCursor(447, 429);
  epd.print("STATUS");
  epd.setCursor(447, 447);
  epd.print("Wybudzenie #");
  epd.print(rtc_bootCount);
  epd.setCursor(447, 464);
  epd.print("EcoFlow: ");
  epd.print(state.connected ? "OK" : "brak");

  // 4. Bateria ekranu: ikona, procent i napiecie; alarm tylko przy niskim SOC.
  epd.setTextSize(1);
  epd.setCursor(622, 429);
  epd.print("BATERIA EKRANU");
  if (state.devBatPct >= 0) {
    const int batteryX = 622, batteryY = 443;
    epd.drawRect(batteryX, batteryY, 25, 13, TFT_BLACK);
    epd.fillRect(batteryX + 25, batteryY + 4, 3, 5, TFT_BLACK);
    const int batteryFill = (int)(21.0f * state.devBatPct / 100.0f);
    if (batteryFill > 0) {
      epd.fillRect(batteryX + 2, batteryY + 2, batteryFill, 9, TFT_BLACK);
    }
    char batteryLabel[24];
    snprintf(batteryLabel, sizeof(batteryLabel), "%.0f%%  %.2fV",
      state.devBatPct, state.devBatV);
    epd.setCursor(656, 452);
    epd.print(batteryLabel);
    if (state.devBatPct <= BATTERY_LOW_PCT) {
      drawBadge(622, 461, 156, 12, "NISKA BATERIA", true);
    } else if (state.devBatRate > 1.0f) {
      epd.setCursor(656, 466);
      epd.print("Ladowanie");
    }
  } else {
    epd.setCursor(622, 452);
    epd.print("Brak miernika");
  }

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
  g_awakeStartUs = esp_timer_get_time();
  rtc_lastWifiMs = 0;
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

  // Bateria ekranu - przed Wi-Fi, zeby odczyt nie byl zanizony przez obciazenie radiem.
  readDeviceBattery(rtc_bootCount == 1);
  const bool batterySaving = state.devBatPct >= 0 && state.devBatPct <= BATTERY_SAVE_PCT;
  const uint64_t normalSleep = batterySaving
    ? max((uint32_t)BATTERY_SAVE_SLEEP_SECONDS, deviceSettings.sleepSeconds)
    : deviceSettings.sleepSeconds;

  if (!connectWiFi()) {
    // Nie nadpisuj dashboardu - e-papier trzyma ostatni obraz.
    if (!rtc_dashboardDrawn) {
      drawMsg("Brak WiFi!", "Spi " + String((uint32_t)(normalSleep / 60)) + " min...");
    }
    WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
    goToSleep(normalSleep);
    return;
  }

  g_wifiStartUs = esp_timer_get_time();

  // Keep the RTC-backed clock between deep-sleep wakes. Start SNTP only
  // on first boot or once per day; never wait on a valid clock.
  const time_t clockNow = time(nullptr);
  const bool clockAlreadyValid = clockNow > 1704067200; // 2024-01-01
  const bool ntpRefreshDue = !clockAlreadyValid || rtc_ntpLastStart == 0 ||
                            clockNow - rtc_ntpLastStart >= 24 * 60 * 60;
  if (ntpRefreshDue) {
    configTzTime(TZ_INFO, NTP_SERVER, NTP_SERVER_2);
    if (!clockAlreadyValid) {
      time_t t = 0; struct tm tmp = {};
      for (int i = 0; i < 12; i++) {
        delay(250);
        t = time(nullptr);
        localtime_r(&t, &tmp);
        if (tmp.tm_year > 100) break;
      }
      if (tmp.tm_year <= 100) {
        Serial.println("NTP: zegar niewazny, zachowuje ekran i ide spac");
        recordWifiActivity();
        WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
        goToSleep(normalSleep);
        return;
      }
    }
    rtc_ntpLastStart = time(nullptr);
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
  unsigned long sleepSec = calcSleepSeconds(nowTm.tm_hour, nowTm.tm_min);
  if (batterySaving && !nightMode) sleepSec = max((unsigned long)normalSleep, sleepSec);

  // Pogoda jest odswiezana najwyzej co 60 minut; ostatni pomiar pozostaje w RTC cache.
  if (!nightMode && (!rtc_weatherValid || now - rtc_weatherLastFetch >= WEATHER_REFRESH_SECONDS)) {
    fetchWeather();
  }

  // W nocy nie pobieramy danych ani nie rysujemy dashboardu - tylko "Dobranoc".
  if (!nightMode) {
    bool s = fetchState();
    bool p = loadPrices(currentHour, nowTm.tm_yday);

    // Ceny sa dodatkiem do dashboardu, a nie warunkiem jego wyswietlenia.
    // Brak /api/prices nie moze blokowac aktualnych danych PV, baterii i przeplywow.
    if (s) {
      drawScreen();
      rtc_dashboardDrawn = true;
      if (!p) Serial.println("prices: niedostepne - rysuje dashboard bez aktualnych danych cenowych");
    } else if (!rtc_dashboardDrawn) {
      // Bez aktualnego /api/state nie mamy danych do narysowania dashboardu.
      drawMsg("Blad pobierania!", "/api/state");
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
  } else if (!rtc_nightUpdateChecked &&
             (rtc_otaRetryAfter == 0 || now >= rtc_otaRetryAfter)) {
    rtc_nightUpdateChecked = true;
    String checkedVersion;
    otaUpdated = checkAndInstallFirmwareUpdate(&checkedVersion);
    if (otaUpdated || !checkedVersion.isEmpty()) {
      rtc_otaFailureCount = 0;
      rtc_otaRetryAfter = 0;
    } else {
      if (rtc_otaFailureCount < 4) rtc_otaFailureCount++;
      const uint32_t backoffHours = min(48UL << (rtc_otaFailureCount - 1), 384UL);
      rtc_otaRetryAfter = now + (time_t)backoffHours * 3600;
      Serial.printf("OTA: retry za %lu h (failure=%u)\n",
        (unsigned long)backoffHours, (unsigned)rtc_otaFailureCount);
    }
    if (!checkedVersion.isEmpty()) {
      Preferences prefs;
      if (prefs.begin("epaper", false)) {
        prefs.putString("ghVersion", checkedVersion);
        prefs.end();
      }
    }
  }

  recordWifiActivity();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);

  if (otaUpdated) {
    Serial.println("OTA: wersja zapisana, restartuje urzadzenie");
    ESP.restart();
  }

  goToSleep(sleepSec);
}

void loop() {}

