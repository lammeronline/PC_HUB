#pragma once
#include "PCData.h"

// Call once in setup() with pointer to the shared PCData struct
void initPCAgent(PCData *data);

// Call every loop() iteration — parses incoming serial lines for PC metrics
void handlePCSerial();
