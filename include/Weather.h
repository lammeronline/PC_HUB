#pragma once
#include <Arduino.h>

struct DayForecast {
    char day[4] = "---";   // "Mon", "Tue", ...
    float temp_max = 0.0f;
    float temp_min = 0.0f;
    int   weather_code = -1;
};

struct WeatherData {
    bool  ok = false;
    float temperature = 0.0f;
    int   humidity = 0;
    float wind_speed = 0.0f;
    int   weather_code = -1;
    DayForecast forecast[7];
};

bool        geocodeCity(const char* city);  // Получить координаты по имени города
bool        fetchWeather(WeatherData &data);
const char* weatherDesc(int code);
