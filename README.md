# PCHUB

An ESP32-based environmental monitoring hub with a 320×240 TFT display, web dashboard, and PC hardware monitoring. Measures temperature, humidity, pressure, CO₂ equivalent, and VOC; shows a 7-day weather forecast; tracks CPU/GPU/RAM metrics from a companion Windows agent; and integrates with Telegram and MQTT.

**Firmware version:** 2.0.0

---

## Features

- **Environmental sensors** — BME680 + BSEC algorithm measures temperature, humidity, atmospheric pressure, IAQ (0–500), CO₂ equivalent (ppm), and breath VOC (ppm). Calibration state persists in NVS and is restored on every reboot.
- **Real-time clock** — DS3231 RTC keeps time across reboots; optionally synced via NTP on boot and/or on a configurable interval.
- **7-day weather forecast** — powered by [Open-Meteo](https://open-meteo.com/), no API key required.
- **PC hardware monitoring** — companion Windows agent sends CPU/GPU temperature, load, power, RAM usage, and laptop battery status over Wi-Fi or USB serial.
- **TFT display** — 320×240 ST7789, four tabs: Sensors / Forecast / PC Monitor / System. Taskbar header shows dynamic Wi-Fi signal strength, SD card status, PC Agent status, and battery icon (laptops only).
- **Web dashboard** — responsive dark-themed SPA; live sensor cards, interactive 24h/7d/30d charts, settings modal.
- **Data logging** — CSV to SD card every minute; history preloaded into charts on reboot.
- **Telegram bot** — `/status`, `/help`, `/reboot` commands; configurable threshold alerts with cooldown.
- **MQTT** — publishes all sensor values to a broker on a configurable interval; subscribes to a command topic.
- **OTA firmware update** — upload a new `.bin` directly from the web UI.
- **RGB LED** — configurable mode: off / temperature-based / humidity-based / air quality colour.
- **Adaptive auto-brightness** — PWM backlight follows a configurable time-of-day schedule with adjustable min/max brightness (%), dawn start/end, and dusk start/end times; adaptive step rate (fast convergence when far from target, smooth finish when close).
- **AP mode with captive portal** — boots into its own Wi-Fi hotspot on first run; works with Android, iOS, Windows, and Chrome captive portal detection.
- **Static IP & configurable AP IP** — optional fixed IP address; AP mode IP configurable.
- **mDNS** — device reachable at `http://pchub.local/`.
- **Factory reset** — single button in the web UI erases all NVS settings.

---

## Hardware

| Component | Part |
|-----------|------|
| Microcontroller | ESP32 DevKit (38-pin or compatible) |
| Display | 2.4" / 2.8" ST7789 320×240 TFT with touch (SPI) |
| Env. sensor | BME680 (I2C, address 0x76 or 0x77) |
| Real-time clock | DS3231 (I2C) |
| Storage | MicroSD card module (SPI) |
| Status LED | Common-cathode RGB LED + 3 × 100–330 Ω resistors |

---

## Wiring

### Display (ST7789 SPI)

| TFT Pin | ESP32 GPIO |
|---------|------------|
| MOSI | 13 |
| CLK / SCK | 14 |
| MISO | 12 |
| CS | 15 |
| DC | 2 |
| RST | — (not connected) |
| BL / backlight | 27 |
| Touch CS | 33 |

### I2C bus (BME680 + DS3231)

| Signal | ESP32 GPIO |
|--------|------------|
| SDA | 32 |
| SCL | 25 |

Both modules share the same I2C bus and 3.3 V power rail.

### SD Card (SPI)

| SD Pin | ESP32 GPIO |
|--------|------------|
| MOSI | 23 |
| MISO | 19 |
| SCK | 18 |
| CS | 5 |

### RGB LED

| LED leg | ESP32 GPIO |
|---------|------------|
| Red | 22 |
| Green | 16 |
| Blue | 17 |

Use 100–330 Ω resistors in series with each leg.

---

## Firmware

### Requirements

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- Python 3 (for the WebUI build script)

### Build & flash

```bash
git clone https://github.com/yourname/pchub
cd pchub

# Build firmware (WebUI.html is converted to WebUI.h automatically by pre_build.py)
pio run -e esp32dev

# Flash to connected ESP32
pio run -e esp32dev --target upload
```

### Modifying the web UI

Edit `src/WebUI.html` — the pre-build script converts it to `src/WebUI.h` automatically on every `pio run`. Do not edit `WebUI.h` directly.

---

## First boot

1. Power on the device. It will start in **AP mode** because no Wi-Fi credentials are stored.
2. Connect your phone or laptop to the Wi-Fi network **`PCHUB-pchub`**.
3. A captive portal page opens automatically (or navigate to `192.168.4.1`).
4. Open **Settings → Network**, scan for networks, enter your SSID and password, and press **Save & reconnect**.
5. The device reboots, connects to your network, and shows its IP address on the boot screen.
6. Access the dashboard at `http://pchub.local/` or the IP shown on screen.

> **Settings survive firmware updates.** All configuration is stored in NVS (ESP32 non-volatile storage). Flashing new firmware via USB or OTA does not erase it. Only a factory reset clears NVS.

---

## Display

### Taskbar (always visible)

The top bar shows:
- **Date and time** (left side; full `Day  HH:MM:SS  DD.MM.YYYY` on non-NOW tabs)
- **Battery icon** *(laptops only, left of Wi-Fi)* — outline: green when on AC, grey when on battery; fill: green >60 %, amber >25 %, red ≤25 %
- **Wi-Fi signal** — 3-level dynamic arc icon (green = connected, red = disconnected)
- **SD card** status
- **PC Agent** status

### Tabs

| Tab | Content |
|-----|---------|
| **NOW** | Large clock; outdoor weather (icon, temperature, wind); 4 sensor mini-cards (temp, humidity, pressure, air quality); PC mini-bar (CPU/GPU/RAM) when agent is active |
| **FCST** | 7-day forecast table with weather icons, min/max temperature, wind speed |
| **MONIT** | Full PC Monitor when agent is active (CPU, GPU, RAM bars with names); Local Monitor when agent is offline (room sensor, outdoor, pressure/IAQ, logger status) |
| **SYS** | Status pills (RTC, BME, SD, WiFi); sensor/storage/WiFi/ESP32 cards with live values |

---

## BME680 / BSEC calibration

The BME680 raw gas resistance is processed by Bosch's BSEC algorithm to produce IAQ, CO₂ equivalent, and VOC values. BSEC requires a calibration warm-up:

| Accuracy | Meaning | Typical time from cold start |
|----------|---------|------------------------------|
| 0 | Warming up — outputs are placeholders (IAQ=25, CO₂=600 ppm) | 0–10 min |
| 1 | Initial baseline established | ~5–10 min |
| 2 | Medium accuracy | ~30–60 min |
| 3 | High accuracy | hours of varied air quality |

The calibration state is saved to NVS automatically:
- Immediately when accuracy first reaches 1 or improves
- Every 6 hours once accuracy ≥ 2

On reboot the saved state is restored, and accuracy 1 is typically re-established within **30–60 seconds** instead of minutes. The web dashboard shows `--` / **Warming up** on CO₂ and IAQ cards until accuracy > 0. Only a factory reset erases the BSEC state.

---

## Web dashboard

The single-page dashboard polls `/api/status` every 2.5 seconds and renders:

- **Sensor cards** — temperature, humidity, pressure, CO₂ equivalent (ppm) with colour-coded progress bars; CO₂ and IAQ show "Warming up" during BSEC accuracy = 0
- **Air Quality Index** — BSEC IAQ 0–500 scale with colour-coded 5-segment gauge
- **Charts** — 24 h / 7 d / 30 d switchable history for temperature, humidity, pressure, and CO₂
- **Footer** — live status of RTC, BME680, SD card, logger, PC Agent, and MQTT

### Settings modal (8 tabs)

| Tab | Contents |
|-----|----------|
| Network | Wi-Fi scan & credentials, static IP / DHCP, AP mode IP |
| NTP | NTP server, UTC offset, sync-on-boot, periodic sync interval |
| Device | Device name, hostname, RGB LED mode; adaptive auto-brightness schedule (min/max %, dawn/dusk times), manual brightness slider, PWM invert; PC Agent toggle; BME680 offset calibration |
| Telegram | Bot token, chat ID, per-metric alert thresholds, alert cooldown |
| MQTT | Broker address, port, credentials, topic prefix, publish interval |
| Data | Weather city, wind unit, chart history info, CSV/weather log download links |
| Info | Live runtime key-value table, sensor status |
| System | Firmware version, OTA upload, SD card clear, reboot, factory reset |

---

## API reference

Base URL: `http://pchub.local` (or device IP, port 80).

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | Web dashboard HTML |
| GET | `/api/status` | Full JSON status: sensor, weather, PC, system |
| GET | `/api/settings` | Current device settings |
| POST | `/api/settings` | Save settings (JSON body, any subset of keys) |
| GET | `/api/scan` | Wi-Fi network scan results |
| GET | `/api/history?range=24h\|7d\|30d` | Sensor history time series |
| GET | `/api/log` | Download `readings.csv` from SD card |
| GET | `/api/weather_log` | Download `weather_log.csv` from SD card |
| GET | `/api/ip` | Current IP configuration |
| POST | `/api/ip` | Save static/DHCP or AP IP settings |
| POST | `/api/pc` | Receive PC metrics JSON from the Windows agent |
| GET | `/api/telegram` | Current Telegram configuration |
| POST | `/api/telegram` | Save Telegram credentials and thresholds |
| POST | `/api/telegram/test` | Send a test Telegram message |
| GET | `/api/mqtt` | Current MQTT configuration |
| POST | `/api/mqtt` | Save MQTT broker settings |
| POST | `/api/ota` | Upload firmware `.bin` (multipart/form-data) |
| POST | `/api/sd/clear` | Erase all files from SD card |
| POST | `/api/history/clear` | Clear in-memory history buffers |
| POST | `/api/reboot` | Reboot the device |
| POST | `/api/factory-reset` | Erase all NVS settings and reboot |

### `/api/status` response structure

```json
{
  "ok": true,
  "fw_version": "2.0.0",
  "ip": "192.168.1.42",
  "device": "pchub",
  "hostname": "pchub.local",
  "wind_unit": "m/s",
  "logger_ready": true,
  "log_path": "/readings.csv",
  "sensor": {
    "rtc_ok": true,
    "bme_ok": true,
    "time": "07.05.2026  14:23:01",
    "temperature": 22.4,
    "humidity": 48.1,
    "pressure": 1013.2,
    "gas": 215.7,
    "iaq": 42.0,
    "iaq_accuracy": 2,
    "co2": 612.0
  },
  "weather": {
    "ok": true,
    "temperature": 18.0,
    "wind_speed": 3.5,
    "forecast": []
  },
  "pc": {
    "ok": true,
    "cpu_name": "Intel Core i7-7700HQ",
    "cpu_temp": 65.5,
    "cpu_load": 42.3,
    "cpu_power": 35.2,
    "gpu_name": "NVIDIA Quadro M1200",
    "gpu_temp": 58.1,
    "gpu_load": 28.5,
    "gpu_vram_used": 1200,
    "gpu_vram_total": 4096,
    "ram_used": 8900,
    "ram_total": 16200
  },
  "system": {
    "uptime_sec": 3600,
    "heap_free": 142320,
    "sd_ready": true,
    "led_mode": 1,
    "wifi_ssid": "MyNetwork",
    "ntp_server": "pool.ntp.org",
    "ntp_offset_sec": 10800,
    "backlight_pct": 80,
    "auto_backlight": true,
    "backlight_inverted": false,
    "bl_min_pct": 15,
    "bl_max_pct": 85,
    "bl_dawn_start": 360,
    "bl_dawn_end": 480,
    "bl_dusk_start": 1200,
    "bl_dusk_end": 1320,
    "pc_enabled": true,
    "weather_city": "Moscow",
    "hist24_rev": 5,
    "hist7_rev": 2,
    "hist30_rev": 1
  },
  "mqtt": { "enabled": true, "connected": true },
  "telegram": { "enabled": true, "has_token": true }
}
```

`bl_dawn_start` / `bl_dawn_end` / `bl_dusk_start` / `bl_dusk_end` are minutes since midnight (e.g. 360 = 06:00, 1320 = 22:00).

### `/api/settings` POST keys (backlight schedule)

| Key | Type | Description |
|-----|------|-------------|
| `backlight_pct` | int 0–100 | Manual brightness % |
| `backlight_inverted` | bool | Invert PWM polarity |
| `auto_backlight` | bool | Enable time-of-day auto-dim |
| `bl_min_pct` | int 0–100 | Night brightness floor |
| `bl_max_pct` | int 0–100 | Daytime brightness ceiling |
| `bl_dawn_start` | int 0–1439 | Ramp-up start (min since midnight) |
| `bl_dawn_end` | int 0–1439 | Ramp-up end |
| `bl_dusk_start` | int 0–1439 | Ramp-down start |
| `bl_dusk_end` | int 0–1439 | Ramp-down end |

### History data format

`GET /api/history?range=24h` returns:

```json
{
  "range": "24h",
  "n": 288,
  "ts":          [1746614400, 1746614700, 1746615000],
  "temperature": [22.1, 22.3, 21.8],
  "humidity":    [48.0, 47.5, 49.1],
  "pressure":    [1013.2, 1013.0, 1012.8],
  "iaq":         [42.0, 45.0, 40.0],
  "co2":         [612.0, 618.0, 608.0],
  "gas":         [215.0, 220.1, 218.5]
}
```

Resolutions: `24h` = 5-minute buckets (up to 288 points), `7d` = 1-hour (168 points), `30d` = 6-hour (720 points). History is preloaded from SD card on every boot; loading time scales with log file size (~30 s for a full 30-day log).

---

## PC Agent (Windows)

The `PCAgent/` directory contains a **.NET 10 Windows application** (WinForms, x64) that reads hardware metrics via [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) and sends them to the device.

> **Administrator rights required.** The agent manifest includes `requireAdministrator` so that LibreHardwareMonitor can install the WinRing0 kernel driver for CPU temperature and power readings.

> **Windows Defender exclusion.** Add the `PCAgent/build/` folder to Defender exclusions before first run, otherwise the WinRing0 driver (`PCAgent.sys`) will be quarantined and CPU temp/power will read as 0:
> ```powershell
> Add-MpPreference -ExclusionPath "D:\path\to\PCAgent\build"
> ```

### Build & publish

```powershell
cd PCAgent
dotnet publish -c Release -o build
```

Or open `PCAgent/PCAgent.sln` in Visual Studio 2022+.

### Configuration

On first launch a settings window appears. Choose a transport:

| Transport | When to use |
|-----------|-------------|
| **Wi-Fi** | Device is on the network. Enter the hostname (`pchub.local`) or IP and port (default 80). Agent POSTs JSON to `/api/pc`. |
| **Serial (USB)** | Device is connected directly via USB. Select the COM port; the ESP32 listens at 115200 baud. |

Settings are saved to `%LOCALAPPDATA%\PCHub\settings.json`.

| Setting | Description |
|---------|-------------|
| Update interval | Seconds between metric pushes (1–60, default 2) |
| Minimize to tray on close | Keep running in the notification area |
| Start minimized to tray | Launch hidden |
| Send battery status to display | Enable battery icon on device (laptops only) |

### Data sent to device

```json
{
  "type": "pc",
  "cn": "Intel Core i7-7700HQ",
  "ct": 65.5,   "cl": 42.3,  "cp": 35.2,
  "gn": "NVIDIA Quadro M1200",
  "gt": 58.1,   "gl": 28.5,  "gvr": 1200,  "gvt": 4096,
  "ru": 8900,   "rt": 16200,
  "bat": 87,    "bch": false, "bac": true,  "bsv": false
}
```

Battery fields are only included when **Send battery status** is enabled and a battery is detected (laptop). They are omitted on desktops.

| Field | Type | Description |
|-------|------|-------------|
| `cn` | string | CPU name (max 31 chars) |
| `ct` | float | CPU temperature (°C) |
| `cl` | float | CPU load (%) |
| `cp` | float | CPU package power (W) |
| `gn` | string | GPU name (max 31 chars) |
| `gt` | float | GPU temperature (°C) |
| `gl` | float | GPU load (%) |
| `gvr` | int | GPU VRAM used (MB) |
| `gvt` | int | GPU VRAM total (MB) |
| `ru` | int | RAM used (MB) |
| `rt` | int | RAM total (MB) |
| `bat` | int | Battery charge (%) — laptops only |
| `bch` | bool | Actively charging |
| `bac` | bool | AC power connected |
| `bsv` | bool | Battery Saver mode active |

PC data is considered stale and cleared from the display if no update arrives for **10 seconds**.

### Battery icon (display)

The battery icon appears in the taskbar header to the left of the Wi-Fi icon, only when `bat_pct > 0` is received:

| Outline colour | Fill colour | State |
|----------------|-------------|-------|
| Green | Green | On AC, charge > 60 % |
| Green | Amber | On AC, charge 25–60 % |
| Green | Red | On AC, charge ≤ 25 % |
| Grey | Green | On battery, charge > 60 % |
| Grey | Amber | On battery, charge 25–60 % |
| Grey | Red | On battery, charge ≤ 25 % |

Battery Saver mode (`bsv = true`) is detected via `PowerGetEffectivePowerMode` (Windows 10 1709+).

---

## Telegram bot

1. Create a bot via [@BotFather](https://t.me/BotFather) and copy the token.
2. Get your chat ID — send any message to your bot, then open `https://api.telegram.org/bot<TOKEN>/getUpdates` in a browser.
3. Open **Settings → Telegram**, enter the token and chat ID, enable the bot, and press **Save credentials**.

### Commands

| Command | Description |
|---------|-------------|
| `/status` | Current sensor readings and outdoor weather |
| `/help` | List of available commands |
| `/reboot` | Reboot the device (requires confirmation reply) |

### Alerts

Threshold alerts fire when a metric crosses a configured limit. Each alert type has an independent enable toggle. A shared cooldown timer (default 10 minutes) prevents repeated alerts for a sustained condition.

| Alert | Default threshold |
|-------|-------------------|
| Temperature HIGH | 30 °C |
| Temperature LOW | 15 °C |
| Humidity HIGH | 75 % |
| Humidity LOW | 30 % |
| IAQ HIGH (bad air) | 150 |

---

## MQTT

Configure the broker in **Settings → MQTT**. The device publishes to the following topics at the configured interval (default 60 s). The prefix and hostname in the table use the defaults.

| Topic | Payload |
|-------|---------|
| `pchub/pchub/temperature` | °C |
| `pchub/pchub/humidity` | % RH |
| `pchub/pchub/pressure` | hPa |
| `pchub/pchub/gas` | raw kΩ |
| `pchub/pchub/outdoor/temperature` | °C (from weather API) |
| `pchub/pchub/state` | Full JSON sensor payload |
| `pchub/pchub/cmd` | *(subscribe)* send `reboot` to reboot device |

---

## Project structure

```
PCHUB/
├── src/
│   ├── main.cpp              Boot flow, sensor loop, weather, logging
│   ├── API.cpp               REST API server, history buffers, OTA
│   ├── UI.cpp                TFT rendering, touch, display tabs
│   ├── Sensors.cpp           BME680/BSEC, DS3231, WiFi, NTP
│   ├── Weather.cpp           Open-Meteo geocoding & forecast
│   ├── Logger.cpp            CSV logging to SD
│   ├── PCAgent.cpp           Serial JSON parser for PC metrics
│   ├── MQTT.cpp              MQTT broker client
│   ├── Telegram.cpp          Telegram bot polling & alerts
│   ├── Backlight.cpp         PWM backlight — adaptive auto schedule
│   ├── Led.cpp               RGB LED
│   ├── RuntimeSettings.cpp   NVS persistence layer
│   └── WebUI.html            Web dashboard source (auto-built → WebUI.h)
├── include/
│   ├── Config.h              Pin definitions, compile-time constants
│   ├── PCData.h              PCData struct (PC metrics + battery)
│   ├── Version.h             FW_VERSION string
│   └── *.h                   Module headers
├── PCAgent/                  Windows companion app (.NET 10, WinForms, x64)
│   ├── PCAgent.csproj
│   ├── Program.cs
│   ├── MainForm.cs           System-tray WinForms UI
│   ├── HardwareMonitor.cs    LHM + WMI battery reading
│   ├── PcMetrics.cs          Data model + JSON serialisation
│   ├── PcHubClient.cs        Wi-Fi / Serial transport
│   └── AppSettings.cs        Settings persistence
├── tools/
│   └── pre_build.py          PlatformIO pre-build hook (HTML → H)
└── platformio.ini
```

---

## Dependencies

### Firmware (ESP32)

| Library | Version | Purpose |
|---------|---------|---------|
| TFT_eSPI | ^2.5.43 | ST7789 display driver |
| BSEC Software Library | ^1.8 | BME680 IAQ/CO₂/VOC algorithm |
| RTClib | ^2.1.1 | DS3231 real-time clock |
| ArduinoJson | ^7.0.0 | JSON serialization |
| PubSubClient | ^2.8 | MQTT client |

### PC Agent (.NET 10)

| Package | Version | Purpose |
|---------|---------|---------|
| LibreHardwareMonitorLib | 0.9.3 | CPU/GPU/RAM sensor reading |
| System.Management | (transitive) | WMI battery detection |

Weather data: [Open-Meteo](https://open-meteo.com/) — free, no API key required.

---

## License

MIT — see [LICENSE](LICENSE) for details.
