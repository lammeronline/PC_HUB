#include "API.h"
#include "Config.h"
#include "Logger.h"
#include <ArduinoJson.h>
#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ESPmDNS.h>

static WebServer server(80);
static const SensorData *_sensor = nullptr;
static const WeatherData *_weather = nullptr;
static bool _apiReady = false;
static bool _sdReady = false;

static void sendJson(JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
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

    JsonObject sensor = doc["sensor"].to<JsonObject>();
    sensor["rtc_ok"] = _sensor->rtc_ok;
    sensor["bme_ok"] = _sensor->bme_ok;
    sensor["time"] = _sensor->timeStr;
    sensor["temperature"] = _sensor->temperature;
    sensor["humidity"] = _sensor->humidity;
    sensor["pressure"] = _sensor->pressure;
    sensor["gas"] = _sensor->gas;

    JsonObject weather = doc["weather"].to<JsonObject>();
    weather["ok"] = _weather->ok;
    weather["temperature"] = _weather->temperature;
    weather["humidity"] = _weather->humidity;
    weather["wind_speed"] = _weather->wind_speed;
    weather["weather_code"] = _weather->weather_code;

    JsonArray forecast = weather["forecast"].to<JsonArray>();
    for (int i = 0; i < 7; i++) {
        JsonObject day = forecast.add<JsonObject>();
        day["day"] = _weather->forecast[i].day;
        day["temp_min"] = _weather->forecast[i].temp_min;
        day["temp_max"] = _weather->forecast[i].temp_max;
        day["weather_code"] = _weather->forecast[i].weather_code;
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

static void handleRoot() {
    server.send(200, "text/plain",
                "PCHUB API\n"
                "GET /api/status\n"
                "GET /api/log\n");
}

bool apiReady() {
    return _apiReady;
}

void initAPI(const SensorData *sensor, const WeatherData *weather, bool sdReady) {
    _sensor = sensor;
    _weather = weather;
    _sdReady = sdReady;

    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/log", HTTP_GET, handleLog);
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
    if (_apiReady) {
        server.handleClient();
    }
}
