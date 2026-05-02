using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace PCHub;

public class AppSettings
{
    public string ConnectionType { get; set; } = "wifi";
    public string WifiHost { get; set; } = "pchub.local";
    public int WifiPort { get; set; } = 80;
    public string SerialPort { get; set; } = "";
    public int UpdateIntervalSec { get; set; } = 2;
    public bool MinimizeToTray { get; set; } = true;

    private static readonly string SettingsDir =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PCHub");

    private static readonly string SettingsFile =
        Path.Combine(SettingsDir, "settings.json");

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true
    };

    public static AppSettings Load()
    {
        try
        {
            if (File.Exists(SettingsFile))
            {
                string json = File.ReadAllText(SettingsFile);
                var loaded = JsonSerializer.Deserialize<AppSettings>(json, JsonOptions);
                if (loaded != null)
                    return loaded;
            }
        }
        catch
        {
            // Return defaults on any error
        }

        return new AppSettings();
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(SettingsDir);
            string json = JsonSerializer.Serialize(this, JsonOptions);
            File.WriteAllText(SettingsFile, json);
        }
        catch
        {
            // Silently ignore save errors
        }
    }
}
