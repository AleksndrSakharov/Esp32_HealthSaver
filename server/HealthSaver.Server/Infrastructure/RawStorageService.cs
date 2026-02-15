using System.Runtime.InteropServices;
using System.Security.Cryptography;
using Microsoft.Extensions.Options;

namespace HealthSaver.Server.Infrastructure;

public sealed class RawStorageService
{
    private readonly string _root;

    public RawStorageService(IOptions<StorageOptions> options)
    {
        _root = options.Value.RawDataPath;
        Directory.CreateDirectory(_root);
    }

    public string GetRawPath(Guid measurementId)
    {
        return Path.Combine(_root, $"{measurementId}.f32");
    }

    public async Task<int> AppendSamplesAsync(Guid measurementId, ReadOnlyMemory<float> samples, CancellationToken ct)
    {
        var path = GetRawPath(measurementId);
        await using var stream = new FileStream(path, FileMode.Append, FileAccess.Write, FileShare.Read, 8192, true);
        var bytes = MemoryMarshal.AsBytes(samples.Span).ToArray();
        await stream.WriteAsync(bytes, ct);
        return samples.Length;
    }

    public async Task<float[]> ReadAllAsync(Guid measurementId, CancellationToken ct)
    {
        var path = GetRawPath(measurementId);
        if (!File.Exists(path))
        {
            return Array.Empty<float>();
        }

        var bytes = await File.ReadAllBytesAsync(path, ct);
        if (bytes.Length % 4 != 0)
        {
            return Array.Empty<float>();
        }

        return MemoryMarshal.Cast<byte, float>(bytes).ToArray();
    }

    public async Task<string> ComputeSha256Async(Guid measurementId, CancellationToken ct)
    {
        var path = GetRawPath(measurementId);
        if (!File.Exists(path))
        {
            return string.Empty;
        }

        await using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 8192, true);
        using var sha = SHA256.Create();
        var hash = await sha.ComputeHashAsync(stream, ct);
        return Convert.ToHexString(hash).ToLowerInvariant();
    }
}
