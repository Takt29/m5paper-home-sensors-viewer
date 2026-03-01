#pragma once

#include <M5EPD.h>
#include <time.h>

struct DisplaySensorData {
    float temperature;
    float humidity;
    uint16_t co2;
    bool hasData;
    time_t updateTimestamp;
};

void displayInit(bool fullClear);
void displayDraw(const DisplaySensorData& co2, const DisplaySensorData& outdoor,
                 time_t nowEpoch,
                 int year, int month, int day, int wday, int batteryPct);
