
/*
  ==============================================================
   Project: Smart Baby Cradle using IoT
   Team: Breeze
   Display: ST7789 2.8" SPI TFT (240x320)
   ESP32: ESP32 Dev Module

   Sensors:
   1. HC-SR04 Ultrasonic Sensor
   2. DHT11 Temperature & Humidity Sensor
   3. Analog Soil Moisture Sensor
   4. Servo Motor

   Members:
     - Viswanathan P    (2022242001)
     - Rohini S         (2022242002)
     - Divya Dharshini B(2022242003)
     - Ganga P          (2022242018)
     - Jamuna Devi A    (2022242024)
  ==============================================================
*/

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <WiFi.h>
#include <WebServer.h>
#include "mobile_app.h"

// ------------------- COLOR DEFINITIONS -------------------

#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_NAVY      0x000F
#define COLOR_CYAN      0x07FF
#define COLOR_YELLOW    0xFFE0
#define COLOR_GREEN     0x07E0
#define COLOR_RED       0xF800
#define COLOR_ORANGE    0xFD20
#define COLOR_DARKGREY  0x4A69
#define COLOR_LIGHTGREY 0xC618

// ------------------- PIN CONFIGURATION -------------------

// TFT Display
#define TFT_CS    21
#define TFT_DC    22
#define TFT_RST   4

// SPI: SCK = GPIO 18, MOSI = GPIO 23

// Ultrasonic Sensor
#define TRIG_PIN  25
#define ECHO_PIN  14

// Servo Motor
#define SERVO_PIN 13

// DHT Temperature Sensor
#define DHT_PIN   27
#define DHT_TYPE  DHT11
// Change DHT11 to DHT22 if using DHT22

// Soil Moisture Sensor
#define MOISTURE_PIN 34

// Other reserved pins
#define SOUND_PIN   35
#define FAN_PIN     26   // transistor/MOSFET control only; never connect fan directly
#define SWITCH_PIN  32   // 3.3 V input; switch closes to ESP32 GND when fan is on

// ------------------- FAN CONFIGURATION -------------------
// CPU fan PWM — 4-pin fans accept 25 kHz PWM on pin 4.
// Speed is 0–255 (0 = off, 255 = full speed).
// Fan turns ON automatically when temperature exceeds FAN_ON_TEMP,
// scales up with heat, and turns OFF below FAN_OFF_TEMP (hysteresis).

#define FAN_PWM_CHANNEL   0        // ESP32 LEDC channel
#define FAN_PWM_FREQ      25000    // 25 kHz — standard CPU fan frequency
#define FAN_PWM_BITS      8        // 8-bit resolution (0-255)
#define FAN_ON_TEMP       28.0f    // °C — fan starts at this temperature
#define FAN_OFF_TEMP      26.0f    // °C — fan stops below this (hysteresis)
#define FAN_MIN_SPEED     80       // minimum PWM to actually spin the fan
#define FAN_MAX_SPEED     255      // maximum PWM (full speed)
#define FAN_HIGH_TEMP     35.0f    // °C — full speed above this

// ------------------- CONFIGURATION -------------------

// Baby/object detection threshold
#define BABY_DETECT_DISTANCE 15.0   // Presence is recognized within 15 cm

// Servo movement limits
#define SERVO_MIN_ANGLE 65
#define SERVO_MAX_ANGLE 115
#define SERVO_CENTER    90

// Moisture calibration values
// Auto-calibration is ON by default: the sketch learns your sensor's
// dry and wet extremes at runtime. You can still override with fixed
// values by setting MOISTURE_AUTOCAL 0 and entering your own numbers.

#define MOISTURE_AUTOCAL     1     // 1 = learn at runtime, 0 = use fixed values

// Fixed-value fallbacks (used when MOISTURE_AUTOCAL 0, or as the initial
// seed range before the auto-calibrator observes real extremes).
// These span the full 12-bit ADC range so the sensor is never stuck at 0%.
#define MOISTURE_DRY_RAW 4095
#define MOISTURE_WET_RAW    0

// Wetness percentage threshold
#define MOISTURE_WET_THRESHOLD 60

#define SERVO_ACTIVE_DISTANCE 6.0   // Keep cradle movement limited to the closer zone
#define MOVING_DISTANCE       4.0   // Existing close-range movement alert threshold

// ------------------- OBJECTS -------------------

// Use the display controller and init sequence from the supplied friend sketch.
// Keep this project's TFT_CS/TFT_DC/TFT_RST values above; do not copy their pin map.
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

Servo cradleServo;

DHT dht(DHT_PIN, DHT_TYPE);
WebServer webServer(80);

// The phone connects to this local ESP32 network and opens http://192.168.4.1
const char *APP_AP_NAME = "SmartCradle-Breeze";
const char *APP_AP_PASSWORD = "cradle123";

// ------------------- GLOBAL VARIABLES -------------------

// Ultrasonic
float distanceCm = -1.0;
bool isRocking = false;

// Servo
int currentServoAngle = SERVO_CENTER;
int sweepDirection = 1;

// Temperature and Humidity
float currentTemp = NAN;
float currentHumidity = NAN;

// Moisture
int moistureRaw = 0;
int moisturePercent = 0;
bool isBedWet = false;

// Auto-calibration: starts INVERTED so the very first real reading
// immediately sets both bounds, then expands from there.
// moistureObsMax = driest seen (highest ADC) → maps to 0 %
// moistureObsMin = wettest seen (lowest  ADC) → maps to 100 %
int moistureObsMin = 4095;   // will drop on first reading
int moistureObsMax = 0;      // will rise on first reading
bool moistureCal   = false;  // true once range > 150 counts

// Sensor status
bool dhtValid = false;
bool moistureValid = false;

// Fan
int fanSpeed = 0;        // current PWM duty (0-255)
bool fanOn   = false;

// Switch / fan state
bool switchFanOn     = false;   // physical switch state (true = switch ON)
bool fanShouldBeOn   = false;   // what temperature logic recommends
unsigned long lastSwitchRead = 0;

// Timing
unsigned long lastSensorRead = 0;
unsigned long lastDHTRead = 0;
unsigned long lastMoistureRead = 0;
unsigned long lastServoMove = 0;
unsigned long lastDisplayUpdate = 0;

// ------------------- ULTRASONIC READING -------------------
// Uses pulseInLong() (ESP32/FreeRTOS-safe) + multi-sample averaging.
// pulseIn() gets corrupted by WiFi background tasks on ESP32;
// pulseInLong() tolerates those interrupts correctly.

#define ULTRASONIC_SAMPLES   3       // readings per call
#define ULTRASONIC_TIMEOUT   38000UL // µs — covers ~650 cm max range
#define ULTRASONIC_MIN_CM    1.0f    // ignore anything closer (noise)
#define ULTRASONIC_MAX_CM    400.0f  // ignore anything farther (noise)

float lastValidDistance = -1.0f;    // sticky: returns last good reading on fail

float readUltrasonicDistance() {

    float sum = 0;
    int   valid = 0;

    for (int i = 0; i < ULTRASONIC_SAMPLES; i++) {

        // Clear any previous echo
        digitalWrite(TRIG_PIN, LOW);
        delayMicroseconds(4);

        // 10 µs trigger pulse
        digitalWrite(TRIG_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(TRIG_PIN, LOW);

        // pulseInLong is interrupt-tolerant — critical on ESP32 with WiFi
        long duration = pulseInLong(ECHO_PIN, HIGH, ULTRASONIC_TIMEOUT);

        if (duration > 0) {
            float cm = (duration * 0.0343f) / 2.0f;
            // Accept only physically plausible values
            if (cm >= ULTRASONIC_MIN_CM && cm <= ULTRASONIC_MAX_CM) {
                sum += cm;
                valid++;
            }
        }

        // Must wait ≥ 20 ms between pings to let the echo decay
        delay(25);
    }

    if (valid == 0) {
        // No good reading — return last known distance so display
        // doesn't flicker to -1 during transient WiFi bursts
        return lastValidDistance;
    }

    lastValidDistance = sum / valid;
    return lastValidDistance;
}

// ------------------- MOISTURE CALIBRATION -------------------

void updateMoisturePercentage() {

    // Average 8 ADC samples — ESP32 ADC is noisy
    long rawSum = 0;
    for (int i = 0; i < 8; i++) {
        rawSum += analogRead(MOISTURE_PIN);
        delayMicroseconds(200);
    }
    moistureRaw = (int)(rawSum / 8);

    int dryRaw = MOISTURE_DRY_RAW;
    int wetRaw = MOISTURE_WET_RAW;

    if (MOISTURE_AUTOCAL) {
        // Learn real extremes while the default range remains usable.
        if (moistureRaw < moistureObsMin) moistureObsMin = moistureRaw;
        if (moistureRaw > moistureObsMax) moistureObsMax = moistureRaw;

        int span = moistureObsMax - moistureObsMin;
        moistureCal = span >= 150;
        if (moistureCal) {
            dryRaw = moistureObsMax;
            wetRaw = moistureObsMin;
        }
    } else {
        moistureCal = true;
    }

    // Default scale: 4095 = dry, 0 = wet. Use learned extremes when ready.
    int percentage = map(moistureRaw, dryRaw, wetRaw, 0, 100);
    moisturePercent = constrain(percentage, 0, 100);
    moistureValid   = true;
    isBedWet        = (moisturePercent >= MOISTURE_WET_THRESHOLD);
}

// ------------------- MOBILE DASHBOARD -------------------

void handleMobileApp() {
    webServer.send_P(200, "text/html; charset=utf-8", MOBILE_APP);
}

void handleMobileStatus() {
    String json = "{\"dhtValid\":";
    json += dhtValid ? "true" : "false";
    json += ",\"temperature\":";
    json += dhtValid ? String(currentTemp, 1) : "null";
    json += ",\"humidity\":";
    json += dhtValid ? String(currentHumidity, 1) : "null";
    json += ",\"distance\":" + String(distanceCm, 1);
    json += ",\"rocking\":";
    json += isRocking ? "true" : "false";
    json += ",\"babyMoving\":";
    json += (distanceCm > 0 && distanceCm <= MOVING_DISTANCE) ? "true" : "false";
    json += ",\"angle\":" + String(currentServoAngle);
    json += ",\"moisture\":" + String(moisturePercent);
    json += ",\"wet\":";
    json += isBedWet ? "true" : "false";
    json += ",\"uptime\":" + String(millis() / 1000);
    json += ",\"fanOn\":";
    json += switchFanOn ? "true" : "false";
    json += ",\"fanRecommended\":";
    json += fanShouldBeOn ? "true" : "false";
    json += "}";
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.sendHeader("Cache-Control", "no-store");
    webServer.send(200, "application/json", json);
}

void handleMobilePreflight() {
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    webServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    webServer.send(204);
}

void startMobileDashboard() {
    if (!WiFi.mode(WIFI_AP)) {
        Serial.println("ERROR: ESP32 could not enable Wi-Fi access-point mode.");
        return;
    }

    if (!WiFi.softAP(APP_AP_NAME, APP_AP_PASSWORD)) {
        Serial.println("ERROR: ESP32 could not start the Wi-Fi access point.");
        Serial.println("Check the ESP32 board selection and power, then reset and try again.");
        return;
    }

    webServer.on("/", HTTP_GET, handleMobileApp);
    webServer.on("/api/status", HTTP_GET, handleMobileStatus);
    webServer.on("/api/status", HTTP_OPTIONS, handleMobilePreflight);
    webServer.onNotFound([]() {
        webServer.sendHeader("Location", "/", true);
        webServer.send(302, "text/plain", "");
    });
    webServer.begin();
    Serial.printf("Wi-Fi access point started: %s | Password: %s\n", APP_AP_NAME, APP_AP_PASSWORD);
    Serial.printf("Open http://%s on your phone\n", WiFi.softAPIP().toString().c_str());
}

// ------------------- DRAW DASHBOARD LAYOUT -------------------

void drawDashboardLayout() {

    tft.fillScreen(COLOR_BLACK);

    // Title
    tft.fillRect(0, 0, 320, 36, COLOR_NAVY);

    tft.drawFastHLine(
        0, 36, 320, COLOR_CYAN
    );

    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(2);
    tft.setCursor(18, 10);

    tft.print("SMART CRADLE - BREEZE");

    // Card 1: Baby Presence
    tft.drawRoundRect(
        8, 44, 148, 80, 8, COLOR_DARKGREY
    );

    tft.setTextSize(1);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(16, 52);
    tft.print("BABY PRESENCE");

    // Card 2: Cradle Motion
    tft.drawRoundRect(
        164, 44, 148, 80, 8, COLOR_DARKGREY
    );

    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(172, 52);
    tft.print("CRADLE MOTION");

    // Card 3: Room Climate
    tft.drawRoundRect(
        8, 132, 148, 80, 8, COLOR_DARKGREY
    );

    tft.setTextColor(COLOR_CYAN);
    tft.setCursor(16, 140);
    tft.print("ROOM CLIMATE (DHT)");

    // Card 4: Bed Moisture
    tft.drawRoundRect(
        164, 132, 148, 80, 8, COLOR_YELLOW
    );

    tft.setTextColor(COLOR_CYAN);
    tft.setCursor(172, 140);
    tft.print("BED MOISTURE");

    // Bottom Status Bar
    tft.fillRect(
        0, 220, 320, 20, COLOR_NAVY
    );

    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 226);

    // Show live fan state + temperature recommendation
    tft.print("FAN: ");
    if (switchFanOn) {
        tft.setTextColor(COLOR_GREEN);
        tft.print("ON ");
    } else {
        tft.setTextColor(COLOR_LIGHTGREY);
        tft.print("OFF");
    }
    tft.setTextColor(COLOR_WHITE);
    tft.print(" | Recommended: ");
    if (fanShouldBeOn) {
        tft.setTextColor(COLOR_ORANGE);
        tft.print("ON  (HOT)");
    } else {
        tft.setTextColor(COLOR_CYAN);
        tft.print("OFF (COOL)");
    }
}

// ------------------- UPDATE DASHBOARD -------------------

void updateDashboardValues() {

    // 1. BABY PRESENCE CARD

tft.fillRect(16, 68, 132, 48, COLOR_BLACK);

tft.setTextSize(2);
tft.setCursor(16, 70);

if (distanceCm > 0 && distanceCm <= MOVING_DISTANCE) {

    tft.setTextColor(COLOR_ORANGE);
    tft.print("MOVING");

}
else if (distanceCm > MOVING_DISTANCE && distanceCm <= BABY_DETECT_DISTANCE) {

    tft.setTextColor(COLOR_GREEN);
    tft.print("PRESENT");

}
else {

    tft.setTextColor(COLOR_RED);
    tft.print("IDLE");
}

tft.setTextSize(1);
tft.setTextColor(COLOR_WHITE);
tft.setCursor(16, 100);

if (distanceCm > 0) {
    tft.printf("Dist: %.1f cm", distanceCm);
}
else {
    tft.print("No Echo");
}
    // 2. CRADLE MOTION CARD

    tft.fillRect(172, 68, 132, 48, COLOR_BLACK);

    tft.setTextSize(2);
    tft.setCursor(172, 70);

    if (isRocking) {

        tft.setTextColor(COLOR_GREEN);
        tft.print("SWING");

    }
    else {

        tft.setTextColor(COLOR_WHITE);
        tft.print("IDLE");
    }

    tft.setTextSize(1);
    tft.setCursor(172, 100);
    tft.setTextColor(COLOR_LIGHTGREY);

    tft.printf("Angle: %d deg", currentServoAngle);

    // 3. ROOM CLIMATE CARD

    tft.fillRect(16, 156, 132, 48, COLOR_BLACK);

    tft.setTextSize(2);
    tft.setCursor(16, 158);

    if (dhtValid) {

        tft.setTextColor(COLOR_CYAN);
        tft.printf("%.1f C", currentTemp);

        tft.setTextSize(1);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(16, 185);

        tft.printf("Humidity: %.0f%%", currentHumidity);

    }
    else {

        tft.setTextColor(COLOR_RED);
        tft.print("DHT ERROR");

        tft.setTextSize(1);
        tft.setCursor(16, 185);
        tft.setTextColor(COLOR_LIGHTGREY);
        tft.print("Check sensor");
    }

    // 4. BED MOISTURE CARD

    tft.fillRect(172, 156, 132, 48, COLOR_BLACK);

    tft.setTextSize(2);
    tft.setCursor(172, 158);

    if (moistureValid) {

        if (isBedWet) {
            tft.setTextColor(COLOR_RED);
            tft.print("WET");
        }
        else {
            tft.setTextColor(COLOR_GREEN);
            tft.print("DRY");
        }

        tft.setTextSize(1);
        tft.setTextColor(COLOR_WHITE);
        tft.setCursor(172, 185);
        tft.printf("Moisture: %d%%", moisturePercent);
    }
    else {
        tft.setTextColor(COLOR_RED);
        tft.print("NO DATA");
    }
}

// ------------------- FAN / SWITCH CONTROL -------------------
// Physical rocker switch (GPIO 32, INPUT_PULLUP) controls motor power.
// ESP32 reads the switch to know fan state and shows it on display + app.
// Temperature logic runs in parallel to show RECOMMENDED state.
// When BC547 transistor is added to GPIO 26 later, full auto-control works.

void updateFanSwitch() {

    // Temperature recommendation (what AUTO mode would do)
    if (dhtValid) {
        if (!fanShouldBeOn && currentTemp >= FAN_ON_TEMP) {
            fanShouldBeOn = true;
        }
        else if (fanShouldBeOn && currentTemp < FAN_OFF_TEMP) {
            fanShouldBeOn = false;
        }
    }

    // fanOn reflects ACTUAL state (physical switch)
    fanOn = switchFanOn;

    Serial.print("Fan Switch: ");
    Serial.print(switchFanOn ? "ON  " : "OFF ");
    if (dhtValid) {
        Serial.print("| Temp recommends: ");
        Serial.print(fanShouldBeOn ? "ON  " : "OFF ");
        if (switchFanOn != fanShouldBeOn) {
            Serial.print("<-- ");
            Serial.print(fanShouldBeOn
                ? "⚠ Hot! Turn fan ON"
                : "Cool enough, fan can go OFF");
        }
    }
    Serial.println();
}

// ------------------- SETUP -------------------

void setup() {

    Serial.begin(115200);

    Serial.println();
    Serial.println("================================");
    Serial.println(" SMART BABY CRADLE - TEAM BREEZE");
    Serial.println(" Initializing Sensors...");
    Serial.println("================================");

    startMobileDashboard();

    // Ultrasonic
    pinMode(TRIG_PIN, OUTPUT);
    digitalWrite(TRIG_PIN, LOW); // ensure TRIG starts LOW
    // PULLDOWN keeps ECHO pin LOW while sensor is idle,
    // preventing false echoes from a floating pin.
    pinMode(ECHO_PIN, INPUT_PULLDOWN);

    // Servo
    cradleServo.setPeriodHertz(50);

    cradleServo.attach(
        SERVO_PIN,
        500,
        2400
    );

    cradleServo.write(SERVO_CENTER);

    // DHT
    dht.begin();

    // Moisture
    pinMode(MOISTURE_PIN, INPUT);
    analogReadResolution(12);

    // Rocker switch (INPUT_PULLUP — no resistor needed)
    // Switch wired: GPIO 32 → Switch → GND
    // LOW  = switch ON  (fan running)
    // HIGH = switch OFF (fan stopped)
    pinMode(SWITCH_PIN, INPUT_PULLUP);

    // TFT
    // ST7789 initialization copied from the supplied display sketch.
    tft.init(240, 320);
    tft.setRotation(1);

    Serial.printf(
        "TFT Width: %d | Height: %d\n",
        tft.width(),
        tft.height()
    );

    drawDashboardLayout();

    Serial.println("All initialization completed.");
    Serial.println("Waiting for sensor readings...");
}

// ------------------- MAIN LOOP -------------------

void loop() {

    webServer.handleClient();

    unsigned long currentMillis = millis();

    // Read the isolated, ground-switched status contact frequently so the
    // dashboard reflects the manual fan switch without waiting for a DHT read.
    if (currentMillis - lastSwitchRead >= 100) {
        lastSwitchRead = currentMillis;
        switchFanOn = (digitalRead(SWITCH_PIN) == LOW);
        fanOn = switchFanOn;
    }

    // 1. ULTRASONIC SENSOR
    // Read every 250 milliseconds


if (currentMillis - lastSensorRead >= 250) {

    lastSensorRead = currentMillis;

    distanceCm = readUltrasonicDistance();

    isRocking = distanceCm > 0 && distanceCm <= SERVO_ACTIVE_DISTANCE;

    Serial.print("Distance: ");
    Serial.print(distanceCm);
    Serial.print(" cm | Status: ");

    if (distanceCm > 0 && distanceCm <= MOVING_DISTANCE) {
        Serial.println("MOVING - SWINGING");
    }
    else if (distanceCm > MOVING_DISTANCE && distanceCm <= BABY_DETECT_DISTANCE) {
        Serial.println("PRESENT");
    }
    else {
        Serial.println("EMPTY");
    }
}

    // 2. DHT TEMPERATURE AND HUMIDITY
    // Read every 2 seconds

    if (currentMillis - lastDHTRead >= 2000) {

        lastDHTRead = currentMillis;

        float temp = dht.readTemperature();
        float humidity = dht.readHumidity();

        if (!isnan(temp) && !isnan(humidity)) {

            currentTemp = temp;
            currentHumidity = humidity;

            dhtValid = true;

            Serial.print("Temperature: ");
            Serial.print(currentTemp);
            Serial.print(" C | Humidity: ");
            Serial.print(currentHumidity);
            Serial.println(" %");

            // Update fan every time we get a fresh temperature reading
            updateFanSwitch();

        }
        else {

            dhtValid = false;

            Serial.println("DHT reading failed!");
        }
    }

    // 3. SOIL MOISTURE SENSOR
    // Read every 1 second

    if (currentMillis - lastMoistureRead >= 1000) {

        lastMoistureRead = currentMillis;

        updateMoisturePercentage();

        Serial.print("Moisture Raw: ");
        Serial.print(moistureRaw);
        Serial.print(" | AutoCal [dry=");
        Serial.print(moistureObsMax);
        Serial.print(", wet=");
        Serial.print(moistureObsMin);
        Serial.print("] | Moisture: ");
        Serial.print(moisturePercent);
        Serial.print("% | Status: ");
        Serial.println(isBedWet ? "WET" : "DRY");
    }

    // 4. SERVO CONTROL
    // Sweep only while the detection condition is true

    if (isRocking) {

        if (currentMillis - lastServoMove >= 30) {

            lastServoMove = currentMillis;

            currentServoAngle += sweepDirection * 2;

            if (currentServoAngle >= SERVO_MAX_ANGLE) {

                currentServoAngle = SERVO_MAX_ANGLE;
                sweepDirection = -1;

            }
            else if (currentServoAngle <= SERVO_MIN_ANGLE) {

                currentServoAngle = SERVO_MIN_ANGLE;
                sweepDirection = 1;
            }

            cradleServo.write(currentServoAngle);
        }

    }
    else {

        // Return servo to center

        if (currentServoAngle != SERVO_CENTER &&
            currentMillis - lastServoMove >= 30) {

            lastServoMove = currentMillis;

            if (currentServoAngle > SERVO_CENTER) {
                currentServoAngle--;
            }
            else if (currentServoAngle < SERVO_CENTER) {
                currentServoAngle++;
            }

            cradleServo.write(currentServoAngle);
        }
    }

    // 5. TFT DISPLAY UPDATE
    // Refresh dashboard every 250 milliseconds

    if (currentMillis - lastDisplayUpdate >= 250) {

        lastDisplayUpdate = currentMillis;

        updateDashboardValues();
    }
}
