#include "Led.h"
#include "Config.h"

void initLED() {
    // Настраиваем пины как выходы
    pinMode(LED_R, OUTPUT);
    pinMode(LED_G, OUTPUT);
    pinMode(LED_B, OUTPUT);
    
    // Выключаем при старте
    offLED(); 
}

void setLED(uint8_t r, uint8_t g, uint8_t b) {
    // ESP32 поддерживает analogWrite из коробки для ШИМ (PWM)
    // Инвертируем значения (255 - X), так как на CYD обычно "Общий Анод"
    analogWrite(LED_R, 255 - r);
    analogWrite(LED_G, 255 - g);
    analogWrite(LED_B, 255 - b);
}

void offLED() {
    setLED(0, 0, 0);
}