# HealthSaver.Agent

Serial bridge that reads the hub SD output and pushes it to the ASP.NET server.

## Usage

```bash
cd agent/HealthSaver.Agent
dotnet run -- --port COM3 --server http://localhost:5000 --device-id hub-01 --sensor pressure --rate 1 --unit mmHg
```

## Notes

- Uses the existing hub command `READ_SD` and expects `---START_FILE---` / `---END_FILE---` markers.
- Sends data as float32 little-endian chunks in base64.
