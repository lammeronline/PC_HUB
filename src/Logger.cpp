#include "Logger.h"
#include <SD.h>

static const char* LOG_PATH         = "/readings.csv";
static const char* WEATHER_LOG_PATH = "/weather_log.csv";
static bool _loggerReady        = false;
static bool _weatherLoggerReady = false;

const char* readingsLogPath() { return LOG_PATH; }
const char* weatherLogPath()  { return WEATHER_LOG_PATH; }
bool loggerReady()        { return _loggerReady; }
bool weatherLoggerReady() { return _weatherLoggerReady; }

bool initLogger(bool sdReady) {
    _loggerReady = false;
    if (!sdReady) {
        Serial.println("Logger: SD not ready");
        return false;
    }

    bool needsHeader = !SD.exists(LOG_PATH);
    File file = SD.open(LOG_PATH, FILE_APPEND);
    if (!file) {
        Serial.println("Logger: open FAILED");
        return false;
    }

    if (needsHeader || file.size() == 0) {
        file.println("time,rtc_ok,bme_ok,temp_c,humidity_pct,pressure_hpa,gas_kohm,weather_ok,out_temp_c,out_humidity_pct,out_wind_kmh,out_weather_code");
    }
    file.close();

    _loggerReady = true;
    Serial.printf("Logger: OK %s\n", LOG_PATH);
    return true;
}

bool initWeatherLogger(bool sdReady) {
    _weatherLoggerReady = false;
    if (!sdReady) return false;

    bool needsHeader = !SD.exists(WEATHER_LOG_PATH);
    File file = SD.open(WEATHER_LOG_PATH, FILE_APPEND);
    if (!file) {
        Serial.println("WeatherLogger: open FAILED");
        return false;
    }

    if (needsHeader || file.size() == 0) {
        file.println("time,ok,out_temp_c,out_humidity_pct,out_wind_kmh,out_weather_code");
    }
    file.close();

    _weatherLoggerReady = true;
    Serial.printf("WeatherLogger: OK %s\n", WEATHER_LOG_PATH);
    return true;
}

void logReading(const SensorData &sensor, const WeatherData &weather) {
    if (!_loggerReady) return;

    File file = SD.open(LOG_PATH, FILE_APPEND);
    if (!file) {
        Serial.println("Logger: write open FAILED");
        _loggerReady = false;
        return;
    }

    file.printf("\"%s\",%d,%d,%.2f,%.2f,%.2f,%.2f,%d,%.2f,%d,%.2f,%d\n",
                sensor.timeStr,
                sensor.rtc_ok ? 1 : 0,
                sensor.bme_ok ? 1 : 0,
                sensor.temperature,
                sensor.humidity,
                sensor.pressure,
                sensor.gas,
                weather.ok ? 1 : 0,
                weather.temperature,
                (int)weather.humidity,
                weather.wind_speed,
                weather.weather_code);
    file.close();
}

void logWeather(const SensorData &sensor, const WeatherData &weather) {
    if (!_weatherLoggerReady || !weather.ok) return;

    File file = SD.open(WEATHER_LOG_PATH, FILE_APPEND);
    if (!file) {
        Serial.println("WeatherLogger: write open FAILED");
        _weatherLoggerReady = false;
        return;
    }

    file.printf("\"%s\",%d,%.2f,%d,%.2f,%d\n",
                sensor.timeStr,
                weather.ok ? 1 : 0,
                weather.temperature,
                (int)weather.humidity,
                weather.wind_speed,
                weather.weather_code);
    file.close();
}
