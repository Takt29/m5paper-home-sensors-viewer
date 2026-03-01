#include "ble_scanner.h"
#include "config.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <algorithm>
#include <string>

static SensorData co2Data;
static SensorData outdoorData;

static BLEScan* pBLEScan = nullptr;

static std::string normalizeMac(const char* mac) {
    std::string s(mac);
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static void logBytes(const char* label, const uint8_t* data, size_t len) {
    Serial.printf("[BLE] %s (len=%d):", label, len);
    for (size_t i = 0; i < len; i++) {
        Serial.printf(" %02X", data[i]);
    }
    Serial.println();
}

// getManufacturerData() は先頭2バイトに Company ID を含むためスキップする
static const int MFR_COMPANY_ID_LEN = 2;

static void parseOutdoorMeter(BLEAdvertisedDevice& device) {
    if (!device.haveManufacturerData()) return;

    std::string mfRaw = device.getManufacturerData();
    logBytes("Outdoor manu_data", (const uint8_t*)mfRaw.data(), mfRaw.length());

    if ((int)mfRaw.length() < MFR_COMPANY_ID_LEN + 12) return;

    const uint8_t* d = (const uint8_t*)mfRaw.data() + MFR_COMPANY_ID_LEN;

    uint8_t humidity = d[10] & 0x7F;
    float temp = (float)(d[8] & 0x0F) / 10.0f + (float)(d[9] & 0x7F);
    if (!(d[9] & 0x80)) {
        temp = -temp;
    }

    outdoorData.temperature = temp;
    outdoorData.humidity = (float)humidity;
    outdoorData.hasData = true;
    outdoorData.lastUpdateMs = millis();

    Serial.printf("[BLE] Outdoor parsed: temp=%.1f, hum=%d\n", temp, humidity);
}

static void parseCO2Sensor(BLEAdvertisedDevice& device) {
    if (!device.haveManufacturerData()) return;

    std::string mfRaw = device.getManufacturerData();
    logBytes("CO2 manu_data", (const uint8_t*)mfRaw.data(), mfRaw.length());

    if ((int)mfRaw.length() < MFR_COMPANY_ID_LEN + 16) return;

    const uint8_t* d = (const uint8_t*)mfRaw.data() + MFR_COMPANY_ID_LEN;

    uint8_t humidity = d[10] & 0x7F;
    float temp = (float)(d[8] & 0x0F) / 10.0f + (float)(d[9] & 0x7F);
    if (!(d[9] & 0x80)) {
        temp = -temp;
    }
    uint16_t co2 = (uint16_t)d[13] * 256 + d[14];

    co2Data.temperature = temp;
    co2Data.humidity = (float)humidity;
    co2Data.co2 = co2;
    co2Data.hasData = true;
    co2Data.lastUpdateMs = millis();

    Serial.printf("[BLE] CO2 parsed: temp=%.1f, hum=%d, co2=%d\n", temp, humidity, co2);
}

class SwitchBotCallback : public BLEAdvertisedDeviceCallbacks {
    std::string co2Mac;
    std::string outdoorMac;
public:
    SwitchBotCallback() {
        co2Mac = normalizeMac(CO2_SENSOR_MAC);
        outdoorMac = normalizeMac(OUTDOOR_METER_MAC);
    }

    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        std::string addr = advertisedDevice.getAddress().toString();
        std::transform(addr.begin(), addr.end(), addr.begin(), ::tolower);

        if (addr == co2Mac) {
            parseCO2Sensor(advertisedDevice);
        } else if (addr == outdoorMac) {
            parseOutdoorMeter(advertisedDevice);
        }
    }
};

void bleInit() {
    BLEDevice::init("");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new SwitchBotCallback(), true);
    pBLEScan->setActiveScan(false);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
}

void bleScan(int durationSec) {
    Serial.printf("[BLE] Scanning for %d seconds...\n", durationSec);
    pBLEScan->start(durationSec, false);
    pBLEScan->clearResults();
    Serial.println("[BLE] Scan complete.");
}

void bleStop() {
    if (pBLEScan) {
        pBLEScan->stop();
    }
    BLEDevice::deinit(false);
}

SensorData& getCO2SensorData() {
    return co2Data;
}

SensorData& getOutdoorMeterData() {
    return outdoorData;
}
