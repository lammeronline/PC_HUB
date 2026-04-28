#include "SDCard.h"
#include "Config.h"
#include <FS.h>
#include <SD.h>
#include <SPI.h>

SPIClass spiSD(HSPI); // Выделенная шина для SD (чтобы не конфликтовать с экраном)

uint64_t initSDCard() {
    spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    
    if (!SD.begin(SD_CS, spiSD)) {
        Serial.println("SD Card: FAILED");
        return 0;
    }
    
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("SD Card: INVALID");
        return 0;
    }

    // Тестовая запись файла скрыта здесь, чтобы не мусорить в main
    File file = SD.open("/test.txt", FILE_WRITE);
    if (file) {
        file.println("SD works!");
        file.close();
    }

    return SD.cardSize() / (1024 * 1024); // Возвращаем мегабайты
}