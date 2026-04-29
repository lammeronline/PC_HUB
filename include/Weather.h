#pragma once
#include <Arduino.h>

struct DayForecast {
    char day[4];   // "Mon", "Tue", ...
    float temp_max;
    float temp_min;
    int   weather_code;
};

struct WeatherData {
    bool  ok = false;
    float temperature;
    int   humidity;
    float wind_speed;
    int   weather_code;
    DayForecast forecast[7];
};

bool        geocodeCity(const char* city);  // Получить координаты по имени города
bool        fetchWeather(WeatherData &data);
const char* weatherDesc(int code);
