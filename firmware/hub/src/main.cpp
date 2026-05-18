#include <Arduino.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <HTTPClient.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <vector>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "CHANGE_ME"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "CHANGE_ME"
#endif

#ifndef WIFI_FALLBACK_SSID
#define WIFI_FALLBACK_SSID ""
#endif

#ifndef WIFI_FALLBACK_PASSWORD
#define WIFI_FALLBACK_PASSWORD ""
#endif

#ifndef WIFI_FALLBACK_ENABLED
#define WIFI_FALLBACK_ENABLED 0
#endif

#ifndef SERVER_BASE_URL
#define SERVER_BASE_URL "http://192.168.1.100:5000"
#endif

#ifndef SERVER_FALLBACK_BASE_URL
#define SERVER_FALLBACK_BASE_URL SERVER_BASE_URL
#endif

#ifndef HUB_ID
#define HUB_ID "hub-01"
#endif

#ifndef DEVICE_ID_PREFIX
#define DEVICE_ID_PREFIX "ble"
#endif

#define SD_CS_PIN 5
#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define HTTP_TIMEOUT_MS 10000
#define CHUNK_SIZE 120

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 30000
#endif

#ifndef WIFI_PRIMARY_ATTEMPTS
#define WIFI_PRIMARY_ATTEMPTS 3
#endif

struct SensorProfile {
    const char* name;
    const char* sensorType;
    const char* unit;
    float sampleRateHz;
};

static BLEUUID serviceUUID(SERVICE_UUID);
static BLEUUID charUUID(CHARACTERISTIC_UUID);
static BLEClient* pClient = nullptr;
static BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;
static BLEAdvertisedDevice* myDevice = nullptr;
static bool doConnect = false;
static bool connected = false;
static bool doScan = false;
static bool isReceiving = false;
static bool sdReady = false;
static bool wifiConfigured = false;
static bool uploadPending = false;
static bool uploadInProgress = false;
static bool bleRunning = false;
static std::vector<float> receivedData;
static const SensorProfile sensorProfiles[] = {
    { "ESP32_BP_Monitor", "pressure", "mmHg", 1.0f }
};
static SensorProfile activeProfile = sensorProfiles[0];
static String measurementSensorType = "pressure";
static String measurementUnit = "mmHg";
static float measurementSampleRateHz = 1.0f;
static int measurementSchemaVersion = 1;
static int expectedSampleCount = -1;
static String activeBleAddress;
static String activeBleName;
static String activeServerBaseUrl = SERVER_BASE_URL;

static String sensorTypeFromCode(const String& code) {
    if (code == "p") {
        return "pressure";
    }

    if (code == "t") {
        return "temperature";
    }

    if (code == "e") {
        return "ecg";
    }

    if (code == "a") {
        return "accel";
    }

    return code;
}

static const char* wifiStatusName(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS:
            return "idle";
        case WL_NO_SSID_AVAIL:
            return "no_ssid_available";
        case WL_SCAN_COMPLETED:
            return "scan_completed";
        case WL_CONNECTED:
            return "connected";
        case WL_CONNECT_FAILED:
            return "connect_failed";
        case WL_CONNECTION_LOST:
            return "connection_lost";
        case WL_DISCONNECTED:
            return "disconnected";
        default:
            return "unknown";
    }
}

static const char* encryptionName(wifi_auth_mode_t type) {
    switch (type) {
        case WIFI_AUTH_OPEN:
            return "open";
        case WIFI_AUTH_WEP:
            return "wep";
        case WIFI_AUTH_WPA_PSK:
            return "wpa";
        case WIFI_AUTH_WPA2_PSK:
            return "wpa2";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "wpa_wpa2";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "wpa2_enterprise";
        case WIFI_AUTH_WPA3_PSK:
            return "wpa3";
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return "wpa2_wpa3";
        default:
            return "unknown";
    }
}

static bool ensureSd() {
    if (sdReady) {
        return true;
    }

    sdReady = SD.begin(SD_CS_PIN);
    if (sdReady) {
        Serial.println("SD remounted");
    } else {
        Serial.println("SD remount failed");
    }

    return sdReady;
}

static String jsonEscape(const String& value) {
    String escaped;
    escaped.reserve(value.length() + 8);
    for (size_t i = 0; i < value.length(); i++) {
        char c = value[i];
        if (c == '"' || c == '\\') {
            escaped += '\\';
            escaped += c;
        } else if (c == '\n') {
            escaped += "\\n";
        } else if (c == '\r') {
            escaped += "\\r";
        } else if (c == '\t') {
            escaped += "\\t";
        } else {
            escaped += c;
        }
    }
    return escaped;
}

static String buildDeviceId() {
    if (activeBleAddress.length() == 0) {
        return String(HUB_ID);
    }

    String id = String(DEVICE_ID_PREFIX) + "-" + activeBleAddress;
    id.replace(":", "");
    id.toLowerCase();
    return id;
}

static int findScannedNetwork(const String& targetSsid, int networks) {
    for (int i = 0; i < networks; i++) {
        if (WiFi.SSID(i) == targetSsid) {
            return i;
        }
    }

    return -1;
}

static void resetWiFiStation() {
    WiFi.disconnect(false, false);
    delay(500);
    WiFi.mode(WIFI_OFF);
    delay(500);
    WiFi.mode(WIFI_STA);
    delay(500);
}

static bool connectWiFiProfile(const char* profileName, const String& ssid, const char* password, const char* serverBaseUrl, int networks, bool lockToScannedNetwork) {
    String configuredSsid = ssid;
    configuredSsid.trim();

    if (configuredSsid.length() == 0) {
        Serial.print("WiFi ");
        Serial.print(profileName);
        Serial.println(" skipped: empty SSID");
        return false;
    }

    Serial.print("WiFi ");
    Serial.print(profileName);
    Serial.print(" SSID: ");
    Serial.println(configuredSsid);
    Serial.print("WiFi ");
    Serial.print(profileName);
    Serial.print(" password length: ");
    Serial.println(String(password).length());

    int selectedNetwork = findScannedNetwork(configuredSsid, networks);
    uint8_t bssid[6];
    int32_t channel = 0;
    bool canLockToNetwork = selectedNetwork >= 0;

    if (selectedNetwork < 0) {
        Serial.print("WiFi ");
        Serial.print(profileName);
        Serial.println(" target SSID not found, trying direct connect");
    } else {
        memcpy(bssid, WiFi.BSSID(selectedNetwork), 6);
        channel = WiFi.channel(selectedNetwork);
        Serial.print("WiFi ");
        Serial.print(profileName);
        Serial.print(" target found channel=");
        Serial.print(channel);
        Serial.print(" rssi=");
        Serial.print(WiFi.RSSI(selectedNetwork));
        Serial.print(" auth=");
        Serial.println(encryptionName(WiFi.encryptionType(selectedNetwork)));
    }

    resetWiFiStation();
    if (lockToScannedNetwork && canLockToNetwork) {
        WiFi.begin(configuredSsid.c_str(), password, channel, bssid, true);
    } else {
        WiFi.begin(configuredSsid.c_str(), password);
    }
    Serial.print("WiFi ");
    Serial.print(profileName);
    if (lockToScannedNetwork && canLockToNetwork) {
        Serial.println(" locked connect requested");
    } else {
        Serial.println(" connect requested");
    }

    unsigned long started = millis();
    wl_status_t lastStatus = WiFi.status();
    while (lastStatus != WL_CONNECTED && millis() - started < WIFI_CONNECT_TIMEOUT_MS) {
        delay(500);
        wl_status_t status = WiFi.status();
        if (status != lastStatus) {
            Serial.print("WiFi ");
            Serial.print(profileName);
            Serial.print(" status: ");
            Serial.print(status);
            Serial.print(" ");
            Serial.println(wifiStatusName(status));
            lastStatus = status;
        }
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("WiFi ");
        Serial.print(profileName);
        Serial.print(" connected: ");
        Serial.println(WiFi.localIP());
        Serial.print("WiFi RSSI: ");
        Serial.println(WiFi.RSSI());
        Serial.print("WiFi gateway: ");
        Serial.println(WiFi.gatewayIP());
        Serial.print("WiFi subnet: ");
        Serial.println(WiFi.subnetMask());
        activeServerBaseUrl = serverBaseUrl;
        Serial.print("HTTP server base URL: ");
        Serial.println(activeServerBaseUrl);
        return true;
    }

    Serial.print("WiFi ");
    Serial.print(profileName);
    Serial.println(" connection failed");
    Serial.print("WiFi ");
    Serial.print(profileName);
    Serial.print(" status: ");
    Serial.print(WiFi.status());
    Serial.print(" ");
    Serial.println(wifiStatusName(WiFi.status()));
    return false;
}

static bool ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }

    if (!wifiConfigured) {
        WiFi.persistent(false);
        wifiConfigured = true;
    }

    WiFi.setSleep(true);
    WiFi.mode(WIFI_OFF);
    delay(250);
    WiFi.mode(WIFI_STA);
    delay(250);
    WiFi.disconnect(false, false);
    delay(250);

    int networks = WiFi.scanNetworks(false, true);
    Serial.print("WiFi networks found: ");
    Serial.println(networks);

    for (int i = 0; i < networks; i++) {
        String ssid = WiFi.SSID(i);
        Serial.print("WiFi network ");
        Serial.print(i);
        Serial.print(": [");
        Serial.print(ssid);
        Serial.print("] channel=");
        Serial.print(WiFi.channel(i));
        Serial.print(" rssi=");
        Serial.print(WiFi.RSSI(i));
        Serial.print(" auth=");
        Serial.println(encryptionName(WiFi.encryptionType(i)));
    }

    for (int attempt = 1; attempt <= WIFI_PRIMARY_ATTEMPTS; attempt++) {
        Serial.print("WiFi primary attempt ");
        Serial.print(attempt);
        Serial.print("/");
        Serial.println(WIFI_PRIMARY_ATTEMPTS);

        bool lockToScannedNetwork = attempt % 2 == 0;
        if (connectWiFiProfile("primary", WIFI_SSID, WIFI_PASSWORD, SERVER_BASE_URL, networks, lockToScannedNetwork)) {
            WiFi.scanDelete();
            return true;
        }
    }

    if (WIFI_FALLBACK_ENABLED == 1) {
        if (connectWiFiProfile("fallback", WIFI_FALLBACK_SSID, WIFI_FALLBACK_PASSWORD, SERVER_FALLBACK_BASE_URL, networks, false)) {
            WiFi.scanDelete();
            return true;
        }
    } else {
        Serial.println("WiFi fallback disabled");
    }

    WiFi.scanDelete();
    Serial.println("WiFi all profiles failed");
    return false;
}

static String partAt(const String& value, int index) {
    int start = 0;
    int current = 0;

    while (start <= value.length()) {
        int end = value.indexOf(';', start);
        if (end < 0) {
            end = value.length();
        }

        if (current == index) {
            return value.substring(start, end);
        }

        current++;
        start = end + 1;
    }

    return "";
}

static String hs1Field(const String& message, const String& key) {
    String marker = key + "=";
    int start = message.indexOf(marker);
    if (start < 0) {
        return "";
    }

    start += marker.length();
    int end = message.indexOf(';', start);
    if (end < 0) {
        end = message.length();
    }

    return message.substring(start, end);
}

static void resetMeasurementContext() {
    measurementSensorType = activeProfile.sensorType;
    measurementUnit = activeProfile.unit;
    measurementSampleRateHz = activeProfile.sampleRateHz;
    measurementSchemaVersion = 1;
    expectedSampleCount = -1;
    receivedData.clear();
}

static void applyStartField(const String& message, const String& key, String& target) {
    String value = hs1Field(message, key);
    if (value.length() > 0) {
        target = value;
    }
}

static const SensorProfile* resolveSensorProfile(BLEAdvertisedDevice& advertisedDevice) {
    std::string name = advertisedDevice.getName();
    for (const SensorProfile& profile : sensorProfiles) {
        if (name == profile.name) {
            return &profile;
        }
    }

    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {
        return &sensorProfiles[0];
    }

    return nullptr;
}

static bool postJson(const String& path, const String& body, String* responseBody = nullptr) {
    if (!ensureWiFi()) {
        return false;
    }

    HTTPClient http;
    String url = activeServerBaseUrl + path;
    Serial.print("HTTP POST ");
    Serial.println(url);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(body);
    String response = http.getString();
    http.end();

    if (responseBody != nullptr) {
        *responseBody = response;
    }

    if (code >= 200 && code < 300) {
        return true;
    }

    Serial.print("HTTP POST failed ");
    Serial.print(path);
    Serial.print(": ");
    Serial.print(code);
    Serial.print(" ");
    if (code < 0) {
        Serial.println(http.errorToString(code));
    } else {
        Serial.println(response);
    }
    return false;
}

static String extractJsonString(const String& json, const String& key) {
    String marker = "\"" + key + "\":";
    int keyIndex = json.indexOf(marker);
    if (keyIndex < 0) {
        return "";
    }

    int firstQuote = json.indexOf('"', keyIndex + marker.length());
    if (firstQuote < 0) {
        return "";
    }

    int secondQuote = json.indexOf('"', firstQuote + 1);
    if (secondQuote < 0) {
        return "";
    }

    return json.substring(firstQuote + 1, secondQuote);
}

static void backupToSd() {
    if (receivedData.empty()) {
        return;
    }

    if (!ensureSd()) {
        return;
    }

    File file = SD.open("/received_data.txt", FILE_WRITE);
    if (!file) {
        Serial.println("SD backup open failed, retrying");
        SD.end();
        sdReady = false;
        delay(250);
        if (!ensureSd()) {
            return;
        }

        file = SD.open("/received_data.txt", FILE_WRITE);
        if (!file) {
            Serial.println("SD backup open failed");
            return;
        }
    }

    file.print("sensorType=");
    file.println(measurementSensorType);
    file.print("unit=");
    file.println(measurementUnit);
    file.print("sampleRateHz=");
    file.println(measurementSampleRateHz, 2);
    file.print("schemaVersion=");
    file.println(measurementSchemaVersion);
    file.print("bleAddress=");
    file.println(activeBleAddress);

    for (float value : receivedData) {
        file.println(value, 2);
    }

    file.close();
    Serial.printf("SD backup saved: %u samples\n", receivedData.size());
}

static String buildStartPayload() {
    String payload;
    payload.reserve(384);
    payload += "{\"deviceId\":\"";
    payload += jsonEscape(buildDeviceId());
    payload += "\",\"sensorType\":\"";
    payload += jsonEscape(measurementSensorType);
    payload += "\",\"schemaVersion\":";
    payload += measurementSchemaVersion;
    payload += ",\"sampleRateHz\":";
    payload += String(measurementSampleRateHz, 2);
    payload += ",\"unit\":\"";
    payload += jsonEscape(measurementUnit);
    payload += "\",\"meta\":{\"source\":\"esp32-hub\",\"hubId\":\"";
    payload += jsonEscape(String(HUB_ID));
    payload += "\",\"bleAddress\":\"";
    payload += jsonEscape(activeBleAddress);
    payload += "\",\"bleName\":\"";
    payload += jsonEscape(activeBleName);
    payload += "\"}}";
    return payload;
}

static String buildChunkPayload(const String& measurementId, int chunkIndex, int totalChunks, int startIndex, int count) {
    String payload;
    payload.reserve(128 + count * 10);
    payload += "{\"measurementId\":\"";
    payload += measurementId;
    payload += "\",\"chunkIndex\":";
    payload += chunkIndex;
    payload += ",\"totalChunks\":";
    payload += totalChunks;
    payload += ",\"samples\":[";

    for (int i = 0; i < count; i++) {
        if (i > 0) {
            payload += ",";
        }
        payload += String(receivedData[startIndex + i], 2);
    }

    payload += "]}";
    return payload;
}

static String buildCompletePayload(const String& measurementId, int totalChunks) {
    String payload;
    payload.reserve(128);
    payload += "{\"measurementId\":\"";
    payload += measurementId;
    payload += "\",\"totalChunks\":";
    payload += totalChunks;
    payload += ",\"sampleCount\":";
    payload += receivedData.size();
    payload += "}";
    return payload;
}

static bool sendMeasurementToServer() {
    if (receivedData.empty()) {
        Serial.println("No samples to send");
        return false;
    }

    String startResponse;
    if (!postJson("/api/ingest/start", buildStartPayload(), &startResponse)) {
        return false;
    }

    String measurementId = extractJsonString(startResponse, "measurementId");
    if (measurementId.length() == 0) {
        Serial.println("MeasurementId not found in server response");
        return false;
    }

    int totalChunks = (receivedData.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;
    for (int chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
        int startIndex = chunkIndex * CHUNK_SIZE;
        int count = min((int)CHUNK_SIZE, (int)receivedData.size() - startIndex);
        String chunkPayload = buildChunkPayload(measurementId, chunkIndex, totalChunks, startIndex, count);

        if (!postJson("/api/ingest/chunk", chunkPayload)) {
            return false;
        }
    }

    if (!postJson("/api/ingest/complete", buildCompletePayload(measurementId, totalChunks))) {
        return false;
    }

    Serial.print("Measurement sent: ");
    Serial.println(measurementId);
    return true;
}

static void finishMeasurement() {
    isReceiving = false;
    backupToSd();
    uploadPending = true;
    expectedSampleCount = -1;
}

static void disconnectBleSensor() {
    if (pClient != nullptr && pClient->isConnected()) {
        Serial.println("Disconnecting BLE before WiFi upload");
        pClient->disconnect();
        delay(500);
    }

    connected = false;
    pRemoteCharacteristic = nullptr;
}

static void handleHs1Start(const String& value) {
    resetMeasurementContext();
    applyStartField(value, "type", measurementSensorType);
    applyStartField(value, "unit", measurementUnit);

    String rate = hs1Field(value, "rate");
    if (rate.length() > 0) {
        measurementSampleRateHz = rate.toFloat();
    }

    String schema = hs1Field(value, "schema");
    if (schema.length() > 0) {
        measurementSchemaVersion = schema.toInt();
    }

    isReceiving = true;
}

static void handleHs1Chunk(const String& value) {
    if (!isReceiving) {
        return;
    }

    String values = hs1Field(value, "values");
    int start = 0;

    while (start < values.length()) {
        int end = values.indexOf(',', start);
        if (end < 0) {
            end = values.length();
        }

        String sample = values.substring(start, end);
        sample.trim();
        if (sample.length() > 0) {
            receivedData.push_back(sample.toFloat());
        }

        start = end + 1;
    }
}

static void handleHs1End(const String& value) {
    String count = hs1Field(value, "sampleCount");
    if (count.length() > 0) {
        expectedSampleCount = count.toInt();
    }

    if (expectedSampleCount >= 0 && expectedSampleCount != (int)receivedData.size()) {
        Serial.print("Sample count mismatch: ");
        Serial.print(expectedSampleCount);
        Serial.print(" expected, ");
        Serial.print(receivedData.size());
        Serial.println(" received");
    }

    finishMeasurement();
}

static void handleH1Start(const String& value) {
    resetMeasurementContext();

    String sensorCode = partAt(value, 1);
    String unit = partAt(value, 2);
    String rate = partAt(value, 3);
    String schema = partAt(value, 4);

    if (sensorCode.length() > 0) {
        measurementSensorType = sensorTypeFromCode(sensorCode);
    }

    if (unit.length() > 0) {
        measurementUnit = unit;
    }

    if (rate.length() > 0) {
        measurementSampleRateHz = rate.toFloat();
    }

    if (schema.length() > 0) {
        measurementSchemaVersion = schema.toInt();
    }

    isReceiving = true;
}

static void handleH1Data(const String& value) {
    if (!isReceiving) {
        return;
    }

    String sample = partAt(value, 2);
    sample.trim();
    if (sample.length() > 0) {
        receivedData.push_back(sample.toFloat());
    }
}

static void handleH1End(const String& value) {
    String count = partAt(value, 1);
    if (count.length() > 0) {
        expectedSampleCount = count.toInt();
    }

    if (expectedSampleCount >= 0 && expectedSampleCount != (int)receivedData.size()) {
        Serial.print("Sample count mismatch: ");
        Serial.print(expectedSampleCount);
        Serial.print(" expected, ");
        Serial.print(receivedData.size());
        Serial.println(" received");
    }

    finishMeasurement();
}

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    String value;
    value.reserve(length);
    for (size_t i = 0; i < length; i++) {
        value += (char)pData[i];
    }

    Serial.print("BLE: ");
    Serial.println(value);

    if (value.startsWith("H1S;")) {
        handleH1Start(value);
        return;
    }

    if (value.startsWith("H1D;")) {
        handleH1Data(value);
        return;
    }

    if (value.startsWith("H1E;")) {
        handleH1End(value);
        return;
    }

    if (value.startsWith("HS1;START")) {
        handleHs1Start(value);
        return;
    }

    if (value.startsWith("HS1;CHUNK")) {
        handleHs1Chunk(value);
        return;
    }

    if (value.startsWith("HS1;END")) {
        handleHs1End(value);
        return;
    }

    if (value == "START") {
        resetMeasurementContext();
        isReceiving = true;
        return;
    }

    if (value == "END") {
        finishMeasurement();
        return;
    }

    if (isReceiving && value.startsWith("D:")) {
        receivedData.push_back(value.substring(2).toFloat());
    }
}

class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
        connected = true;
        doScan = false;
    }

    void onDisconnect(BLEClient* pclient) {
        connected = false;
        if (!uploadInProgress) {
            doScan = true;
        }
        Serial.println("BLE disconnected");
    }
};

static bool connectToSensor() {
    if (myDevice == nullptr) {
        return false;
    }

    activeBleAddress = myDevice->getAddress().toString().c_str();
    activeBleName = myDevice->getName().c_str();

    Serial.print("Connecting to BLE sensor ");
    Serial.println(activeBleAddress);

    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());

    if (!pClient->connect(myDevice)) {
        Serial.println("BLE connect failed");
        return false;
    }

    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        Serial.println("BLE service not found");
        pClient->disconnect();
        return false;
    }

    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
        Serial.println("BLE characteristic not found");
        pClient->disconnect();
        return false;
    }

    if (pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
    }

    connected = true;
    return true;
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        Serial.print("BLE advertised device: ");
        Serial.println(advertisedDevice.toString().c_str());

        const SensorProfile* profile = resolveSensorProfile(advertisedDevice);
        if (profile == nullptr) {
            return;
        }

        activeProfile = *profile;
        BLEDevice::getScan()->stop();
        myDevice = new BLEAdvertisedDevice(advertisedDevice);
        doConnect = true;
        doScan = true;
    }
};

static void startBle() {
    if (bleRunning) {
        return;
    }

    BLEDevice::init("");
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setInterval(1349);
    pBLEScan->setWindow(449);
    pBLEScan->setActiveScan(true);
    doScan = true;
    pBLEScan->start(5, false);
    bleRunning = true;
}

static void stopBle() {
    if (!bleRunning) {
        return;
    }

    disconnectBleSensor();
    BLEDevice::getScan()->stop();
    BLEDevice::deinit(false);
    bleRunning = false;
    doConnect = false;
    doScan = false;
    connected = false;
    pClient = nullptr;
    pRemoteCharacteristic = nullptr;
}

static void stopWiFi() {
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    wifiConfigured = false;
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    sdReady = SD.begin(SD_CS_PIN);
    if (sdReady) {
        Serial.println("SD ready");
    } else {
        Serial.println("SD unavailable");
    }

    startBle();
}

void loop() {
    if (doConnect) {
        if (connectToSensor()) {
            Serial.println("BLE sensor connected");
        } else {
            Serial.println("BLE sensor connection failed");
        }
        doConnect = false;
    }

    if (!connected && doScan && !uploadInProgress) {
        BLEDevice::getScan()->start(5, false);
    }

    if (uploadPending) {
        uploadInProgress = true;
        stopBle();
        uploadPending = false;
        bool uploaded = sendMeasurementToServer();
        if (!uploaded) {
            Serial.println("Measurement upload failed");
        }
        stopWiFi();
        receivedData.clear();
        uploadInProgress = false;
        startBle();
    }

    delay(1000);
}
