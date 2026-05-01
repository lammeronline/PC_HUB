#pragma once
#include <Arduino.h>
#include "Sensors.h"
#include "Weather.h"

bool initLogger(bool sdReady);
bool loggerReady();
void logReading(const SensorData &sensor, const WeatherData &weather);
const char* readingsLogPath();
