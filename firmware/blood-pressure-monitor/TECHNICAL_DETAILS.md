# Technical Implementation Details

## Overview

This document provides detailed technical information about the blood pressure monitoring system implementation, focusing on data acquisition, storage, and DMA optimization.

## System Architecture

```
┌─────────────────┐
│  Pressure       │
│  Sensor         │──────> GPIO 34 (ADC)
│  XGZP6847A      │
└─────────────────┘

┌─────────────────┐
│  ESP32          │
│  ┌───────────┐  │
│  │   ADC     │  │ → Read every 10ms
│  └───────────┘  │
│  ┌───────────┐  │
│  │State      │  │ → Peak Detection
│  │Machine    │  │
│  └───────────┘  │
│  ┌───────────┐  │
│  │Data       │  │ → 512-byte buffer
│  │Buffer     │  │
│  └───────────┘  │
│  ┌───────────┐  │
│  │SPI + DMA  │  │ → SD Card writes
│  └───────────┘  │
└────────┬────────┘
         │
         v
┌─────────────────┐
│  microSD Card   │
│  Module         │
│  (SPI)          │
└─────────────────┘
```

## ADC Configuration

### Hardware Setup
- **Pin**: GPIO 34 (ADC1_CH6)
- **Resolution**: 12-bit (0-4095)
- **Attenuation**: 11dB (full 0-3.3V range)
- **Sampling Rate**: 100 Hz (10ms intervals)

### Code Implementation
```cpp
analogReadResolution(12);           // 12-bit resolution
analogSetAttenuation(ADC_11db);     // 0-3.3V range
uint16_t Padc = analogRead(SENSOR_PIN);
```

### Calibration
The system applies a zero-offset calibration:
```cpp
Padc = analogRead(SENSOR_PIN) - SENSOR_ZERO_OFFSET;
```

To calibrate for your sensor:
1. Ensure no pressure is applied
2. Read ADC value multiple times
3. Calculate average → This is your SENSOR_ZERO_OFFSET

## State Machine Implementation

### State Diagram
```
     ┌─────────┐
     │  IDLE   │
     └────┬────┘
          │ Pressure > THRESHOLD
          v
     ┌─────────┐
     │ RISING  │ ← Monitor for peak
     └────┬────┘
          │ 3 consecutive decreases
          v
     ┌──────────┐
     │RECORDING │ ← Log to SD card
     └────┬─────┘
          │ Pressure < THRESHOLD/2
          v
     ┌─────────┐
     │  IDLE   │
     └─────────┘
```

### State Transitions

#### IDLE → RISING
- **Trigger**: `Padc > PRESSURE_THRESHOLD`
- **Action**: Initialize peak tracking
- **Variables**: Reset `peakPadc`, `lastPadc`

#### RISING → RECORDING
- **Trigger**: 3 consecutive pressure decreases
- **Action**: Create new file, start buffering
- **Variables**: Set `measurementStartTime`, open `dataFile`

#### RECORDING → IDLE
- **Trigger**: `Padc < PRESSURE_THRESHOLD / 2`
- **Action**: Flush buffer, close file
- **Variables**: Reset all measurement variables

### Peak Detection Algorithm

```cpp
consecutiveDecreaseCount++;

if (consecutiveDecreaseCount >= 3) {
    // Confirmed: pressure is decreasing
    currentState = RECORDING;
}
```

**Why 3 consecutive decreases?**
- Filters out sensor noise
- Prevents false triggers from fluctuations
- Ensures true deflation has begun

## Data Buffering and DMA Optimization

### Buffer Management

#### Buffer Structure
```cpp
char dataBuffer[BUFFER_SIZE];  // 512 bytes
int bufferIndex = 0;           // Current position
```

#### Write Flow
```
Data Point → Format as CSV → Add to Buffer → Buffer Full? → Flush to SD
    ↓                                              |
    |                                              No
    +----------------------------------------------+
                                                   ↓
                                            Continue buffering
```

### DMA Implementation

While ESP32's SD library doesn't expose direct DMA control, we optimize for DMA-like behavior through:

1. **Large Block Writes**
   ```cpp
   dataFile.write((const uint8_t*)dataBuffer, bufferIndex);
   ```
   - Writes 512-byte blocks instead of individual lines
   - ESP32 SPI controller uses DMA for large transfers automatically
   - Reduces CPU overhead

2. **Immediate Flush**
   ```cpp
   dataFile.flush();
   ```
   - Forces data to physical media
   - Ensures data persistence even if power loss occurs
   - Critical for medical data integrity

3. **Buffer Size Optimization**
   - 512 bytes aligns with SD card sector size (512 bytes)
   - Maximizes write efficiency
   - Reduces wear on SD card

### Write Performance Analysis

| Method | Writes per Second | CPU Usage | Reliability |
|--------|-------------------|-----------|-------------|
| Individual writes | ~20 | High | Medium |
| Buffered (no flush) | ~100 | Low | Low |
| **Buffered + flush** | **~90** | **Medium** | **High** |

## File System Organization

### File Naming Convention
```
/bp_XXXXXXXX.txt
    └─ Unix timestamp / 1000
```

**Advantages:**
- Unique filenames prevent overwrites
- Chronologically sortable
- Simple implementation (no RTC required)

### File Format

#### CSV Structure
```
Header Line 1: Title
Header Line 2: Column names
Data Lines: timestamp,pressure
...
Footer Lines: Summary statistics
```

**Why CSV?**
- Universal compatibility
- Human-readable
- Easy to parse in any language
- Small file size

## SD Card Interface

### SPI Configuration

```cpp
SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
```

| Signal | GPIO | Function |
|--------|------|----------|
| CS | 5 | Chip Select (SS) |
| MOSI | 23 | Master Out, Slave In |
| MISO | 19 | Master In, Slave Out |
| SCK | 18 | Serial Clock |

### SPI Speed
- Default: 4 MHz (conservative)
- Can be increased to 20-40 MHz for faster writes
- Lower speeds are more reliable with long wires

### Error Handling

```cpp
if (!SD.begin(SD_CS_PIN)) {
    Serial.println("FAILED!");
    // System continues without SD logging
    return;
}
```

**Graceful Degradation:**
- System doesn't crash if SD card fails
- User is informed via serial output
- Monitoring continues (useful for debugging)

## Memory Management

### RAM Usage

| Component | Size | Location |
|-----------|------|----------|
| Data Buffer | 512 bytes | SRAM |
| File Object | ~100 bytes | SRAM |
| State Variables | ~20 bytes | SRAM |
| **Total** | **~632 bytes** | **SRAM** |

ESP32 has 520 KB SRAM, so this is only 0.12% usage - very efficient!

### Flash Usage
- Program size: ~200-300 KB
- ESP32 has 4 MB flash (typical)
- Plenty of room for future features

## Timing Analysis

### Sampling Interval: 10ms

```
One measurement cycle:
├─ ADC Read: ~100 µs
├─ Calculations: ~50 µs
├─ Buffer Write: ~20 µs
├─ Serial Print: ~500 µs (only when recording)
└─ Delay: 9.33 ms (remainder of 10ms)
```

### File Write Timing

**Buffered Write (every 512 bytes):**
- Frequency: Every ~26 samples (260ms)
- Duration: ~5-10ms per write
- Impact: Minimal on sampling rate

**Flush Operation:**
- Duration: ~15-30ms
- Frequency: Every buffer flush + end of measurement
- Impact: May delay one sample slightly

## Data Integrity Measures

### 1. Immediate Flush
```cpp
dataFile.flush();
```
Ensures data is physically written to SD card, not just cached.

### 2. File Close on Completion
```cpp
dataFile.close();
```
Properly closes file handle and writes filesystem metadata.

### 3. Buffer Overflow Protection
```cpp
if (bufferIndex + lineLength >= BUFFER_SIZE - 1) {
    flushBufferToFile();
}
```
Prevents buffer overrun.

### 4. Null Checks
```cpp
if (!dataFile) return;
```
Prevents crashes if file operations fail.

## Power Consumption

### Typical Current Draw
- ESP32 Active: ~80-160 mA
- SD Card Write: +50-100 mA (peaks)
- Pressure Sensor: ~5-10 mA
- **Total**: ~150-270 mA

### Battery Life Estimation
With 2000 mAh battery:
- Active measurement (30s): ~2-4 mAh
- Standby (1 hour): ~80-160 mAh
- **~20-25 measurements per charge** (with 1hr between)

## Performance Optimization Tips

### 1. Increase Buffer Size
```cpp
#define BUFFER_SIZE 1024  // Double the buffer
```
- Fewer SD writes
- Slightly more RAM usage

### 2. Reduce Flush Frequency
```cpp
// Only flush every 2 buffers
if (flushCounter++ % 2 == 0) {
    dataFile.flush();
}
```
- Faster writes
- Lower data integrity

### 3. Use SDIO Instead of SPI
```cpp
// Use SD_MMC library for 4-bit SDIO
// Much faster: up to 40 MB/s vs 4 MB/s
```
- Requires more GPIO pins
- Better for high-speed logging

### 4. Reduce Sample Rate
```cpp
#define SAMPLE_RATE_MS 20  // 50 Hz instead of 100 Hz
```
- Lower CPU usage
- Less data to store
- Still adequate for BP monitoring

## Debugging Tips

### 1. Monitor Serial Output
```cpp
Serial.print("Recording: Time=");
Serial.print(currentMeasurementTime);
```

### 2. Test Without SD Card
```cpp
#define DEBUG_MODE
#ifdef DEBUG_MODE
    // Skip SD operations
#endif
```

### 3. Add Diagnostic Counters
```cpp
static int writeCount = 0;
writeCount++;
Serial.printf("Writes: %d\n", writeCount);
```

### 4. Check File Size
```cpp
Serial.printf("File size: %d bytes\n", dataFile.size());
```

## Future Enhancement Possibilities

### 1. Real-Time Clock (RTC)
Add DS3231 module for actual timestamps:
```cpp
DateTime now = rtc.now();
sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d", 
        now.year(), now.month(), now.day(),
        now.hour(), now.minute(), now.second());
```

### 2. Blood Pressure Calculation
Implement oscillometric algorithm:
```cpp
// Detect oscillations during deflation
// Calculate systolic/diastolic pressure
// More complex signal processing required
```

### 3. WiFi Data Upload
```cpp
#include <WiFi.h>
#include <HTTPClient.h>

// Upload data to cloud server
WiFiClient client;
client.connect("server.com", 80);
```

### 4. Display Integration
```cpp
#include <Adafruit_SSD1306.h>

// Show real-time pressure on OLED
display.printf("BP: %d/%d", systolic, diastolic);
```

## Compliance and Safety

### Medical Device Regulations
⚠️ **This device is NOT certified for medical use.**

To make it medical-grade:
- FDA 510(k) clearance (USA)
- CE marking (Europe)
- ISO 13485 compliance
- Clinical validation studies
- Extensive testing and documentation

### Safety Features to Add
1. Pressure release valve (hardware)
2. Maximum pressure limit (software)
3. Timeout protection (automatic deflation)
4. Error logging and alerts
5. Calibration verification

## References

- [ESP32 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
- [XGZP6847A Datasheet](http://www.cfsensor.com/static/upload/file/20201113/XGZP6847A%20Pressure%20Sensor%20Module%20V2.4.pdf)
- [SD Card Specification](https://www.sdcard.org/downloads/pls/)
- [Oscillometric Blood Pressure Measurement](https://en.wikipedia.org/wiki/Sphygmomanometer#Oscillometric)

## License

This project is provided for educational purposes only.

---

**Last Updated**: October 2025
**Version**: 1.0

