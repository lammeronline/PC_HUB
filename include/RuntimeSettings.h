#pragma once
#include <Arduino.h>

namespace RuntimeSettings {
    void begin();
    void reload();

    String wifiSsid();
    String wifiPassword();
    String ntpServer();
    long ntpOffsetSec();

    void saveWifi(const String &ssid, const String &password);
    void saveNtp(const String &server, long offsetSec);
}
