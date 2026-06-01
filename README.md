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

HealthSaver.sln                   .NET solution for server and agent
```

## Prerequisites

Install these tools on a clean Windows machine:

- [.NET 8 SDK](https://dotnet.microsoft.com/en-US/download/dotnet/8.0) for the ASP.NET server.
- [PostgreSQL for Windows](https://www.postgresql.org/download/windows/) for server metadata storage.
- [PlatformIO IDE for VS Code](https://docs.platformio.org/en/latest/integration/ide/vscode.html) for ESP32 firmware build and upload.
- [Git for Windows](https://git-scm.com/install/windows) if you clone the repository from Git.

Install a USB-UART driver if the ESP32 board does not appear as a COM port:

- [Silicon Labs CP210x driver](https://www.silabs.com/developer-tools/usb-to-uart-bridge-vcp-drivers)
- [WCH CH340/CH341 driver](https://www.wch-ic.com/downloads/CH341SER_EXE.html)

## Current Data Flow

```text
ESP32 health sensor -> BLE -> ESP32 hub -> WiFi HTTP -> ASP.NET server -> Web/Mobile UI
```

The hub parses BLE measurements, assigns `deviceId`, `sensorType`, `unit`, and
`sampleRateHz`, optionally stores an SD backup, and sends chunks to the ASP.NET
ingestion API.

On first boot the hub starts a provisioning access point named `ESP32HUB`.
After WiFi credentials are saved, the ASP.NET server is discovered automatically
through UDP broadcast, so the hub no longer needs a hardcoded server URL.
At runtime the hub keeps BLE collection and WiFi upload separated: completed
measurements are queued by the BLE side and uploaded by a network task.

## Branching

Use branches for changes, not for separate components. For example:

- `main` for stable code
- `feature/add-ecg-sensor` for a new sensor
- `feature/ai-recommendations` for AI recommendation work
- `fix/hub-ble-reconnect` for a focused bug fix

See [docs/run.md](docs/run.md) for local run instructions.
