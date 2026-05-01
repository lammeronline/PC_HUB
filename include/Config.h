#pragma once

// I2C pins
#define I2C_SDA 32
#define I2C_SCL 25

// SD card SPI pins
#define SD_CS   5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK  18

// Display
#define TFT_BL_PIN 27

// RGB LED
#define LED_R 22
#define LED_G 16
#define LED_B 17

// WiFi / NTP
#define WIFI_SSID     "Interneta.NET (2.4 GHz)"
#define WIFI_PASSWORD "q2795844q"
#define NTP_SERVER    "pool.ntp.org"
#define NTP_OFFSET    10800

// Weather (Open-Meteo)
#define WEATHER_CITY "Dnipro"

// Update intervals
#define WEATHER_UPDATE_INTERVAL_SEC 600
#define DATA_LOG_INTERVAL_SEC       60
