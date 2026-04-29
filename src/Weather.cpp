#include "Weather.h"
#include "Config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

static const char* DAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static float _lat = 0.0f;
static float _lon = 0.0f;

static bool ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }
    return WiFi.status() == WL_CONNECTED;
}

static String httpsGet(const char* url) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(8000);
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("HTTP error %d: %s\n", code, url);
        http.end();
        return "";
    }
    String body = http.getString();  // буферизуем до закрытия соединения
    http.end();
    return body;
}

bool geocodeCity(const char* city) {
    if (!ensureWiFi()) return false;

    char url[200];
    snprintf(url, sizeof(url),
             "https://geocoding-api.open-meteo.com/v1/search"
             "?name=%s&count=1&language=en&format=json",
             city);

    String body = httpsGet(url);
    if (body.isEmpty()) return false;

    JsonDocument filter;
    filter["results"][0]["latitude"]  = true;
    filter["results"][0]["longitude"] = true;
    filter["results"][0]["name"]      = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));

    if (err || !doc["results"][0]) {
        Serial.printf("Geocode parse error: %s\n", err ? err.c_str() : "city not found");
        return false;
    }

    _lat = doc["results"][0]["latitude"].as<float>();
    _lon = doc["results"][0]["longitude"].as<float>();
    Serial.printf("Geocode OK: %s -> %.4f, %.4f\n",
                  doc["results"][0]["name"].as<const char*>(), _lat, _lon);
    return true;
}

bool fetchWeather(WeatherData &data) {
    if (_lat == 0.0f && _lon == 0.0f) { data.ok = false; return false; }
    if (!ensureWiFi())                 { data.ok = false; return false; }

    char url[300];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast"
             "?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
             "&daily=temperature_2m_max,temperature_2m_min,weather_code"
             "&timezone=auto&forecast_days=7",
             _lat, _lon);

    String body = httpsGet(url);
    if (body.isEmpty()) { data.ok = false; return false; }

    JsonDocument filter;
    filter["current"]["temperature_2m"]       = true;
    filter["current"]["relative_humidity_2m"] = true;
    filter["current"]["weather_code"]         = true;
    filter["current"]["wind_speed_10m"]       = true;
    filter["daily"]["temperature_2m_max"]     = true;
    filter["daily"]["temperature_2m_min"]     = true;
    filter["daily"]["weather_code"]           = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));

    if (err) {
        data.ok = false;
        Serial.printf("Weather JSON error: %s\n", err.c_str());
        return false;
    }

    JsonObject cur = doc["current"];
    data.temperature  = cur["temperature_2m"].as<float>();
    data.humidity     = cur["relative_humidity_2m"].as<int>();
    data.weather_code = cur["weather_code"].as<int>();
    data.wind_speed   = cur["wind_speed_10m"].as<float>();

    JsonObject daily = doc["daily"];
    JsonArray maxT   = daily["temperature_2m_max"];
    JsonArray minT   = daily["temperature_2m_min"];
    JsonArray codes  = daily["weather_code"];

    struct tm timeinfo;
    getLocalTime(&timeinfo);
    int baseDay = timeinfo.tm_wday;

    for (int i = 0; i < 7; i++) {
        strncpy(data.forecast[i].day, DAY_NAMES[(baseDay + i) % 7], 3);
        data.forecast[i].day[3]       = '\0';
        data.forecast[i].temp_max     = maxT[i].as<float>();
        data.forecast[i].temp_min     = minT[i].as<float>();
        data.forecast[i].weather_code = codes[i].as<int>();
    }

    data.ok = true;
    Serial.println("Weather: OK");
    return true;
}

const char* weatherDesc(int code) {
    if (code == 0)  return "Clear   ";
    if (code <= 3)  return "Cloudy  ";
    if (code <= 48) return "Fog     ";
    if (code <= 55) return "Drizzle ";
    if (code <= 65) return "Rain    ";
    if (code <= 77) return "Snow    ";
    if (code <= 82) return "Showers ";
    if (code <= 86) return "SnowShwr";
    return "Storm   ";
}
