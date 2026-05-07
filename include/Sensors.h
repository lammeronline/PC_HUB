#pragma once
#include <Arduino.h>

// Структура для хранения всех показаний
struct SensorData {
    bool    rtc_ok  = false;
    bool    bme_ok  = false;
    char    timeStr[21] = "--.--.----  --:--:--";
    uint8_t weekday = 0;   // 0=Sun … 6=Sat
    float   temperature = 0.0f;
    float   humidity    = 0.0f;
    float   pressure    = 0.0f;
    float   gas         = 0.0f;
};

// Объявляем функции, которые будем вызывать из main
void initSensors();
void updateSensors(SensorData &data);
bool connectWiFi();    // Подключение к WiFi в режиме STA, возвращает true при успехе
bool syncRTCfromNTP(); // Синхронизация RTC по NTP (WiFi уже должен быть подключён)
uint32_t getRTCUnixTime(); // Unix-время от RTC; 0 если RTC не найден
