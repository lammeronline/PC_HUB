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

// ── boot screen helpers ─────────────────────────────────────────────────────

static void bootLabel(TFT_eSPI &t, int x, int y, const char *text,
                      uint16_t bg, uint16_t col) {
    t.setTextFont(2);
    t.setTextColor(col, bg);
    t.setTextPadding(200);
    t.drawString(text, x, y);
    t.setTextPadding(0);
}

static void bootStatus(TFT_eSPI &t, int x, int y, bool ok,
                       uint16_t bg, const char *detail = nullptr) {
    t.setTextFont(2);
    t.setTextColor(ok ? 0x07E0u : 0xF986u, bg);
    t.setTextPadding(92);
    t.drawString(detail ? detail : (ok ? "OK" : "FAIL"), x, y);
    t.setTextPadding(0);
}

// ── setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    initLED();
    setLED(0, 0, 255);

    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);
    tft.init();
    tft.setRotation(1);
    initUI(tft);

    // ── boot screen ──────────────────────────────────────────────────────────
    const uint16_t BG    = 0x1082;
    const uint16_t BLUE  = 0x1B9F;
    const uint16_t AMBER = 0xFD00;
    const uint16_t MUTED = 0xA514;
    const int LX = 14;   // label x
    const int SX = 222;  // status x
    const int ROW = 22;

    tft.fillScreen(BG);
    tft.fillRect(0, 36, 320, 2, BLUE);

    tft.setTextFont(4);
    tft.setTextColor(TFT_WHITE, BG);
    tft.drawString("PCHUB", 10, 5);
    tft.setTextFont(2);
    tft.setTextColor(MUTED, BG);
    tft.drawString("System Boot", 110, 12);

    int y = 46;

    // Hardware init (fast — no pending indicator needed)
    initSensors();
    updateSensors(currentData);

    bootLabel(tft, LX, y, "RTC",    BG, MUTED);
    bootStatus(tft, SX, y, currentData.rtc_ok, BG);
    y += ROW;

    bootLabel(tft, LX, y, "BME680", BG, MUTED);
    bootStatus(tft, SX, y, currentData.bme_ok, BG);
    y += ROW;

    sdSizeMb = initSDCard();
    sdReady  = sdSizeMb > 0;
    initLogger(sdReady);

    bootLabel(tft, LX, y, "SD Card", BG, MUTED);
    if (sdSizeMb > 0) {
        char sdBuf[16];
        snprintf(sdBuf, sizeof(sdBuf), "%llu MB", sdSizeMb);
        bootStatus(tft, SX, y, true, BG, sdBuf);
    } else {
        bootStatus(tft, SX, y, false, BG);
    }
    y += ROW;

    // Dim separator between hardware and network steps
    tft.drawLine(LX, y + 4, 305, y + 4, 0x2945);
    y += 12;

    // NTP (slow — show pending first)
    bootLabel(tft, LX, y, "NTP sync", BG, MUTED);
    tft.setTextFont(2); tft.setTextColor(AMBER, BG);
    tft.setTextPadding(92); tft.drawString("...", SX, y); tft.setTextPadding(0);
    bool ntp_ok = syncRTCfromNTP();
    bootStatus(tft, SX, y, ntp_ok, BG);
    y += ROW;

    // Geocoding (slow)
    bootLabel(tft, LX, y, "Geocoding", BG, MUTED);
    tft.setTextFont(2); tft.setTextColor(AMBER, BG);
    tft.setTextPadding(92); tft.drawString("...", SX, y); tft.setTextPadding(0);
    bool geo_ok = geocodeCity(WEATHER_CITY);
    bootStatus(tft, SX, y, geo_ok, BG, geo_ok ? WEATHER_CITY : nullptr);
    y += ROW;

    // Weather fetch (slow)
    bootLabel(tft, LX, y, "Weather", BG, MUTED);
    tft.setTextFont(2); tft.setTextColor(AMBER, BG);
    tft.setTextPadding(92); tft.drawString("...", SX, y); tft.setTextPadding(0);
    fetchWeather(weatherData);
    invalidateForecastUI();
    lastWeatherUpdate = millis();
    bootStatus(tft, SX, y, weatherData.ok, BG);

    // Finish init
    updateSensors(currentData);
    initAPI(&currentData, &weatherData, sdReady);
    logReading(currentData, weatherData);
    lastLogWrite     = millis();
    lastSensorUpdate = millis();

    if (currentData.rtc_ok && currentData.bme_ok && sdSizeMb > 0) {
        setLED(0, 255, 0);
    } else {
        setLED(255, 0, 0);
    }

    delay(1500);
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
