#include "Led.h"
#include "Config.h"
#include "RuntimeSettings.h"

void initLED() {
    // Сначала HIGH (общий анод — HIGH = выключено), потом OUTPUT
    // чтобы не было белой вспышки при старте
    digitalWrite(LED_R, HIGH); pinMode(LED_R, OUTPUT);
    digitalWrite(LED_G, HIGH); pinMode(LED_G, OUTPUT);
    digitalWrite(LED_B, HIGH); pinMode(LED_B, OUTPUT);
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

void updateLED(float temp, float hum, float gas, bool bme_ok) {
    uint8_t mode = RuntimeSettings::ledMode();

    if (mode == 0 || !bme_ok) {
        offLED();
        return;
    }

    // Цвета совпадают с веб-интерфейсом: blue/green/yellow/orange/red
    // blue=#38c7ff  green=#43e884  yellow=#ffbd2e  orange=#ff922e  red=#ff4f5f
    uint8_t r, g, b;

    // Тёплые цвета (yellow/orange/red): b=0, иначе синий канал
    // даёт розово-белый оттенок даже при малом значении (common anode)
    if (mode == 1) {                            // Температура
        if      (temp < 18.0f) { r = 30;  g = 80;  b = 255; }  // blue   — Cold
        else if (temp < 26.0f) { r = 0;   g = 210; b = 0;   }  // green  — Comfort/Warm
        else if (temp < 30.0f) { r = 255; g = 110; b = 0;   }  // orange — Hot
        else                   { r = 255; g = 0;   b = 0;   }  // red    — Very hot
    } else if (mode == 2) {                     // Влажность
        if      (hum < 30.0f)  { r = 255; g = 180; b = 0;   }  // yellow — Dry
        else if (hum < 60.0f)  { r = 0;   g = 210; b = 0;   }  // green  — Normal
        else if (hum < 70.0f)  { r = 255; g = 180; b = 0;   }  // yellow — High
        else                   { r = 255; g = 110; b = 0;   }  // orange — Very high
    } else {                                    // Качество воздуха (газ)
        if      (gas > 150.0f) { r = 0;   g = 210; b = 0;   }  // green  — Excellent/Good
        else if (gas > 100.0f) { r = 255; g = 180; b = 0;   }  // yellow — Moderate
        else if (gas > 50.0f)  { r = 255; g = 110; b = 0;   }  // orange — Poor
        else                   { r = 255; g = 0;   b = 0;   }  // red    — Very poor
    }

    setLED(r, g, b);
}