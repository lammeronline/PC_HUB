#pragma once
#include <Arduino.h>

struct PCData {
    bool     ok             = false;
    float    cpu_temp       = 0;
    float    cpu_load       = 0;
    float    cpu_power      = 0;
    float    gpu_temp       = 0;
    float    gpu_load       = 0;
    uint16_t gpu_vram_used  = 0;   // MB
    uint16_t gpu_vram_total = 0;   // MB
    uint32_t ram_used       = 0;   // MB
    uint32_t ram_total      = 0;   // MB
    unsigned long lastMs    = 0;
};

static const unsigned long PC_STALE_MS = 10000UL;

inline bool pcFresh(const PCData &d) {
    return d.ok && (millis() - d.lastMs < PC_STALE_MS);
}
