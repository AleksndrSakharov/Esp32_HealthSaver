namespace HealthSaver.Server.Contracts;

public sealed class LiveMeasurementMessage
{
    public string Type { get; set; } = string.Empty;
    public MeasurementListItem Measurement { get; set; } = new();
}
