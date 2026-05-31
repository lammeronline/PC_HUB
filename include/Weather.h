#pragma once
#include <Arduino.h>

struct DayForecast {
    char day[4] = "---";   // "Mon", "Tue", ...
    char date[6] = "--.--"; // "dd.mm"
    float temp_max = 0.0f;
    float temp_min = 0.0f;
    float wind_speed = 0.0f;
    int   weather_code = -1;
};

struct WeatherData {
    bool  ok = false;
    float temperature = 0.0f;
    int   humidity = 0;
    float wind_speed = 0.0f;
    int   weather_code = -1;
    bool  is_day = true;
    DayForecast forecast[7];
};

bool        geocodeCity(const char* city);
bool        fetchWeather(WeatherData &data);
const char* weatherDesc(int code);

// Background FreeRTOS task — call once after WiFi connects.
// All subsequent weather/geocode work happens on core 0 so the
// main loop (web server) is never blocked by HTTPS calls.
void initWeatherTask(WeatherData* data);
void triggerWeatherFetch();                     // refresh weather only
void triggerGeocodeAndFetch(const char* city);  // re-geocode then refresh
bool weatherTaskBusy();

// Set to true by the task after each successful fetch; main loop checks
// this flag to write the weather log entry, then clears it.
extern volatile bool weatherWasUpdated;
