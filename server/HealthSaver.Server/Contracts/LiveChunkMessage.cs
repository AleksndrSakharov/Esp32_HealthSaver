namespace HealthSaver.Server.Contracts;

public sealed class LiveChunkMessage
{
    public Guid MeasurementId { get; set; }
    public int ChunkIndex { get; set; }
    public IReadOnlyList<SeriesPoint> Points { get; set; } = Array.Empty<SeriesPoint>();
}
