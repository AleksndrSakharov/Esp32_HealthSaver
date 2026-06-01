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

The server broadcasts its local URL over UDP port `50505`. The hub uses this
announcement instead of a manually configured `SERVER_BASE_URL`.
Keep the server bound to `0.0.0.0:5000`, otherwise the broadcast can be visible
while the HTTP endpoint remains unavailable from ESP32. Windows Firewall must
allow inbound TCP `5000`; UDP `50505` is used for discovery broadcast.

## 3) Hub WiFi provisioning

On first boot, or when WiFi credentials are missing, the hub starts a temporary
access point:

```text
SSID: ESP32HUB
Password: healthsaver
URL: http://192.168.4.1
```

Connect to this WiFi from a phone or laptop and enter the target WiFi SSID and
password. The hub stores credentials in ESP32 NVS, restarts, joins that WiFi,
listens for ASP.NET server discovery broadcasts, and then uses the discovered
URL for ingestion.

`firmware/hub/include/secrets.h` is now only needed for optional compile-time
settings such as `HUB_ID`.

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

The hub keeps BLE and WiFi active at the same time after provisioning. BLE
collects a complete measurement and queues it, while a separate network task
maintains WiFi, listens for UDP discovery, and uploads queued measurements.
