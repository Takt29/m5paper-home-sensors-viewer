#include <M5EPD.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "ble_scanner.h"
#include "display.h"

// Deep sleep 後も維持される変数 (esp_deep_sleep_start 使用時のみ有効)
RTC_DATA_ATTR static uint32_t bootCount = 0;
RTC_DATA_ATTR static DisplaySensorData persistCO2 = {};
RTC_DATA_ATTR static DisplaySensorData persistOutdoor = {};

static bool syncNTP() {
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
        Serial.println("[NTP] WiFi connection failed");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return false;
    }
    Serial.println("[NTP] WiFi connected");

    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    bool ok = false;
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10000)) {
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

        ok = true;
        Serial.println("[NTP] RTC updated");
    } else {
        Serial.println("[NTP] Failed to obtain time");
    }

    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("[NTP] WiFi disconnected");
    return ok;
}

static void enterDeepSleep() {
    Serial.printf("[Sleep] Deep sleep for %d seconds (boot #%d)\n",
                  SLEEP_DURATION_SEC, bootCount);
    Serial.flush();
    M5.disableEPDPower();
    esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_SEC * 1000000ULL);
    esp_deep_sleep_start();
}

void setup() {
    M5.begin(false, false, true, true, true);
    bootCount++;

    Serial.printf("\n==== M5Paper Home Sensors Viewer (boot #%d) ====\n", bootCount);

    // Display initialization
    displayInit(bootCount == 1);

    // NTP synchronization
    bool needNtpSync = (bootCount == 1)
                    || (bootCount % NTP_SYNC_EVERY_N_BOOTS == 0);

    if (needNtpSync) {
        Serial.println("[NTP] Sync scheduled for this boot");
        syncNTP();
    } else {
        Serial.printf("[NTP] Skipped (next sync at boot #%d)\n",
                      ((bootCount / NTP_SYNC_EVERY_N_BOOTS) + 1) * NTP_SYNC_EVERY_N_BOOTS);
    }

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

    uint32_t battVoltage = M5.getBatteryVoltage();
    int batteryPct = constrain((int)(battVoltage - 3300) * 100 / 1050, 0, 100);

    Serial.printf("[Info] %04d/%02d/%02d %02d:%02d, Battery: %d%%\n",
                  year, month, day, hour, minute, batteryPct);

    // BLE scan
    bleInit();
    bleScan(BLE_SCAN_DURATION_SEC);
    bleStop();

    SensorData& co2Scan = getCO2SensorData();
    SensorData& outdoorScan = getOutdoorMeterData();

    // 新データが取れたら永続領域を更新、取れなければ前回値をそのまま使う
    if (co2Scan.hasData) {
        persistCO2.temperature = co2Scan.temperature;
        persistCO2.humidity    = co2Scan.humidity;
        persistCO2.co2         = co2Scan.co2;
        persistCO2.hasData     = true;
        persistCO2.updateHour  = (int8_t)hour;
        persistCO2.updateMin   = (int8_t)minute;
        Serial.printf("[Data] CO2: temp=%.1f, hum=%.1f, co2=%d (updated)\n",
                      co2Scan.temperature, co2Scan.humidity, co2Scan.co2);
    } else {
        Serial.printf("[Data] CO2: no new data, using cached (hasData=%d)\n",
                      persistCO2.hasData);
    }

    if (outdoorScan.hasData) {
        persistOutdoor.temperature = outdoorScan.temperature;
        persistOutdoor.humidity    = outdoorScan.humidity;
        persistOutdoor.co2         = 0;
        persistOutdoor.hasData     = true;
        persistOutdoor.updateHour  = (int8_t)hour;
        persistOutdoor.updateMin   = (int8_t)minute;
        Serial.printf("[Data] Outdoor: temp=%.1f, hum=%.1f (updated)\n",
                      outdoorScan.temperature, outdoorScan.humidity);
    } else {
        Serial.printf("[Data] Outdoor: no new data, using cached (hasData=%d)\n",
                      persistOutdoor.hasData);
    }

    displayDraw(persistCO2, persistOutdoor,
                year, month, day, wday, batteryPct);

    delay(2000);
    enterDeepSleep();
}

void loop() {
}
