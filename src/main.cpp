#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Config.h"
#include "Sensors.h"
#include "SDCard.h"
#include "Led.h"
#include "Weather.h"
#include "Logger.h"
#include "API.h"
#include "UI.h"

TFT_eSPI tft = TFT_eSPI();
SensorData  currentData;
WeatherData weatherData;

static const unsigned long SENSOR_INTERVAL_MS = 1000;
static const unsigned long WEATHER_UPDATE_INTERVAL_MS = WEATHER_UPDATE_INTERVAL_SEC * 1000UL;
static const unsigned long DATA_LOG_INTERVAL_MS = DATA_LOG_INTERVAL_SEC * 1000UL;
static unsigned long lastWeatherUpdate = 0;
static unsigned long lastLogWrite = 0;
static unsigned long lastSensorUpdate = 0;
static bool sdReady = false;
static uint64_t sdSizeMb = 0;

static UiStatus currentUiStatus() {
    UiStatus status;
    status.sdReady = sdReady;
    status.sdSizeMb = sdSizeMb;
    status.lastLogWriteMs = lastLogWrite;
    return status;
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
    initUI(tft);

    tft.setTextFont(4);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setCursor(10, 10);
    tft.println("System Boot...");

    int y_pos = 40;
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    initSensors();
    sdSizeMb = initSDCard();
    sdReady = sdSizeMb > 0;
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
    invalidateForecastUI();
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
    if (sdSizeMb > 0) { tft.print(sdSizeMb); tft.println(" MB"); }
    else            { tft.println("FAILED"); }

    tft.setCursor(10, y_pos); y_pos += 20;
    tft.print("NTP: ");     tft.println(ntp_ok ? "OK" : "FAILED");

    tft.setCursor(10, y_pos); y_pos += 20;
    tft.print("Weather: "); tft.println(weatherData.ok ? "OK" : "FAILED");

    tft.setCursor(10, y_pos); y_pos += 20;
    tft.print("City: "); tft.println(WEATHER_CITY);

    if (currentData.rtc_ok && currentData.bme_ok && sdSizeMb > 0) {
        setLED(0, 255, 0);
    } else {
        setLED(255, 0, 0);
    }

    delay(2000);
    tft.fillScreen(TFT_BLACK);
    invalidateUI();
    drawUI(currentData, weatherData, currentUiStatus());
    offLED();
}

void loop() {
    unsigned long now = millis();

    handleAPI();

    if (handleUI()) {
        drawUI(currentData, weatherData, currentUiStatus());
    }

    if (now - lastSensorUpdate >= SENSOR_INTERVAL_MS) {
        updateSensors(currentData);
        drawUI(currentData, weatherData, currentUiStatus());
        lastSensorUpdate = now;
    }

    if (now - lastWeatherUpdate >= WEATHER_UPDATE_INTERVAL_MS) {
        fetchWeather(weatherData);
        invalidateForecastUI();
        lastWeatherUpdate = now;
    }

    if (now - lastLogWrite >= DATA_LOG_INTERVAL_MS) {
        logReading(currentData, weatherData);
        lastLogWrite = now;
    }

    delay(10);
}
