#pragma once
#include <Arduino.h>

namespace Backlight {
    void begin();
    void apply(uint8_t percent);
    void setBrightness(uint8_t percent, bool save);
    uint8_t brightness();
}
