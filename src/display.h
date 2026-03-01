#pragma once

#include <M5EPD.h>

struct DisplaySensorData {
    float temperature;
    float humidity;
    uint16_t co2;
    bool hasData;
    int8_t updateHour;
    int8_t updateMin;
};

void displayInit(bool fullClear);
void displayDraw(const DisplaySensorData& co2, const DisplaySensorData& outdoor,
                 int year, int month, int day, int wday, int batteryPct);
