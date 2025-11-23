#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <SD.h>
#include <SPI.h>
#include <vector>

// === Настройки SD ===
#define SD_CS_PIN   5
// Используем стандартные SPI пины для ESP32: MOSI=23, MISO=19, SCK=18

// === Настройки BLE ===
// UUIDs должны совпадать с сервером (main.cpp)
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

static BLEUUID serviceUUID(SERVICE_UUID);
static BLEUUID charUUID(CHARACTERISTIC_UUID);

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

File logFile;
bool isReceiving = false;
std::vector<double> receivedData;

// Callback для уведомлений
static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    
    // Преобразуем данные в строку
    String value = "";
    for (int i = 0; i < length; i++) {
        value += (char)pData[i];
    }
    
    Serial.print("Received: ");
    Serial.println(value);

    if (value == "START") {
        Serial.println("Start receiving data...");
        isReceiving = true;
        receivedData.clear();
    } 
    else if (value == "END") {
        Serial.println("End of transmission.");
        isReceiving = false;
        
        // Сохраняем весь массив на SD
        logFile = SD.open("/received_data.txt", FILE_WRITE);
        if (logFile) {
            for(double val : receivedData) {
                logFile.println(val);
            }
            logFile.close();
            Serial.printf("Saved %d values to SD card.\n", receivedData.size());
        } else {
            Serial.println("Failed to open file for writing!");
        }
    } 
    else if (value.startsWith("D:")) {
        // Данные формата "D:120.55"
        String dataStr = value.substring(2); // Убираем "D:"
        double val = dataStr.toDouble();
        receivedData.push_back(val);
    }
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
  }

  void onDisconnect(BLEClient* pclient) {
    connected = false;
    Serial.println("Disconnected");
  }
};

bool connectToServer() {
    Serial.print("Forming a connection to ");
    Serial.println(myDevice->getAddress().toString().c_str());
    
    BLEClient*  pClient  = BLEDevice::createClient();
    Serial.println(" - Created client");

    pClient->setClientCallbacks(new MyClientCallback());

    // Connect to the remote BLE Server.
    pClient->connect(myDevice); 
    Serial.println(" - Connected to server");

    // Obtain a reference to the service we are after in the remote BLE server.
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
      Serial.print("Failed to find our service UUID: ");
      Serial.println(serviceUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our service");


    // Obtain a reference to the characteristic in the service of the remote BLE server.
    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(charUUID.toString().c_str());
      pClient->disconnect();
      return false;
    }
    Serial.println(" - Found our characteristic");

    // Read the value of the characteristic.
    if(pRemoteCharacteristic->canRead()) {
      std::string value = pRemoteCharacteristic->readValue();
      Serial.print("The characteristic value was: ");
      Serial.println(value.c_str());
    }

    if(pRemoteCharacteristic->canNotify())
      pRemoteCharacteristic->registerForNotify(notifyCallback);

    connected = true;
    return true;
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    // Проверяем по UUID сервиса ИЛИ по имени устройства
    if ((advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) || 
        (advertisedDevice.getName() == "ESP32_BP_Monitor")) {

      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;

    } 
  } 
}; 


void setup() {
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");
  
  // Инициализация SD
  if (!SD.begin(SD_CS_PIN)) {
      Serial.println("SD Card Mount Failed");
      return;
  }
  Serial.println("SD Card initialized.");

  BLEDevice::init("");

  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
} 


void loop() {
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
    } else {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
    doConnect = false;
  }

  if (connected) {
    // Just waiting for notifications
    delay(1000);
  }else if(doScan){
    BLEDevice::getScan()->start(0); 
  }
  
  delay(1000); 
} 