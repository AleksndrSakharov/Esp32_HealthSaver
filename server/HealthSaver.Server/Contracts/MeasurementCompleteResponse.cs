namespace HealthSaver.Server.Contracts;

public sealed class MeasurementCompleteResponse
{
    public string Status { get; set; } = string.Empty;
    public string Sha256 { get; set; } = string.Empty;
}
