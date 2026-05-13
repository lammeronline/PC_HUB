#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <TFT_eSPI.h>
#include "Config.h"
#include "Sensors.h"
#include "SDCard.h"
#include "Led.h"
#include "Weather.h"
#include "Logger.h"
#include "API.h"
#include "UI.h"
#include "PCAgent.h"
#include "RuntimeSettings.h"
#include "Backlight.h"
#include "Telegram.h"
#include "MQTT.h"

TFT_eSPI tft = TFT_eSPI();
SensorData  currentData;
WeatherData weatherData;
PCData      currentPC;

static const unsigned long SENSOR_INTERVAL_MS       = 1000;
static const unsigned long WEATHER_UPDATE_INTERVAL_MS = WEATHER_UPDATE_INTERVAL_SEC * 1000UL;
static const unsigned long DATA_LOG_INTERVAL_MS     = DATA_LOG_INTERVAL_SEC * 1000UL;
static unsigned long lastWeatherUpdate  = 0;
static unsigned long lastLogWrite       = 0;
static unsigned long lastSensorUpdate   = 0;
static unsigned long lastNtpSync        = 0;
static unsigned long wifiDownSince      = 0;
static String activeWeatherCity;
static bool sdReady   = false;
static uint64_t sdSizeMb = 0;
static bool      _apMode = false;
static DNSServer _dnsServer;

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

static void bootPending(TFT_eSPI &t, int x, int y, uint16_t bg, uint16_t amber) {
    t.setTextFont(2);
    t.setTextColor(amber, bg);
    t.setTextPadding(92);
    t.drawString("...", x, y);
    t.setTextPadding(0);
}

// ── setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    RuntimeSettings::begin();

    initLED();
    setLED(0, 0, 255);

    tft.init();
    Backlight::begin();
    tft.setRotation(1);
    initUI(tft);

    // ── boot screen ──────────────────────────────────────────────────────────
    const uint16_t BG    = 0x1082;
    const uint16_t BLUE  = 0x1B9F;
    const uint16_t AMBER = 0xFD00;
    const uint16_t MUTED = 0xA514;
    const int LX = 14;
    const int SX = 222;
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

    // ── Hardware ─────────────────────────────────────────────────────────────
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
    initWeatherLogger(sdReady);

    bootLabel(tft, LX, y, "SD Card", BG, MUTED);
    if (sdSizeMb > 0) {
        char sdBuf[16];
        snprintf(sdBuf, sizeof(sdBuf), "%llu MB", sdSizeMb);
        bootStatus(tft, SX, y, true, BG, sdBuf);
    } else {
        bootStatus(tft, SX, y, false, BG);
    }
    y += ROW;

    tft.drawLine(LX, y + 4, 305, y + 4, 0x2945);
    y += 12;

    // ── WiFi ─────────────────────────────────────────────────────────────────
    bootLabel(tft, LX, y, "WiFi", BG, MUTED);
    bootPending(tft, SX, y, BG, AMBER);

    bool wifiOk = connectWiFi();

    if (wifiOk) {
        String ip = WiFi.localIP().toString();
        bootStatus(tft, SX, y, true, BG, ip.c_str());
        y += ROW;
    } else {
        bootStatus(tft, SX, y, false, BG, "AP MODE");
        y += ROW;

        // ── AP mode ──────────────────────────────────────────────────────────
        String apSsid = "PCHUB-" + RuntimeSettings::hostname();
        String apIpStr = RuntimeSettings::apIp();
        IPAddress apIpAddr, apSubnet(255, 255, 255, 0);
        if (!apIpAddr.fromString(apIpStr)) apIpAddr = IPAddress(192, 168, 4, 1);
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(apIpAddr, apIpAddr, apSubnet);
        WiFi.softAP(apSsid.c_str());
        _apMode = true;
        setApiApMode(true);

        _dnsServer.start(53, "*", apIpAddr);

        setLED(255, 128, 0);            // оранжевый = AP режим

        initAPI(&currentData, &weatherData, &currentPC, sdReady);

        delay(1500);
        tft.fillScreen(TFT_BLACK);
        drawAPScreen(tft, apSsid, apIpStr.c_str());
        offLED();
        return;
    }

    // ── Network (только если WiFi подключён) ─────────────────────────────────

    bootLabel(tft, LX, y, "NTP sync", BG, MUTED);
    bootPending(tft, SX, y, BG, AMBER);
    bool ntp_ok = false;
    if (RuntimeSettings::ntpEnabled() && RuntimeSettings::ntpSyncOnBoot()) {
        ntp_ok = syncRTCfromNTP();
    }
    bootStatus(tft, SX, y, ntp_ok || !RuntimeSettings::ntpEnabled(), BG,
               !RuntimeSettings::ntpEnabled() ? "DISABLED" : (ntp_ok ? "OK" : "FAIL"));
    lastNtpSync = millis();
    y += ROW;

    bootLabel(tft, LX, y, "Geocoding", BG, MUTED);
    bootPending(tft, SX, y, BG, AMBER);
    activeWeatherCity = RuntimeSettings::weatherCity();
    bool geo_ok = geocodeCity(activeWeatherCity.c_str());
    bootStatus(tft, SX, y, geo_ok, BG, geo_ok ? activeWeatherCity.c_str() : nullptr);
    y += ROW;

    bootLabel(tft, LX, y, "Weather", BG, MUTED);
    bootPending(tft, SX, y, BG, AMBER);
    fetchWeather(weatherData);
    invalidateForecastUI();
    lastWeatherUpdate = millis();
    bootStatus(tft, SX, y, weatherData.ok, BG);

    // ── Finish init ──────────────────────────────────────────────────────────
    updateSensors(currentData);
    initAPI(&currentData, &weatherData, &currentPC, sdReady);
    Telegram::begin(&currentData, &weatherData);
    MQTT::begin(&currentData, &weatherData);
    initPCAgent(&currentPC);
    initPCDisplay(&currentPC);
    initWeatherTask(&weatherData);

    // ── Preload history from SD ───────────────────────────────────────────────
    if (sdReady && currentData.rtc_ok) {
        bootLabel(tft, LX, y, "SD history", BG, MUTED);
        bootPending(tft, SX, y, BG, AMBER);
        preloadHistoryFromSD();
        bootStatus(tft, SX, y, true, BG);
        y += ROW;
    }

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
    // ── Pending WiFi restart (срабатывает и в AP и в обычном режиме) ─────────
    if (apiPendingRestart()) {
        drawConnectingScreen(tft, RuntimeSettings::wifiSsid());
        delay(800);
        ESP.restart();
    }

    // ── AP mode: только веб-сервер ───────────────────────────────────────────
    if (_apMode) {
        _dnsServer.processNextRequest();
        handleAPI();
        delay(10);
        return;
    }

    // ── Normal mode ──────────────────────────────────────────────────────────
    unsigned long now = millis();

    handleAPI();
    handlePCSerial();
    MQTT::handle();

    // WiFi watchdog — reconnect if disconnected for over 60 s
    if (WiFi.status() != WL_CONNECTED) {
        if (wifiDownSince == 0) wifiDownSince = now;
        else if (now - wifiDownSince >= 60000UL) {
            Serial.println("WiFi: lost >60s, reconnecting");
            WiFi.reconnect();
            wifiDownSince = now;
        }
    } else {
        wifiDownSince = 0;
    }

    uint8_t ntpIntervalH = RuntimeSettings::ntpSyncIntervalH();
    if (ntpIntervalH > 0 && RuntimeSettings::ntpEnabled()) {
        unsigned long ntpIntervalMs = (unsigned long)ntpIntervalH * 3600000UL;
        if (now - lastNtpSync >= ntpIntervalMs) {
            syncRTCfromNTP();
            lastNtpSync = now;
        }
    }

    if (handleUI()) {
        drawUI(currentData, weatherData, currentUiStatus());
    }

    if (now - lastSensorUpdate >= SENSOR_INTERVAL_MS) {
        updateSensors(currentData);
        updateLED(currentData.temperature, currentData.humidity, currentData.gas, currentData.bme_ok);
        if (RuntimeSettings::autoBacklight() && currentData.rtc_ok)
            Backlight::autoUpdate(currentData.timeStr);
        drawUI(currentData, weatherData, currentUiStatus());
        lastSensorUpdate = now;
    }

    if (now - lastWeatherUpdate >= WEATHER_UPDATE_INTERVAL_MS && !weatherTaskBusy()) {
        triggerWeatherFetch();
        lastWeatherUpdate = now;
    }

    // Log weather after the background task signals a successful fetch
    if (weatherWasUpdated) {
        weatherWasUpdated = false;
        if (RuntimeSettings::weatherLogEnabled())
            logWeather(currentData, weatherData);
    }

    String weatherCity = RuntimeSettings::weatherCity();
    if (weatherCity != activeWeatherCity && !weatherTaskBusy()) {
        activeWeatherCity = weatherCity;
        triggerGeocodeAndFetch(activeWeatherCity.c_str());
        invalidateUI();
        lastWeatherUpdate = now;
    }

    if (now - lastLogWrite >= DATA_LOG_INTERVAL_MS) {
        logReading(currentData, weatherData);
        lastLogWrite = now;
    }

    delay(10);
}
