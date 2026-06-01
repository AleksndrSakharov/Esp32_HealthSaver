using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Options;

namespace HealthSaver.Server.Infrastructure;

public sealed class ServerDiscoveryBroadcaster : BackgroundService
{
    private readonly DiscoveryOptions _options;
    private readonly ILogger<ServerDiscoveryBroadcaster> _logger;

    public ServerDiscoveryBroadcaster(IOptions<DiscoveryOptions> options, ILogger<ServerDiscoveryBroadcaster> logger)
    {
        _options = options.Value;
        _logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        if (!_options.Enabled)
        {
            return;
        }

        using var udp = new UdpClient(AddressFamily.InterNetwork);
        udp.EnableBroadcast = true;

        while (!stoppingToken.IsCancellationRequested)
        {
            foreach (var endpoint in GetBroadcastEndpoints())
            {
                var payload = JsonSerializer.Serialize(new
                {
                    service = "healthsaver-server",
                    version = 1,
                    baseUrl = $"http://{endpoint.Address}:{_options.HttpPort}",
                    httpPort = _options.HttpPort
                });

                var bytes = Encoding.UTF8.GetBytes(payload);

                try
                {
                    await udp.SendAsync(bytes, endpoint.Broadcast, stoppingToken);
                }
                catch (Exception ex) when (ex is SocketException or ObjectDisposedException or OperationCanceledException)
                {
                    if (!stoppingToken.IsCancellationRequested)
                    {
                        _logger.LogDebug(ex, "Discovery broadcast failed for {Broadcast}", endpoint.Broadcast);
                    }
                }
            }

            await Task.Delay(TimeSpan.FromSeconds(Math.Max(1, _options.IntervalSeconds)), stoppingToken);
        }
    }

    private IEnumerable<DiscoveryEndpoint> GetBroadcastEndpoints()
    {
        foreach (var networkInterface in NetworkInterface.GetAllNetworkInterfaces())
        {
            if (networkInterface.OperationalStatus != OperationalStatus.Up)
            {
                continue;
            }

            var properties = networkInterface.GetIPProperties();
            foreach (var unicast in properties.UnicastAddresses)
            {
                if (unicast.Address.AddressFamily != AddressFamily.InterNetwork || unicast.IPv4Mask == null)
                {
                    continue;
                }

                if (IPAddress.IsLoopback(unicast.Address))
                {
                    continue;
                }

                yield return new DiscoveryEndpoint(
                    unicast.Address,
                    new IPEndPoint(GetBroadcastAddress(unicast.Address, unicast.IPv4Mask), _options.UdpPort));
            }
        }
    }

    private static IPAddress GetBroadcastAddress(IPAddress address, IPAddress mask)
    {
        var addressBytes = address.GetAddressBytes();
        var maskBytes = mask.GetAddressBytes();
        var broadcastBytes = new byte[addressBytes.Length];

        for (var i = 0; i < addressBytes.Length; i++)
        {
            broadcastBytes[i] = (byte)(addressBytes[i] | ~maskBytes[i]);
        }

        return new IPAddress(broadcastBytes);
    }

    private sealed record DiscoveryEndpoint(IPAddress Address, IPEndPoint Broadcast);
}
