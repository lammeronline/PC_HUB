#pragma once
#include <Arduino.h>

// Структура для хранения всех показаний
struct SensorData {
    bool rtc_ok = false;
    bool bme_ok = false;
    String timeStr = "--.--.----  --:--:--";
    float temperature = 0.0f;
    float humidity = 0.0f;
    float pressure = 0.0f;
    float gas = 0.0f;
};

// Объявляем функции, которые будем вызывать из main
void initSensors();
void updateSensors(SensorData &data);
bool syncRTCfromNTP(); // Синхронизация RTC по NTP, возвращает true при успехе
