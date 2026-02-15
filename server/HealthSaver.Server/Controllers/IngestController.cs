using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using HealthSaver.Server.Contracts;
using HealthSaver.Server.Infrastructure;
using HealthSaver.Server.Models;
using Microsoft.AspNetCore.Mvc;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;

namespace HealthSaver.Server.Controllers;

[ApiController]
[Route("api/ingest")]
public sealed class IngestController : ControllerBase
{
    private readonly AppDbContext _db;
    private readonly RawStorageService _storage;
    private readonly LiveHub _liveHub;
    private readonly IngestOptions _options;

    public IngestController(AppDbContext db, RawStorageService storage, LiveHub liveHub, IOptions<IngestOptions> options)
    {
        _db = db;
        _storage = storage;
        _liveHub = liveHub;
        _options = options.Value;
    }

    [HttpPost("start")]
    public async Task<ActionResult<MeasurementStartResponse>> Start([FromBody] MeasurementStartRequest request, CancellationToken ct)
    {
        if (string.IsNullOrWhiteSpace(request.DeviceId) || string.IsNullOrWhiteSpace(request.SensorType))
        {
            return BadRequest("DeviceId and SensorType are required.");
        }

        var device = await _db.Devices.FirstOrDefaultAsync(d => d.DeviceId == request.DeviceId, ct);
        if (device == null)
        {
            device = new Device
            {
                DeviceId = request.DeviceId,
                LastSeenUtc = DateTime.UtcNow
            };
            _db.Devices.Add(device);
        }
        else
        {
            device.LastSeenUtc = DateTime.UtcNow;
        }

        var sensor = await _db.SensorTypes.FirstOrDefaultAsync(s => s.Code == request.SensorType && s.SchemaVersion == request.SchemaVersion, ct);
        if (sensor == null)
        {
            sensor = new SensorType
            {
                Code = request.SensorType,
                SchemaVersion = request.SchemaVersion,
                Unit = request.Unit
            };
            _db.SensorTypes.Add(sensor);
        }

        var measurementId = request.MeasurementId ?? Guid.NewGuid();
        var exists = await _db.Measurements.AnyAsync(m => m.Id == measurementId, ct);
        if (exists)
        {
            return Conflict("MeasurementId already exists.");
        }

        var measurement = new Measurement
        {
            Id = measurementId,
            Device = device,
            SensorType = sensor,
            Status = MeasurementStatus.InProgress,
            SampleRateHz = request.SampleRateHz,
            Unit = request.Unit ?? sensor.Unit ?? string.Empty,
            StartTimeUtc = request.StartTimeUtc ?? DateTime.UtcNow,
            RawFilePath = _storage.GetRawPath(measurementId),
            MetaJson = request.Meta == null ? null : JsonSerializer.Serialize(request.Meta)
        };

        _db.Measurements.Add(measurement);
        await _db.SaveChangesAsync(ct);

        return Ok(new MeasurementStartResponse
        {
            MeasurementId = measurementId,
            Status = measurement.Status
        });
    }

    [HttpPost("chunk")]
    public async Task<ActionResult<MeasurementChunkResponse>> Chunk([FromBody] MeasurementChunkRequest request, CancellationToken ct)
    {
        if (request.MeasurementId == Guid.Empty)
        {
            return BadRequest("MeasurementId is required.");
        }

        var measurement = await _db.Measurements.FirstOrDefaultAsync(m => m.Id == request.MeasurementId, ct);
        if (measurement == null)
        {
            return NotFound("Measurement not found.");
        }

        if (!string.Equals(measurement.Status, MeasurementStatus.InProgress, StringComparison.OrdinalIgnoreCase))
        {
            return Conflict("Measurement is not accepting chunks.");
        }

        var duplicate = await _db.MeasurementChunks.AnyAsync(c => c.MeasurementId == request.MeasurementId && c.ChunkIndex == request.ChunkIndex, ct);
        if (duplicate)
        {
            return Ok(new MeasurementChunkResponse
            {
                Accepted = true,
                Duplicate = true,
                SampleCount = measurement.SampleCount,
                Message = "Duplicate chunk"
            });
        }

        float[] samples;
        if (request.Samples != null && request.Samples.Count > 0)
        {
            samples = request.Samples.ToArray();
        }
        else if (!string.IsNullOrWhiteSpace(request.DataBase64))
        {
            samples = BinarySampleCodec.DecodeF32LeBase64(request.DataBase64);
        }
        else
        {
            return BadRequest("Samples or DataBase64 payload is required.");
        }

        var added = await _storage.AppendSamplesAsync(request.MeasurementId, samples, ct);
        measurement.SampleCount += added;
        measurement.ChunkCount += 1;

        var chunkHash = ComputeSha256(samples);
        _db.MeasurementChunks.Add(new MeasurementChunk
        {
            MeasurementId = request.MeasurementId,
            ChunkIndex = request.ChunkIndex,
            SizeBytes = added * 4,
            Sha256 = chunkHash
        });

        await _db.SaveChangesAsync(ct);

        var downsampled = Downsample.MinMax(samples, Math.Max(10, _options.MaxLivePoints));
        await _liveHub.BroadcastAsync(new LiveChunkMessage
        {
            MeasurementId = request.MeasurementId,
            ChunkIndex = request.ChunkIndex,
            Points = downsampled
        }, ct);

        return Ok(new MeasurementChunkResponse
        {
            Accepted = true,
            Duplicate = false,
            SampleCount = measurement.SampleCount
        });
    }

    [HttpPost("complete")]
    public async Task<ActionResult<MeasurementCompleteResponse>> Complete([FromBody] MeasurementCompleteRequest request, CancellationToken ct)
    {
        var measurement = await _db.Measurements.FirstOrDefaultAsync(m => m.Id == request.MeasurementId, ct);
        if (measurement == null)
        {
            return NotFound("Measurement not found.");
        }

        measurement.Status = MeasurementStatus.Completed;
        measurement.CompletedUtc = DateTime.UtcNow;
        measurement.RawSha256 = await _storage.ComputeSha256Async(request.MeasurementId, ct);

        await _db.SaveChangesAsync(ct);

        return Ok(new MeasurementCompleteResponse
        {
            Status = measurement.Status,
            Sha256 = measurement.RawSha256 ?? string.Empty
        });
    }

    private static string ComputeSha256(ReadOnlySpan<float> samples)
    {
        var bytes = MemoryMarshal.AsBytes(samples);
        var hash = SHA256.HashData(bytes);
        return Convert.ToHexString(hash).ToLowerInvariant();
    }
}
