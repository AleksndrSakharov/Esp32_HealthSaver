namespace HealthSaver.Server.Contracts;

public sealed class MeasurementDetail
{
    public Guid Id { get; set; }
    public string DeviceId { get; set; } = string.Empty;
    public string SensorType { get; set; } = string.Empty;
    public string Unit { get; set; } = string.Empty;
    public double SampleRateHz { get; set; }
    public string Status { get; set; } = string.Empty;
    public int SampleCount { get; set; }
    public int ChunkCount { get; set; }
    public DateTime StartTimeUtc { get; set; }
    public DateTime? CompletedUtc { get; set; }
    public string? RawSha256 { get; set; }
}
