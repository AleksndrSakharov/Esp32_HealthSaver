# HealthSaver Data Contract (Phase 0)

## BLE Protocol HS1

```text
H1S;p;mmHg;1;1
H1D;0;120.55
H1D;1;118.30
H1E;2
```

Compact frames are used by default because ESP32 BLE notifications can be
limited to 20 bytes when MTU negotiation is unavailable.

```text
HS1;START;type=pressure;unit=mmHg;rate=1;schema=1
HS1;CHUNK;index=0;values=120.55,118.30,115.20
HS1;END;sampleCount=3
```

The sensor sends metadata in `START`, one or more numeric chunks in `CHUNK`,
and the final sample count in `END`. The ESP32 hub parses this protocol and
maps it to the HTTP ingestion contract below.

## Measurement Start

```json
{
  "deviceId": "hub-01",
  "sensorType": "pressure",
  "schemaVersion": 1,
  "sampleRateHz": 1,
  "unit": "mmHg",
  "startTimeUtc": "2026-02-05T12:00:00Z",
  "measurementId": "optional-guid",
  "meta": {
    "fw": "1.0.0",
    "sensor": "XGZP6847A"
  }
}
```

## Chunk

```json
{
  "measurementId": "guid",
  "chunkIndex": 0,
  "totalChunks": 12,
  "encoding": "f32le-base64",
  "dataBase64": "..."
}
```

## Complete

```json
{
  "measurementId": "guid",
  "totalChunks": 12,
  "sampleCount": 6000
}
```

## Sensor Types

- `pressure` (unit: mmHg)
- `ecg` (unit: mV)
- `accel` (unit: m/s^2, axes: x,y,z)
