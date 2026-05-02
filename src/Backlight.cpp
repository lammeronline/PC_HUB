#include "Backlight.h"
#include "Config.h"
#include "RuntimeSettings.h"

namespace Backlight {

static const uint8_t PWM_CHANNEL = 7;
static const uint32_t PWM_FREQ = 5000;
static const uint8_t PWM_BITS = 8;
static uint8_t _brightness = 100;

static uint8_t clampPercent(int percent) {
    if (percent < 0) return 0;
    if (percent > 100) return 100;
    return (uint8_t)percent;
}

static uint8_t dutyFromPercent(uint8_t percent) {
    uint8_t duty = (uint8_t)((uint16_t)percent * 255U / 100U);
    return RuntimeSettings::backlightInverted() ? (uint8_t)(255U - duty) : duty;
}

void begin() {
    ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_BITS);
    ledcAttachPin(TFT_BL_PIN, PWM_CHANNEL);
    apply(RuntimeSettings::backlightPercent());
}

void apply(uint8_t percent) {
    _brightness = clampPercent(percent);
    ledcWrite(PWM_CHANNEL, dutyFromPercent(_brightness));
}

void setBrightness(uint8_t percent, bool save) {
    apply(percent);
    if (save) RuntimeSettings::saveBacklight(_brightness);
}

uint8_t brightness() {
    return _brightness;
}

} // namespace Backlight
