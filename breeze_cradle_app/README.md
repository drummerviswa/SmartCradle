# Breeze Cradle Flutter app

A native Flutter dashboard for the Smart Cradle ESP32 in this repository. It reads the firmware's `GET /api/status` endpoint over the device's local Wi-Fi and refreshes every two seconds.

## Connect

1. Upload `sketch_oct22a.ino` and `mobile_app.h` to the ESP32.
2. Connect the phone to **SmartCradle-Breeze** (password **cradle123**). If prompted, stay connected even though this network has no internet.
3. Open the app. The default device address is `192.168.4.1`; tap refresh if needed.

The app displays temperature, humidity, ultrasonic presence/distance, cradle motion and servo angle, mattress moisture, fan state, connection state, and device uptime. It sends Android notifications for temperature at or above 30 °C, moisture at or above 60%, and close-range movement at 4 cm or less. Keep the app open and connected to the ESP32 for live alerts; this local Wi-Fi project does not send cloud or background alerts.

Presence is reported within 15 cm. Servo movement remains limited to the existing 6 cm zone. The moisture sensor uses a default 4095 (dry) to 0 (wet) scale immediately, then improves that scale as it observes sensor readings.

## Fan switch wiring

GPIO32 is an input only and accepts 3.3 V logic. The firmware reads it with an internal pull-up; a separate, isolated switch contact must connect GPIO32 to ESP32 GND when the fan is switched on. Do not connect the fan's 5 V line to GPIO32. The 5 V fan is powered through its physical switch. To let the ESP32 control the fan automatically, add a transistor or logic-level MOSFET on GPIO26; never drive a fan directly from an ESP32 pin.

## Build

Install Flutter 3.38.1 or newer with Android tooling, then run:

```sh
flutter pub get
flutter run
```

For an Android APK:

```sh
flutter build apk --release
```

The Android app permits local cleartext HTTP because the ESP32 serves its API over plain HTTP. The app works only while the ESP32 is powered and the phone is connected to its Wi-Fi. Sensor values are project readings, not medical or safety monitoring.
