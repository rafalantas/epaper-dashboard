# E-paper Dashboard

Firmware for a XIAO ESP32-C3 and a 7.5-inch, 800 × 480 monochrome e-paper display using the UC8179 controller. The display shows solar production, battery state, household consumption, energy prices and related statistics fetched from an HTTP dashboard API.

The ESP32 wakes periodically, connects to Wi-Fi, fetches the data, refreshes the display and returns to deep sleep. During the configured night hours it sleeps until the quiet period ends.

## Hardware

- Seeed Studio XIAO ESP32-C3
- Seeed 7.5-inch monochrome ePaper display (UC8179, 800 × 480)
- A server reachable by the ESP32 that provides the API routes below

The display driver is selected in [`driver.h`](driver.h).

## Requirements

- Arduino IDE with the ESP32 board support package installed
- Seeed GFX library and its XIAO ePaper driver support
- ArduinoJson library
- The TFT_eSPI-compatible display interface supplied by the selected Seeed GFX setup

The release build uses ESP32 board support package 3.0.7, Seeed_GFX 3.1.0 and ArduinoJson 6.21.5. Use those versions for a matching manual build. Follow Seeed's setup instructions for the display library and board configuration.

## Configuration

Credentials and device-specific settings are kept in a local `config.h`, which is excluded from Git. Start from the sanitized template:

1. Copy `config.example.h` to `config.h` in the sketch folder.
2. Set your Wi-Fi network name and password.
3. Set `API_HOST` to the base URL of your dashboard server, including the scheme and port when needed (for example, `http://192.168.1.10:8080`).
4. Adjust the sleep interval, quiet hours and peak solar power for your installation.

Do not commit `config.h` or put real credentials in `config.example.h`. If credentials have already been published, change them at the corresponding service.

## API

The firmware requests these routes from `API_HOST`:

| Route | Use |
| --- | --- |
| `/api/state` | Current solar, battery, grid and household values |
| `/api/energy?period=day` | Daily energy and efficiency values |
| `/api/efficiency` | Solar production forecast |
| `/api/prices` | Current and upcoming energy prices |

The server must be reachable from the device over the local network. This firmware currently uses plain HTTP.

## Upload

1. Open `epaper.ino` in Arduino IDE. Keep the sketch and its header files together in one sketch directory.
2. Select the XIAO ESP32-C3 board and the port connected to the device.
3. Create and fill in `config.h` as described above.
4. Compile and upload the sketch.

After upload, the device connects to Wi-Fi, synchronizes time using NTP and refreshes the display. The time zone is currently configured in the sketch for Central European time.

## Automatic firmware updates

The device checks the latest stable GitHub Release once when it enters the configured quiet hours, before deep sleep. It does not wake up repeatedly to check during the night. If the release has a higher `MAJOR.MINOR.PATCH` version, the firmware downloads the release asset over HTTPS, installs it into the inactive OTA flash slot and restarts. Otherwise it turns Wi-Fi off and sleeps as usual. The e-paper is not refreshed to show update progress; it keeps its night image.

Tag a release as `vMAJOR.MINOR.PATCH` (for example, `v1.2.3`) and push the tag to GitHub. The release workflow builds the XIAO ESP32-C3 firmware and attaches `epaper-dashboard-xiao-esp32c3.bin`. Only published stable releases are checked; draft and prerelease versions are ignored.

**One-time setup:** the current firmware has no OTA updater. Connect the XIAO to USB and upload this version once, with `partitions.csv` in the sketch folder so the board gets the two OTA firmware slots. Later releases can install automatically. The partition table reserves two 1.875 MiB slots; the Arduino IDE build must use the same board and local partition file as the release build.

## Settings

The local configuration file controls Wi-Fi and API connection details, the normal sleep interval, quiet hours and the peak PV power used to scale display bars. `config.example.h` documents the available settings and safe example values.
