# Blood Pressure Monitor with SD Card Logging

This is an ESP32-based blood pressure monitoring system that automatically detects and records pressure measurements to a microSD card.

## Hardware Requirements

### Components
- **ESP32 Development Board**
- **XGZP6847A Pressure Sensor** (connected to GPIO 34)
- **MicroSD Card Adapter Module**
- **MicroSD Card** (any size, FAT32 formatted)

### Wiring Connections

#### Pressure Sensor
- Signal Output → GPIO 34 (ADC1_CH6)
- VCC → 3.3V
- GND → GND

#### MicroSD Card Adapter
- CS (Chip Select) → GPIO 5
- MOSI (Master Out) → GPIO 23
- MISO (Master In) → GPIO 19
- SCK (Clock) → GPIO 18
- VCC → 5V (or 3.3V depending on your module)
- GND → GND

## How It Works

### Measurement Process

1. **IDLE State**: System waits for pressure to exceed the threshold (default: 100 ADC units)
2. **RISING State**: System monitors pressure increase and detects the peak
3. **RECORDING State**: When pressure starts decreasing (after 3 consecutive decreases), the system:
   - Creates a new measurement file
   - Records timestamp and pressure data every 10ms
   - Saves data to microSD card using buffered writes
   - Stops recording when pressure drops below threshold/2

### Peak Detection Algorithm

The system uses a state machine with consecutive decrease detection:
- Tracks peak pressure value
- Requires 3 consecutive pressure decreases to confirm the trend
- This prevents false triggers from sensor noise

### Data Storage

#### File Format
- **Filename**: `/bp_XXXXXXXX.txt` (timestamp-based)
- **Format**: CSV (Comma-Separated Values)

#### File Structure
```
Blood Pressure Measurement Data
Time(ms),Pressure(ADC)
0,1234
10,1230
20,1225
...

Peak Pressure: 1234
Total Duration: 5000 ms
```

### DMA Optimization

The system uses buffered writes to optimize SD card performance:
- Data is accumulated in a 512-byte buffer
- Buffer is flushed to SD card when full or at measurement end
- `flush()` is called to ensure immediate write to physical media
- This approach minimizes SD card write operations and improves reliability

## Configuration Parameters

You can adjust these parameters in `main.cpp`:

```cpp
#define SENSOR_ZERO_OFFSET 271      // ADC value at zero pressure
#define PRESSURE_THRESHOLD 100       // Minimum pressure to start recording
#define SAMPLE_RATE_MS 10            // Sampling interval (milliseconds)
#define BUFFER_SIZE 512              // DMA buffer size (bytes)
```

### Pin Configuration

If you need to use different pins, modify these in `main.cpp`:

```cpp
#define SENSOR_PIN 34       // ADC input pin
#define SD_CS_PIN 5         // SD Card Chip Select
#define SD_MOSI_PIN 23      // SD Card MOSI
#define SD_MISO_PIN 19      // SD Card MISO
#define SD_SCK_PIN 18       // SD Card Clock
```

## Building and Uploading

### Using PlatformIO CLI
```bash
cd blood_pressure_monitor
pio run -t upload
pio device monitor
```

### Using PlatformIO IDE
1. Open the `blood_pressure_monitor` folder in VS Code
2. Click "Upload" button in PlatformIO toolbar
3. Click "Serial Monitor" to view output

## Usage Instructions

1. **Power on** the ESP32 with SD card inserted
2. **Wait** for "System Ready" message in serial monitor
3. **Inflate** the blood pressure cuff
4. The system will **automatically detect** when pressure starts decreasing
5. **Wait** for "Measurement complete" message
6. Data is saved to SD card in `/bp_XXXXXXXX.txt`

## Serial Monitor Output

```
=== Blood Pressure Monitor with SD Card Logging ===
Initializing SD card...OK
SD Card Type: SDHC
SD Card Size: 7580MB
SD Card initialized successfully!
System Ready. Waiting for measurement...
Inflate cuff to start measurement.
Pressure detected. Monitoring for peak...
Peak detected: 1234 - Starting recording...
Created file: /bp_12345.txt
Recording: Time=0ms, Pressure=1234
Recording: Time=10ms, Pressure=1230
Recording: Time=20ms, Pressure=1225
...
Measurement complete. Ready for next measurement.
Data saved to: /bp_12345.txt
Peak pressure: 1234
Duration: 5.0 seconds
--------------------------------------
```

## Troubleshooting

### SD Card Not Detected
- Check wiring connections
- Ensure SD card is formatted as FAT32
- Try a different SD card
- Verify the SD module is getting proper power (5V or 3.3V depending on module)

### No Measurements Being Recorded
- Check that `PRESSURE_THRESHOLD` is appropriate for your sensor readings
- Monitor serial output to see current ADC values
- Adjust `SENSOR_ZERO_OFFSET` if needed

### Incorrect Pressure Values
- Calibrate `SENSOR_ZERO_OFFSET` by reading ADC value at zero pressure
- Check sensor connections
- Verify sensor power supply is stable

## Data Analysis

The recorded CSV files can be easily imported into:
- **Excel/LibreOffice Calc**: For manual analysis
- **Python (pandas)**: For automated processing
- **MATLAB**: For signal processing and analysis
- **R**: For statistical analysis

Example Python code to read the data:
```python
import pandas as pd
data = pd.read_csv('/path/to/bp_12345.txt', skiprows=1)
print(data.head())
```

## Technical Specifications

- **ADC Resolution**: 12-bit (0-4095)
- **Sampling Rate**: 100 Hz (10ms interval)
- **Buffer Size**: 512 bytes
- **File System**: FAT32
- **Data Format**: CSV (text)
- **Timestamp Resolution**: 1 millisecond

## Safety Notice

⚠️ **Important**: This device is for educational and experimental purposes only. It is NOT a certified medical device and should NOT be used for actual medical diagnosis or treatment. Always consult with qualified healthcare professionals for medical advice.

## License

This project is provided as-is for educational purposes.

## Future Enhancements

Possible improvements:
- Add RTC module for real timestamps (date/time)
- Implement oscillometric algorithm for BP calculation
- Add LCD display for real-time readings
- WiFi data transmission
- Battery power support
- Multiple measurement session storage
- Data compression for longer recording periods

