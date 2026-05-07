#pragma once
#include <Arduino.h>

namespace RuntimeSettings {
    void begin();
    void reload();

    String wifiSsid();
    String wifiPassword();
    String ntpServer();
    String deviceName();
    String hostname();
    String weatherCity();
    bool windMetric();
    long ntpOffsetSec();
    uint8_t backlightPercent();
    bool backlightInverted();
    bool autoBacklight();

    void saveWifi(const String &ssid, const String &password);
    void saveNtp(const String &server, long offsetSec);
    void saveDeviceIdentity(const String &deviceName, const String &hostname);
    void saveWeatherCity(const String &city);
    void saveWindMetric(bool metric);
    uint8_t ledMode();
    bool pcEnabled();

    bool     tgEnabled();
    String   tgToken();
    String   tgChatId();
    bool     tgTempHiEn();
    float    tgTempHi();
    bool     tgTempLoEn();
    float    tgTempLo();
    bool     tgHumHiEn();
    float    tgHumHi();
    bool     tgHumLoEn();
    float    tgHumLo();
    bool     tgGasLoEn();
    float    tgGasLo();
    uint16_t tgCooldownMin();

    void saveBacklight(uint8_t percent);
    void saveBacklightInverted(bool inverted);
    void saveAutoBacklight(bool enabled);
    void saveLedMode(uint8_t mode);
    void savePcEnabled(bool enabled);

    void saveTgEnabled(bool enabled);
    void saveTgCredentials(const String &token, const String &chatId);
    void saveTgThresholds(bool tempHiEn, float tempHi,
                          bool tempLoEn, float tempLo,
                          bool humHiEn,  float humHi,
                          bool humLoEn,  float humLo,
                          bool gasLoEn,  float gasLo,
                          uint16_t cooldownMin);
}
