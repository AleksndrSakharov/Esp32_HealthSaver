# HealthSaver

HealthSaver is a monorepo for the ESP32 health monitoring system.

## Repository Layout

```text
agent/
  HealthSaver.Agent/              Legacy serial bridge

docs/                             Data contract, run guide, reports

firmware/
  blood-pressure-monitor/         ESP32 blood pressure sensor firmware
  hub/                            ESP32 BLE-to-WiFi HTTP hub firmware

server/
  HealthSaver.Server/             ASP.NET Core API and web console
  db/                             PostgreSQL initialization scripts

.github/workflows/                Monorepo CI workflows
HealthSaver.sln                   .NET solution for server and agent
```

## Current Data Flow

```text
ESP32 health sensor -> BLE -> ESP32 hub -> WiFi HTTP -> ASP.NET server -> Web/Mobile UI
```

The hub parses BLE measurements, assigns `deviceId`, `sensorType`, `unit`, and
`sampleRateHz`, optionally stores an SD backup, and sends chunks to the ASP.NET
ingestion API.

## Branching

Use branches for changes, not for separate components. For example:

- `main` for stable code
- `feature/add-ecg-sensor` for a new sensor
- `feature/ai-recommendations` for AI recommendation work
- `fix/hub-ble-reconnect` for a focused bug fix

See [docs/run.md](docs/run.md) for local run instructions.
