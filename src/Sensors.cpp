#include "Sensors.h"
#include "Config.h"
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include <RTClib.h>

Adafruit_BME680 bme;
RTC_DS3231 rtc;

bool _bme_found = false;
bool _rtc_found = false;

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

// Функция обновления данных, заполняет нашу структуру
void updateSensors(SensorData &data) {
    data.rtc_ok = _rtc_found;
    data.bme_ok = _bme_found;

    if (_rtc_found) {
        DateTime now = rtc.now();
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "%02d.%02d.%04d  %02d:%02d:%02d", 
                now.day(), now.month(), now.year(), 
                now.hour(), now.minute(), now.second());
        data.timeStr = String(timeBuf);
    }

    if (_bme_found && bme.performReading()) {
        data.temperature = bme.temperature;
        data.humidity = bme.humidity;
        data.pressure = bme.pressure / 100.0; // в hPa
        data.gas = bme.gas_resistance / 1000.0; // в KOhms
    }
}