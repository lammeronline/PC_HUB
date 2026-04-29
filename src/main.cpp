#include <Arduino.h>
#include <TFT_eSPI.h>
#include "Config.h"
#include "Sensors.h"
#include "SDCard.h"
#include "Led.h" // Подключили LED!

TFT_eSPI tft = TFT_eSPI();
SensorData currentData; 

void setup() {
  Serial.begin(115200);

  initLED(); // Инициализируем светодиод
  setLED(0, 0, 255); // Зажигаем СИНИМ (загрузка)

  pinMode(TFT_BL_PIN, OUTPUT);
  digitalWrite(TFT_BL_PIN, HIGH); 
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextFont(4);
  tft.setCursor(10, 10);
  tft.println("System Boot...");
  
  int y_pos = 40;

  initSensors();
  uint64_t sdSize = initSDCard();

  updateSensors(currentData);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, y_pos); y_pos += 30;
  tft.print("RTC: "); tft.println(currentData.rtc_ok ? "OK" : "FAILED");

  tft.setCursor(10, y_pos); y_pos += 30;
  tft.print("BME680: "); tft.println(currentData.bme_ok ? "OK" : "FAILED");

  tft.setCursor(10, y_pos); y_pos += 30;
  tft.print("SD Card: "); 
  if (sdSize > 0) {
      tft.print(sdSize); tft.println(" MB");
  } else {
      tft.println("FAILED");
  }

  // Логика светодиода после загрузки:
  // Если всё ок - Зеленый. Если хоть что-то отвалилось - Красный.
  if (currentData.rtc_ok && currentData.bme_ok && sdSize > 0) {
      setLED(0, 255, 0); // Зеленый
  } else {
      setLED(255, 0, 0); // Красный
  }

  delay(2000); 
  tft.fillScreen(TFT_BLACK); 
  offLED(); // Выключаем светодиод, чтобы не слепил в рабочем режиме
}

void loop() {
  // Получаем свежие данные с датчиков
  updateSensors(currentData);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(4);

  // Вывод времени
  if (currentData.rtc_ok) {
    tft.setCursor(10, 10);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.print("Time: ");
    tft.println(currentData.timeStr);
  }

  // Вывод погоды
  if (currentData.bme_ok) {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    
    tft.setCursor(10, 60);
    tft.printf("Temp: %.1f *C  \n", currentData.temperature);

    tft.setCursor(10, 100);
    tft.printf("Hum:  %.1f %%  \n", currentData.humidity);

    tft.setCursor(10, 140);
    tft.printf("Pres: %.1f hPa \n", currentData.pressure);

    tft.setCursor(10, 180);
    tft.printf("Gas:  %.1f kOhm\n", currentData.gas);
  }

  delay(1000);
}