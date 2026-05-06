using System;
using System.Text;
using System.Text.Json;

namespace PCHub;

public class PcMetrics
{
    public DateTime Timestamp { get; set; } = DateTime.UtcNow;

    public string CpuName { get; set; } = "";
    public float CpuTemp { get; set; }
    public float CpuLoad { get; set; }
    public float CpuPower { get; set; }

    public string GpuName { get; set; } = "";
    public float GpuTemp { get; set; }
    public float GpuLoad { get; set; }
    public long GpuVramUsedMb { get; set; }
    public long GpuVramTotalMb { get; set; }

    public long RamUsedMb { get; set; }
    public long RamTotalMb { get; set; }

    /// <summary>
    /// Returns compact JSON for sending to PCHUB device.
    /// Format: {"type":"pc","ct":...,"cl":...,"cp":...,"gt":...,"gl":...,"gvr":...,"gvt":...,"ru":...,"rt":...}
    /// </summary>
    // NaN/Infinity → 0 so the JSON stays valid when a sensor read fails
    private static string F(float v) => float.IsFinite(v) ? v.ToString("F1", System.Globalization.CultureInfo.InvariantCulture) : "0.0";

    public string ToJson()
    {
        static string Esc(string s) => s.Replace("\\", "\\\\").Replace("\"", "\\\"");
        var sb = new StringBuilder();
        sb.Append("{");
        sb.Append("\"type\":\"pc\",");
        sb.Append($"\"cn\":\"{Esc(CpuName[..Math.Min(CpuName.Length, 31)])}\",");
        sb.Append($"\"gn\":\"{Esc(GpuName[..Math.Min(GpuName.Length, 31)])}\",");
        sb.Append($"\"ct\":{F(CpuTemp)},");
        sb.Append($"\"cl\":{F(CpuLoad)},");
        sb.Append($"\"cp\":{F(CpuPower)},");
        sb.Append($"\"gt\":{F(GpuTemp)},");
        sb.Append($"\"gl\":{F(GpuLoad)},");
        sb.Append($"\"gvr\":{GpuVramUsedMb},");
        sb.Append($"\"gvt\":{GpuVramTotalMb},");
        sb.Append($"\"ru\":{RamUsedMb},");
        sb.Append($"\"rt\":{RamTotalMb}");
        sb.Append("}");
        return sb.ToString();
    }
}
