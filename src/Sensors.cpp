#include "Sensors.h"
#include "Config.h"
#include "RuntimeSettings.h"
#include <Wire.h>
#include "bsec.h"
#include <RTClib.h>
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>

static Bsec        _bme;
static RTC_DS3231  rtc;
static Preferences _prefs;

static bool          _bme_found        = false;
static bool          _rtc_found        = false;
static uint8_t       _lastSavedAccuracy = 0;
static unsigned long _lastStateSave     = 0;

// ── BSEC state NVS ───────────────────────────────────────────────────────────

static void saveBsecState() {
    uint8_t buf[BSEC_MAX_STATE_BLOB_SIZE];
    _bme.getState(buf);
    if (_bme.bsecStatus != BSEC_OK) return;
    _prefs.begin("bsec", false);
    _prefs.putBytes("state", buf, BSEC_MAX_STATE_BLOB_SIZE);
    _prefs.end();
    Serial.printf("BSEC: state saved (accuracy=%d)\n", _bme.iaqAccuracy);
}

static void loadBsecState() {
    _prefs.begin("bsec", true);
    size_t len = _prefs.getBytesLength("state");
    if (len != BSEC_MAX_STATE_BLOB_SIZE) {
        _prefs.end();
        Serial.println("BSEC: no saved state, fresh calibration");
        return;
    }
    uint8_t buf[BSEC_MAX_STATE_BLOB_SIZE];
    _prefs.getBytes("state", buf, len);
    _prefs.end();

    _bme.setState(buf);
    if (_bme.bsecStatus == BSEC_OK) {
        Serial.println("BSEC: state restored from NVS");
    } else {
        Serial.printf("BSEC: state restore failed (%d), clearing NVS\n", _bme.bsecStatus);
        _prefs.begin("bsec", false);
        _prefs.clear();
        _prefs.end();
        _bme.bsecStatus = BSEC_OK;
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void initSensors() {
    Wire.begin(I2C_SDA, I2C_SCL);

    // RTC DS3231
    if (!rtc.begin(&Wire)) {
        Serial.println("RTC: FAILED");
    } else {
        _rtc_found = true;
        Serial.println("RTC: OK");
        if (rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    // BME680 via BSEC — try 0x76 then 0x77
    _bme.begin(0x76, Wire);
    if (_bme.bsecStatus != BSEC_OK || _bme.bme68xStatus != BME68X_OK)
        _bme.begin(0x77, Wire);

    Serial.printf("BME680 BSEC init: bsec=%d bme68x=%d\n", _bme.bsecStatus, _bme.bme68xStatus);

    if (_bme.bsecStatus == BSEC_OK && _bme.bme68xStatus == BME68X_OK) {
        bsec_virtual_sensor_t sensors[] = {
            BSEC_OUTPUT_IAQ,
            BSEC_OUTPUT_CO2_EQUIVALENT,
            BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
            BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
            BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
            BSEC_OUTPUT_RAW_PRESSURE,
            BSEC_OUTPUT_RAW_GAS,
        };
        _bme.updateSubscription(sensors, sizeof(sensors) / sizeof(sensors[0]), BSEC_SAMPLE_RATE_LP);
        if (_bme.bsecStatus == BSEC_OK) {
            _bme_found = true;
            loadBsecState();
            Serial.println("BME680 BSEC: OK");
        } else {
            Serial.printf("BME680 BSEC subscription FAILED: bsec=%d bme68x=%d\n", _bme.bsecStatus, _bme.bme68xStatus);
        }
    } else {
        Serial.printf("BME680 BSEC: FAILED (bsec=%d bme68x=%d)\n", _bme.bsecStatus, _bme.bme68xStatus);
    }
}


void applyNtpConfig() {
    // ntpServerBuf must outlive this call: lwIP's sntp module (configTime/configTzTime
    // -> sntp_setservername) stores a raw pointer to the server name, not a copy, and
    // resolves it later/asynchronously, not synchronously within this call. A local
    // String here would be destroyed the instant this function returns, leaving lwIP
    // with a dangling pointer — the first call after boot can still appear to work
    // (the freed heap bytes haven't been reused yet), but any later call reads
    // whatever has since overwritten that memory and fails to resolve. Plain static
    // char array (not static String) — no constructor/destructor registration needed.
    static char ntpServerBuf[64];
    RuntimeSettings::ntpServer().toCharArray(ntpServerBuf, sizeof(ntpServerBuf));
    if (RuntimeSettings::ntpAutoDst()) {
        configTzTime(RuntimeSettings::ntpTzRule().c_str(), ntpServerBuf);
    } else {
        configTime(RuntimeSettings::ntpOffsetSec(), 0, ntpServerBuf);
    }
}

bool syncRTCfromNTP() {
    if (WiFi.status() != WL_CONNECTED) return false;

    // Reverted to a single applyNtpConfig() call (one sntp_init()) instead of
    // re-running it on every retry: an experiment comparing this against the
    // repeated-reinit version on the same network showed the simple version
    // syncing reliably within ~10-15s on 3/3 fresh boots, while repeatedly
    // tearing down and restarting the SNTP session on each retry made failures
    // more common, not less — likely destabilizing, not helping, lwIP's sntp
    // state machine. Keep it to one init; just poll it a couple of times.
    applyNtpConfig();

    struct tm timeinfo;
    // First attempt — network may still be settling after fresh connect
    if (!getLocalTime(&timeinfo, 4000)) {
        delay(1500);
        if (!getLocalTime(&timeinfo, 4000)) {
            Serial.println("NTP: time FAILED");
            return false;
        }
    }

    if (_rtc_found) {
        rtc.adjust(DateTime(
            timeinfo.tm_year + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday,
            timeinfo.tm_hour,
            timeinfo.tm_min,
            timeinfo.tm_sec
        ));
    }

    Serial.println("NTP: OK");
    return true;
}

uint32_t getRTCUnixTime() {
    if (!_rtc_found) return 0;
    return rtc.now().unixtime();
}

bool setRTCManual(int year, int month, int day, int hour, int minute, int second) {
    if (!_rtc_found) return false;
    if (year < 2000 || year > 2099) return false;
    if (month < 1 || month > 12 || day < 1 || day > 31) return false;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return false;
    rtc.adjust(DateTime((uint16_t)year, (uint8_t)month, (uint8_t)day,
                         (uint8_t)hour, (uint8_t)minute, (uint8_t)second));
    return true;
}

void updateSensors(SensorData &data) {
    data.rtc_ok = _rtc_found;

    if (_rtc_found) {
        DateTime now = rtc.now();
        snprintf(data.timeStr, sizeof(data.timeStr), "%02d.%02d.%04d  %02d:%02d:%02d",
                 now.day(), now.month(), now.year(),
                 now.hour(), now.minute(), now.second());
        data.weekday = now.dayOfTheWeek();
    } else {
        strncpy(data.timeStr, "--.--.----  --:--:--", sizeof(data.timeStr));
        data.weekday = 0;
    }

    if (!_bme_found) {
        data.bme_ok = false;
        return;
    }

    // run() returns true only when new data is ready (~every 3s in LP mode)
    if (_bme.run()) {
        data.bme_ok       = true;
        data.temperature  = _bme.temperature + RuntimeSettings::bmeTempOffset();
        data.humidity     = constrain(_bme.humidity + RuntimeSettings::bmeHumOffset(), 0.0f, 100.0f);
        data.pressure     = _bme.pressure / 100.0f;
        data.gas          = _bme.gasResistance / 1000.0f;  // raw kΩ for logging
        data.iaq          = _bme.iaq;
        data.iaq_accuracy = _bme.iaqAccuracy;
        data.co2          = _bme.co2Equivalent;
        data.voc          = _bme.breathVocEquivalent;

        // Save state when accuracy first reaches 1 or improves — so next boot converges fast
        // Also re-save periodically every 6 h once stable (acc >= 2)
        uint8_t acc = _bme.iaqAccuracy;
        unsigned long now = millis();
        bool improvedAndGood = acc >= 1 && acc > _lastSavedAccuracy;
        bool periodicSave    = acc >= 2 && now - _lastStateSave >= 6UL * 3600000UL;
        if (improvedAndGood || periodicSave) {
            saveBsecState();
            _lastStateSave    = now;
            _lastSavedAccuracy = acc;
        }
    }
    // No new data this tick — keep existing values unchanged
}
