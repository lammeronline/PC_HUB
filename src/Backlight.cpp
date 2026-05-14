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

// Returns target brightness for a given time-of-day (minutes since midnight)
static uint8_t targetForMinutes(int t) {
    uint8_t dim    = RuntimeSettings::backlightMin();
    uint8_t bright = RuntimeSettings::backlightMax();
    int dawnS = RuntimeSettings::backlightDawnStart();
    int dawnE = RuntimeSettings::backlightDawnEnd();
    int duskS = RuntimeSettings::backlightDuskStart();
    int duskE = RuntimeSettings::backlightDuskEnd();

    if (t >= dawnS && t < dawnE && dawnE > dawnS)
        return dim + (uint8_t)((long)(bright - dim) * (t - dawnS) / (dawnE - dawnS));
    if (t >= dawnE && t < duskS)
        return bright;
    if (t >= duskS && t < duskE && duskE > duskS)
        return bright - (uint8_t)((long)(bright - dim) * (t - duskS) / (duskE - duskS));
    return dim;
}

// Adaptive step: large delta → fast approach, small delta → smooth finish
static uint8_t stepToward(uint8_t current, uint8_t target) {
    if (current == target) return current;
    uint8_t delta = current > target ? current - target : target - current;
    uint8_t step  = delta >= 20 ? 5 : delta >= 10 ? 3 : delta >= 3 ? 2 : 1;
    if (step > delta) step = delta;
    return current < target ? current + step : current - step;
}

// timeStr format: "DD.MM.YYYY  HH:MM:SS" — hours at [12], minutes at [15]
void autoUpdate(const char* timeStr) {
    if (!timeStr || timeStr[12] < '0' || timeStr[12] > '9') return;
    int h = (timeStr[12] - '0') * 10 + (timeStr[13] - '0');
    int m = (timeStr[15] - '0') * 10 + (timeStr[16] - '0');
    uint8_t target = targetForMinutes(h * 60 + m);
    uint8_t next   = stepToward(_brightness, target);
    if (next != _brightness) apply(next);
}

uint8_t brightness() {
    return _brightness;
}

} // namespace Backlight
