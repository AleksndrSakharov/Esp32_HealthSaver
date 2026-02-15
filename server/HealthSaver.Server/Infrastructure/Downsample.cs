using HealthSaver.Server.Contracts;

namespace HealthSaver.Server.Infrastructure;

public static class Downsample
{
    public static IReadOnlyList<SeriesPoint> MinMax(float[] samples, int maxPoints)
    {
        if (samples.Length == 0)
        {
            return Array.Empty<SeriesPoint>();
        }

        if (samples.Length <= maxPoints)
        {
            return samples.Select((value, index) => new SeriesPoint(index, value)).ToArray();
        }

        var bucketSize = (int)Math.Ceiling(samples.Length / (double)maxPoints);
        var result = new List<SeriesPoint>(maxPoints * 2);

        for (var start = 0; start < samples.Length; start += bucketSize)
        {
            var end = Math.Min(start + bucketSize, samples.Length);
            var minValue = float.MaxValue;
            var maxValue = float.MinValue;
            var minIndex = start;
            var maxIndex = start;

            for (var i = start; i < end; i++)
            {
                var value = samples[i];
                if (value < minValue)
                {
                    minValue = value;
                    minIndex = i;
                }
                if (value > maxValue)
                {
                    maxValue = value;
                    maxIndex = i;
                }
            }

            if (minIndex <= maxIndex)
            {
                result.Add(new SeriesPoint(minIndex, minValue));
                result.Add(new SeriesPoint(maxIndex, maxValue));
            }
            else
            {
                result.Add(new SeriesPoint(maxIndex, maxValue));
                result.Add(new SeriesPoint(minIndex, minValue));
            }
        }

        return result;
    }
}
