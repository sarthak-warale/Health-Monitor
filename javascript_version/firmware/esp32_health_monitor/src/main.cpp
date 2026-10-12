#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.example.h to include/secrets.h and configure it first."
#endif

#ifndef WIFI_SSID
#error "Set WIFI_SSID in include/secrets.h"
#endif
#ifndef WIFI_PASSWORD
#error "Set WIFI_PASSWORD in include/secrets.h"
#endif
#ifndef API_URL
#error "Set API_URL in include/secrets.h"
#endif
#ifndef DEVICE_API_KEY
#error "Set DEVICE_API_KEY in include/secrets.h"
#endif
#ifndef PATIENT_ID
#define PATIENT_ID 1
#endif
#ifndef SERVER_ROOT_CA
#define SERVER_ROOT_CA ""
#endif

namespace {
constexpr uint8_t kSdaPin = 21;
constexpr uint8_t kSclPin = 22;
constexpr int kSamples = 100;
constexpr unsigned long kSendIntervalMs = 10000;
constexpr uint32_t kFingerThreshold = 50000;

MAX30105 sensor;
uint32_t irBuffer[kSamples];
uint32_t redBuffer[kSamples];
int32_t heartRate = 0;
int32_t oxygen = 0;
int8_t heartRateValid = 0;
int8_t oxygenValid = 0;
unsigned long lastSendMs = 0;

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("Connecting to Wi-Fi: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected; IP: ");
    Serial.println(WiFi.localIP());
    configTime(0, 0, "pool.ntp.org", "time.google.com");
  } else {
    Serial.println("Wi-Fi connection failed; will retry.");
  }
}

bool readVitals() {
  for (int i = 0; i < kSamples; ++i) {
    const unsigned long waitStarted = millis();
    while (!sensor.available()) {
      sensor.check();
      if (millis() - waitStarted > 2000) {
        Serial.println("Timed out waiting for MAX30102 sample.");
        return false;
      }
      delay(1);
    }
    redBuffer[i] = sensor.getRed();
    irBuffer[i] = sensor.getIR();
    sensor.nextSample();
  }

  if (irBuffer[kSamples - 1] < kFingerThreshold) {
    Serial.println("Place a finger on the sensor.");
    return false;
  }

  maxim_heart_rate_and_oxygen_saturation(
    irBuffer, kSamples, redBuffer, &oxygen, &oxygenValid, &heartRate, &heartRateValid);

  // Guard against implausible or invalid algorithm outputs before sending.
  return heartRateValid && oxygenValid && heartRate >= 30 && heartRate <= 220 && oxygen >= 70 && oxygen <= 100;
}

void sendReading() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;
  }

  const String url = API_URL;
  HTTPClient http;
  int status = -1;
  if (url.startsWith("https://")) {
    if (strlen(SERVER_ROOT_CA) == 0) {
      Serial.println("HTTPS needs SERVER_ROOT_CA in secrets.h; reading not sent.");
      return;
    }
    WiFiClientSecure client;
    client.setCACert(SERVER_ROOT_CA);
    if (!http.begin(client, url)) {
      Serial.println("Could not start HTTPS request.");
      return;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Key", DEVICE_API_KEY);
    const time_t now = time(nullptr);
    char body[160];
    snprintf(body, sizeof(body), "{\"patient_id\":%d,\"hr\":%ld,\"spo2\":%ld,\"timestamp\":%ld}",
      PATIENT_ID, static_cast<long>(heartRate), static_cast<long>(oxygen), static_cast<long>(now));
    status = http.POST(reinterpret_cast<uint8_t *>(body), strlen(body));
  } else {
    WiFiClient client;
    if (!http.begin(client, url)) {
      Serial.println("Could not start HTTP request.");
      return;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Key", DEVICE_API_KEY);
    const time_t now = time(nullptr);
    char body[160];
    snprintf(body, sizeof(body), "{\"patient_id\":%d,\"hr\":%ld,\"spo2\":%ld,\"timestamp\":%ld}",
      PATIENT_ID, static_cast<long>(heartRate), static_cast<long>(oxygen), static_cast<long>(now));
    status = http.POST(reinterpret_cast<uint8_t *>(body), strlen(body));
  }
  Serial.printf("Reading sent; HTTP status: %d\n", status);
  if (status > 0) Serial.println(http.getString());
  http.end();
}
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Wire.begin(kSdaPin, kSclPin);
  if (!sensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found. Check 3.3V, GND, SDA (21), and SCL (22).");
    while (true) delay(1000);
  }
  sensor.setup(60, 4, 2, 100, 411, 4096);
  sensor.setPulseAmplitudeRed(0x1F);
  sensor.setPulseAmplitudeIR(0x1F);
  sensor.setPulseAmplitudeGreen(0);
  Serial.println("MAX30102 initialized.");
  connectWiFi();
}

void loop() {
  if (!readVitals()) {
    delay(500);
    return;
  }

  Serial.printf("HR: %ld bpm | SpO2: %ld%%\n", static_cast<long>(heartRate), static_cast<long>(oxygen));
  if (millis() - lastSendMs >= kSendIntervalMs) {
    sendReading();
    lastSendMs = millis();
  }
}
