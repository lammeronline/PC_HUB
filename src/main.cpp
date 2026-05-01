#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Config.h"
#include "Sensors.h"
#include "SDCard.h"
#include "Led.h"
#include "Weather.h"
#include "Logger.h"
#include "API.h"

TFT_eSPI tft = TFT_eSPI();
SensorData  currentData;
WeatherData weatherData;

static const unsigned long SENSOR_INTERVAL_MS = 1000;
static const unsigned long WEATHER_UPDATE_INTERVAL_MS = WEATHER_UPDATE_INTERVAL_SEC * 1000UL;
static const unsigned long DATA_LOG_INTERVAL_MS = DATA_LOG_INTERVAL_SEC * 1000UL;
static unsigned long lastWeatherUpdate = 0;
static unsigned long lastLogWrite = 0;
static unsigned long lastSensorUpdate = 0;
static bool forecastDirty = true;

static void drawPaddedText(int x, int y, int font, uint16_t color, const char *text, int width) {
    tft.setTextFont(font);
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextPadding(width);
    tft.drawString(text, x, y);
    tft.setTextPadding(0);
}

// -------------------------------------------------------
// Отрисовка главного экрана
// -------------------------------------------------------
void drawScreen() {
    // fillScreen убран — текст рисуется с фоновым цветом, перезаписывая старое содержимое

    // --- Время и дата (Font 4, зелёный) ---
    drawPaddedText(10, 5, 4, TFT_GREEN, currentData.timeStr.c_str(), 300);

    // --- Уличная погода (Font 2, жёлтый) ---
    char line[96];
    if (weatherData.ok) {
        snprintf(line, sizeof(line), "Out: %.1fC  %s  Wind: %.1f km/h",
                 weatherData.temperature,
                 weatherDesc(weatherData.weather_code),
                 weatherData.wind_speed);
        drawPaddedText(10, 35, 2, TFT_YELLOW, line, 300);
    } else {
        drawPaddedText(10, 35, 2, TFT_DARKGREY, "Out: no data", 300);
    }

    // --- Локальные датчики BME680 (Font 2, голубой) ---
    if (currentData.bme_ok) {
        snprintf(line, sizeof(line), "T:%.1fC  H:%d%%  P:%.0fhPa  G:%.0fkOhm",
                 currentData.temperature,
                 (int)currentData.humidity,
                 currentData.pressure,
                 currentData.gas);
        drawPaddedText(10, 53, 2, TFT_CYAN, line, 300);
    } else {
        drawPaddedText(10, 53, 2, TFT_DARKGREY, "BME680: no data", 300);
    }

    if (forecastDirty) {
        // --- Заголовок прогноза ---
        drawPaddedText(10, 72, 2, TFT_WHITE, "--- 7-day Forecast ---", 300);

        // --- Прогноз на 7 дней, 2 колонки ---
        if (weatherData.ok) {
            // Левая колонка: дни 0-3, правая: дни 4-6
            for (int i = 0; i < 7; i++) {
                int col = i < 4 ? 0 : 1;
                int row = i < 4 ? i : i - 4;
                int x = col == 0 ? 10 : 170;
                int y = 90 + row * 18;

                const DayForecast &d = weatherData.forecast[i];
                snprintf(line, sizeof(line), "%-3s %+.0f/%+.0f %-8s",
                         d.day, d.temp_min, d.temp_max, weatherDesc(d.weather_code));
                drawPaddedText(x, y, 2, TFT_WHITE, line, 145);
            }
            drawPaddedText(170, 144, 2, TFT_WHITE, "", 145);
        } else {
            drawPaddedText(10, 90, 2, TFT_DARKGREY, "No forecast data", 300);
            for (int i = 1; i < 4; i++) {
                drawPaddedText(10, 90 + i * 18, 2, TFT_DARKGREY, "", 300);
            }
        }
        forecastDirty = false;
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
    bool sdReady = sdSize > 0;
    initLogger(sdReady);

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
    forecastDirty = true;
    lastWeatherUpdate = millis();

    updateSensors(currentData);
    initAPI(&currentData, &weatherData, sdReady);
    logReading(currentData, weatherData);
    lastLogWrite = millis();
    lastSensorUpdate = 0;

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
    unsigned long now = millis();

    handleAPI();

    if (now - lastSensorUpdate >= SENSOR_INTERVAL_MS) {
        updateSensors(currentData);
        drawScreen();
        lastSensorUpdate = now;
    }

    if (now - lastWeatherUpdate >= WEATHER_UPDATE_INTERVAL_MS) {
        fetchWeather(weatherData);
        forecastDirty = true;
        lastWeatherUpdate = now;
    }

    if (now - lastLogWrite >= DATA_LOG_INTERVAL_MS) {
        logReading(currentData, weatherData);
        lastLogWrite = now;
    }

    delay(10);
}
