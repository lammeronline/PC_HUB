#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Sensors.h"
#include "Weather.h"
#include "PCData.h"

struct UiStatus {
    bool sdReady = false;
    uint64_t sdSizeMb = 0;
    unsigned long lastLogWriteMs = 0;
};

void initUI(TFT_eSPI &display);
void initPCDisplay(const PCData *pc);   // call after PCData is available
bool handleUI();
void invalidateUI();
void invalidateForecastUI();
void drawUI(const SensorData &sensor, const WeatherData &weather, const UiStatus &status);
