using System;
using System.IO.Ports;
using System.Net.Http;
using System.Text;
using System.Threading.Tasks;

namespace PCHub;

public class PcHubClient : IDisposable
{
    private readonly AppSettings _settings;
    private HttpClient? _httpClient;
    private SerialPort? _serialPort;
    private bool _disposed;

    public bool IsConnected { get; private set; }
    public bool IsPaused { get; private set; }
    public string StatusMessage { get; private set; } = "Disconnected";
    public string TransportInfo { get; private set; } = "";

    public PcHubClient(AppSettings settings)
    {
        _settings = settings;
        InitializeTransport();
    }

    private void InitializeTransport()
    {
        // Tear down any existing transport
        _httpClient?.Dispose();
        _httpClient = null;

        try { _serialPort?.Close(); } catch { }
        _serialPort?.Dispose();
        _serialPort = null;

        IsConnected = false;

        if (_settings.ConnectionType.Equals("wifi", StringComparison.OrdinalIgnoreCase))
        {
            _httpClient = new HttpClient
            {
                Timeout = TimeSpan.FromSeconds(3)
            };
            // HTTP client is considered "ready" immediately; actual connection is tested on first send
            TransportInfo = $"{_settings.WifiHost}:{_settings.WifiPort}";
            StatusMessage = "Ready (WiFi)";
        }
        else
        {
            TransportInfo = _settings.SerialPort;
            StatusMessage = string.IsNullOrWhiteSpace(_settings.SerialPort)
                ? "No serial port configured"
                : "Serial not opened yet";
        }
    }

    public void Reinitialize()
    {
        InitializeTransport();
    }

    public async Task<bool> SendAsync(PcMetrics metrics)
    {
        if (_disposed) return false;

        string json = metrics.ToJson();

        if (_settings.ConnectionType.Equals("wifi", StringComparison.OrdinalIgnoreCase))
        {
            return await SendHttpAsync(json);
        }
        else
        {
            return SendSerial(json);
        }
    }

    private async Task<bool> SendHttpAsync(string json)
    {
        if (_httpClient == null) return false;

        try
        {
            string url = $"http://{_settings.WifiHost}:{_settings.WifiPort}/api/pc";
            var content = new StringContent(json, Encoding.UTF8, "text/plain");
            var response = await _httpClient.PostAsync(url, content);

            if (response.IsSuccessStatusCode)
            {
                string body = await response.Content.ReadAsStringAsync();
                bool paused = false;
                try
                {
                    using var doc = System.Text.Json.JsonDocument.Parse(body);
                    paused = doc.RootElement.TryGetProperty("paused", out var p) && p.GetBoolean();
                }
                catch { }

                if (paused)
                {
                    IsConnected = false;
                    IsPaused = true;
                    StatusMessage = "Paused by device";
                    return true;
                }

                IsConnected = true;
                IsPaused = false;
                StatusMessage = $"Connected via WiFi · {_settings.WifiHost}";
                return true;
            }
            else
            {
                IsConnected = false;
                IsPaused = false;
                StatusMessage = $"HTTP {(int)response.StatusCode} from device";
                return false;
            }
        }
        catch (TaskCanceledException)
        {
            IsConnected = false;
            IsPaused = false;
            StatusMessage = "WiFi timeout — device unreachable";
            return false;
        }
        catch (HttpRequestException ex)
        {
            IsConnected = false;
            IsPaused = false;
            StatusMessage = $"WiFi error: {ex.Message}";
            return false;
        }
        catch (Exception ex)
        {
            IsConnected = false;
            IsPaused = false;
            StatusMessage = $"Send error: {ex.Message}";
            return false;
        }
    }

    private bool SendSerial(string json)
    {
        if (string.IsNullOrWhiteSpace(_settings.SerialPort))
        {
            IsConnected = false;
            StatusMessage = "No serial port configured";
            return false;
        }

        // Attempt to open if not open
        if (_serialPort == null || !_serialPort.IsOpen)
        {
            if (!OpenSerial())
                return false;
        }

        try
        {
            _serialPort!.WriteLine(json);
            IsConnected = true;
            StatusMessage = $"Connected via Serial · {_settings.SerialPort}";
            return true;
        }
        catch (Exception ex)
        {
            IsConnected = false;
            StatusMessage = $"Serial error: {ex.Message}";

            // Close and null so next attempt tries to reconnect
            try { _serialPort?.Close(); } catch { }
            _serialPort?.Dispose();
            _serialPort = null;
            return false;
        }
    }

    private bool OpenSerial()
    {
        try
        {
            _serialPort?.Dispose();
            _serialPort = new SerialPort(_settings.SerialPort, 115200)
            {
                ReadTimeout = 1000,
                WriteTimeout = 1000,
                NewLine = "\n"
            };
            _serialPort.Open();
            IsConnected = true;
            StatusMessage = $"Connected via Serial · {_settings.SerialPort}";
            return true;
        }
        catch (Exception ex)
        {
            IsConnected = false;
            StatusMessage = $"Cannot open {_settings.SerialPort}: {ex.Message}";
            _serialPort?.Dispose();
            _serialPort = null;
            return false;
        }
    }

    /// <summary>
    /// Performs a quick connectivity test and returns a result message.
    /// </summary>
    public async Task<string> TestConnectionAsync()
    {
        if (_settings.ConnectionType.Equals("wifi", StringComparison.OrdinalIgnoreCase))
        {
            return await TestHttpAsync();
        }
        else
        {
            return TestSerial();
        }
    }

    private async Task<string> TestHttpAsync()
    {
        if (_httpClient == null)
            return "HTTP client not initialized.";

        string base_ = $"http://{_settings.WifiHost}:{_settings.WifiPort}";
        try
        {
            // Step 1: reachability
            var getResp = await _httpClient.GetAsync(base_ + "/");
            if (!getResp.IsSuccessStatusCode)
                return $"GET / → HTTP {(int)getResp.StatusCode}";

            // Step 2: test actual POST /api/pc endpoint
            string testJson = "{\"type\":\"pc\",\"ct\":1.0,\"cl\":1.0,\"cp\":0.0," +
                               "\"gt\":1.0,\"gl\":1.0,\"gvr\":0,\"gvt\":0,\"ru\":0,\"rt\":0}";
            var postContent = new StringContent(testJson, Encoding.UTF8, "text/plain");
            var postResp = await _httpClient.PostAsync(base_ + "/api/pc", postContent);

            if (postResp.IsSuccessStatusCode)
                return $"OK — device reached, POST /api/pc accepted ({(int)postResp.StatusCode})";
            else
            {
                string body = await postResp.Content.ReadAsStringAsync();
                return $"GET / OK but POST /api/pc → {(int)postResp.StatusCode}: {body}";
            }
        }
        catch (TaskCanceledException)
        {
            return $"Timeout — {_settings.WifiHost} unreachable";
        }
        catch (Exception ex)
        {
            return $"Error: {ex.Message}";
        }
    }

    private string TestSerial()
    {
        if (string.IsNullOrWhiteSpace(_settings.SerialPort))
            return "No serial port selected.";

        bool ok = OpenSerial();
        return ok
            ? $"Opened {_settings.SerialPort} at 115200 baud successfully."
            : StatusMessage;
    }

    public void Dispose()
    {
        if (!_disposed)
        {
            _disposed = true;
            _httpClient?.Dispose();
            try { _serialPort?.Close(); } catch { }
            _serialPort?.Dispose();
        }
    }
}
