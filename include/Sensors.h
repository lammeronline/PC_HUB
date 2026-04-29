#pragma once
#include <Arduino.h>

// Структура для хранения всех показаний
struct SensorData {
    bool rtc_ok;
    bool bme_ok;
    String timeStr;
    float temperature;
    float humidity;
    float pressure;
    float gas;
};

// Объявляем функции, которые будем вызывать из main
void initSensors();
void updateSensors(SensorData &data);
bool syncRTCfromNTP(); // Синхронизация RTC по NTP, возвращает true при успехе