// Copy this file to config.h and enter values for your own installation.
// config.h is excluded from Git so credentials stay local.

#ifndef CONFIG_H
#define CONFIG_H

// Wi-Fi network
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

// Base URL of the server running the dashboard API; no trailing slash.
#define API_HOST       "http://192.168.1.10:8080"

// Normal refresh interval and quiet hours (local time, 24-hour clock)
#define SLEEP_SECONDS      300
#define NIGHT_HOUR_START    23
#define NIGHT_HOUR_END       6

// Peak PV power in watts, used to scale production bars
#define PV_MAX_W       1000.0f

// Set a new unique value and upload once over USB to replace saved settings.
#define SETTINGS_RESET_TOKEN ""

#endif
