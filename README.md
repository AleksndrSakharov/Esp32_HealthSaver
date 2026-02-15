# ESP32 HealthSaver - Server Plan Implementation

This workspace now includes a full implementation for phases 0-5:

- Phase 0: Data contract and schema (see docs).
- Phase 1: Transport (HTTP + WebSocket) and serial bridge.
- Phase 2: Ingestion service with chunking and dedupe.
- Phase 3: Storage (PostgreSQL metadata + raw files).
- Phase 4: Preprocessing hooks via downsampling for UI.
- Phase 5: Web console (SPA).

See [docs/run.md](docs/run.md) for instructions.

## Быстрые переходы к веткам устройств

GitHub не умеет автоматически показывать содержимое других веток при клике по папке в `main`.
Вместо этого используйте прямые ссылки на нужные ветки:

- [HUB/Esp32_HealthSaver/Esp32_hub](../../tree/Esp32_hub/HUB/Esp32_HealthSaver/Esp32_hub)
- [blood pressure monitor/Esp32_HealthSaver/blood_pressure_monitor](../../tree/Esp32_blood_pressure_monitor/blood%20pressure%20monitor/Esp32_HealthSaver/blood_pressure_monitor)
