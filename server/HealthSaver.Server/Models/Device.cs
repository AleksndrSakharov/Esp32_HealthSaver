namespace HealthSaver.Server.Models;

public sealed class Device
{
    public int Id { get; set; }
    public string DeviceId { get; set; } = string.Empty;
    public string? Name { get; set; }
    public DateTime CreatedUtc { get; set; } = DateTime.UtcNow;
    public DateTime LastSeenUtc { get; set; } = DateTime.UtcNow;

    public ICollection<Measurement> Measurements { get; set; } = new List<Measurement>();
}
