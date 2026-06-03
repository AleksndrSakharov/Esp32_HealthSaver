namespace HealthSaver.Server.Contracts;

public sealed class MeasurementUploadRequest
{
    public string DeviceId { get; set; } = string.Empty;
    public string SensorType { get; set; } = string.Empty;
    public int SchemaVersion { get; set; } = 1;
    public double SampleRateHz { get; set; } = 1;
    public string? Unit { get; set; }
    public DateTime? StartTimeUtc { get; set; }
    public Guid? MeasurementId { get; set; }
    public Dictionary<string, string>? Meta { get; set; }
    public string? Encoding { get; set; }
    public string? DataBase64 { get; set; }
    public List<float>? Samples { get; set; }
}
