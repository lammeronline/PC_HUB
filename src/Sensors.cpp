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

bool connectWiFi() {
    String ssid = RuntimeSettings::wifiSsid();
    if (ssid.isEmpty()) return false;

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(RuntimeSettings::hostname().c_str());

    if (RuntimeSettings::staticIpEnabled()) {
        IPAddress ip, gw, sn, dns;
        if (ip.fromString(RuntimeSettings::staticIp()) &&
            gw.fromString(RuntimeSettings::staticGateway()) &&
            sn.fromString(RuntimeSettings::staticSubnet())) {
            dns.fromString(RuntimeSettings::staticDns());
            WiFi.config(ip, gw, sn, dns);
        }
    } else {
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    }

    WiFi.begin(ssid.c_str(), RuntimeSettings::wifiPassword().c_str());
    WiFi.setAutoReconnect(true);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(true);
        Serial.println("WiFi: FAILED");
        return false;
    }
    Serial.printf("WiFi: OK %s\n", WiFi.localIP().toString().c_str());
    return true;
}

bool syncRTCfromNTP() {
    if (WiFi.status() != WL_CONNECTED) return false;

    String ntpServer = RuntimeSettings::ntpServer();
    configTime(RuntimeSettings::ntpOffsetSec(), 0, ntpServer.c_str());

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
