using System.Runtime.InteropServices;

namespace HealthSaver.Server.Infrastructure;

public static class BinarySampleCodec
{
    public static float[] DecodeF32LeBase64(string base64)
    {
        var bytes = Convert.FromBase64String(base64);
        if (bytes.Length % 4 != 0)
        {
            throw new InvalidDataException("Invalid float32 payload size.");
        }

        return MemoryMarshal.Cast<byte, float>(bytes).ToArray();
    }

    public static string EncodeF32LeBase64(ReadOnlySpan<float> samples)
    {
        var bytes = MemoryMarshal.AsBytes(samples);
        return Convert.ToBase64String(bytes);
    }
}
