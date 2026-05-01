#include "Logger.h"
#include <SD.h>

static const char* LOG_PATH = "/readings.csv";
static bool _loggerReady = false;

const char* readingsLogPath() {
    return LOG_PATH;
}

bool loggerReady() {
    return _loggerReady;
}

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

void logReading(const SensorData &sensor, const WeatherData &weather) {
    if (!_loggerReady) return;

    File file = SD.open(LOG_PATH, FILE_APPEND);
    if (!file) {
        Serial.println("Logger: write open FAILED");
        _loggerReady = false;
        return;
    }

    file.printf("\"%s\",%d,%d,%.2f,%.2f,%.2f,%.2f,%d,%.2f,%d,%.2f,%d\n",
                sensor.timeStr.c_str(),
                sensor.rtc_ok ? 1 : 0,
                sensor.bme_ok ? 1 : 0,
                sensor.temperature,
                sensor.humidity,
                sensor.pressure,
                sensor.gas,
                weather.ok ? 1 : 0,
                weather.temperature,
                weather.humidity,
                weather.wind_speed,
                weather.weather_code);
    file.close();
}
