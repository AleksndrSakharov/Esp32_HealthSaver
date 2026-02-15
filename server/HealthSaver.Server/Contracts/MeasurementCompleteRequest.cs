namespace HealthSaver.Server.Contracts;

public sealed class MeasurementCompleteRequest
{
    public Guid MeasurementId { get; set; }
    public int TotalChunks { get; set; }
    public int SampleCount { get; set; }
}
