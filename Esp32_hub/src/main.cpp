#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "secrets.h"

static NimBLEScan* pBLEScan;

void sendToServer(String id, float blood_pressure, float pulse) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<200> doc;
    doc["device_id"] = id;
    doc["blood_pressure"] = blood_pressure;
    doc["pulse"] = pulse;

    String body;
    serializeJson(doc, body);

    int code = http.POST(body);
    Serial.printf("HTTP Response code: %d\n", code);
    http.end();
  } else {
    Serial.println("WiFi disconnected");
  }
}

class MyAdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* advertisedDevice) override {
    if (advertisedDevice->haveServiceUUID() &&
        advertisedDevice->isAdvertisingService(NimBLEUUID("12345678-1234-1234-1234-123456789abc"))) {
      Serial.println("Found a device with target service UUID!");
      NimBLEClient* pClient = NimBLEDevice::createClient();
      if (!pClient->connect(advertisedDevice)) {
        Serial.println("Failed to connect");
        NimBLEDevice::deleteClient(pClient);
        return;
      }

      NimBLERemoteService* pService = pClient->getService("12345678-1234-1234-1234-123456789abc");
      if (pService) {
        NimBLERemoteCharacteristic* pCharBS = pService->getCharacteristic("11111111-2222-3333-4444-555555555555");
        NimBLERemoteCharacteristic* pCharPulse = pService->getCharacteristic("66666666-7777-8888-9999-000000000000");

        if (pCharBS && pCharPulse) {
          float BS = atof(pCharBS->readValue().c_str());
          float pulse = atof(pCharPulse->readValue().c_str());
          Serial.printf("Device %s: BS=%.2fmm Hg, P=%.2f%%\n",
                        advertisedDevice->getAddress().toString().c_str(), BS, pulse);

          sendToServer(advertisedDevice->getAddress().toString().c_str(), BS, pulse);
        }
      }
      pClient->disconnect();
    }
  }
};

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected");

  NimBLEDevice::init("ESP32_Hub");
  pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
}

void loop() {
  NimBLEScanResults results = pBLEScan->start(5, false);
  pBLEScan->clearResults();
  delay(10000);
}
