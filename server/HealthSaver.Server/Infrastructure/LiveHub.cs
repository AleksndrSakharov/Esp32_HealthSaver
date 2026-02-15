using System.Collections.Concurrent;
using System.Net.WebSockets;
using System.Text.Json;

namespace HealthSaver.Server.Infrastructure;

public sealed class LiveHub
{
    private readonly ConcurrentDictionary<Guid, WebSocket> _clients = new();
    private readonly JsonSerializerOptions _jsonOptions = new(JsonSerializerDefaults.Web);

    public async Task HandleClientAsync(WebSocket socket, CancellationToken ct)
    {
        var clientId = Guid.NewGuid();
        _clients[clientId] = socket;

        var buffer = new byte[1024];

        try
        {
            while (socket.State == WebSocketState.Open && !ct.IsCancellationRequested)
            {
                var result = await socket.ReceiveAsync(buffer, ct);
                if (result.MessageType == WebSocketMessageType.Close)
                {
                    break;
                }
            }
        }
        finally
        {
            _clients.TryRemove(clientId, out _);
            if (socket.State == WebSocketState.Open)
            {
                await socket.CloseAsync(WebSocketCloseStatus.NormalClosure, "Closing", ct);
            }
        }
    }

    public async Task BroadcastAsync<T>(T message, CancellationToken ct)
    {
        var payload = JsonSerializer.SerializeToUtf8Bytes(message, _jsonOptions);
        var stale = new List<Guid>();

        foreach (var pair in _clients)
        {
            var socket = pair.Value;
            if (socket.State != WebSocketState.Open)
            {
                stale.Add(pair.Key);
                continue;
            }

            await socket.SendAsync(payload, WebSocketMessageType.Text, true, ct);
        }

        foreach (var id in stale)
        {
            _clients.TryRemove(id, out _);
        }
    }
}
