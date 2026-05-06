#pragma once
#include <Arduino.h>

void initLED();
void setLED(uint8_t r, uint8_t g, uint8_t b);
void offLED();
void updateLED(float temp, float hum, float gas, bool bme_ok);