#pragma once
#include <Arduino.h>

namespace Backlight {
    void begin();
    void apply(uint8_t percent);
    void setBrightness(uint8_t percent, bool save);
    void autoUpdate(const char* timeStr);   // call every second when auto mode is on
    uint8_t brightness();
}
