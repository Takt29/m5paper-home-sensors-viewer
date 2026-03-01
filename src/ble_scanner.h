#pragma once

#include <Arduino.h>

struct SensorData {
    float temperature;
    float humidity;
    uint16_t co2;
    bool hasData;
    unsigned long lastUpdateMs;

    SensorData() : temperature(0), humidity(0), co2(0),
                   hasData(false), lastUpdateMs(0) {}
};

void bleInit();
void bleScan(int durationSec);
void bleStop();

SensorData& getCO2SensorData();
SensorData& getOutdoorMeterData();
