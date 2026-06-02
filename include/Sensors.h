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
    float   gas          = 0.0f;   // raw resistance kΩ (for logging)
    float   iaq          = 0.0f;   // IAQ 0–500 (lower = better)
    uint8_t iaq_accuracy = 0;      // 0=warming up, 1=low, 2=medium, 3=high
    float   co2          = 0.0f;   // CO₂ equivalent ppm
    float   voc          = 0.0f;   // breath VOC equivalent ppm
};

// Объявляем функции, которые будем вызывать из main
void initSensors();
void updateSensors(SensorData &data);
bool syncRTCfromNTP(); // Синхронизация RTC по NTP (WiFi уже должен быть подключён)
uint32_t getRTCUnixTime(); // Unix-время от RTC; 0 если RTC не найден
