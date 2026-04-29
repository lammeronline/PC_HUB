#pragma once
#include <Arduino.h>

void initLED();
// Функция принимает значения цвета от 0 (выключен) до 255 (максимум)
void setLED(uint8_t r, uint8_t g, uint8_t b); 
void offLED();