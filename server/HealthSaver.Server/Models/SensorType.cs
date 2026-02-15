namespace HealthSaver.Server.Models;

public sealed class SensorType
{
    public int Id { get; set; }
    public string Code { get; set; } = string.Empty;
    public string? Unit { get; set; }
    public string? Axes { get; set; }
    public int SchemaVersion { get; set; } = 1;

    public ICollection<Measurement> Measurements { get; set; } = new List<Measurement>();
}
