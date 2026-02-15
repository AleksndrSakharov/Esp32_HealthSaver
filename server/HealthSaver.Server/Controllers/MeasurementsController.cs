using HealthSaver.Server.Contracts;
using HealthSaver.Server.Infrastructure;
using Microsoft.AspNetCore.Mvc;
using Microsoft.EntityFrameworkCore;

namespace HealthSaver.Server.Controllers;

[ApiController]
[Route("api/measurements")]
public sealed class MeasurementsController : ControllerBase
{
    private readonly AppDbContext _db;
    private readonly RawStorageService _storage;

    public MeasurementsController(AppDbContext db, RawStorageService storage)
    {
        _db = db;
        _storage = storage;
    }

    [HttpGet]
    public async Task<ActionResult<IReadOnlyList<MeasurementListItem>>> List([FromQuery] string? deviceId, [FromQuery] string? sensorType, [FromQuery] int take = 100, CancellationToken ct = default)
    {
        var query = _db.Measurements
            .AsNoTracking()
            .Include(m => m.Device)
            .Include(m => m.SensorType)
            .OrderByDescending(m => m.StartTimeUtc)
            .AsQueryable();

        if (!string.IsNullOrWhiteSpace(deviceId))
        {
            query = query.Where(m => m.Device.DeviceId == deviceId);
        }

        if (!string.IsNullOrWhiteSpace(sensorType))
        {
            query = query.Where(m => m.SensorType.Code == sensorType);
        }

        var items = await query.Take(Math.Clamp(take, 1, 500))
            .Select(m => new MeasurementListItem
            {
                Id = m.Id,
                DeviceId = m.Device.DeviceId,
                SensorType = m.SensorType.Code,
                StartTimeUtc = m.StartTimeUtc,
                Status = m.Status
            })
            .ToListAsync(ct);

        return Ok(items);
    }

    [HttpGet("{id:guid}")]
    public async Task<ActionResult<MeasurementDetail>> Detail(Guid id, CancellationToken ct)
    {
        var measurement = await _db.Measurements
            .AsNoTracking()
            .Include(m => m.Device)
            .Include(m => m.SensorType)
            .FirstOrDefaultAsync(m => m.Id == id, ct);

        if (measurement == null)
        {
            return NotFound();
        }

        return Ok(new MeasurementDetail
        {
            Id = measurement.Id,
            DeviceId = measurement.Device.DeviceId,
            SensorType = measurement.SensorType.Code,
            Unit = measurement.Unit,
            SampleRateHz = measurement.SampleRateHz,
            Status = measurement.Status,
            SampleCount = measurement.SampleCount,
            ChunkCount = measurement.ChunkCount,
            StartTimeUtc = measurement.StartTimeUtc,
            CompletedUtc = measurement.CompletedUtc,
            RawSha256 = measurement.RawSha256
        });
    }

    [HttpGet("{id:guid}/series")]
    public async Task<ActionResult<SeriesResponse>> Series(Guid id, [FromQuery] int maxPoints = 1000, CancellationToken ct = default)
    {
        var measurement = await _db.Measurements
            .AsNoTracking()
            .FirstOrDefaultAsync(m => m.Id == id, ct);

        if (measurement == null)
        {
            return NotFound();
        }

        var samples = await _storage.ReadAllAsync(id, ct);
        var points = Downsample.MinMax(samples, Math.Clamp(maxPoints, 10, 5000));

        return Ok(new SeriesResponse
        {
            MeasurementId = id,
            SampleRateHz = measurement.SampleRateHz,
            Points = points
        });
    }
}
