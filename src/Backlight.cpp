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

// Returns target brightness (15–85%) for a given time-of-day (minutes since midnight)
static uint8_t targetForMinutes(int t) {
    const uint8_t DIM    = 15;
    const uint8_t BRIGHT = 85;
    const int DAWN_S = 6  * 60;  // 360
    const int DAWN_E = 8  * 60;  // 480
    const int DUSK_S = 20 * 60;  // 1200
    const int DUSK_E = 22 * 60;  // 1320
    if (t >= DAWN_S && t < DAWN_E)
        return DIM + (uint8_t)((long)(BRIGHT - DIM) * (t - DAWN_S) / (DAWN_E - DAWN_S));
    if (t >= DAWN_E && t < DUSK_S)
        return BRIGHT;
    if (t >= DUSK_S && t < DUSK_E)
        return BRIGHT - (uint8_t)((long)(BRIGHT - DIM) * (t - DUSK_S) / (DUSK_E - DUSK_S));
    return DIM;
}

// timeStr format: "DD.MM.YYYY  HH:MM:SS" — hours at [12], minutes at [15]
void autoUpdate(const char* timeStr) {
    if (!timeStr || timeStr[12] < '0' || timeStr[12] > '9') return;
    int h = (timeStr[12] - '0') * 10 + (timeStr[13] - '0');
    int m = (timeStr[15] - '0') * 10 + (timeStr[16] - '0');
    uint8_t target = targetForMinutes(h * 60 + m);
    if (_brightness < target)      apply(_brightness + 1);
    else if (_brightness > target) apply(_brightness - 1);
}

uint8_t brightness() {
    return _brightness;
}

} // namespace Backlight
