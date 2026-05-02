#include "API.h"
#include "Config.h"
#include "Logger.h"
#include "RuntimeSettings.h"
#include "WebUI.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <time.h>

static WebServer          server(80);
static const SensorData  *_sensor  = nullptr;
static const WeatherData *_weather = nullptr;
static PCData            *_pcData  = nullptr;
static bool               _apiReady = false;
static bool               _sdReady  = false;

struct HistoryPoint {
    uint32_t ts = 0;
    float temperature = 0.0f;
    float humidity = 0.0f;
    float pressure = 0.0f;
    float gas = 0.0f;
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

static HistoryPoint makeHistoryPoint(uint32_t ts, const SensorData &sensor) {
    HistoryPoint p;
    p.ts = ts;
    p.temperature = sensor.temperature;
    p.humidity = sensor.humidity;
    p.pressure = sensor.pressure;
    p.gas = sensor.gas;
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
        return;
    }

    acc.count++;
    acc.temperatureSum += sensor.temperature;
    acc.humiditySum += sensor.humidity;
    acc.pressureSum += sensor.pressure;
    acc.gasSum += sensor.gas;
}

static void updateHistory() {
    if (!_sensor) return;
    static unsigned long lastSampleMs = 0;
    unsigned long nowMs = millis();
    if (nowMs - lastSampleMs < 1000UL) return;
    lastSampleMs = nowMs;

    uint32_t nowSec = millis() / 1000UL;
    updateHistoryBucket(_acc24, _hist24, _hist24Rev, 5UL * 60UL, nowSec, *_sensor);
    updateHistoryBucket(_acc7, _hist7, _hist7Rev, 60UL * 60UL, nowSec, *_sensor);
    updateHistoryBucket(_acc30, _hist30, _hist30Rev, 6UL * 60UL * 60UL, nowSec, *_sensor);
}

template <size_t N>
static void sendHistoryJson(const HistoryRing<N> &ring, const char *range) {
    const size_t total = ring.size();
    const size_t maxPoints = 360;
    size_t step = total > maxPoints ? (total + maxPoints - 1) / maxPoints : 1;

    String out;
    out.reserve((total / step + 1) * 58 + 128);
    out += "{\"range\":\"";
    out += range;
    out += "\",\"n\":";
    out += (total + step - 1) / step;
    out += ",\"ts\":[";

    for (size_t i = 0; i < total; i += step) {
        if (i) out += ',';
        out += ring.at(i).ts;
    }
    out += "],\"temperature\":[";
    for (size_t i = 0; i < total; i += step) {
        if (i) out += ',';
        out += String(ring.at(i).temperature, 1);
    }
    out += "],\"humidity\":[";
    for (size_t i = 0; i < total; i += step) {
        if (i) out += ',';
        out += String(ring.at(i).humidity, 1);
    }
    out += "],\"pressure\":[";
    for (size_t i = 0; i < total; i += step) {
        if (i) out += ',';
        out += String(ring.at(i).pressure, 1);
    }
    out += "],\"gas\":[";
    for (size_t i = 0; i < total; i += step) {
        if (i) out += ',';
        out += String(ring.at(i).gas, 1);
    }
    out += "]}";

    server.send(200, "application/json", out);
}

static void sendJson(JsonDocument &doc) {
    String out;
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
    doc["ok"]           = true;
    doc["device"]       = DEVICE_NAME;
    doc["ip"]           = WiFi.localIP().toString();
    doc["hostname"]     = DEVICE_NAME ".local";
    doc["wind_unit"]    = WIND_UNIT_MS ? "m/s" : "km/h";
    doc["log_path"]     = readingsLogPath();
    doc["logger_ready"] = loggerReady();

    JsonObject system = doc["system"].to<JsonObject>();
    system["sd_ready"]             = _sdReady;
    system["api_ready"]            = _apiReady;
    system["heap_free"]            = ESP.getFreeHeap();
    system["uptime_sec"]           = millis() / 1000UL;
    system["weather_city"]         = WEATHER_CITY;
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

    JsonObject sensor = doc["sensor"].to<JsonObject>();
    sensor["rtc_ok"]      = _sensor->rtc_ok;
    sensor["bme_ok"]      = _sensor->bme_ok;
    sensor["time"]        = _sensor->timeStr;
    sensor["temperature"] = _sensor->temperature;
    sensor["humidity"]    = _sensor->humidity;
    sensor["pressure"]    = _sensor->pressure;
    sensor["gas"]         = _sensor->gas;

    JsonObject weather = doc["weather"].to<JsonObject>();
    float apiWindSpeed = _weather->wind_speed;
#if WIND_UNIT_MS
    apiWindSpeed /= 3.6f;
#endif
    weather["ok"]           = _weather->ok;
    weather["temperature"]  = _weather->temperature;
    weather["humidity"]     = _weather->humidity;
    weather["wind_speed"]   = apiWindSpeed;
    weather["weather_code"] = _weather->weather_code;

    JsonArray forecast = weather["forecast"].to<JsonArray>();
    for (int i = 0; i < 7; i++) {
        JsonObject day = forecast.add<JsonObject>();
        day["day"]          = _weather->forecast[i].day;
        day["temp_min"]     = _weather->forecast[i].temp_min;
        day["temp_max"]     = _weather->forecast[i].temp_max;
        day["weather_code"] = _weather->forecast[i].weather_code;
    }

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
    doc["ntp_server"]     = RuntimeSettings::ntpServer();
    doc["ntp_offset_sec"] = RuntimeSettings::ntpOffsetSec();
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

    JsonDocument resp;
    resp["ok"] = true;
    resp["wifi_changed"] = wifiChanged;
    resp["ntp_changed"] = ntpChanged;
    sendJson(resp);

    if (wifiChanged) {
        delay(200);
        WiFi.disconnect(false, false);
        WiFi.mode(WIFI_STA);
        String ssid = RuntimeSettings::wifiSsid();
        String pass = RuntimeSettings::wifiPassword();
        WiFi.begin(ssid.c_str(), pass.c_str());
    }
}

static void handleScan() {
    int n = WiFi.scanNetworks(false, true);
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

// POST /api/pc  — receives PC metrics from the PC Agent (WiFi path)
static void handlePCPost() {
    if (!_pcData) {
        server.send(503, "application/json", "{\"ok\":false,\"error\":\"not ready\"}");
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
    Serial.printf("[PC] CPU=%.0fC/%.0f%% GPU=%.0fC/%.0f%% RAM=%lu/%luMB\n",
        _pcData->cpu_temp, _pcData->cpu_load,
        _pcData->gpu_temp, _pcData->gpu_load,
        (unsigned long)_pcData->ram_used, (unsigned long)_pcData->ram_total);
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

    server.on("/",           HTTP_GET,  handleRoot);
    server.on("/api",        HTTP_GET,  handleApiInfo);
    server.on("/api/status", HTTP_GET,  handleStatus);
    server.on("/api/settings", HTTP_GET, handleSettingsGet);
    server.on("/api/settings", HTTP_POST, handleSettingsPost);
    server.on("/api/scan", HTTP_GET, handleScan);
    server.on("/api/history", HTTP_GET, handleHistory);
    server.on("/api/log",    HTTP_GET,  handleLog);
    server.on("/api/reboot", HTTP_POST, handleReboot);
    server.on("/api/pc",     HTTP_POST, handlePCPost);
    server.onNotFound([]() {
        server.send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
    });

    MDNS.begin(DEVICE_NAME);
    MDNS.addService("http", "tcp", 80);

    server.begin();
    _apiReady = true;

    Serial.printf("API: OK http://%s.local/  (%s)\n",
                  DEVICE_NAME, WiFi.localIP().toString().c_str());
}

void handleAPI() {
    updateHistory();
    if (_apiReady) server.handleClient();
}
