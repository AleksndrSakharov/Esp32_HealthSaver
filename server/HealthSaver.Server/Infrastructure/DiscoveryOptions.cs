namespace HealthSaver.Server.Infrastructure;

public sealed class DiscoveryOptions
{
    public bool Enabled { get; set; } = true;
    public int UdpPort { get; set; } = 50505;
    public int HttpPort { get; set; } = 5000;
    public int IntervalSeconds { get; set; } = 3;
}
