using System.Globalization;
using System.IO.Ports;
using System.Net.Http.Json;
using System.Runtime.InteropServices;
using System.Text.Json;

var options = AgentOptions.FromArgs(args);
if (!options.IsValid())
{
    AgentOptions.PrintUsage();
    return;
}

Console.WriteLine($"Connecting to serial port {options.Port}...");
var samples = ReadSamplesFromSerial(options);
if (samples.Count == 0)
{
    Console.WriteLine("No samples received.");
    return;
}

Console.WriteLine($"Samples received: {samples.Count}");

using var http = new HttpClient { BaseAddress = new Uri(options.ServerUrl) };

var startRequest = new MeasurementStartRequest
{
    DeviceId = options.DeviceId,
    SensorType = options.SensorType,
    SchemaVersion = 1,
    SampleRateHz = options.SampleRateHz,
    Unit = options.Unit,
    StartTimeUtc = DateTime.UtcNow,
    Meta = new Dictionary<string, string>
    {
        ["source"] = "serial-agent",
        ["port"] = options.Port
    }
};

var startResponse = await http.PostAsJsonAsync("/api/ingest/start", startRequest);
startResponse.EnsureSuccessStatusCode();
var startPayload = await startResponse.Content.ReadFromJsonAsync<MeasurementStartResponse>(JsonOptions());
if (startPayload == null)
{
    Console.WriteLine("Failed to parse start response.");
    return;
}

var measurementId = startPayload.MeasurementId;
Console.WriteLine($"Measurement created: {measurementId}");

var totalChunks = (int)Math.Ceiling(samples.Count / (double)options.ChunkSize);
var chunkIndex = 0;
for (var i = 0; i < samples.Count; i += options.ChunkSize)
{
    var count = Math.Min(options.ChunkSize, samples.Count - i);
    var chunk = samples.GetRange(i, count).ToArray();
    var payload = new MeasurementChunkRequest
    {
        MeasurementId = measurementId,
        ChunkIndex = chunkIndex,
        TotalChunks = totalChunks,
        Encoding = "f32le-base64",
        DataBase64 = EncodeF32LeBase64(chunk)
    };

    var chunkResponse = await http.PostAsJsonAsync("/api/ingest/chunk", payload);
    chunkResponse.EnsureSuccessStatusCode();
    chunkIndex++;
}

var completePayload = new MeasurementCompleteRequest
{
    MeasurementId = measurementId,
    TotalChunks = totalChunks,
    SampleCount = samples.Count
};

var completeResponse = await http.PostAsJsonAsync("/api/ingest/complete", completePayload);
completeResponse.EnsureSuccessStatusCode();
var complete = await completeResponse.Content.ReadFromJsonAsync<MeasurementCompleteResponse>(JsonOptions());
Console.WriteLine($"Completed: {complete?.Status}, sha256={complete?.Sha256}");

static List<float> ReadSamplesFromSerial(AgentOptions options)
{
    var samples = new List<float>();
    using var port = new SerialPort(options.Port, 115200)
    {
        ReadTimeout = 2000,
        NewLine = "\n"
    };

    port.Open();
    port.DiscardInBuffer();
    port.WriteLine("READ_SD");

    var started = false;
    var deadline = DateTime.UtcNow.AddSeconds(options.TimeoutSeconds);

    while (DateTime.UtcNow < deadline)
    {
        try
        {
            var line = port.ReadLine().Trim();
            if (line == "---START_FILE---")
            {
                started = true;
                continue;
            }
            if (line == "---END_FILE---")
            {
                break;
            }

            if (!started)
            {
                continue;
            }

            if (float.TryParse(line, NumberStyles.Float, CultureInfo.InvariantCulture, out var value))
            {
                samples.Add(value);
            }
        }
        catch (TimeoutException)
        {
            if (started)
            {
                break;
            }
        }
    }

    port.Close();
    return samples;
}

static string EncodeF32LeBase64(ReadOnlySpan<float> samples)
{
    var bytes = MemoryMarshal.AsBytes(samples);
    return Convert.ToBase64String(bytes);
}

static JsonSerializerOptions JsonOptions()
{
    return new JsonSerializerOptions(JsonSerializerDefaults.Web);
}

sealed class AgentOptions
{
    public string Port { get; set; } = string.Empty;
    public string ServerUrl { get; set; } = "http://localhost:5000";
    public string DeviceId { get; set; } = "hub-01";
    public string SensorType { get; set; } = "pressure";
    public double SampleRateHz { get; set; } = 1;
    public string Unit { get; set; } = "mmHg";
    public int ChunkSize { get; set; } = 500;
    public int TimeoutSeconds { get; set; } = 15;

    public bool IsValid() => !string.IsNullOrWhiteSpace(Port);

    public static AgentOptions FromArgs(string[] args)
    {
        var options = new AgentOptions();
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--"))
            {
                continue;
            }

            var value = i + 1 < args.Length ? args[i + 1] : string.Empty;
            switch (key)
            {
                case "--port":
                    options.Port = value;
                    break;
                case "--server":
                    options.ServerUrl = value;
                    break;
                case "--device-id":
                    options.DeviceId = value;
                    break;
                case "--sensor":
                    options.SensorType = value;
                    break;
                case "--rate":
                    if (double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var rate))
                    {
                        options.SampleRateHz = rate;
                    }
                    break;
                case "--unit":
                    options.Unit = value;
                    break;
                case "--chunk":
                    if (int.TryParse(value, out var chunk))
                    {
                        options.ChunkSize = chunk;
                    }
                    break;
                case "--timeout":
                    if (int.TryParse(value, out var timeout))
                    {
                        options.TimeoutSeconds = timeout;
                    }
                    break;
            }
        }

        return options;
    }

    public static void PrintUsage()
    {
        Console.WriteLine("Usage: dotnet run -- --port COM3 --server http://localhost:5000 --device-id hub-01 --sensor pressure --rate 1 --unit mmHg");
    }
}

sealed class MeasurementStartRequest
{
    public string DeviceId { get; set; } = string.Empty;
    public string SensorType { get; set; } = string.Empty;
    public int SchemaVersion { get; set; }
    public double SampleRateHz { get; set; }
    public string Unit { get; set; } = string.Empty;
    public DateTime StartTimeUtc { get; set; }
    public Dictionary<string, string> Meta { get; set; } = new();
}

sealed class MeasurementStartResponse
{
    public Guid MeasurementId { get; set; }
    public string Status { get; set; } = string.Empty;
}

sealed class MeasurementChunkRequest
{
    public Guid MeasurementId { get; set; }
    public int ChunkIndex { get; set; }
    public int TotalChunks { get; set; }
    public string Encoding { get; set; } = "f32le-base64";
    public string DataBase64 { get; set; } = string.Empty;
}

sealed class MeasurementCompleteRequest
{
    public Guid MeasurementId { get; set; }
    public int TotalChunks { get; set; }
    public int SampleCount { get; set; }
}

sealed class MeasurementCompleteResponse
{
    public string Status { get; set; } = string.Empty;
    public string Sha256 { get; set; } = string.Empty;
}
