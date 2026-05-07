#pragma once
#include "Sensors.h"
#include "Weather.h"

namespace Telegram {
    void begin(const SensorData *sensor, const WeatherData *weather);
    void handle();
    bool sendMessage(const String &text);
}
