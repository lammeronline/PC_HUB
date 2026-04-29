#pragma once // Защита от двойного включения файла

// --- ПИНЫ I2C (Датчики) ---
#define I2C_SDA 32
#define I2C_SCL 25

// --- ПИНЫ SPI для SD Карты ---
#define SD_CS   5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK  18

// --- Дисплей ---
#define TFT_BL_PIN 27 // Подсветка

// --- RGB LED ---
#define LED_R 22
#define LED_G 16
#define LED_B 17

// --- WiFi / NTP ---
#define WIFI_SSID     "Interneta.NET (2.4 GHz)"
#define WIFI_PASSWORD "q2795844q"
#define NTP_SERVER    "pool.ntp.org"
#define NTP_OFFSET    10800  // UTC+3 (Москва)

// --- Weather (Open-Meteo) ---
#define WEATHER_CITY "Dnipro"  // Название города на английском