namespace HealthSaver.Server.Models;

public sealed class Measurement
{
    public Guid Id { get; set; }
    public int DeviceId { get; set; }
    public Device Device { get; set; } = null!;
    public int SensorTypeId { get; set; }
    public SensorType SensorType { get; set; } = null!;

    public string Status { get; set; } = MeasurementStatus.InProgress;
    public DateTime StartTimeUtc { get; set; }
    public DateTime? CompletedUtc { get; set; }
    public double SampleRateHz { get; set; }
    public string Unit { get; set; } = string.Empty;

    public int SampleCount { get; set; }
    public int ChunkCount { get; set; }

    public string RawFilePath { get; set; } = string.Empty;
    public string? RawSha256 { get; set; }

    public string? MetaJson { get; set; }
    public DateTime CreatedUtc { get; set; } = DateTime.UtcNow;

    public ICollection<MeasurementChunk> Chunks { get; set; } = new List<MeasurementChunk>();
}
