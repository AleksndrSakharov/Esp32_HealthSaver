namespace HealthSaver.Server.Contracts;

public sealed class MeasurementListItem
{
    public Guid Id { get; set; }
    public string DeviceId { get; set; } = string.Empty;
    public string SensorType { get; set; } = string.Empty;
    public DateTime StartTimeUtc { get; set; }
    public string Status { get; set; } = string.Empty;
}
