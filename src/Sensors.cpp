#include "Sensors.h"
#include "Config.h"
#include "RuntimeSettings.h"
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <RTClib.h>
#include <WiFi.h>
#include <time.h>

Adafruit_BME680 bme;
RTC_DS3231 rtc;

static bool _bme_found = false;
static bool _rtc_found = false;

void initSensors() {
    Wire.begin(I2C_SDA, I2C_SCL);

    // Инициализация RTC
    if (!rtc.begin(&Wire)) {
        Serial.println("RTC: FAILED");
    } else {
        _rtc_found = true;
        Serial.println("RTC: OK");
        if (rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    // Инициализация BME680
    if (!bme.begin(0x76, &Wire) && !bme.begin(0x77, &Wire)) {
        Serial.println("BME680: FAILED");
    } else {
        _bme_found = true;
        Serial.println("BME680: OK");
        bme.setTemperatureOversampling(BME680_OS_8X);
        bme.setHumidityOversampling(BME680_OS_2X);
        bme.setPressureOversampling(BME680_OS_4X);
        bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
        bme.setGasHeater(320, 150);
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
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE); // DHCP
    }

    WiFi.begin(ssid.c_str(), RuntimeSettings::wifiPassword().c_str());

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
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("NTP: no WiFi");
        return false;
    }

    String ntpServer = RuntimeSettings::ntpServer();
    configTime(RuntimeSettings::ntpOffsetSec(), 0, ntpServer.c_str());

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5000)) {
        WiFi.disconnect(true);
        Serial.println("NTP: time FAILED");
        return false;
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

    // WiFi не отключаем — нужен для запросов погоды
    Serial.println("NTP: OK");
    return true;
}

uint32_t getRTCUnixTime() {
    if (!_rtc_found) return 0;
    return rtc.now().unixtime();
}

// Функция обновления данных, заполняет нашу структуру
void updateSensors(SensorData &data) {
    data.rtc_ok = _rtc_found;
    data.bme_ok = false;

    if (_rtc_found) {
        DateTime now = rtc.now();
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "%02d.%02d.%04d  %02d:%02d:%02d",
                 now.day(), now.month(), now.year(),
                 now.hour(), now.minute(), now.second());
        strncpy(data.timeStr, timeBuf, sizeof(data.timeStr));
        data.weekday = now.dayOfTheWeek();   // 0=Sun … 6=Sat
    } else {
        strncpy(data.timeStr, "--.--.----  --:--:--", sizeof(data.timeStr));
        data.weekday = 0;
    }

    if (_bme_found && bme.performReading()) {
        data.bme_ok = true;
        data.temperature = bme.temperature + RuntimeSettings::bmeTempOffset();
        data.humidity    = constrain(bme.humidity + RuntimeSettings::bmeHumOffset(), 0.0f, 100.0f);
        data.pressure    = bme.pressure / 100.0f;
        data.gas         = bme.gas_resistance / 1000.0f;
    }
}
