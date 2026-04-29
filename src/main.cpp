#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Config.h"
#include "Sensors.h"
#include "SDCard.h"
#include "Led.h"
#include "Weather.h"

TFT_eSPI tft = TFT_eSPI();
SensorData  currentData;
WeatherData weatherData;

// Обновляем погоду каждые 10 минут
static const unsigned long WEATHER_INTERVAL_MS = 10UL * 60 * 1000;
static unsigned long lastWeatherUpdate = 0;

// -------------------------------------------------------
// Отрисовка главного экрана
// -------------------------------------------------------
void drawScreen() {
    // fillScreen убран — текст рисуется с фоновым цветом, перезаписывая старое содержимое

    // --- Время и дата (Font 4, зелёный) ---
    tft.setTextFont(4);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(10, 5);
    tft.print(currentData.timeStr);

    // --- Уличная погода (Font 2, жёлтый) ---
    tft.setTextFont(2);
    if (weatherData.ok) {
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setCursor(10, 35);
        tft.printf("Out: %.1fC  %s  Wind: %.1f km/h",
                   weatherData.temperature,
                   weatherDesc(weatherData.weather_code),
                   weatherData.wind_speed);
    } else {
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.setCursor(10, 35);
        tft.print("Out: no data");
    }

    // --- Локальные датчики BME680 (Font 2, голубой) ---
    tft.setTextFont(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setCursor(10, 53);
    if (currentData.bme_ok) {
        tft.printf("T:%.1fC  H:%d%%  P:%.0fhPa  G:%.0fkOhm",
                   currentData.temperature,
                   (int)currentData.humidity,
                   currentData.pressure,
                   currentData.gas);
    } else {
        tft.print("BME680: no data");
    }

    // --- Заголовок прогноза ---
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(10, 72);
    tft.print("--- 7-day Forecast ---");

    // --- Прогноз на 7 дней, 2 колонки ---
    if (weatherData.ok) {
        // Левая колонка: дни 0-3, правая: дни 4-6
        for (int i = 0; i < 7; i++) {
            int col = i < 4 ? 0 : 1;
            int row = i < 4 ? i : i - 4;
            int x = col == 0 ? 10 : 170;
            int y = 90 + row * 18;

            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.setCursor(x, y);

            const DayForecast &d = weatherData.forecast[i];
            tft.printf("%-3s %+.0f/%+.0f %-8s",
                       d.day, d.temp_min, d.temp_max, weatherDesc(d.weather_code));
        }
    } else {
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.setCursor(10, 90);
        tft.print("No forecast data");
    }
}

// -------------------------------------------------------
void setup() {
    Serial.begin(115200);

    initLED();
    setLED(0, 0, 255); // Синий — загрузка

    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    tft.setTextFont(4);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(10, 10);
    tft.println("System Boot...");

    int y_pos = 40;
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    initSensors();
    uint64_t sdSize = initSDCard();

    tft.setCursor(10, y_pos); y_pos += 20;
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.print("NTP sync...");
    bool ntp_ok = syncRTCfromNTP();

    tft.setCursor(10, y_pos); y_pos += 20;
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.print("Geocoding city...");
    geocodeCity(WEATHER_CITY);

    tft.setCursor(10, y_pos); y_pos += 20;
    tft.print("Weather fetch...");
    fetchWeather(weatherData);
    lastWeatherUpdate = millis();

    updateSensors(currentData);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.setCursor(10, y_pos); y_pos += 20;
    tft.print("RTC: "); tft.println(currentData.rtc_ok ? "OK" : "FAILED");

    tft.setCursor(10, y_pos); y_pos += 20;
    tft.print("BME680: "); tft.println(currentData.bme_ok ? "OK" : "FAILED");

    tft.setCursor(10, y_pos); y_pos += 20;
    tft.print("SD Card: ");
    if (sdSize > 0) { tft.print(sdSize); tft.println(" MB"); }
    else            { tft.println("FAILED"); }

    tft.setCursor(10, y_pos); y_pos += 20;
    tft.print("NTP: ");     tft.println(ntp_ok ? "OK" : "FAILED");

    tft.setCursor(10, y_pos); y_pos += 20;
    tft.print("Weather: "); tft.println(weatherData.ok ? "OK" : "FAILED");

    tft.setCursor(10, y_pos); y_pos += 20;
    tft.print("City: "); tft.println(WEATHER_CITY);

    if (currentData.rtc_ok && currentData.bme_ok && sdSize > 0) {
        setLED(0, 255, 0);
    } else {
        setLED(255, 0, 0);
    }

    delay(2000);
    tft.fillScreen(TFT_BLACK);
    offLED();
}

void loop() {
    updateSensors(currentData);

    unsigned long now = millis();
    if (now - lastWeatherUpdate >= WEATHER_INTERVAL_MS) {
        fetchWeather(weatherData);
        lastWeatherUpdate = now;
    }

    drawScreen();
    delay(1000);
}
