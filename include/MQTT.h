#pragma once
#include "Sensors.h"
#include "Weather.h"

namespace MQTT {
    void begin(const SensorData *sensor, const WeatherData *weather);
    void handle();
    bool connected();
}
