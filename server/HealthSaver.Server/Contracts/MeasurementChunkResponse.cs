namespace HealthSaver.Server.Contracts;

public sealed class MeasurementChunkResponse
{
    public bool Accepted { get; set; }
    public bool Duplicate { get; set; }
    public int SampleCount { get; set; }
    public string? Message { get; set; }
}
