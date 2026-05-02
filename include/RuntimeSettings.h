#pragma once
#include <Arduino.h>

namespace RuntimeSettings {
    void begin();
    void reload();

    String wifiSsid();
    String wifiPassword();
    String ntpServer();
    long ntpOffsetSec();
    uint8_t backlightPercent();
    bool backlightInverted();

    void saveWifi(const String &ssid, const String &password);
    void saveNtp(const String &server, long offsetSec);
    void saveBacklight(uint8_t percent);
    void saveBacklightInverted(bool inverted);
}
