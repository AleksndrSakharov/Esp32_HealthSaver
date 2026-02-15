# HealthSaver Data Contract (Phase 0)

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
