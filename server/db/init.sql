CREATE TABLE IF NOT EXISTS devices (
    id SERIAL PRIMARY KEY,
    device_id VARCHAR(100) NOT NULL UNIQUE,
    name VARCHAR(100),
    created_utc TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    last_seen_utc TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS sensor_types (
    id SERIAL PRIMARY KEY,
    code VARCHAR(50) NOT NULL,
    unit VARCHAR(20),
    axes VARCHAR(20),
    schema_version INT NOT NULL DEFAULT 1,
    UNIQUE (code, schema_version)
);

CREATE TABLE IF NOT EXISTS measurements (
    id UUID PRIMARY KEY,
    device_id INT NOT NULL REFERENCES devices(id),
    sensor_type_id INT NOT NULL REFERENCES sensor_types(id),
    status VARCHAR(20) NOT NULL,
    start_time_utc TIMESTAMPTZ NOT NULL,
    completed_utc TIMESTAMPTZ,
    sample_rate_hz DOUBLE PRECISION NOT NULL,
    unit VARCHAR(20) NOT NULL,
    sample_count INT NOT NULL DEFAULT 0,
    chunk_count INT NOT NULL DEFAULT 0,
    raw_file_path VARCHAR(300) NOT NULL,
    raw_sha256 VARCHAR(64),
    meta_json TEXT,
    created_utc TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS measurements_start_time_idx ON measurements(start_time_utc);

CREATE TABLE IF NOT EXISTS measurement_chunks (
    id SERIAL PRIMARY KEY,
    measurement_id UUID NOT NULL REFERENCES measurements(id),
    chunk_index INT NOT NULL,
    size_bytes INT NOT NULL,
    sha256 VARCHAR(64) NOT NULL,
    received_utc TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    UNIQUE (measurement_id, chunk_index)
);

-- Optional TimescaleDB table for downsampled points
-- CREATE EXTENSION IF NOT EXISTS timescaledb;
-- CREATE TABLE IF NOT EXISTS measurement_samples (
--     measurement_id UUID NOT NULL,
--     sample_index INT NOT NULL,
--     value REAL NOT NULL,
--     recorded_utc TIMESTAMPTZ NOT NULL DEFAULT NOW()
-- );
-- SELECT create_hypertable('measurement_samples', 'recorded_utc', if_not_exists => TRUE);
