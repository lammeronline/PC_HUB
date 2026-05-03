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

    void saveWifi(const String &ssid, const String &password);
    void saveNtp(const String &server, long offsetSec);
    void saveDeviceIdentity(const String &deviceName, const String &hostname);
    void saveWeatherCity(const String &city);
    void saveWindMetric(bool metric);
    void saveBacklight(uint8_t percent);
    void saveBacklightInverted(bool inverted);
}
