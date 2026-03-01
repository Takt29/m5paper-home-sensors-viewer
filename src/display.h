#pragma once

#include <M5EPD.h>
#include "ble_scanner.h"

void displayInit();
void displayDraw(const SensorData& co2, const SensorData& outdoor,
                 int year, int month, int day, int wday,
                 int hour, int minute, int batteryPct);
