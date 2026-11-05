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

    public bool HasBattery { get; set; }
    public float BatteryPercent { get; set; }
    public bool IsCharging { get; set; }
    public bool IsOnAc { get; set; }
    public int BatterySecondsRemaining { get; set; }
    public bool IsBatterySaver { get; set; }

    /// <summary>
    /// Returns compact JSON for sending to PCHUB device.
    /// Format: {"type":"pc","ct":...,"cl":...,"cp":...,"gt":...,"gl":...,"gvr":...,"gvt":...,"ru":...,"rt":...}
    /// </summary>
    // NaN/Infinity → 0 so the JSON stays valid when a sensor read fails
    private static float Finite(float v) => float.IsFinite(v) ? v : 0f;

    public string ToJson()
    {
        using var stream = new System.IO.MemoryStream();
        using (var w = new Utf8JsonWriter(stream))
        {
            w.WriteStartObject();
            w.WriteString("type", "pc");
            w.WriteString("cn", CpuName[..Math.Min(CpuName.Length, 31)]);
            w.WriteString("gn", GpuName[..Math.Min(GpuName.Length, 31)]);
            w.WriteNumber("ct", MathF.Round(Finite(CpuTemp), 1));
            w.WriteNumber("cl", MathF.Round(Finite(CpuLoad), 1));
            w.WriteNumber("cp", MathF.Round(Finite(CpuPower), 1));
            w.WriteNumber("gt", MathF.Round(Finite(GpuTemp), 1));
            w.WriteNumber("gl", MathF.Round(Finite(GpuLoad), 1));
            w.WriteNumber("gvr", GpuVramUsedMb);
            w.WriteNumber("gvt", GpuVramTotalMb);
            w.WriteNumber("ru", RamUsedMb);
            w.WriteNumber("rt", RamTotalMb);
            if (HasBattery)
            {
                w.WriteNumber("bat", (int)Math.Round(BatteryPercent));
                w.WriteBoolean("bch", IsCharging);
                w.WriteBoolean("bac", IsOnAc);
                w.WriteBoolean("bsv", IsBatterySaver);
            }
            w.WriteEndObject();
        }
        return Encoding.UTF8.GetString(stream.ToArray());
    }
}
