#include "Weather.h"
#include "Config.h"
#include "RuntimeSettings.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

static const char* DAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static float _lat = 0.0f;
static float _lon = 0.0f;
static bool _coords_ok = false;
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 3000;
static const unsigned long HTTP_GET_TIMEOUT_MS = 3500;

static String urlEncode(const char* text) {
    String encoded;
    while (*text) {
        char c = *text++;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            encoded += c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", static_cast<uint8_t>(c));
            encoded += buf;
        }
    }
    return encoded;
}

static int weekdayFromDate(const char* isoDate, int fallback) {
    if (!isoDate) return fallback;

    int y = 0;
    int m = 0;
    int d = 0;
    if (sscanf(isoDate, "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) {
        return fallback;
    }

    static const int offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + offsets[m - 1] + d) % 7;
}

static void shortDateFromIso(const char* isoDate, char* out, size_t outSize) {
    if (!out || outSize == 0) return;

    int y = 0;
    int m = 0;
    int d = 0;
    if (!isoDate || sscanf(isoDate, "%d-%d-%d", &y, &m, &d) != 3) {
        snprintf(out, outSize, "--.--");
        return;
    }

    snprintf(out, outSize, "%02d.%02d", d, m);
}

static bool ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    String hostname = RuntimeSettings::hostname();
    WiFi.setHostname(hostname.c_str());
    String ssid = RuntimeSettings::wifiSsid();
    String pass = RuntimeSettings::wifiPassword();
    WiFi.begin(ssid.c_str(), pass.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        delay(100);
    }
    return WiFi.status() == WL_CONNECTED;
}

static String httpsGet(const char* url) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, url)) {
        Serial.printf("HTTP begin failed: %s\n", url);
        return "";
    }
    http.setTimeout(HTTP_GET_TIMEOUT_MS);
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

    String encodedCity = urlEncode(city);
    char url[200];
    snprintf(url, sizeof(url),
             "https://geocoding-api.open-meteo.com/v1/search"
             "?name=%s&count=1&language=en&format=json",
             encodedCity.c_str());

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
    _coords_ok = true;
    Serial.printf("Geocode OK: %s -> %.4f, %.4f\n",
                  doc["results"][0]["name"].as<const char*>(), _lat, _lon);
    return true;
}

bool fetchWeather(WeatherData &data) {
    if (!_coords_ok)   { data.ok = false; return false; }
    if (!ensureWiFi()) { data.ok = false; return false; }

    char url[360];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast"
             "?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
             "&daily=temperature_2m_max,temperature_2m_min,weather_code,wind_speed_10m_max"
             "&timezone=auto&forecast_days=7",
             _lat, _lon);

    String body = httpsGet(url);
    if (body.isEmpty()) { data.ok = false; return false; }

    JsonDocument filter;
    filter["current"]["temperature_2m"]       = true;
    filter["current"]["relative_humidity_2m"] = true;
    filter["current"]["weather_code"]         = true;
    filter["current"]["wind_speed_10m"]       = true;
    filter["current"]["is_day"]               = true;
    filter["daily"]["time"]                   = true;
    filter["daily"]["temperature_2m_max"]     = true;
    filter["daily"]["temperature_2m_min"]     = true;
    filter["daily"]["weather_code"]           = true;
    filter["daily"]["wind_speed_10m_max"]     = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));

    if (err) {
        data.ok = false;
        Serial.printf("Weather JSON error: %s\n", err.c_str());
        return false;
    }

    JsonObject cur = doc["current"];
    JsonObject daily = doc["daily"];
    JsonArray dates  = daily["time"];
    JsonArray maxT   = daily["temperature_2m_max"];
    JsonArray minT   = daily["temperature_2m_min"];
    JsonArray codes  = daily["weather_code"];
    JsonArray wind   = daily["wind_speed_10m_max"];

    if (cur.isNull() || dates.size() < 7 || maxT.size() < 7 ||
        minT.size() < 7 || codes.size() < 7 || wind.size() < 7) {
        data.ok = false;
        Serial.println("Weather JSON error: incomplete data");
        return false;
    }

    data.temperature  = cur["temperature_2m"].as<float>();
    data.humidity     = cur["relative_humidity_2m"].as<int>();
    data.weather_code = cur["weather_code"].as<int>();
    data.wind_speed   = cur["wind_speed_10m"].as<float>();
    data.is_day       = cur["is_day"].as<int>() != 0;

    struct tm timeinfo;
    int baseDay = getLocalTime(&timeinfo, 1000) ? timeinfo.tm_wday : 0;

    for (int i = 0; i < 7; i++) {
        const char* isoDate = dates[i].as<const char*>();
        int dayIndex = weekdayFromDate(isoDate, (baseDay + i) % 7);
        strncpy(data.forecast[i].day, DAY_NAMES[dayIndex], 3);
        data.forecast[i].day[3]       = '\0';
        shortDateFromIso(isoDate, data.forecast[i].date, sizeof(data.forecast[i].date));
        data.forecast[i].temp_max     = maxT[i].as<float>();
        data.forecast[i].temp_min     = minT[i].as<float>();
        data.forecast[i].wind_speed   = wind[i].as<float>();
        data.forecast[i].weather_code = codes[i].as<int>();
    }

    data.ok = true;
    Serial.println("Weather: OK");
    return true;
}

const char* weatherDesc(int code) {
    if (code == 0)  return "Clear   ";
    if (code <= 2)  return "Partly  ";
    if (code <= 3)  return "Cloudy  ";
    if (code <= 48) return "Fog     ";
    if (code <= 55) return "Drizzle ";
    if (code <= 65) return "Rain    ";
    if (code <= 77) return "Snow    ";
    if (code <= 82) return "Showers ";
    if (code <= 86) return "SnowShwr";
    return "Storm   ";
}
