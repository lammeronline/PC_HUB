#include "PCAgent.h"
#include <ArduinoJson.h>

static PCData *_pc  = nullptr;
static String  _buf;

void initPCAgent(PCData *data) {
    _pc = data;
    _buf.reserve(256);
}

// Non-blocking: reads chars from Serial until newline, then parses JSON
void handlePCSerial() {
    if (!_pc) return;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n') {
            _buf.trim();
            if (_buf.length() > 10) {
                JsonDocument doc;
                if (deserializeJson(doc, _buf) == DeserializationError::Ok) {
                    const char *type = doc["type"] | "";
                    if (strcmp(type, "pc") == 0) {
                        _pc->cpu_temp       = doc["ct"]  | 0.0f;
                        _pc->cpu_load       = doc["cl"]  | 0.0f;
                        _pc->cpu_power      = doc["cp"]  | 0.0f;
                        _pc->gpu_temp       = doc["gt"]  | 0.0f;
                        _pc->gpu_load       = doc["gl"]  | 0.0f;
                        _pc->gpu_vram_used  = doc["gvr"] | (uint16_t)0;
                        _pc->gpu_vram_total = doc["gvt"] | (uint16_t)0;
                        _pc->ram_used       = doc["ru"]  | (uint32_t)0;
                        _pc->ram_total      = doc["rt"]  | (uint32_t)0;
                        _pc->ok             = true;
                        _pc->lastMs         = millis();
                    }
                }
            }
            _buf = "";
        } else if (_buf.length() < 255) {
            _buf += c;
        }
    }
}
