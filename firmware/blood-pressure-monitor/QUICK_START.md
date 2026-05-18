# Quick Start Guide

## 1. Hardware Setup (5 minutes)

### Connect Pressure Sensor
```
XGZP6847A → ESP32
├─ Signal → GPIO 34
├─ VCC    → 3.3V
└─ GND    → GND
```

### Connect SD Card Module
```
SD Card Module → ESP32
├─ CS   → GPIO 5
├─ MOSI → GPIO 23
├─ MISO → GPIO 19
├─ SCK  → GPIO 18
├─ VCC  → 5V (or 3.3V)
└─ GND  → GND
```

### Insert SD Card
- Format as FAT32
- Any size works (even 512 MB)

## 2. Software Setup (3 minutes)

### Install PlatformIO
```bash
# If using VS Code:
# Install "PlatformIO IDE" extension

# If using CLI:
pip install platformio
```

### Upload Code
```bash
cd blood_pressure_monitor
pio run -t upload
```

## 3. First Test (2 minutes)

### Open Serial Monitor
```bash
pio device monitor
```

### Expected Output
```
=== Blood Pressure Monitor with SD Card Logging ===
Initializing SD card...OK
SD Card Type: SDHC
SD Card Size: 7580MB
System Ready. Waiting for measurement...
```

## 4. Take a Measurement

1. **Inflate** the cuff
2. **Wait** for "Pressure detected" message
3. System **automatically** detects peak and records
4. **Wait** for "Measurement complete"
5. **Check** SD card for `/bp_XXXXXXX.txt` file

## 5. View Results

### Option 1: Read File Directly
- Remove SD card
- Open `.txt` file in any text editor

### Option 2: Use Python Script
```bash
pip install -r requirements.txt
python analyze_data.py bp_12345.txt
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| SD card not detected | Check wiring, try different card |
| No measurements | Lower `PRESSURE_THRESHOLD` in code |
| Wrong pressure values | Adjust `SENSOR_ZERO_OFFSET` |
| Serial not working | Set baud rate to 115200 |

## Configuration

Edit these values in `main.cpp`:

```cpp
#define PRESSURE_THRESHOLD 100  // Lower = more sensitive
#define SAMPLE_RATE_MS 10       // Sampling speed
#define SENSOR_ZERO_OFFSET 271  // Calibration value
```

## That's It!

You're ready to start monitoring blood pressure data. See `README.md` for detailed documentation.

---

**Need Help?** Check `TECHNICAL_DETAILS.md` for in-depth information.

