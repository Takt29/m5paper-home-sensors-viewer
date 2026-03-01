#include <M5EPD.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "ble_scanner.h"
#include "display.h"

static void syncNTP() {
    Serial.println("[NTP] Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 40) {
        delay(500);
        Serial.print(".");
        retries++;
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[NTP] WiFi connection failed, using RTC time");
        return;
    }
    Serial.println("[NTP] WiFi connected");

    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10000)) {
        Serial.println("[NTP] Failed to obtain time");
    } else {
        Serial.printf("[NTP] Time: %04d/%02d/%02d %02d:%02d:%02d\n",
                      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                      timeinfo.tm_mday, timeinfo.tm_hour,
                      timeinfo.tm_min, timeinfo.tm_sec);

        rtc_time_t rtcTime;
        rtcTime.hour = timeinfo.tm_hour;
        rtcTime.min  = timeinfo.tm_min;
        rtcTime.sec  = timeinfo.tm_sec;
        M5.RTC.setTime(&rtcTime);

        rtc_date_t rtcDate;
        rtcDate.year = timeinfo.tm_year + 1900;
        rtcDate.mon  = timeinfo.tm_mon + 1;
        rtcDate.day  = timeinfo.tm_mday;
        rtcDate.week = timeinfo.tm_wday;
        M5.RTC.setDate(&rtcDate);

        Serial.println("[NTP] RTC updated");
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[NTP] WiFi disconnected");
}

static void enterDeepSleep() {
    Serial.printf("[Sleep] Entering deep sleep for %d seconds\n", SLEEP_DURATION_SEC);
    Serial.flush();
    M5.shutdown(SLEEP_DURATION_SEC);
    // Fallback: if shutdown didn't work, use ESP deep sleep
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_SEC * 1000000ULL);
    esp_deep_sleep_start();
}

void setup() {
    M5.begin(false, false, true, true, true);

    Serial.println("============================");
    Serial.println("M5Paper Home Sensors Viewer");
    Serial.println("============================");

    // NTP sync via WiFi
    syncNTP();

    // Read RTC
    rtc_time_t rtcTime;
    rtc_date_t rtcDate;
    M5.RTC.getTime(&rtcTime);
    M5.RTC.getDate(&rtcDate);

    int year   = rtcDate.year;
    int month  = rtcDate.mon;
    int day    = rtcDate.day;
    int wday   = rtcDate.week;
    int hour   = rtcTime.hour;
    int minute = rtcTime.min;

    // Battery level (mV -> %)
    uint32_t battVoltage = M5.getBatteryVoltage();
    // M5Paper: ~4350mV = 100%, ~3300mV = 0%
    int batteryPct = constrain((int)(battVoltage - 3300) * 100 / 1050, 0, 100);

    Serial.printf("[Info] Date: %04d/%02d/%02d (%d), Time: %02d:%02d, Battery: %d%%\n",
                  year, month, day, wday, hour, minute, batteryPct);

    // BLE scan
    bleInit();
    bleScan(BLE_SCAN_DURATION_SEC);
    bleStop();

    SensorData& co2Data = getCO2SensorData();
    SensorData& outdoorData = getOutdoorMeterData();

    Serial.printf("[Data] CO2 Sensor: hasData=%d, temp=%.1f, hum=%.1f, co2=%d\n",
                  co2Data.hasData, co2Data.temperature, co2Data.humidity, co2Data.co2);
    Serial.printf("[Data] Outdoor: hasData=%d, temp=%.1f, hum=%.1f\n",
                  outdoorData.hasData, outdoorData.temperature, outdoorData.humidity);

    // Initialize display first
    displayInit();

    // Draw display
    displayDraw(co2Data, outdoorData,
                year, month, day, wday,
                hour, minute, batteryPct);

    // Wait a moment for display update to finish
    delay(2000);

    // Enter deep sleep
    enterDeepSleep();
}

void loop() {
    // Never reached due to deep sleep
}
