# Firmware

This folder contains ESP32 firmware projects managed by PlatformIO.

## Projects

- `blood-pressure-monitor` - reads the pressure sensor and sends measurement history over BLE.
- `hub` - receives BLE data from sensor devices, stores an optional SD backup, and sends chunks to ASP.NET over WiFi HTTP.

## Build

```bash
cd firmware/blood-pressure-monitor
pio run

cd ../hub
pio run
```

Do not commit generated PlatformIO output from `.pio/`, local IDE settings, or real WiFi/server credentials.

The hub stores WiFi credentials in ESP32 NVS after provisioning through the
temporary `ESP32HUB` access point. `secrets.h` is only needed for optional
compile-time values such as `HUB_ID`.
