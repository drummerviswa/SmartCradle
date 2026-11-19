# Smart Cradle setup

## Flash the ESP32

1. Open `sketch_oct22a.ino` in Arduino IDE and keep `mobile_app.h` in the same sketch folder.
2. Install **Adafruit GFX Library**, **Adafruit ST7735 and ST7789 Library**, **ESP32Servo**, **DHT sensor library by Adafruit**, and its **Adafruit Unified Sensor** dependency.
3. Select **ESP32 Dev Module** and the correct port, then upload.
4. Open Serial Monitor at **115200 baud**. The ESP32 prints the Wi-Fi access point name, password, and dashboard address.

## Connect the phone

1. Join **SmartCradle-Breeze** (password **cradle123**). If prompted, stay connected without internet.
2. Install and open the Flutter app in `breeze_cradle_app`, or use the fallback dashboard at **http://192.168.4.1**.
3. The app reads live data from the ESP32 over its local Wi-Fi. Keep it open and connected for notifications.

The app can alert for temperature at or above 30 °C, moisture at or above 60%, and close-range movement at 4 cm or less. Alerts run while the app is open and connected; there are no cloud or background alerts.

Presence is reported within 15 cm. Servo movement remains limited to the existing 6 cm zone. Moisture uses a default 4095 (dry) to 0 (wet) scale immediately, then improves that scale as it observes sensor readings.

## Fan switch wiring

GPIO32 is an input only and accepts 3.3 V logic. The firmware reads it with an internal pull-up; a separate, isolated switch contact must connect GPIO32 to ESP32 GND when the fan is switched on. Do not connect the fan's 5 V line to GPIO32. The 5 V fan is powered through its physical switch. To let the ESP32 control the fan automatically, add a transistor or logic-level MOSFET on GPIO26; never drive a fan directly from an ESP32 pin.

## Build the app

Install Flutter 3.38.1 or newer with Android tooling. From `breeze_cradle_app`, run:

```sh
flutter pub get
flutter run
```

For a release APK:

```sh
flutter build apk --release
```

The app uses local cleartext HTTP because the ESP32 serves its API over plain HTTP. Sensor values are project readings, not medical or safety monitoring.
