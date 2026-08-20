#pragma once
#include <Arduino.h>

namespace Backlight {
    void begin();
    void apply(uint8_t percent);
    void setBrightness(uint8_t percent, bool save);
    void autoUpdate(const char* timeStr);   // call every second when auto mode is on
    void restore(const char* timeStr);      // restore configured brightness after temporary screen-off
    uint8_t brightness();
}
