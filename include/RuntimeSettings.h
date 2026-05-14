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
    bool    ntpEnabled();
    bool    ntpSyncOnBoot();
    uint8_t ntpSyncIntervalH();

    void saveNtp(const String &server, long offsetSec);
    void saveNtpEnabled(bool enabled);
    void saveNtpSyncOnBoot(bool enabled);
    void saveNtpSyncIntervalH(uint8_t hours);
    void saveDeviceIdentity(const String &deviceName, const String &hostname);
    void saveWeatherCity(const String &city);
    void saveWindMetric(bool metric);
    bool weatherLogEnabled();
    void saveWeatherLogEnabled(bool enabled);
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

    uint8_t  backlightMin();
    uint8_t  backlightMax();
    uint16_t backlightDawnStart();  // minutes since midnight
    uint16_t backlightDawnEnd();
    uint16_t backlightDuskStart();
    uint16_t backlightDuskEnd();

    void saveBacklight(uint8_t percent);
    void saveBacklightInverted(bool inverted);
    void saveAutoBacklight(bool enabled);
    void saveBacklightSchedule(uint8_t minPct, uint8_t maxPct,
                               uint16_t dawnStart, uint16_t dawnEnd,
                               uint16_t duskStart, uint16_t duskEnd);
    void saveLedMode(uint8_t mode);
    void savePcEnabled(bool enabled);
    float bmeTempOffset();
    float bmeHumOffset();
    void saveBmeCalibration(float tempOffset, float humOffset);

    bool     mqttEnabled();
    String   mqttBroker();
    uint16_t mqttPort();
    String   mqttUser();
    String   mqttPassword();
    String   mqttPrefix();
    uint16_t mqttIntervalSec();

    bool   staticIpEnabled();
    String staticIp();
    String staticGateway();
    String staticSubnet();
    String staticDns();
    String apIp();

    void saveIpSettings(bool staticEnabled, const String &ip, const String &gw,
                        const String &sn, const String &dns);
    void saveApIp(const String &ip);

    void saveMqttEnabled(bool enabled);
    void saveMqttSettings(const String &broker, uint16_t port,
                          const String &user, const String &password,
                          const String &prefix, uint16_t intervalSec);

    void saveTgEnabled(bool enabled);
    void saveTgCredentials(const String &token, const String &chatId);
    void saveTgThresholds(bool tempHiEn, float tempHi,
                          bool tempLoEn, float tempLo,
                          bool humHiEn,  float humHi,
                          bool humLoEn,  float humLo,
                          bool gasLoEn,  float gasLo,
                          uint16_t cooldownMin);
}
