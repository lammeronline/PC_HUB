#include "API.h"
#include "Config.h"
#include "Version.h"
#include "Logger.h"
#include "RuntimeSettings.h"
#include "Backlight.h"
#include "Telegram.h"
#include "MQTT.h"
#include "WebUI.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <time.h>

static WebServer          server(80);
static const SensorData  *_sensor  = nullptr;
static const WeatherData *_weather = nullptr;
static PCData            *_pcData  = nullptr;
static bool               _apiReady = false;
static bool               _sdReady  = false;

static float weatherWindForApi() {
    if (!_weather) return 0.0f;
    return RuntimeSettings::windMetric() ? (_weather->wind_speed / 3.6f) : _weather->wind_speed;
}

struct HistoryPoint {
    uint32_t ts = 0;
    float temperature = 0.0f;
    float humidity = 0.0f;
    float pressure = 0.0f;
    float gas = 0.0f;   // raw kΩ (from SD log)
    float iaq = 0.0f;   // IAQ 0-500 (live BSEC)
    float co2 = 0.0f;   // CO₂ equivalent ppm (live BSEC)
};

template <size_t N>
class HistoryRing {
public:
    void push(const HistoryPoint &p) {
        _buf[_head] = p;
        _head = (_head + 1) % N;
        if (_count < N) _count++;
    }

    size_t size() const { return _count; }

    HistoryPoint at(size_t i) const {
        size_t idx = (_head + N - _count + i) % N;
        return _buf[idx];
    }

    void clear() { _head = 0; _count = 0; }

private:
    HistoryPoint _buf[N];
    size_t _head = 0;
    size_t _count = 0;
};

struct HistoryAccumulator {
    bool active = false;
    uint32_t bucketStart = 0;
    uint16_t count = 0;
    float temperatureSum = 0.0f;
    float humiditySum = 0.0f;
    float pressureSum = 0.0f;
    float gasSum = 0.0f;
    float iaqSum = 0.0f;
    float co2Sum = 0.0f;
};

static HistoryRing<288> _hist24;
static HistoryRing<168> _hist7;
static HistoryRing<720> _hist30;
static HistoryAccumulator _acc24;
static HistoryAccumulator _acc7;
static HistoryAccumulator _acc30;
static uint32_t _hist24Rev = 0;
static uint32_t _hist7Rev = 0;
static uint32_t _hist30Rev = 0;
static uint64_t _sdTotalMb = 0;
static uint64_t _sdUsedMb  = 0;
static bool _otaFailed = false;
static String _otaMessage;
static bool _pendingRestart = false;
static bool _isApMode      = false;

bool apiPendingRestart()    { return _pendingRestart; }
void setApiApMode(bool v)   { _isApMode = v; }

static HistoryPoint makeHistoryPoint(uint32_t ts, const SensorData &sensor) {
    HistoryPoint p;
    p.ts = ts;
    p.temperature = sensor.temperature;
    p.humidity = sensor.humidity;
    p.pressure = sensor.pressure;
    p.gas = sensor.gas;
    p.iaq = sensor.iaq;
    p.co2 = sensor.co2;
    return p;
}

template <size_t N>
static void flushHistoryBucket(HistoryAccumulator &acc, HistoryRing<N> &ring,
                               uint32_t &rev) {
    if (!acc.active || acc.count == 0) return;

    HistoryPoint p;
    p.ts = acc.bucketStart;
    p.temperature = acc.temperatureSum / acc.count;
    p.humidity = acc.humiditySum / acc.count;
    p.pressure = acc.pressureSum / acc.count;
    p.gas = acc.gasSum / acc.count;
    p.iaq = acc.iaqSum / acc.count;
    p.co2 = acc.co2Sum / acc.count;
    ring.push(p);
    rev++;
}

template <size_t N>
static void updateHistoryBucket(HistoryAccumulator &acc, HistoryRing<N> &ring,
                                uint32_t &rev, uint32_t bucketSec,
                                uint32_t nowSec, const SensorData &sensor) {
    if (!sensor.bme_ok || bucketSec == 0) return;

    uint32_t bucketStart = nowSec - (nowSec % bucketSec);
    if (!acc.active) {
        acc.active = true;
        acc.bucketStart = bucketStart;
        acc.count = 1;
        acc.temperatureSum = sensor.temperature;
        acc.humiditySum = sensor.humidity;
        acc.pressureSum = sensor.pressure;
        acc.gasSum = sensor.gas;
        acc.iaqSum = sensor.iaq;
        acc.co2Sum = sensor.co2;
        ring.push(makeHistoryPoint(nowSec, sensor));
        rev++;
        return;
    }

    if (acc.bucketStart != bucketStart) {
        flushHistoryBucket(acc, ring, rev);
        acc.bucketStart = bucketStart;
        acc.count = 1;
        acc.temperatureSum = sensor.temperature;
        acc.humiditySum = sensor.humidity;
        acc.pressureSum = sensor.pressure;
        acc.gasSum = sensor.gas;
        acc.iaqSum = sensor.iaq;
        acc.co2Sum = sensor.co2;
        return;
    }

    acc.count++;
    acc.temperatureSum += sensor.temperature;
    acc.humiditySum += sensor.humidity;
    acc.pressureSum += sensor.pressure;
    acc.gasSum += sensor.gas;
    acc.iaqSum += sensor.iaq;
    acc.co2Sum += sensor.co2;
}

static void updateHistory() {
    if (!_sensor) return;
    static unsigned long lastSampleMs = 0;
    unsigned long nowMs = millis();
    if (nowMs - lastSampleMs < 1000UL) return;
    lastSampleMs = nowMs;

    uint32_t nowSec = getRTCUnixTime();
    if (nowSec == 0) nowSec = millis() / 1000UL; // fallback when no RTC
    updateHistoryBucket(_acc24, _hist24, _hist24Rev, 5UL * 60UL, nowSec, *_sensor);
    updateHistoryBucket(_acc7, _hist7, _hist7Rev, 60UL * 60UL, nowSec, *_sensor);
    updateHistoryBucket(_acc30, _hist30, _hist30Rev, 6UL * 60UL * 60UL, nowSec, *_sensor);
}

// Читает хвост CSV-лога и заполняет кольцевые буферы истории.
// Объявлена в API.h — вызывается из main.cpp при буте.
// Максимальное окно чтения — последние READ_WINDOW байт (≈ 30 дней).
void preloadHistoryFromSD() {
    const char *path = "/readings.csv";
    if (!SD.exists(path)) return;

    File f = SD.open(path, FILE_READ);
    if (!f) return;

    uint32_t rtcNow = getRTCUnixTime();
    if (rtcNow == 0) { f.close(); return; } // нет RTC — нет абсолютного времени

    // Ограничение: читаем не более последних ~30 дней (≈ 3,5 МБ при 60 с интервале)
    const size_t READ_WINDOW = 3500UL * 1024UL;
    size_t fileSize = f.size();
    if (fileSize > READ_WINDOW + 256) {
        f.seek(fileSize - READ_WINDOW);
        f.readStringUntil('\n'); // пропускаем неполную строку
    } else {
        f.readStringUntil('\n'); // пропускаем заголовок
    }

    // Следующий допустимый момент для каждого кольцевого буфера (time-based decimation)
    uint32_t next24 = 0, next7 = 0, next30 = 0;

    int loaded = 0;
    char buf[140];
    while (f.available()) {
        int len = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
        if (len < 24) continue;
        buf[len] = '\0';

        // Формат: "DD.MM.YYYY  HH:MM:SS",rtc_ok,bme_ok,temp,hum,press,gas,...
        int dd, mm, yy, hh, mi, ss, rtc_ok, bme_ok_i;
        float temp, hum, press, gas, co2 = 0.0f;
        int parsed = sscanf(buf, "\"%d.%d.%d  %d:%d:%d\",%d,%d,%f,%f,%f,%f,%f",
                   &dd, &mm, &yy, &hh, &mi, &ss,
                   &rtc_ok, &bme_ok_i, &temp, &hum, &press, &gas, &co2);
        if (parsed < 12) continue;
        if (!bme_ok_i || yy < 2020 || mm < 1 || mm > 12) continue;

        // Приближённое unix-время строки (без учёта секунд DST — достаточно для истории)
        // Используем RTClib DateTime напрямую невозможно без включения заголовка,
        // поэтому считаем вручную через простую формулу Томаса Дальмана.
        int a = (14 - mm) / 12;
        int y = yy + 4800 - a;
        int m = mm + 12 * a - 3;
        uint32_t jdn = dd + (153 * m + 2) / 5 + 365UL * y + y / 4 - y / 100 + y / 400 - 32045;
        uint32_t rowUnix = (jdn - 2440588UL) * 86400UL + hh * 3600UL + mi * 60UL + ss;

        if (rowUnix > rtcNow) continue;
        uint32_t age = rtcNow - rowUnix;
        if (age > 30UL * 24 * 3600) continue;

        HistoryPoint p;
        p.ts          = rowUnix;
        p.temperature = temp;
        p.humidity    = hum;
        p.pressure    = press;
        p.gas         = gas;
        p.co2         = co2;

        // Помещаем в кольцо только если прошёл нужный интервал (decimation)
        if (age <= 24UL * 3600 && rowUnix >= next24) {
            _hist24.push(p); _hist24Rev++;
            next24 = rowUnix + 5 * 60; // бакет 5 минут
        }
        if (age <= 7UL * 24 * 3600 && rowUnix >= next7) {
            _hist7.push(p); _hist7Rev++;
            next7 = rowUnix + 60 * 60; // бакет 1 час
        }
        if (rowUnix >= next30) {
            _hist30.push(p); _hist30Rev++;
            next30 = rowUnix + 6 * 60 * 60; // бакет 6 часов
        }
        loaded++;
    }
    f.close();
    Serial.printf("History preload: %d points from SD\n", loaded);
}

// Streams the response through a 256-byte stack buffer — no heap allocation for
// the JSON payload itself, avoiding fragmentation from ~1440 temporary Strings.
template <size_t N>
static void sendHistoryJson(const HistoryRing<N> &ring, const char *range) {
    const size_t total  = ring.size();
    const size_t maxPts = 360;
    const size_t step   = total > maxPts ? (total + maxPts - 1) / maxPts : 1;
    const size_t pts    = (total + step - 1) / step;

    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");

    char   buf[256];
    size_t pos = 0;

    auto flush = [&]() {
        if (pos) { server.sendContent(buf, pos); pos = 0; }
    };
    auto cat = [&](const char *s, size_t n) {
        if (pos + n >= sizeof(buf)) flush();
        memcpy(buf + pos, s, n); pos += n;
    };
    auto catS = [&](const char *s) { cat(s, strlen(s)); };

    char tmp[32]; size_t n;

    n = (size_t)snprintf(tmp, sizeof(tmp), "{\"range\":\"%s\",\"n\":%u,\"ts\":[",
                         range, (unsigned)pts);
    cat(tmp, n);
    for (size_t i = 0; i < total; i += step) {
        n = (size_t)snprintf(tmp, sizeof(tmp), i ? ",%lu" : "%lu",
                             (unsigned long)ring.at(i).ts);
        cat(tmp, n);
    }
    catS("],\"temperature\":[");
    for (size_t i = 0; i < total; i += step) {
        n = (size_t)snprintf(tmp, sizeof(tmp), i ? ",%.1f" : "%.1f",
                             ring.at(i).temperature);
        cat(tmp, n);
    }
    catS("],\"humidity\":[");
    for (size_t i = 0; i < total; i += step) {
        n = (size_t)snprintf(tmp, sizeof(tmp), i ? ",%.1f" : "%.1f",
                             ring.at(i).humidity);
        cat(tmp, n);
    }
    catS("],\"pressure\":[");
    for (size_t i = 0; i < total; i += step) {
        n = (size_t)snprintf(tmp, sizeof(tmp), i ? ",%.1f" : "%.1f",
                             ring.at(i).pressure);
        cat(tmp, n);
    }
    catS("],\"gas\":[");
    for (size_t i = 0; i < total; i += step) {
        n = (size_t)snprintf(tmp, sizeof(tmp), i ? ",%.1f" : "%.1f",
                             ring.at(i).gas);
        cat(tmp, n);
    }
    catS("],\"iaq\":[");
    for (size_t i = 0; i < total; i += step) {
        n = (size_t)snprintf(tmp, sizeof(tmp), i ? ",%.0f" : "%.0f",
                             ring.at(i).iaq);
        cat(tmp, n);
    }
    catS("],\"co2\":[");
    for (size_t i = 0; i < total; i += step) {
        n = (size_t)snprintf(tmp, sizeof(tmp), i ? ",%.0f" : "%.0f",
                             ring.at(i).co2);
        cat(tmp, n);
    }
    catS("]}");
    flush();
}

static void sendJson(JsonDocument &doc) {
    String out;
    out.reserve(measureJson(doc) + 1);  // exact size — no reallocation
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static String jsonEscape(const String &text) {
    String out;
    out.reserve(text.length() + 8);
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out += c;
        }
    }
    return out;
}

static void handleStatus() {
    if (!_sensor || !_weather) {
        server.send(503, "application/json", "{\"ok\":false,\"error\":\"data not ready\"}");
        return;
    }

    JsonDocument doc;
    doc["ok"]                   = true;
    doc["fw_version"]           = FW_VERSION;
    doc["device"]               = RuntimeSettings::deviceName();
    doc["ip"]                   = WiFi.localIP().toString();
    doc["hostname"]             = RuntimeSettings::hostname() + ".local";
    doc["wind_unit"]            = RuntimeSettings::windMetric() ? "m/s" : "km/h";
    doc["log_path"]             = readingsLogPath();
    doc["logger_ready"]         = loggerReady();
    doc["weather_log_path"]     = weatherLogPath();
    doc["weather_logger_ready"] = weatherLoggerReady();

    JsonObject system = doc["system"].to<JsonObject>();
    system["sd_ready"]             = _sdReady;
    system["api_ready"]            = _apiReady;
    system["heap_free"]            = ESP.getFreeHeap();
    system["uptime_sec"]           = millis() / 1000UL;
    system["weather_city"]         = RuntimeSettings::weatherCity();
    system["weather_interval_sec"] = WEATHER_UPDATE_INTERVAL_SEC;
    system["log_interval_sec"]     = DATA_LOG_INTERVAL_SEC;
    system["hist24_rev"]           = _hist24Rev;
    system["hist7_rev"]            = _hist7Rev;
    system["hist30_rev"]           = _hist30Rev;
    system["hist24_interval_min"]  = 5;
    system["hist7_interval_min"]   = 60;
    system["hist30_interval_min"]  = 360;
    system["wifi_ssid"]            = RuntimeSettings::wifiSsid();
    system["ntp_server"]           = RuntimeSettings::ntpServer();
    system["ntp_offset_sec"]       = RuntimeSettings::ntpOffsetSec();
    system["backlight_pct"]        = Backlight::brightness();
    system["backlight_inverted"]   = RuntimeSettings::backlightInverted();
    system["auto_backlight"]       = RuntimeSettings::autoBacklight();
    system["led_mode"]             = RuntimeSettings::ledMode();
    system["pc_enabled"]           = RuntimeSettings::pcEnabled();
    system["ntp_enabled"]          = RuntimeSettings::ntpEnabled();
    system["ntp_sync_on_boot"]     = RuntimeSettings::ntpSyncOnBoot();
    system["ntp_sync_interval_h"]  = RuntimeSettings::ntpSyncIntervalH();
    system["sd_total_mb"]          = _sdTotalMb;
    system["sd_used_mb"]           = _sdUsedMb;
    system["weather_log_enabled"]  = RuntimeSettings::weatherLogEnabled();
    system["bme_temp_offset"]      = RuntimeSettings::bmeTempOffset();
    system["bme_hum_offset"]       = RuntimeSettings::bmeHumOffset();

    JsonObject mqtt = doc["mqtt"].to<JsonObject>();
    mqtt["enabled"]      = RuntimeSettings::mqttEnabled();
    mqtt["connected"]    = MQTT::connected();
    mqtt["broker"]       = RuntimeSettings::mqttBroker();
    mqtt["port"]         = RuntimeSettings::mqttPort();
    mqtt["prefix"]       = RuntimeSettings::mqttPrefix();
    mqtt["interval_sec"] = RuntimeSettings::mqttIntervalSec();
    mqtt["has_user"]     = RuntimeSettings::mqttUser().length() > 0;

    JsonObject tg = doc["telegram"].to<JsonObject>();
    tg["enabled"]      = RuntimeSettings::tgEnabled();
    tg["has_token"]    = RuntimeSettings::tgToken().length() > 0;
    tg["chat_id"]      = RuntimeSettings::tgChatId();
    tg["cooldown_min"] = RuntimeSettings::tgCooldownMin();
    tg["temp_hi_en"]   = RuntimeSettings::tgTempHiEn();
    tg["temp_hi"]      = RuntimeSettings::tgTempHi();
    tg["temp_lo_en"]   = RuntimeSettings::tgTempLoEn();
    tg["temp_lo"]      = RuntimeSettings::tgTempLo();
    tg["hum_hi_en"]    = RuntimeSettings::tgHumHiEn();
    tg["hum_hi"]       = RuntimeSettings::tgHumHi();
    tg["hum_lo_en"]    = RuntimeSettings::tgHumLoEn();
    tg["hum_lo"]       = RuntimeSettings::tgHumLo();
    tg["gas_lo_en"]    = RuntimeSettings::tgGasLoEn();
    tg["gas_lo"]       = RuntimeSettings::tgGasLo();

    JsonObject sensor = doc["sensor"].to<JsonObject>();
    sensor["rtc_ok"]      = _sensor->rtc_ok;
    sensor["bme_ok"]      = _sensor->bme_ok;
    sensor["time"]        = _sensor->timeStr;
    sensor["temperature"]   = _sensor->temperature;
    sensor["humidity"]      = _sensor->humidity;
    sensor["pressure"]      = _sensor->pressure;
    sensor["gas"]           = _sensor->gas;
    sensor["iaq"]           = _sensor->iaq;
    sensor["iaq_accuracy"]  = _sensor->iaq_accuracy;
    sensor["co2"]           = _sensor->co2;

    JsonObject weather = doc["weather"].to<JsonObject>();
    float apiWindSpeed = weatherWindForApi();
    weather["ok"]           = _weather->ok;
    weather["temperature"]  = _weather->temperature;
    weather["humidity"]     = _weather->humidity;
    weather["wind_speed"]   = apiWindSpeed;
    weather["weather_code"] = _weather->weather_code;
    weather["is_day"]       = _weather->is_day;

    JsonArray forecast = weather["forecast"].to<JsonArray>();
    for (int i = 0; i < 7; i++) {
        JsonObject day = forecast.add<JsonObject>();
        day["date"]         = _weather->forecast[i].date;
        day["day"]          = _weather->forecast[i].day;
        day["temp_min"]     = _weather->forecast[i].temp_min;
        day["temp_max"]     = _weather->forecast[i].temp_max;
        day["wind_speed"]   = RuntimeSettings::windMetric()
                              ? (_weather->forecast[i].wind_speed / 3.6f)
                              : _weather->forecast[i].wind_speed;
        day["weather_code"] = _weather->forecast[i].weather_code;
    }

    JsonObject ipCfg = doc["ip_config"].to<JsonObject>();
    ipCfg["static_enabled"] = RuntimeSettings::staticIpEnabled();
    ipCfg["static_ip"]      = RuntimeSettings::staticIp();
    ipCfg["static_gw"]      = RuntimeSettings::staticGateway();
    ipCfg["static_sn"]      = RuntimeSettings::staticSubnet();
    ipCfg["static_dns"]     = RuntimeSettings::staticDns();
    ipCfg["ap_ip"]          = RuntimeSettings::apIp();

    JsonObject pc = doc["pc"].to<JsonObject>();
    if (_pcData) {
        unsigned long ageMs = _pcData->lastMs ? millis() - _pcData->lastMs : 0;
        pc["ok"]             = pcFresh(*_pcData);
        pc["raw_ok"]         = _pcData->ok;
        pc["age_ms"]         = ageMs;
        pc["cpu_temp"]       = _pcData->cpu_temp;
        pc["cpu_load"]       = _pcData->cpu_load;
        pc["cpu_power"]      = _pcData->cpu_power;
        pc["gpu_temp"]       = _pcData->gpu_temp;
        pc["gpu_load"]       = _pcData->gpu_load;
        pc["gpu_vram_used"]  = _pcData->gpu_vram_used;
        pc["gpu_vram_total"] = _pcData->gpu_vram_total;
        pc["ram_used"]       = _pcData->ram_used;
        pc["ram_total"]      = _pcData->ram_total;
    } else {
        pc["ok"] = false;
    }

    sendJson(doc);
}

static void handleLog() {
    if (!_sdReady || !SD.exists(readingsLogPath())) {
        server.send(404, "text/plain", "Log file not found");
        return;
    }
    File file = SD.open(readingsLogPath(), FILE_READ);
    if (!file) {
        server.send(500, "text/plain", "Failed to open log file");
        return;
    }
    server.streamFile(file, "text/csv");
    file.close();
}

static void handleWeatherLog() {
    if (!_sdReady || !SD.exists(weatherLogPath())) {
        server.send(404, "text/plain", "Weather log not found");
        return;
    }
    File file = SD.open(weatherLogPath(), FILE_READ);
    if (!file) {
        server.send(500, "text/plain", "Failed to open weather log");
        return;
    }
    server.streamFile(file, "text/csv");
    file.close();
}

static void handleHistory() {
    String range = server.arg("range");
    if (range == "7d") {
        sendHistoryJson(_hist7, "7d");
    } else if (range == "30d") {
        sendHistoryJson(_hist30, "30d");
    } else {
        sendHistoryJson(_hist24, "24h");
    }
}

static void handleSettingsGet() {
    JsonDocument doc;
    doc["ok"]             = true;
    doc["wifi_ssid"]      = RuntimeSettings::wifiSsid();
    doc["device"]         = RuntimeSettings::deviceName();
    doc["hostname"]       = RuntimeSettings::hostname();
    doc["weather_city"]   = RuntimeSettings::weatherCity();
    doc["wind_unit"]      = RuntimeSettings::windMetric() ? "m/s" : "km/h";
    doc["ntp_server"]     = RuntimeSettings::ntpServer();
    doc["ntp_offset_sec"] = RuntimeSettings::ntpOffsetSec();
    doc["backlight_pct"]      = Backlight::brightness();
    doc["backlight_inverted"] = RuntimeSettings::backlightInverted();
    doc["led_mode"]           = RuntimeSettings::ledMode();
    doc["bme_temp_offset"]    = RuntimeSettings::bmeTempOffset();
    doc["bme_hum_offset"]     = RuntimeSettings::bmeHumOffset();
    sendJson(doc);
}

static void handleSettingsPost() {
    String body = server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
        return;
    }

    bool wifiChanged = false;
    bool ntpChanged = false;
    bool deviceChanged = false;
    bool weatherChanged = false;
    bool windChanged = false;
    bool backlightChanged = false;
    bool ledChanged = false;

    if (doc["wifi_ssid"].is<const char*>()) {
        String ssid = doc["wifi_ssid"].as<String>();
        String pass = doc["wifi_password"] | "";
        ssid.trim();
        if (ssid.length() > 0) {
            RuntimeSettings::saveWifi(ssid, pass);
            wifiChanged = true;
        }
    }

    if (doc["ntp_server"].is<const char*>() || doc["ntp_offset_sec"].is<long>()) {
        String serverName = doc["ntp_server"] | RuntimeSettings::ntpServer();
        long offsetSec = doc["ntp_offset_sec"] | RuntimeSettings::ntpOffsetSec();
        serverName.trim();
        RuntimeSettings::saveNtp(serverName, offsetSec);
        String ntpServer = RuntimeSettings::ntpServer();
        configTime(RuntimeSettings::ntpOffsetSec(), 0, ntpServer.c_str());
        ntpChanged = true;
    }

    if (doc["device"].is<const char*>() || doc["hostname"].is<const char*>()) {
        String deviceName = doc["device"] | RuntimeSettings::deviceName();
        String hostname = doc["hostname"] | RuntimeSettings::hostname();
        RuntimeSettings::saveDeviceIdentity(deviceName, hostname);
        String appliedHostname = RuntimeSettings::hostname();
        WiFi.setHostname(appliedHostname.c_str());
        MDNS.end();
        MDNS.begin(appliedHostname.c_str());
        MDNS.addService("http", "tcp", 80);
        deviceChanged = true;
    }

    if (doc["weather_city"].is<const char*>()) {
        String city = doc["weather_city"].as<String>();
        city.trim();
        if (city.length() > 0) {
            RuntimeSettings::saveWeatherCity(city);
            weatherChanged = true;
        }
    }

    if (doc["wind_unit"].is<const char*>()) {
        String unit = doc["wind_unit"].as<String>();
        unit.trim();
        unit.toLowerCase();
        RuntimeSettings::saveWindMetric(unit == "m/s" || unit == "ms");
        windChanged = true;
    }

    if (doc["backlight_pct"].is<int>()) {
        int pct = doc["backlight_pct"].as<int>();
        Backlight::setBrightness((uint8_t)constrain(pct, 0, 100), true);
        backlightChanged = true;
    }

    if (doc["backlight_inverted"].is<bool>()) {
        RuntimeSettings::saveBacklightInverted(doc["backlight_inverted"].as<bool>());
        Backlight::apply(RuntimeSettings::backlightPercent());
        backlightChanged = true;
    }

    if (doc["auto_backlight"].is<bool>()) {
        RuntimeSettings::saveAutoBacklight(doc["auto_backlight"].as<bool>());
        if (!RuntimeSettings::autoBacklight())
            Backlight::apply(RuntimeSettings::backlightPercent());
        backlightChanged = true;
    }

    if (doc["led_mode"].is<int>()) {
        RuntimeSettings::saveLedMode((uint8_t)constrain(doc["led_mode"].as<int>(), 0, 3));
        ledChanged = true;
    }

    if (doc["pc_enabled"].is<bool>()) {
        RuntimeSettings::savePcEnabled(doc["pc_enabled"].as<bool>());
    }

    if (doc["bme_temp_offset"].is<float>() || doc["bme_hum_offset"].is<float>()) {
        float tOff = doc["bme_temp_offset"] | RuntimeSettings::bmeTempOffset();
        float hOff = doc["bme_hum_offset"]  | RuntimeSettings::bmeHumOffset();
        tOff = constrain(tOff, -10.0f, 10.0f);
        hOff = constrain(hOff, -20.0f, 20.0f);
        RuntimeSettings::saveBmeCalibration(tOff, hOff);
    }

    if (doc["ntp_enabled"].is<bool>()) {
        RuntimeSettings::saveNtpEnabled(doc["ntp_enabled"].as<bool>());
    }
    if (doc["ntp_sync_on_boot"].is<bool>()) {
        RuntimeSettings::saveNtpSyncOnBoot(doc["ntp_sync_on_boot"].as<bool>());
    }
    if (doc["ntp_sync_interval_h"].is<int>()) {
        RuntimeSettings::saveNtpSyncIntervalH(
            (uint8_t)constrain(doc["ntp_sync_interval_h"].as<int>(), 0, 255));
    }

    if (doc["weather_log_enabled"].is<bool>()) {
        RuntimeSettings::saveWeatherLogEnabled(doc["weather_log_enabled"].as<bool>());
    }

    bool inApMode = _isApMode;

    JsonDocument resp;
    resp["ok"] = true;
    resp["wifi_changed"] = wifiChanged;
    resp["ntp_changed"] = ntpChanged;
    resp["device_changed"] = deviceChanged;
    resp["weather_changed"] = weatherChanged;
    resp["wind_changed"] = windChanged;
    resp["backlight_changed"] = backlightChanged;
    resp["led_changed"] = ledChanged;
    if (wifiChanged && inApMode) {
        resp["rebooting"] = true;
        resp["ap_mode"]   = true;
    }
    sendJson(resp);

    if (wifiChanged) {
        if (inApMode) {
            // Отложенный restart — выполнится из main loop, чтобы TFT успел обновиться
            _pendingRestart = true;
        } else {
            WiFi.disconnect(false, false);
            WiFi.mode(WIFI_STA);
            String ssid = RuntimeSettings::wifiSsid();
            String pass = RuntimeSettings::wifiPassword();
            WiFi.begin(ssid.c_str(), pass.c_str());
        }
    }
}

static void handleScan() {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {
        WiFi.scanNetworks(true, true);
        server.send(200, "application/json", "{\"ok\":false,\"scanning\":true}");
        return;
    }
    if (n == WIFI_SCAN_RUNNING) {
        server.send(200, "application/json", "{\"ok\":false,\"scanning\":true}");
        return;
    }
    String out;
    out.reserve(n * 64 + 16);
    out = "{\"ok\":true,\"networks\":[";
    for (int i = 0; i < n; i++) {
        if (i) out += ',';
        out += "{\"ssid\":\"";
        out += jsonEscape(WiFi.SSID(i));
        out += "\",\"rssi\":";
        out += WiFi.RSSI(i);
        out += ",\"secure\":";
        out += (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true");
        out += "}";
    }
    out += "]}";
    WiFi.scanDelete();
    server.send(200, "application/json", out);
}

static void handleReboot() {
    server.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    delay(300);
    ESP.restart();
}

static bool removePathRecursive(const String &path) {
    File entry = SD.open(path);
    if (!entry) return false;

    if (!entry.isDirectory()) {
        entry.close();
        return SD.remove(path);
    }

    bool ok = true;
    File child = entry.openNextFile();
    while (child) {
        String childPath = child.path();
        bool childIsDir = child.isDirectory();
        child.close();

        if (childIsDir) {
            if (!removePathRecursive(childPath)) ok = false;
            if (!SD.rmdir(childPath)) ok = false;
        } else if (!SD.remove(childPath)) {
            ok = false;
        }

        child = entry.openNextFile();
    }
    entry.close();
    return ok;
}

static void handleHistoryClear() {
    _hist24.clear(); _hist7.clear(); _hist30.clear();
    _acc24 = {}; _acc7 = {}; _acc30 = {};
    _hist24Rev++; _hist7Rev++; _hist30Rev++;
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleSdClear() {
    if (!_sdReady) {
        server.send(503, "application/json", "{\"ok\":false,\"error\":\"sd not ready\"}");
        return;
    }

    File root = SD.open("/");
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"sd open failed\"}");
        return;
    }

    bool ok = true;
    File entry = root.openNextFile();
    while (entry) {
        String path = entry.path();
        bool isDir = entry.isDirectory();
        entry.close();

        if (isDir) {
            if (!removePathRecursive(path)) ok = false;
            if (!SD.rmdir(path)) ok = false;
        } else if (!SD.remove(path)) {
            ok = false;
        }

        entry = root.openNextFile();
    }
    root.close();

    initLogger(_sdReady);

    // Reset in-memory history so the charts don't show stale data after SD clear
    _hist24.clear(); _hist7.clear(); _hist30.clear();
    _acc24 = {}; _acc7 = {}; _acc30 = {};
    _hist24Rev++; _hist7Rev++; _hist30Rev++;

    server.send(ok ? 200 : 500, "application/json",
                ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"clear failed\"}");
}

static void otaFail(const char *message) {
    _otaFailed = true;
    _otaMessage = message;
    Update.abort();
}

static void handleOtaUpload() {
    HTTPUpload &upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
        _otaFailed = false;
        _otaMessage = "";
        Serial.printf("OTA: start %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            otaFail("ota begin failed");
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (!_otaFailed && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            otaFail("ota write failed");
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (!_otaFailed && !Update.end(true)) {
            otaFail("ota end failed");
        }
        if (!_otaFailed) {
            Serial.printf("OTA: success %u bytes\n", upload.totalSize);
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        otaFail("ota aborted");
    }
}

static void handleOtaDone() {
    if (_otaFailed) {
        String out = "{\"ok\":false,\"error\":\"" + jsonEscape(_otaMessage) + "\"}";
        server.send(500, "application/json", out);
        return;
    }

    server.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    delay(500);
    ESP.restart();
}

// POST /api/pc  — receives PC metrics from the PC Agent (WiFi path)
static void handlePCPost() {
    if (!_pcData) {
        server.send(503, "application/json", "{\"ok\":false,\"error\":\"not ready\"}");
        return;
    }
    if (!RuntimeSettings::pcEnabled()) {
        server.send(200, "application/json", "{\"ok\":false,\"paused\":true}");
        return;
    }
    String body = server.arg("plain");
    if (body.isEmpty() && server.args() > 0)
        body = server.arg(0);   // fallback for some WebServer versions
    if (body.isEmpty()) {
        Serial.printf("[PCPost] empty body args=%d ct=%s\n",
            server.args(), server.header("Content-Type").c_str());
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
        return;
    }
    const char *type = doc["type"] | "";
    if (strcmp(type, "pc") != 0) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"wrong type\"}");
        return;
    }
    const char *cn = doc["cn"] | "";
    strncpy(_pcData->cpu_name, cn, sizeof(_pcData->cpu_name) - 1);
    _pcData->cpu_name[sizeof(_pcData->cpu_name) - 1] = '\0';
    const char *gn = doc["gn"] | "";
    strncpy(_pcData->gpu_name, gn, sizeof(_pcData->gpu_name) - 1);
    _pcData->gpu_name[sizeof(_pcData->gpu_name) - 1] = '\0';
    _pcData->cpu_temp       = doc["ct"]  | 0.0f;
    _pcData->cpu_load       = doc["cl"]  | 0.0f;
    _pcData->cpu_power      = doc["cp"]  | 0.0f;
    _pcData->gpu_temp       = doc["gt"]  | 0.0f;
    _pcData->gpu_load       = doc["gl"]  | 0.0f;
    _pcData->gpu_vram_used  = doc["gvr"] | (uint16_t)0;
    _pcData->gpu_vram_total = doc["gvt"] | (uint16_t)0;
    _pcData->ram_used       = doc["ru"]  | (uint32_t)0;
    _pcData->ram_total      = doc["rt"]  | (uint32_t)0;
    _pcData->ok             = true;
    _pcData->lastMs         = millis();
    Serial.printf("[PC] CPU=%s %.0fC/%.0f%% GPU=%s %.0fC/%.0f%% RAM=%lu/%luMB\n",
        _pcData->cpu_name, _pcData->cpu_temp, _pcData->cpu_load,
        _pcData->gpu_name, _pcData->gpu_temp, _pcData->gpu_load,
        (unsigned long)_pcData->ram_used, (unsigned long)_pcData->ram_total);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleTelegramGet() {
    JsonDocument doc;
    doc["ok"]          = true;
    doc["enabled"]     = RuntimeSettings::tgEnabled();
    doc["has_token"]   = RuntimeSettings::tgToken().length() > 0;
    doc["chat_id"]     = RuntimeSettings::tgChatId();
    doc["cooldown_min"]= RuntimeSettings::tgCooldownMin();
    doc["temp_hi_en"]  = RuntimeSettings::tgTempHiEn();
    doc["temp_hi"]     = RuntimeSettings::tgTempHi();
    doc["temp_lo_en"]  = RuntimeSettings::tgTempLoEn();
    doc["temp_lo"]     = RuntimeSettings::tgTempLo();
    doc["hum_hi_en"]   = RuntimeSettings::tgHumHiEn();
    doc["hum_hi"]      = RuntimeSettings::tgHumHi();
    doc["hum_lo_en"]   = RuntimeSettings::tgHumLoEn();
    doc["hum_lo"]      = RuntimeSettings::tgHumLo();
    doc["gas_lo_en"]   = RuntimeSettings::tgGasLoEn();
    doc["gas_lo"]      = RuntimeSettings::tgGasLo();
    sendJson(doc);
}

static void handleTelegramPost() {
    String body = server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
        return;
    }

    if (doc["enabled"].is<bool>())
        RuntimeSettings::saveTgEnabled(doc["enabled"].as<bool>());

    if (doc["chat_id"].is<const char*>() || doc["token"].is<const char*>()) {
        String token  = doc["token"]   | "";
        String chatId = doc["chat_id"] | RuntimeSettings::tgChatId();
        chatId.trim();
        RuntimeSettings::saveTgCredentials(token, chatId);
    }

    bool threshChanged =
        doc["temp_hi_en"].is<bool>() || doc["temp_hi"].is<float>() ||
        doc["temp_lo_en"].is<bool>() || doc["temp_lo"].is<float>() ||
        doc["hum_hi_en"].is<bool>()  || doc["hum_hi"].is<float>()  ||
        doc["hum_lo_en"].is<bool>()  || doc["hum_lo"].is<float>()  ||
        doc["gas_lo_en"].is<bool>()  || doc["gas_lo"].is<float>()  ||
        doc["cooldown_min"].is<int>();

    if (threshChanged) {
        RuntimeSettings::saveTgThresholds(
            doc["temp_hi_en"] | RuntimeSettings::tgTempHiEn(),
            doc["temp_hi"]    | RuntimeSettings::tgTempHi(),
            doc["temp_lo_en"] | RuntimeSettings::tgTempLoEn(),
            doc["temp_lo"]    | RuntimeSettings::tgTempLo(),
            doc["hum_hi_en"]  | RuntimeSettings::tgHumHiEn(),
            doc["hum_hi"]     | RuntimeSettings::tgHumHi(),
            doc["hum_lo_en"]  | RuntimeSettings::tgHumLoEn(),
            doc["hum_lo"]     | RuntimeSettings::tgHumLo(),
            doc["gas_lo_en"]  | RuntimeSettings::tgGasLoEn(),
            doc["gas_lo"]     | RuntimeSettings::tgGasLo(),
            (uint16_t)(doc["cooldown_min"] | (int)RuntimeSettings::tgCooldownMin())
        );
    }

    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleTelegramTest() {
    if (RuntimeSettings::tgToken().isEmpty() || RuntimeSettings::tgChatId().isEmpty()) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"token or chat_id not set\"}");
        return;
    }
    bool ok = Telegram::sendMessage("PCHUB test message — bot is working.");
    server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"queue full\"}");
}

static void handleMqttGet() {
    JsonDocument doc;
    doc["ok"]          = true;
    doc["enabled"]     = RuntimeSettings::mqttEnabled();
    doc["connected"]   = MQTT::connected();
    doc["broker"]      = RuntimeSettings::mqttBroker();
    doc["port"]        = RuntimeSettings::mqttPort();
    doc["user"]        = RuntimeSettings::mqttUser();
    doc["prefix"]      = RuntimeSettings::mqttPrefix();
    doc["interval_sec"]= RuntimeSettings::mqttIntervalSec();
    sendJson(doc);
}

static void handleMqttPost() {
    String body = server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
        return;
    }
    if (doc["enabled"].is<bool>())
        RuntimeSettings::saveMqttEnabled(doc["enabled"].as<bool>());

    if (doc["broker"].is<const char*>()) {
        String broker   = doc["broker"]   | RuntimeSettings::mqttBroker();
        uint16_t port   = doc["port"]     | (int)RuntimeSettings::mqttPort();
        String user     = doc["user"]     | RuntimeSettings::mqttUser();
        String pass     = doc["password"] | String("");
        String prefix   = doc["prefix"]   | RuntimeSettings::mqttPrefix();
        uint16_t intv   = doc["interval_sec"] | (int)RuntimeSettings::mqttIntervalSec();
        broker.trim();
        RuntimeSettings::saveMqttSettings(broker, port, user, pass, prefix, intv);
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleFactoryReset() {
    Preferences prefs;
    prefs.begin("pchub", false);
    prefs.clear();
    prefs.end();
    server.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
    delay(300);
    ESP.restart();
}

static void handleIpGet() {
    JsonDocument doc;
    doc["ok"]             = true;
    doc["static_enabled"] = RuntimeSettings::staticIpEnabled();
    doc["static_ip"]      = RuntimeSettings::staticIp();
    doc["static_gw"]      = RuntimeSettings::staticGateway();
    doc["static_sn"]      = RuntimeSettings::staticSubnet();
    doc["static_dns"]     = RuntimeSettings::staticDns();
    doc["ap_ip"]          = RuntimeSettings::apIp();
    sendJson(doc);
}

static void handleIpPost() {
    String body = server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad json\"}");
        return;
    }

    if (doc["static_enabled"].is<bool>() || doc["static_ip"].is<const char*>()) {
        bool staticEn = doc["static_enabled"] | RuntimeSettings::staticIpEnabled();
        String ip  = doc["static_ip"]  | RuntimeSettings::staticIp();
        String gw  = doc["static_gw"]  | RuntimeSettings::staticGateway();
        String sn  = doc["static_sn"]  | RuntimeSettings::staticSubnet();
        String dns = doc["static_dns"] | RuntimeSettings::staticDns();
        RuntimeSettings::saveIpSettings(staticEn, ip, gw, sn, dns);
    }

    if (doc["ap_ip"].is<const char*>()) {
        RuntimeSettings::saveApIp(doc["ap_ip"].as<String>());
    }

    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleRoot() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", WEB_INDEX);
}

static void handleApiInfo() {
    server.send(200, "text/plain",
                "PCHUB API\n"
                "GET  /\n"
                "GET  /api\n"
                "GET  /api/status\n"
                "GET  /api/settings\n"
                "POST /api/settings\n"
                "GET  /api/scan\n"
                "GET  /api/history?range=24h|7d|30d\n"
                "GET  /api/log\n"
                "POST /api/ota\n"
                "POST /api/sd/clear\n"
                "POST /api/reboot\n"
                "POST /api/pc\n");
}

bool apiReady() { return _apiReady; }

void initAPI(const SensorData *sensor, const WeatherData *weather,
             PCData *pcData, bool sdReady) {
    _sensor  = sensor;
    _weather = weather;
    _pcData  = pcData;
    _sdReady = sdReady;
    if (sdReady) {
        _sdTotalMb = SD.cardSize() / (1024ULL * 1024ULL);
        _sdUsedMb  = SD.usedBytes() / (1024ULL * 1024ULL);
    }

    // Captive portal: каждая ОС проверяет свой URL — перенаправляем на главную
    auto cpRedirect = []() {
        String url = "http://" + RuntimeSettings::apIp() + "/";
        server.sendHeader("Location", url, true);
        server.send(302, "text/plain", "");
    };
    server.on("/generate_204",        HTTP_GET, cpRedirect);  // Android
    server.on("/hotspot-detect.html", HTTP_GET, cpRedirect);  // iOS / macOS
    server.on("/connecttest.txt",     HTTP_GET, cpRedirect);  // Windows
    server.on("/ncsi.txt",            HTTP_GET, cpRedirect);  // Windows (старый)
    server.on("/redirect",            HTTP_GET, cpRedirect);  // Chrome
    server.on("/canonical.html",      HTTP_GET, cpRedirect);  // Chrome

    server.on("/",           HTTP_GET,  handleRoot);
    server.on("/api",        HTTP_GET,  handleApiInfo);
    server.on("/api/status", HTTP_GET,  handleStatus);
    server.on("/api/settings", HTTP_GET, handleSettingsGet);
    server.on("/api/settings", HTTP_POST, handleSettingsPost);
    server.on("/api/scan", HTTP_GET, handleScan);
    server.on("/api/history", HTTP_GET, handleHistory);
    server.on("/api/log",         HTTP_GET,  handleLog);
    server.on("/api/weather_log", HTTP_GET,  handleWeatherLog);
    server.on("/api/ota",    HTTP_POST, handleOtaDone, handleOtaUpload);
    server.on("/api/sd/clear",      HTTP_POST, handleSdClear);
    server.on("/api/history/clear", HTTP_POST, handleHistoryClear);
    server.on("/api/reboot",        HTTP_POST, handleReboot);
    server.on("/api/pc",            HTTP_POST, handlePCPost);
    server.on("/api/telegram",      HTTP_GET,  handleTelegramGet);
    server.on("/api/telegram",      HTTP_POST, handleTelegramPost);
    server.on("/api/telegram/test",  HTTP_POST, handleTelegramTest);
    server.on("/api/factory-reset", HTTP_POST, handleFactoryReset);
    server.on("/api/mqtt",          HTTP_GET,  handleMqttGet);
    server.on("/api/mqtt",          HTTP_POST, handleMqttPost);
    server.on("/api/ip",            HTTP_GET,  handleIpGet);
    server.on("/api/ip",            HTTP_POST, handleIpPost);
    server.onNotFound([]() {
        if (WiFi.getMode() == WIFI_MODE_AP) {
            String url = "http://" + RuntimeSettings::apIp() + "/";
            server.sendHeader("Location", url, true);
            server.send(302, "text/plain", "");
        } else {
            server.send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
        }
    });

    String hostname = RuntimeSettings::hostname();
    MDNS.begin(hostname.c_str());
    MDNS.addService("http", "tcp", 80);

    server.begin();
    _apiReady = true;

    Serial.printf("API: OK http://%s.local/  (%s)\n",
                  hostname.c_str(), WiFi.localIP().toString().c_str());
}

void handleAPI() {
    updateHistory();
    if (_apiReady) server.handleClient();
}
