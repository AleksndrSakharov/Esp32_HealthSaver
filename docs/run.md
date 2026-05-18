# Run Guide

## Repository

This project is organized as a monorepo:

- `firmware/blood-pressure-monitor` - ESP32 blood pressure monitor
- `firmware/hub` - ESP32 BLE-to-WiFi HTTP hub
- `server/HealthSaver.Server` - ASP.NET Core server and web UI
- `agent/HealthSaver.Agent` - legacy serial bridge agent
- `docs` - contracts, run guides, reports

## 1) Database

Create a PostgreSQL database and apply `server/db/init.sql`.

## 2) Server

```bash
cd server/HealthSaver.Server
dotnet restore
dotnet run --urls http://0.0.0.0:5000
```

## 3) Hub WiFi settings

Configure `WIFI_SSID`, `WIFI_PASSWORD`, `SERVER_BASE_URL`, and optionally
`HUB_ID` in `firmware/hub/include/secrets.h` using
`firmware/hub/include/secrets.example.h` as a template, or set them as
PlatformIO build flags.
`SERVER_BASE_URL` must point to the ASP.NET server from the ESP32 network, for
example `http://192.168.1.50:5000`.

## 4) UI

Open `http://localhost:5000`.

## 5) Firmware

Build the ESP32 blood pressure monitor:

```bash
cd firmware/blood-pressure-monitor
pio run
```

Build the ESP32 hub:

```bash
cd firmware/hub
pio run
```

The normal runtime flow is:

```text
BLE sensor -> ESP32 hub -> WiFi HTTP -> ASP.NET server -> Web/Mobile UI
```
