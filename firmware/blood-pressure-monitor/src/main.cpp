#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "driver/i2s.h"
#include <vector>


#define SENSOR_PIN 34
#define SENSOR_KOEF 0.14f
#define SENSOR_ZERO_OFFSET 271


#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define HS1_SENSOR_TYPE "pressure"
#define HS1_SENSOR_CODE "p"
#define HS1_UNIT "mmHg"
#define HS1_SAMPLE_RATE_HZ 1
#define HS1_SCHEMA_VERSION 1

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
bool pendingTransmission = false;

void sendHistory();

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Device connected");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Device disconnected");
    }
};


#define I2S_PORT I2S_NUM_0
#define SAMPLE_COUNT 256
#define SAMPLES_PER_SECOND 1000
#define PRESSURE_THRESHOLD 70.0f
#define PRESSURE_DROP_THRESHOLD 15.0f

uint16_t adcBuffer[SAMPLE_COUNT];
float processedData[SAMPLE_COUNT];


std::vector<double> pressureHistory;


float samplesSum = 0.0f;
int samplesCount = 0;
bool isRecording = false;
float maxPressure = 0.0f;
float lastAveragePressure = 0.0f;
int recordedValuesCount = 0;


bool isCheckingDrop = false;
float checkStartPressure = 0.0f;
int checkCounter = 0;
float bufferValues[4];
int bufferIndex = 0;

void initI2S()
{
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN),
        .sample_rate = 1000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S_LSB,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = SAMPLE_COUNT,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    esp_err_t err;

    err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("I2S driver install failed: %d\n", err);
        while (true) delay(100);
    }

    err = i2s_set_adc_mode(ADC_UNIT_1, ADC1_CHANNEL_6);
    if (err != ESP_OK) {
        Serial.printf("I2S set ADC mode failed: %d\n", err);
        while (true) delay(100);
    }

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);

    err = i2s_adc_enable(I2S_PORT);
    if (err != ESP_OK) {
        Serial.printf("I2S ADC enable failed: %d\n", err);
        while (true) delay(100);
    }
}

void sendHistory() {
    if (deviceConnected) {
        Serial.println("Starting transmission...");
        pendingTransmission = false;

        String startMessage = "H1S;";
        startMessage += HS1_SENSOR_CODE;
        startMessage += ";";
        startMessage += HS1_UNIT;
        startMessage += ";";
        startMessage += HS1_SAMPLE_RATE_HZ;
        startMessage += ";";
        startMessage += HS1_SCHEMA_VERSION;
        pCharacteristic->setValue(startMessage.c_str());
        pCharacteristic->notify();
        delay(100);

        for (size_t i = 0; i < pressureHistory.size(); i++) {
            String chunk = "H1D;";
            chunk += i;
            chunk += ";";
            chunk += String(pressureHistory[i], 2);
            pCharacteristic->setValue(chunk.c_str());
            pCharacteristic->notify();
            delay(20);
        }

        String endMessage = "H1E;";
        endMessage += pressureHistory.size();
        pCharacteristic->setValue(endMessage.c_str());
        pCharacteristic->notify();
        Serial.println("Transmission complete.");
    } else {
        Serial.println("Device not connected, cannot send history.");
        pendingTransmission = true;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Initializing...");


    BLEDevice::init("ESP32_BP_Monitor");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);

    pCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_READ   |
                        BLECharacteristic::PROPERTY_WRITE  |
                        BLECharacteristic::PROPERTY_NOTIFY |
                        BLECharacteristic::PROPERTY_INDICATE
                      );

    pCharacteristic->addDescriptor(new BLE2902());

    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x0);
    BLEDevice::startAdvertising();
    Serial.println("BLE initialized! Waiting for client connection...");


    initI2S();
    Serial.println("I2S DMA initialized!");
    Serial.println("Waiting for pressure rise and drop to start sending...");
}

void loop() {
    size_t bytesRead = 0;


    esp_err_t result = i2s_read(I2S_PORT, (void *)adcBuffer, sizeof(adcBuffer), &bytesRead, portMAX_DELAY);

    if (result == ESP_OK && bytesRead > 0) {
        int samples = bytesRead / sizeof(uint16_t);


        for (int i = 0; i < samples; i++) {
            uint16_t Padc = adcBuffer[i] & 0x0FFF;

            if (Padc > SENSOR_ZERO_OFFSET)
                Padc -= SENSOR_ZERO_OFFSET;
            else
                Padc = 0;

            processedData[i] = Padc * SENSOR_KOEF;


            samplesSum += processedData[i];
            samplesCount++;
        }


        if (samplesCount >= SAMPLES_PER_SECOND) {
            float averagePressure = samplesSum / samplesCount;


            if (averagePressure > maxPressure) {
                maxPressure = averagePressure;
            }


            if (!isRecording && !isCheckingDrop) {

                if (maxPressure >= PRESSURE_THRESHOLD &&
                    averagePressure < (maxPressure - PRESSURE_DROP_THRESHOLD)) {

                    isCheckingDrop = true;
                    checkStartPressure = averagePressure;
                    checkCounter = 0;
                    bufferIndex = 0;
                    Serial.println("=== Checking pressure drop... ===");
                    Serial.printf("Start pressure: %.2f mmHg\n", checkStartPressure);
                }
            }


            if (isCheckingDrop && !isRecording) {

                if (bufferIndex < 4) {
                    bufferValues[bufferIndex] = averagePressure;
                    bufferIndex++;
                }
                checkCounter++;

                Serial.printf("Check %d/4: %.2f mmHg\n", checkCounter, averagePressure);


                if (checkCounter >= 4) {
                    if (averagePressure < checkStartPressure) {

                        isRecording = true;
                        isCheckingDrop = false;

                        Serial.println("=== SENDING STARTED (Confirmed drop) ===");
                        Serial.printf("Max pressure: %.2f mmHg\n", maxPressure);
                        Serial.printf("Drop confirmed: %.2f -> %.2f mmHg\n", checkStartPressure, averagePressure);


                        pressureHistory.clear();
                        for (int i = 0; i < bufferIndex; i++) {
                            pressureHistory.push_back((double)bufferValues[i]);
                        }
                        Serial.printf("Buffered %d values to history\n", bufferIndex);
                    } else {

                        isCheckingDrop = false;
                        checkCounter = 0;
                        bufferIndex = 0;
                        Serial.println("=== Check cancelled (No continuous drop) ===");
                    }
                }
            }


            if (isRecording) {
                pressureHistory.push_back((double)averagePressure);
                recordedValuesCount++;
                Serial.printf("Recorded: %.2f mmHg [%d]\n", averagePressure, pressureHistory.size());


                if (averagePressure < 40.0f) {
                    isRecording = false;
                    isCheckingDrop = false;
                    maxPressure = 0.0f;
                    checkCounter = 0;
                    bufferIndex = 0;
                    Serial.println("=== RECORDING STOPPED (Low pressure) ===");

                    Serial.println("\n--- Sending Data via BLE ---");
                    sendHistory();
                    Serial.println("---------------------------\n");

                    recordedValuesCount = 0;
                }
            } else if (!isCheckingDrop) {

                Serial.printf("Monitoring... Current: %.2f mmHg, Max: %.2f mmHg\n",
                              averagePressure, maxPressure);
            }

            lastAveragePressure = averagePressure;


            samplesSum = 0.0f;
            samplesCount = 0;
        }
    }


    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        pServer->startAdvertising();
        Serial.println("Start advertising");
        oldDeviceConnected = deviceConnected;
    }

    if (deviceConnected && !oldDeviceConnected) {

        oldDeviceConnected = deviceConnected;
    }

    if (deviceConnected && pendingTransmission && !pressureHistory.empty()) {
        Serial.println("Sending pending measurement...");
        sendHistory();
    }
}
