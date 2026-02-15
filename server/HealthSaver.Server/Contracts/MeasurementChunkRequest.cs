namespace HealthSaver.Server.Contracts;

public sealed class MeasurementChunkRequest
{
    public Guid MeasurementId { get; set; }
    public int ChunkIndex { get; set; }
    public int TotalChunks { get; set; }
    public string? Encoding { get; set; }
    public string? DataBase64 { get; set; }
    public List<float>? Samples { get; set; }
}
