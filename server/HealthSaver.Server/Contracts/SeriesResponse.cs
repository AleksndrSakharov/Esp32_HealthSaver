namespace HealthSaver.Server.Contracts;

public sealed class SeriesResponse
{
    public Guid MeasurementId { get; set; }
    public double SampleRateHz { get; set; }
    public IReadOnlyList<SeriesPoint> Points { get; set; } = Array.Empty<SeriesPoint>();
}
