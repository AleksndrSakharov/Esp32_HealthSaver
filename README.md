# ESP32 HealthSaver - Server Plan Implementation

This workspace now includes a full implementation for phases 0-5:

- Phase 0: Data contract and schema (see docs).
- Phase 1: Transport (HTTP + WebSocket) and serial bridge.
- Phase 2: Ingestion service with chunking and dedupe.
- Phase 3: Storage (PostgreSQL metadata + raw files).
- Phase 4: Preprocessing hooks via downsampling for UI.
- Phase 5: Web console (SPA).

See [docs/run.md](docs/run.md) for instructions.
