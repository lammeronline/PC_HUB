using System;
using System.Management;
using System.Runtime.InteropServices;
using LibreHardwareMonitor.Hardware;
using WinForms = System.Windows.Forms;

namespace PCHub;

public class HardwareMonitor : IDisposable
{
    // RootPowerKey must be IntPtr.Zero; returns S_OK (0) on success
    [DllImport("powrprof.dll", SetLastError = false)]
    private static extern int PowerGetEffectivePowerMode(IntPtr rootPowerKey, out uint mode);

    // null = unknown, true = available, false = not on this Windows version
    private static bool? _powerModeSupported;


    private readonly Computer _computer;
    private bool _disposed;

    public bool ReadBattery { get; set; } = true;

    public HardwareMonitor()
    {
        _computer = new Computer
        {
            IsCpuEnabled = true,
            IsGpuEnabled = true,
            IsMemoryEnabled = true,
            IsMotherboardEnabled = false,
            IsControllerEnabled = false,
            IsStorageEnabled = false,
            IsNetworkEnabled = false,
            IsBatteryEnabled = false
        };
        _computer.Open();
        _computer.Accept(new UpdateVisitor());
    }

    public PcMetrics Read()
    {
        _computer.Accept(new UpdateVisitor());

        var metrics = new PcMetrics
        {
            Timestamp = DateTime.UtcNow
        };

        foreach (var hardware in _computer.Hardware)
        {
            switch (hardware.HardwareType)
            {
                case HardwareType.Cpu:
                    ReadCpu(hardware, metrics);
                    break;

                case HardwareType.GpuNvidia:
                case HardwareType.GpuAmd:
                case HardwareType.GpuIntel:
                    // Only read the first GPU found
                    if (string.IsNullOrEmpty(metrics.GpuName))
                        ReadGpu(hardware, metrics);
                    break;

                case HardwareType.Memory:
                    ReadMemory(hardware, metrics);
                    break;
            }
        }

        if (ReadBattery)
            ReadBatteryStatus(metrics);

        return metrics;
    }

    private static void ReadBatteryStatus(PcMetrics metrics)
    {
        // Path 1: WMI Win32_Battery — most reliable on laptops
        try
        {
            using var searcher = new ManagementObjectSearcher(
                "root\\cimv2", "SELECT EstimatedChargeRemaining, BatteryStatus FROM Win32_Battery");
            foreach (ManagementObject bat in searcher.Get())
            {
                var charge = bat["EstimatedChargeRemaining"];
                if (charge != null)
                {
                    metrics.HasBattery = true;
                    metrics.BatteryPercent = Convert.ToSingle(charge);
                    int status = Convert.ToInt32(bat["BatteryStatus"] ?? 0);
                    metrics.IsOnAc = status != 1;
                    // status 6-9 = explicitly charging; status 2 on some OEMs means
                    // "AC/Full" but also covers active charging when battery isn't full
                    metrics.IsCharging = status >= 6 ||
                                         (status == 2 && metrics.BatteryPercent < 95f);
                }
                break;
            }
        }
        catch { }

        // Path 2: SystemInformation.PowerStatus fallback
        if (!metrics.HasBattery)
        {
            var ps = WinForms.SystemInformation.PowerStatus;
            float pct = ps.BatteryLifePercent;
            if (pct >= 0f && pct <= 1.0f)
            {
                metrics.HasBattery     = true;
                metrics.BatteryPercent = pct * 100f;
                metrics.IsCharging     = (ps.BatteryChargeStatus & WinForms.BatteryChargeStatus.Charging) != 0;
                metrics.IsOnAc         = ps.PowerLineStatus == WinForms.PowerLineStatus.Online;
            }
        }

        if (!metrics.HasBattery) return;

        // mode 0 = BatterySaver (Windows 10 1709+)
        if (_powerModeSupported != false)
        {
            try
            {
                int hr = PowerGetEffectivePowerMode(IntPtr.Zero, out uint mode);
                _powerModeSupported = true;
                metrics.IsBatterySaver = hr == 0 && mode == 0;
            }
            catch (EntryPointNotFoundException) { _powerModeSupported = false; }
        }
    }

    private static void ReadCpu(IHardware hardware, PcMetrics metrics)
    {
        metrics.CpuName = hardware.Name;

        foreach (var sensor in hardware.Sensors)
        {
            if (!sensor.Value.HasValue) continue;

            switch (sensor.SensorType)
            {
                case SensorType.Temperature:
                    // "CPU Package" or first temp sensor
                    if (sensor.Name.Contains("Package", StringComparison.OrdinalIgnoreCase) ||
                        sensor.Name.Equals("CPU Package", StringComparison.OrdinalIgnoreCase))
                    {
                        metrics.CpuTemp = sensor.Value.Value;
                    }
                    else if (metrics.CpuTemp == 0f)
                    {
                        metrics.CpuTemp = sensor.Value.Value;
                    }
                    break;

                case SensorType.Load:
                    // "CPU Total" load
                    if (sensor.Name.Contains("Total", StringComparison.OrdinalIgnoreCase) ||
                        sensor.Name.Equals("CPU Total", StringComparison.OrdinalIgnoreCase))
                    {
                        metrics.CpuLoad = sensor.Value.Value;
                    }
                    else if (metrics.CpuLoad == 0f)
                    {
                        metrics.CpuLoad = sensor.Value.Value;
                    }
                    break;

                case SensorType.Power:
                    // "CPU Package" power
                    if (sensor.Name.Contains("Package", StringComparison.OrdinalIgnoreCase) ||
                        sensor.Name.Equals("CPU Package", StringComparison.OrdinalIgnoreCase))
                    {
                        metrics.CpuPower = sensor.Value.Value;
                    }
                    else if (metrics.CpuPower == 0f)
                    {
                        metrics.CpuPower = sensor.Value.Value;
                    }
                    break;
            }
        }
    }

    private static void ReadGpu(IHardware hardware, PcMetrics metrics)
    {
        metrics.GpuName = hardware.Name;

        foreach (var sensor in hardware.Sensors)
        {
            if (!sensor.Value.HasValue) continue;

            switch (sensor.SensorType)
            {
                case SensorType.Temperature:
                    if (sensor.Name.Contains("Core", StringComparison.OrdinalIgnoreCase) ||
                        sensor.Name.Equals("GPU Core", StringComparison.OrdinalIgnoreCase))
                    {
                        metrics.GpuTemp = sensor.Value.Value;
                    }
                    else if (metrics.GpuTemp == 0f)
                    {
                        metrics.GpuTemp = sensor.Value.Value;
                    }
                    break;

                case SensorType.Load:
                    if (sensor.Name.Contains("Core", StringComparison.OrdinalIgnoreCase) ||
                        sensor.Name.Equals("GPU Core", StringComparison.OrdinalIgnoreCase))
                    {
                        metrics.GpuLoad = sensor.Value.Value;
                    }
                    else if (metrics.GpuLoad == 0f)
                    {
                        metrics.GpuLoad = sensor.Value.Value;
                    }
                    break;

                case SensorType.SmallData:
                    // VRAM sensors are reported as SmallData (MB)
                    if (sensor.Name.Contains("GPU Memory Used", StringComparison.OrdinalIgnoreCase) ||
                        sensor.Name.Contains("D3D Dedicated Memory Used", StringComparison.OrdinalIgnoreCase))
                    {
                        metrics.GpuVramUsedMb = (long)sensor.Value.Value;
                    }
                    else if (sensor.Name.Contains("GPU Memory Total", StringComparison.OrdinalIgnoreCase) ||
                             sensor.Name.Contains("D3D Dedicated Memory Total", StringComparison.OrdinalIgnoreCase))
                    {
                        metrics.GpuVramTotalMb = (long)sensor.Value.Value;
                    }
                    break;
            }
        }
    }

    private static void ReadMemory(IHardware hardware, PcMetrics metrics)
    {
        float usedGb = 0f;
        float availableGb = 0f;

        foreach (var sensor in hardware.Sensors)
        {
            if (!sensor.Value.HasValue) continue;

            if (sensor.SensorType == SensorType.Data)
            {
                // exclude "Virtual Memory *" — it includes page file and can exceed physical RAM
                bool isVirtual = sensor.Name.Contains("Virtual", StringComparison.OrdinalIgnoreCase);

                if (!isVirtual &&
                    sensor.Name.Contains("Used", StringComparison.OrdinalIgnoreCase) &&
                    sensor.Name.Contains("Memory", StringComparison.OrdinalIgnoreCase))
                {
                    usedGb = sensor.Value.Value;
                }
                else if (!isVirtual &&
                         sensor.Name.Contains("Available", StringComparison.OrdinalIgnoreCase) &&
                         sensor.Name.Contains("Memory", StringComparison.OrdinalIgnoreCase))
                {
                    availableGb = sensor.Value.Value;
                }
            }
        }

        // Convert GB to MB
        metrics.RamUsedMb = (long)(usedGb * 1024f);
        long totalMb = (long)((usedGb + availableGb) * 1024f);
        metrics.RamTotalMb = totalMb > 0 ? totalMb : metrics.RamUsedMb;
    }

    public void Dispose()
    {
        if (!_disposed)
        {
            _computer.Close();
            _disposed = true;
        }
    }

    // Required by LibreHardwareMonitor to trigger sensor updates
    private sealed class UpdateVisitor : IVisitor
    {
        public void VisitComputer(IComputer computer)
        {
            computer.Traverse(this);
        }

        public void VisitHardware(IHardware hardware)
        {
            hardware.Update();
            foreach (var subHardware in hardware.SubHardware)
                subHardware.Accept(this);
        }

        public void VisitSensor(ISensor sensor) { }

        public void VisitParameter(IParameter parameter) { }
    }
}
