# SmartCradle Breeze

ESP32 based cradle monitor with a Flutter Android app and a local device dashboard.

## Project files

- `sketch_oct22a.ino` — ESP32 firmware.
- `mobile_app.h` — dashboard and JSON API served by the ESP32.
- `breeze_cradle_app/` — Flutter app source.
- `MOBILE_APP_GUIDE.md` — wiring, flashing, connection, and build instructions.

Start with [MOBILE_APP_GUIDE.md](MOBILE_APP_GUIDE.md). The app connects to the ESP32's `SmartCradle-Breeze` access point and uses its local HTTP API. It requires the phone to stay connected to the device Wi-Fi for live readings and alerts.

Sensor readings and alerts are informational and are not a medical or safety monitoring system.
