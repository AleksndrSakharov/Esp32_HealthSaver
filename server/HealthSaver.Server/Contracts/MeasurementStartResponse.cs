namespace HealthSaver.Server.Contracts;

public sealed class MeasurementStartResponse
{
    public Guid MeasurementId { get; set; }
    public string Status { get; set; } = string.Empty;
}
