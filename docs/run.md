# Run Guide

## 1) Database

Create a PostgreSQL database and apply [server/db/init.sql](server/db/init.sql).

## 2) Server

```bash
cd server/HealthSaver.Server
dotnet restore
dotnet run
```

## 3) Agent

```bash
cd agent/HealthSaver.Agent
dotnet run -- --port COM3 --server http://localhost:5000 --device-id hub-01 --sensor pressure --rate 1 --unit mmHg
```

## 4) UI

Open `http://localhost:5000`.
