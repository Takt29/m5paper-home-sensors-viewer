#pragma once

#include <Arduino.h>

struct SensorData {
    float temperature;
    float humidity;
    uint16_t co2;
    bool hasData;
    uint32_t receivedAtMs;

    SensorData() : temperature(0), humidity(0), co2(0),
                   hasData(false), receivedAtMs(0) {}
};

void bleInit();
void bleScan(int durationSec);
void bleStop();

SensorData& getCO2SensorData();
SensorData& getOutdoorMeterData();
