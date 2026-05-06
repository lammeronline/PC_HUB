#include "Led.h"
#include "Config.h"
#include "RuntimeSettings.h"

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

void updateLED(float temp, float hum, float gas, bool bme_ok) {
    uint8_t mode = RuntimeSettings::ledMode();

    if (mode == 0 || !bme_ok) {
        offLED();
        return;
    }

    uint8_t r, g, b;

    if (mode == 1) {
        if      (temp < 18.0f) { r = 0;   g = 100; b = 255; }
        else if (temp < 22.0f) { r = 67;  g = 232; b = 132; }
        else if (temp < 26.0f) { r = 253; g = 189; b = 46;  }
        else                   { r = 255; g = 79;  b = 95;  }
    } else if (mode == 2) {
        if      (hum < 30.0f)  { r = 253; g = 189; b = 46;  }
        else if (hum < 60.0f)  { r = 67;  g = 232; b = 132; }
        else if (hum < 70.0f)  { r = 150; g = 80;  b = 255; }
        else                   { r = 255; g = 79;  b = 95;  }
    } else {
        if      (gas > 300.0f) { r = 67;  g = 232; b = 132; }
        else if (gas > 150.0f) { r = 150; g = 232; b = 50;  }
        else if (gas > 100.0f) { r = 253; g = 189; b = 46;  }
        else if (gas > 50.0f)  { r = 255; g = 140; b = 0;   }
        else                   { r = 255; g = 79;  b = 95;  }
    }

    setLED(r, g, b);
}