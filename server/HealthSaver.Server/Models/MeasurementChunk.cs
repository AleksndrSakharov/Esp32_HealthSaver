namespace HealthSaver.Server.Models;

public sealed class MeasurementChunk
{
    public int Id { get; set; }
    public Guid MeasurementId { get; set; }
    public Measurement Measurement { get; set; } = null!;
    public int ChunkIndex { get; set; }
    public int SizeBytes { get; set; }
    public string Sha256 { get; set; } = string.Empty;
    public DateTime ReceivedUtc { get; set; } = DateTime.UtcNow;
}
