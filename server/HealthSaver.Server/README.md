# HealthSaver.Server

ASP.NET Core server for ingesting sensor arrays, storing raw data, and serving the web console.

## Run

```bash
cd server/HealthSaver.Server
dotnet restore
dotnet run
```

## API

- `POST /api/ingest/start`
- `POST /api/ingest/chunk`
- `POST /api/ingest/complete`
- `GET /api/measurements`
- `GET /api/measurements/{id}`
- `GET /api/measurements/{id}/series?maxPoints=1200`
- `WS /ws/live`

## Storage

Raw arrays are stored in `Data/raw` as float32 little-endian (`.f32` files). Metadata is stored in PostgreSQL.
