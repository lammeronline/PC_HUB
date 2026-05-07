#pragma once
#include <Arduino.h>
#include "Sensors.h"
#include "Weather.h"
#include "PCData.h"

void initAPI(const SensorData *sensor, const WeatherData *weather,
             PCData *pcData, bool sdReady);
void handleAPI();
bool apiReady();
bool apiPendingRestart();
void setApiApMode(bool v);
void preloadHistoryFromSD();
