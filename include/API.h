#pragma once
#include <Arduino.h>
#include "Sensors.h"
#include "Weather.h"

void initAPI(const SensorData *sensor, const WeatherData *weather, bool sdReady);
void handleAPI();
bool apiReady();
