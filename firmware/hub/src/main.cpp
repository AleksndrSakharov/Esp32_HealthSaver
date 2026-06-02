#include <Arduino.h>
#include <AsyncTCP.h>
#include <BLEAdvertisedDevice.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <vector>

#if __has_include("secrets.h")
#include "secrets.h"
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
#define DISCOVERY_PORT 50505
#define DISCOVERY_TIMEOUT_MS 10000
#define DISCOVERY_REFRESH_MS 60000
#define PROVISIONING_DNS_PORT 53
#define PROVISIONING_HTTP_PORT 80
#define PROVISIONING_AP_SSID "ESP32HUB"
#define PROVISIONING_AP_PASSWORD "healthsaver"
#define BOOT_BUTTON_PIN 0
#define BOOT_PROVISIONING_HOLD_MS 2500
#define BOOT_PROVISIONING_WINDOW_MS 5000
#define BOOT_RUNTIME_HOLD_MS 5000
#define MAX_RECEIVED_SAMPLES 700
#define SERVER_URL_MAX_LENGTH 96

#ifndef SD_BACKUP_ENABLED
#define SD_BACKUP_ENABLED 1
#endif

#ifndef SD_BACKUP_MIN_FREE_HEAP
#define SD_BACKUP_MIN_FREE_HEAP 50000
#endif

#ifndef HTTP_UPLOAD_MIN_FREE_HEAP
#define HTTP_UPLOAD_MIN_FREE_HEAP 45000
#endif

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 12000
#endif

#ifndef WIFI_PRIMARY_ATTEMPTS
#define WIFI_PRIMARY_ATTEMPTS 3
#endif

#ifndef WIFI_RETRY_DELAY_MS
#define WIFI_RETRY_DELAY_MS 30000
#endif

#ifndef BLE_VERBOSE_DATA_LOGS
#define BLE_VERBOSE_DATA_LOGS 0
#endif

struct SensorProfile {
    const char* name;
    const char* sensorType;
    const char* unit;
    float sampleRateHz;
};

struct MeasurementBatch {
    String deviceId;
    String sensorType;
    String unit;
    float sampleRateHz;
    int schemaVersion;
    String bleAddress;
    String bleName;
    std::vector<float> samples;
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
static bool bleRunning = false;
static bool measurementCompletePending = false;
static QueueHandle_t measurementQueue = nullptr;
static TaskHandle_t networkTaskHandle = nullptr;
static float receivedData[MAX_RECEIVED_SAMPLES];
static size_t receivedDataCount = 0;
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
static String configuredWifiSsid;
static String configuredWifiPassword;
static char activeServerBaseUrl[SERVER_URL_MAX_LENGTH] = "";
static unsigned long lastServerDiscoveryAt = 0;
static bool provisioningMode = false;
static bool provisioningSaved = false;
static unsigned long provisioningRestartAt = 0;
static Preferences preferences;
static DNSServer dnsServer;
static AsyncWebServer provisioningServer(PROVISIONING_HTTP_PORT);
static WiFiUDP discoveryUdp;
static bool discoveryUdpStarted = false;
static unsigned long lastWiFiAttemptAt = 0;
static unsigned long nextUploadRetryAt = 0;
static bool bleScanPausedForWiFi = false;
static bool wifiConnectInProgress = false;
static bool bleConnectInProgress = false;
static unsigned long bootButtonPressedAt = 0;

static void stopBle();
static void stopWiFi();
static void startProvisioningPortal();

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

static String jsonStringValue(const String& json, const String& key) {
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

static bool loadHubConfig() {
    preferences.begin("hubcfg", false);
    configuredWifiSsid = preferences.getString("ssid", "");
    configuredWifiPassword = preferences.getString("password", "");
    String cachedServer = preferences.isKey("server") ? preferences.getString("server", "") : "";
    preferences.end();

    configuredWifiSsid.trim();
    configuredWifiPassword.trim();
    cachedServer.trim();
    strlcpy(activeServerBaseUrl, cachedServer.c_str(), sizeof(activeServerBaseUrl));

    return configuredWifiSsid.length() > 0;
}

static void saveHubConfig(const String& ssid, const String& password) {
    preferences.begin("hubcfg", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    if (preferences.isKey("server")) {
        preferences.remove("server");
    }
    preferences.end();
    activeServerBaseUrl[0] = '\0';
}

static void saveDiscoveredServer(const String& baseUrl) {
    preferences.begin("hubcfg", false);
    preferences.putString("server", baseUrl);
    preferences.end();
}

static void clearHubConfig() {
    preferences.begin("hubcfg", false);
    preferences.clear();
    preferences.end();
    configuredWifiSsid = "";
    configuredWifiPassword = "";
    activeServerBaseUrl[0] = '\0';
    lastServerDiscoveryAt = 0;
}

static bool bootButtonPressed() {
    return digitalRead(BOOT_BUTTON_PIN) == LOW;
}

static bool waitForProvisioningButton() {
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    Serial.print("Press BOOT for provisioning within ");
    Serial.print(BOOT_PROVISIONING_WINDOW_MS / 1000);
    Serial.println(" seconds");

    unsigned long windowStarted = millis();
    unsigned long holdStarted = 0;

    while ((long)(millis() - windowStarted) < BOOT_PROVISIONING_WINDOW_MS || holdStarted > 0) {
        if (bootButtonPressed()) {
            if (holdStarted == 0) {
                holdStarted = millis();
                Serial.println("BOOT press detected");
            }

            if ((long)(millis() - holdStarted) >= BOOT_PROVISIONING_HOLD_MS) {
                return true;
            }
        } else if (holdStarted > 0) {
            holdStarted = 0;
        }

        delay(20);
    }

    return false;
}

static void enterProvisioningWithConfigClear() {
    Serial.println("BOOT held, clearing WiFi config");
    clearHubConfig();
    Serial.println("Starting WiFi provisioning");
    startProvisioningPortal();
}

static void handleRuntimeProvisioningButton() {
    if (provisioningMode) {
        return;
    }

    if (bootButtonPressed()) {
        if (bootButtonPressedAt == 0) {
            bootButtonPressedAt = millis();
            Serial.println("BOOT press detected");
        }

        if ((long)(millis() - bootButtonPressedAt) >= BOOT_RUNTIME_HOLD_MS) {
            enterProvisioningWithConfigClear();
        }
    } else {
        bootButtonPressedAt = 0;
    }
}

static String provisioningPage(const String& message = "") {
    String html;
    html.reserve(2500);
    html += "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    html += "<title>HealthSaver Hub</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:0;background:#f4f7fb;color:#172033}.wrap{max-width:420px;margin:0 auto;padding:32px 18px}.panel{background:white;border:1px solid #dbe3ef;border-radius:8px;padding:22px;box-shadow:0 10px 30px rgba(20,40,70,.08)}h1{font-size:24px;margin:0 0 8px}p{color:#5b6778;line-height:1.45}label{display:block;font-weight:700;margin:18px 0 8px}input{box-sizing:border-box;width:100%;font-size:16px;padding:12px;border:1px solid #b8c4d6;border-radius:6px}button{width:100%;margin-top:20px;padding:13px;border:0;border-radius:6px;background:#1463ff;color:white;font-size:16px;font-weight:700}.msg{padding:12px;background:#e8f3ff;border-radius:6px;margin:12px 0;color:#17406e}</style>";
    html += "</head><body><main class=\"wrap\"><section class=\"panel\"><h1>HealthSaver Hub</h1><p>Enter WiFi credentials. The hub will find the ASP.NET server automatically.</p>";
    if (message.length() > 0) {
        html += "<div class=\"msg\">";
        html += message;
        html += "</div>";
    }
    html += "<form method=\"post\" action=\"/save\"><label for=\"ssid\">WiFi SSID</label><input id=\"ssid\" name=\"ssid\" autocomplete=\"off\" required>";
    html += "<label for=\"password\">WiFi Password</label><input id=\"password\" name=\"password\" type=\"password\" autocomplete=\"current-password\">";
    html += "<button type=\"submit\">Save and restart</button></form></section></main></body></html>";
    return html;
}

static void startProvisioningPortal() {
    if (provisioningMode) {
        return;
    }

    stopBle();
    stopWiFi();
    provisioningMode = true;
    WiFi.mode(WIFI_AP);
    bool apStarted = WiFi.softAP(PROVISIONING_AP_SSID, PROVISIONING_AP_PASSWORD);

    if (apStarted) {
        Serial.print("Provisioning AP started: ");
        Serial.println(PROVISIONING_AP_SSID);
        Serial.print("Provisioning IP: ");
        Serial.println(WiFi.softAPIP());
    } else {
        Serial.println("Provisioning AP start failed");
    }

    dnsServer.start(PROVISIONING_DNS_PORT, "*", WiFi.softAPIP());

    provisioningServer.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", provisioningPage());
    });

    provisioningServer.on("/save", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("ssid", true)) {
            request->send(400, "text/html", provisioningPage("SSID is required."));
            return;
        }

        String ssid = request->getParam("ssid", true)->value();
        String password = request->hasParam("password", true) ? request->getParam("password", true)->value() : "";
        ssid.trim();

        if (ssid.length() == 0) {
            request->send(400, "text/html", provisioningPage("SSID is required."));
            return;
        }

        saveHubConfig(ssid, password);
        request->send(200, "text/html", provisioningPage("Saved. Hub will restart now."));
        provisioningSaved = true;
        provisioningRestartAt = millis() + 1500;
    });

    provisioningServer.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    provisioningServer.on("/fwlink", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    provisioningServer.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", provisioningPage());
    });
    provisioningServer.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->redirect("/");
    });
    provisioningServer.onNotFound([](AsyncWebServerRequest* request) {
        request->redirect("/");
    });

    provisioningServer.begin();
}

static int findScannedNetwork(const String& targetSsid, int networks) {
    for (int i = 0; i < networks; i++) {
        if (WiFi.SSID(i) == targetSsid) {
            return i;
        }
    }

    return -1;
}

static bool bleBusy() {
    return isReceiving || measurementCompletePending || bleConnectInProgress || doConnect;
}

static bool bleConnectedOrBusy() {
    return connected || bleBusy();
}

static bool wifiBlockedByBle() {
    return bleBusy();
}

static void pauseBleScanForWiFi() {
    if (!bleRunning || connected || bleScanPausedForWiFi) {
        return;
    }

    BLEDevice::getScan()->stop();
    doScan = false;
    bleScanPausedForWiFi = true;
    Serial.println("BLE scan paused for WiFi");
}

static void resumeBleScanAfterWiFi() {
    if (!bleRunning || connected || !bleScanPausedForWiFi || provisioningMode) {
        return;
    }

    doScan = true;
    bleScanPausedForWiFi = false;
    Serial.println("BLE scan resumed");
}

static void stopDisconnectedWiFiRadio() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    wifiConfigured = false;
    delay(500);
}

static void resetWiFiStation() {
    WiFi.disconnect(false, false);
    delay(200);
    if (!bleRunning) {
        WiFi.mode(WIFI_OFF);
        delay(300);
    }
    WiFi.mode(WIFI_STA);
    delay(300);
}

static bool connectWiFiProfile(const char* profileName, const String& ssid, const String& password, int networks, bool lockToScannedNetwork) {
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
    Serial.println(password.length());

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
        WiFi.begin(configuredSsid.c_str(), password.c_str(), channel, bssid, true);
    } else {
        WiFi.begin(configuredSsid.c_str(), password.c_str());
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
        if (wifiBlockedByBle()) {
            WiFi.disconnect(false, false);
            Serial.print("WiFi ");
            Serial.print(profileName);
            Serial.println(" connect paused by BLE");
            return false;
        }

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
    WiFi.disconnect(false, false);
    delay(300);
    return false;
}

static bool discoverServer(unsigned long timeoutMs) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    if (!discoveryUdpStarted) {
        discoveryUdpStarted = discoveryUdp.begin(DISCOVERY_PORT);
        if (!discoveryUdpStarted) {
            Serial.println("Server discovery UDP start failed");
            return activeServerBaseUrl[0] != '\0';
        }
    }

    Serial.print("Waiting for server discovery on UDP ");
    Serial.println(DISCOVERY_PORT);

    unsigned long started = millis();
    while (millis() - started < timeoutMs) {
        int packetSize = discoveryUdp.parsePacket();
        if (packetSize <= 0) {
            delay(100);
            continue;
        }

        char buffer[512];
        int length = discoveryUdp.read(buffer, sizeof(buffer) - 1);
        if (length <= 0) {
            continue;
        }

        buffer[length] = '\0';
        String payload = buffer;
        if (payload.indexOf("healthsaver-server") < 0) {
            continue;
        }

        String baseUrl = jsonStringValue(payload, "baseUrl");
        baseUrl.trim();
        if (baseUrl.length() == 0) {
            continue;
        }

        strlcpy(activeServerBaseUrl, baseUrl.c_str(), sizeof(activeServerBaseUrl));
        lastServerDiscoveryAt = millis();
        saveDiscoveredServer(baseUrl);
        Serial.print("Server discovered: ");
        Serial.println(activeServerBaseUrl);
        return true;
    }

    if (activeServerBaseUrl[0] != '\0') {
        lastServerDiscoveryAt = millis();
        Serial.print("Server discovery timeout, using cached URL: ");
        Serial.println(activeServerBaseUrl);
        return true;
    }

    Serial.println("Server discovery timeout");
    return false;
}

static bool ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        if (bleBusy()) {
            return activeServerBaseUrl[0] != '\0';
        }

        if (activeServerBaseUrl[0] != '\0' && lastServerDiscoveryAt > 0 && (long)(millis() - lastServerDiscoveryAt) < DISCOVERY_REFRESH_MS) {
            return true;
        }

        return discoverServer(activeServerBaseUrl[0] != '\0' ? 3000 : DISCOVERY_TIMEOUT_MS);
    }

    if (configuredWifiSsid.length() == 0) {
        Serial.println("WiFi is not configured");
        startProvisioningPortal();
        return false;
    }

    if (!wifiConfigured) {
        WiFi.persistent(false);
        WiFi.setSleep(bleRunning);
        WiFi.mode(WIFI_STA);
        wifiConfigured = true;
    }

    if (wifiBlockedByBle()) {
        return false;
    }

    if (lastWiFiAttemptAt > 0 && (long)(millis() - lastWiFiAttemptAt) < WIFI_RETRY_DELAY_MS) {
        return false;
    }

    lastWiFiAttemptAt = millis();
    wifiConnectInProgress = true;
    pauseBleScanForWiFi();
    WiFi.setSleep(bleRunning);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    delay(150);

    int networks = WiFi.scanNetworks(false, true);
    Serial.print("WiFi networks found: ");
    Serial.println(networks);

    if (networks < 0) {
        WiFi.scanDelete();
        WiFi.disconnect(false, false);
        resumeBleScanAfterWiFi();
        wifiConnectInProgress = false;
        Serial.println("WiFi scan failed");
        return false;
    }

    if (wifiBlockedByBle()) {
        WiFi.scanDelete();
        resumeBleScanAfterWiFi();
        wifiConnectInProgress = false;
        return false;
    }

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
        if (wifiBlockedByBle()) {
            WiFi.scanDelete();
            resumeBleScanAfterWiFi();
            wifiConnectInProgress = false;
            return false;
        }

        Serial.print("WiFi primary attempt ");
        Serial.print(attempt);
        Serial.print("/");
        Serial.println(WIFI_PRIMARY_ATTEMPTS);

        bool lockToScannedNetwork = attempt % 2 == 0;
        if (connectWiFiProfile("primary", configuredWifiSsid, configuredWifiPassword, networks, lockToScannedNetwork)) {
            WiFi.scanDelete();
            resumeBleScanAfterWiFi();
            wifiConnectInProgress = false;
            return discoverServer(activeServerBaseUrl[0] != '\0' ? 3000 : DISCOVERY_TIMEOUT_MS);
        }
    }

    WiFi.scanDelete();
    resumeBleScanAfterWiFi();
    wifiConnectInProgress = false;
    if (!bleRunning) {
        stopDisconnectedWiFiRadio();
    }
    Serial.println("WiFi all attempts failed");
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
    measurementCompletePending = false;
    receivedDataCount = 0;
}

static bool addReceivedSample(float value) {
    if (receivedDataCount >= MAX_RECEIVED_SAMPLES) {
        Serial.println("Received sample buffer full");
        return false;
    }

    receivedData[receivedDataCount++] = value;
    return true;
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

    if (activeServerBaseUrl[0] == '\0') {
        Serial.println("HTTP server URL is not available");
        return false;
    }

    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < HTTP_UPLOAD_MIN_FREE_HEAP) {
        Serial.print("HTTP POST deferred, free heap: ");
        Serial.println(freeHeap);
        return false;
    }

    HTTPClient http;
    char url[SERVER_URL_MAX_LENGTH + 40];
    snprintf(url, sizeof(url), "%s%s", activeServerBaseUrl, path.c_str());
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
    return jsonStringValue(json, key);
}

static void backupToSd(const MeasurementBatch& batch) {
#if SD_BACKUP_ENABLED
    if (batch.samples.empty()) {
        return;
    }

    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < SD_BACKUP_MIN_FREE_HEAP) {
        Serial.print("SD backup skipped, free heap: ");
        Serial.println(freeHeap);
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
    file.println(batch.sensorType);
    file.print("unit=");
    file.println(batch.unit);
    file.print("sampleRateHz=");
    file.println(batch.sampleRateHz, 2);
    file.print("schemaVersion=");
    file.println(batch.schemaVersion);
    file.print("bleAddress=");
    file.println(batch.bleAddress);

    for (float value : batch.samples) {
        file.println(value, 2);
    }

    file.close();
    Serial.printf("SD backup saved: %u samples\n", batch.samples.size());
#else
    (void)batch;
#endif
}

static String buildStartPayload(const MeasurementBatch& batch) {
    String payload;
    payload.reserve(384);
    payload += "{\"deviceId\":\"";
    payload += jsonEscape(batch.deviceId);
    payload += "\",\"sensorType\":\"";
    payload += jsonEscape(batch.sensorType);
    payload += "\",\"schemaVersion\":";
    payload += batch.schemaVersion;
    payload += ",\"sampleRateHz\":";
    payload += String(batch.sampleRateHz, 2);
    payload += ",\"unit\":\"";
    payload += jsonEscape(batch.unit);
    payload += "\",\"meta\":{\"source\":\"esp32-hub\",\"hubId\":\"";
    payload += jsonEscape(String(HUB_ID));
    payload += "\",\"bleAddress\":\"";
    payload += jsonEscape(batch.bleAddress);
    payload += "\",\"bleName\":\"";
    payload += jsonEscape(batch.bleName);
    payload += "\"}}";
    return payload;
}

static String buildChunkPayload(const MeasurementBatch& batch, const String& measurementId, int chunkIndex, int totalChunks, int startIndex, int count) {
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
        payload += String(batch.samples[startIndex + i], 2);
    }

    payload += "]}";
    return payload;
}

static String buildCompletePayload(const MeasurementBatch& batch, const String& measurementId, int totalChunks) {
    String payload;
    payload.reserve(128);
    payload += "{\"measurementId\":\"";
    payload += measurementId;
    payload += "\",\"totalChunks\":";
    payload += totalChunks;
    payload += ",\"sampleCount\":";
    payload += batch.samples.size();
    payload += "}";
    return payload;
}

static bool sendMeasurementToServer(const MeasurementBatch& batch) {
    if (batch.samples.empty()) {
        Serial.println("No samples to send");
        return false;
    }

    String startResponse;
    if (!postJson("/api/ingest/start", buildStartPayload(batch), &startResponse)) {
        return false;
    }

    String measurementId = extractJsonString(startResponse, "measurementId");
    if (measurementId.length() == 0) {
        Serial.println("MeasurementId not found in server response");
        return false;
    }

    int totalChunks = (batch.samples.size() + CHUNK_SIZE - 1) / CHUNK_SIZE;
    for (int chunkIndex = 0; chunkIndex < totalChunks; chunkIndex++) {
        int startIndex = chunkIndex * CHUNK_SIZE;
        int count = min((int)CHUNK_SIZE, (int)batch.samples.size() - startIndex);
        String chunkPayload = buildChunkPayload(batch, measurementId, chunkIndex, totalChunks, startIndex, count);

        if (!postJson("/api/ingest/chunk", chunkPayload)) {
            return false;
        }
    }

    if (!postJson("/api/ingest/complete", buildCompletePayload(batch, measurementId, totalChunks))) {
        return false;
    }

    Serial.print("Measurement sent: ");
    Serial.println(measurementId);
    return true;
}

static MeasurementBatch* createMeasurementBatch() {
    auto* batch = new MeasurementBatch();
    batch->deviceId = buildDeviceId();
    batch->sensorType = measurementSensorType;
    batch->unit = measurementUnit;
    batch->sampleRateHz = measurementSampleRateHz;
    batch->schemaVersion = measurementSchemaVersion;
    batch->bleAddress = activeBleAddress;
    batch->bleName = activeBleName;
    batch->samples.reserve(receivedDataCount);
    for (size_t i = 0; i < receivedDataCount; i++) {
        batch->samples.push_back(receivedData[i]);
    }
    return batch;
}

static void enqueueCompletedMeasurement() {
    if (receivedDataCount == 0) {
        Serial.println("No samples to enqueue");
        receivedDataCount = 0;
        measurementCompletePending = false;
        return;
    }

    MeasurementBatch* batch = createMeasurementBatch();

    if (measurementQueue == nullptr || xQueueSend(measurementQueue, &batch, 0) != pdTRUE) {
        Serial.println("Measurement queue full, upload skipped");
        backupToSd(*batch);
        delete batch;
    } else {
        Serial.print("Measurement queued: ");
        Serial.print(batch->samples.size());
        Serial.println(" samples");
        backupToSd(*batch);
    }

    receivedDataCount = 0;
    measurementCompletePending = false;
    expectedSampleCount = -1;
}

static void finishMeasurement() {
    isReceiving = false;
    measurementCompletePending = true;
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
    if (isReceiving) {
        Serial.println("BLE start ignored: measurement already active");
        return;
    }

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
            addReceivedSample(sample.toFloat());
        }

        start = end + 1;
    }
}

static void handleHs1End(const String& value) {
    String count = hs1Field(value, "sampleCount");
    if (count.length() > 0) {
        expectedSampleCount = count.toInt();
    }

    if (expectedSampleCount >= 0 && expectedSampleCount != (int)receivedDataCount) {
        Serial.print("Sample count mismatch: ");
        Serial.print(expectedSampleCount);
        Serial.print(" expected, ");
        Serial.print(receivedDataCount);
        Serial.println(" received");
    }

    finishMeasurement();
}

static void handleH1Start(const String& value) {
    if (isReceiving) {
        Serial.println("BLE start ignored: measurement already active");
        return;
    }

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
        addReceivedSample(sample.toFloat());
    }
}

static void handleH1End(const String& value) {
    if (!isReceiving) {
        Serial.println("BLE end ignored without active measurement");
        return;
    }

    String count = partAt(value, 1);
    if (count.length() > 0) {
        expectedSampleCount = count.toInt();
    }

    if (expectedSampleCount >= 0 && expectedSampleCount != (int)receivedDataCount) {
        Serial.print("Sample count mismatch: ");
        Serial.print(expectedSampleCount);
        Serial.print(" expected, ");
        Serial.print(receivedDataCount);
        Serial.println(" received");
    }

    finishMeasurement();
}

static bool startsWithBytes(const uint8_t* data, size_t length, const char* prefix) {
    size_t prefixLength = strlen(prefix);
    if (length < prefixLength) {
        return false;
    }

    for (size_t i = 0; i < prefixLength; i++) {
        if ((char)data[i] != prefix[i]) {
            return false;
        }
    }

    return true;
}

static bool handleH1DataBytes(const uint8_t* data, size_t length) {
    if (!isReceiving || !startsWithBytes(data, length, "H1D;")) {
        return false;
    }

    size_t secondSeparator = length;
    for (size_t i = 4; i < length; i++) {
        if ((char)data[i] == ';') {
            secondSeparator = i;
            break;
        }
    }

    if (secondSeparator == length || secondSeparator + 1 >= length) {
        return true;
    }

    char buffer[24];
    size_t valueLength = length - secondSeparator - 1;
    if (valueLength >= sizeof(buffer)) {
        valueLength = sizeof(buffer) - 1;
    }

    memcpy(buffer, data + secondSeparator + 1, valueLength);
    buffer[valueLength] = '\0';
    addReceivedSample(strtof(buffer, nullptr));
    return true;
}

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    if (handleH1DataBytes(pData, length)) {
        return;
    }

    String value;
    value.reserve(length);
    for (size_t i = 0; i < length; i++) {
        value += (char)pData[i];
    }

    if (BLE_VERBOSE_DATA_LOGS || !value.startsWith("H1D;")) {
        Serial.print("BLE: ");
        Serial.println(value);
    }

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
        addReceivedSample(value.substring(2).toFloat());
    }
}

class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
        connected = true;
        doScan = false;
    }

    void onDisconnect(BLEClient* pclient) {
        connected = false;
        if (!provisioningMode) {
            doScan = true;
        }
        Serial.println("BLE disconnected");
    }
};

static bool connectToSensor() {
    if (myDevice == nullptr) {
        return false;
    }

    bleConnectInProgress = true;
    activeBleAddress = myDevice->getAddress().toString().c_str();
    activeBleName = myDevice->getName().c_str();

    Serial.print("Connecting to BLE sensor ");
    Serial.println(activeBleAddress);

    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());

    if (!pClient->connect(myDevice)) {
        Serial.println("BLE connect failed");
        bleConnectInProgress = false;
        return false;
    }

    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        Serial.println("BLE service not found");
        pClient->disconnect();
        bleConnectInProgress = false;
        return false;
    }

    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
        Serial.println("BLE characteristic not found");
        pClient->disconnect();
        bleConnectInProgress = false;
        return false;
    }

    if (pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(notifyCallback);
    }

    connected = true;
    bleConnectInProgress = false;
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

    stopDisconnectedWiFiRadio();
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
    if (pClient != nullptr) {
        delete pClient;
    }
    if (myDevice != nullptr) {
        delete myDevice;
    }
    bleRunning = false;
    doConnect = false;
    doScan = false;
    connected = false;
    bleConnectInProgress = false;
    pClient = nullptr;
    pRemoteCharacteristic = nullptr;
    myDevice = nullptr;
}

static void stopWiFi() {
    if (discoveryUdpStarted) {
        discoveryUdp.stop();
        discoveryUdpStarted = false;
    }
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    wifiConfigured = false;
}

static bool readyForUpload() {
    return WiFi.status() == WL_CONNECTED && activeServerBaseUrl[0] != '\0';
}

static void closeWiFiUploadWindow() {
    stopWiFi();
    lastWiFiAttemptAt = 0;
    nextUploadRetryAt = millis() + WIFI_RETRY_DELAY_MS;
}

static bool releaseBleForUploadIfNeeded(bool forceRelease) {
    if (!bleRunning || isReceiving || measurementCompletePending || bleConnectInProgress) {
        return false;
    }

    uint32_t freeHeap = ESP.getFreeHeap();
    if (!forceRelease && freeHeap >= HTTP_UPLOAD_MIN_FREE_HEAP) {
        return false;
    }

    Serial.print("Releasing BLE for HTTP upload, free heap: ");
    Serial.println(freeHeap);
    wifiConnectInProgress = true;
    stopBle();
    delay(700);
    Serial.print("Heap after BLE release: ");
    Serial.println(ESP.getFreeHeap());
    return true;
}

static void restoreBleAfterUpload(bool releasedBle) {
    if (!releasedBle || provisioningMode) {
        wifiConnectInProgress = false;
        return;
    }

    closeWiFiUploadWindow();
    startBle();
    wifiConnectInProgress = false;
    Serial.print("Heap after BLE restart: ");
    Serial.println(ESP.getFreeHeap());
}

static void networkTask(void* parameter) {
    MeasurementBatch* pendingBatch = nullptr;
    bool waitingForConnectivity = false;

    while (true) {
        if (provisioningMode) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (pendingBatch == nullptr && measurementQueue != nullptr) {
            xQueueReceive(measurementQueue, &pendingBatch, pdMS_TO_TICKS(1000));
        }

        if (pendingBatch != nullptr) {
            if (waitingForConnectivity && readyForUpload() && nextUploadRetryAt != 0) {
                nextUploadRetryAt = 0;
                waitingForConnectivity = false;
                Serial.println("Upload retry released: WiFi/server ready");
            }

            if (nextUploadRetryAt == 0 || (long)(millis() - nextUploadRetryAt) >= 0) {
                while (bleBusy()) {
                    vTaskDelay(pdMS_TO_TICKS(200));
                }

                bool releasedBle = releaseBleForUploadIfNeeded(true);

                if (!readyForUpload()) {
                    Serial.println("Connecting WiFi for queued measurement");
                    ensureWiFi();
                }

                if (!readyForUpload()) {
                    restoreBleAfterUpload(releasedBle);
                    Serial.println("Measurement upload deferred: WiFi/server unavailable");
                    waitingForConnectivity = true;
                    nextUploadRetryAt = millis() + WIFI_RETRY_DELAY_MS;
                    Serial.print("Measurement retry in ms: ");
                    Serial.println(WIFI_RETRY_DELAY_MS);
                    continue;
                }

                bool uploaded = false;
                if (ESP.getFreeHeap() < HTTP_UPLOAD_MIN_FREE_HEAP) {
                    Serial.print("Measurement upload deferred: low heap ");
                    Serial.println(ESP.getFreeHeap());
                } else {
                    uploaded = sendMeasurementToServer(*pendingBatch);
                }
                restoreBleAfterUpload(releasedBle);

                if (!uploaded) {
                    Serial.println("Measurement upload failed");
                    Serial.println("Measurement retained for retry");
                    waitingForConnectivity = false;
                    nextUploadRetryAt = millis() + WIFI_RETRY_DELAY_MS;
                    Serial.print("Measurement retry in ms: ");
                    Serial.println(WIFI_RETRY_DELAY_MS);
                } else {
                    delete pendingBatch;
                    pendingBatch = nullptr;
                    nextUploadRetryAt = millis() + WIFI_RETRY_DELAY_MS;
                    waitingForConnectivity = false;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void startNetworkTask() {
    if (networkTaskHandle != nullptr) {
        return;
    }

    xTaskCreatePinnedToCore(networkTask, "network", 10000, nullptr, 1, &networkTaskHandle, 0);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    measurementQueue = xQueueCreate(8, sizeof(MeasurementBatch*));
    if (measurementQueue == nullptr) {
        Serial.println("Measurement queue create failed");
    }

    sdReady = SD.begin(SD_CS_PIN);
    if (sdReady) {
        Serial.println("SD ready");
    } else {
        Serial.println("SD unavailable");
    }

    bool forceProvisioning = waitForProvisioningButton();
    if (forceProvisioning) {
        enterProvisioningWithConfigClear();
    } else if (loadHubConfig()) {
        Serial.print("Configured WiFi SSID: ");
        Serial.println(configuredWifiSsid);
        WiFi.setSleep(true);
        startBle();
        startNetworkTask();
    } else {
        Serial.println("WiFi config not found");
        startProvisioningPortal();
    }
}

void loop() {
    if (provisioningMode) {
        dnsServer.processNextRequest();
        if (provisioningSaved && (long)(millis() - provisioningRestartAt) >= 0) {
            ESP.restart();
        }
        delay(10);
        return;
    }

    handleRuntimeProvisioningButton();

    if (doConnect && !wifiConnectInProgress) {
        if (connectToSensor()) {
            Serial.println("BLE sensor connected");
        } else {
            Serial.println("BLE sensor connection failed");
        }
        doConnect = false;
    }

    if (!connected && doScan && !wifiConnectInProgress) {
        BLEDevice::getScan()->start(5, false);
    }

    if (measurementCompletePending) {
        enqueueCompletedMeasurement();
    }

    delay(100);
}
