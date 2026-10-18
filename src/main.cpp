#include <M5EPD.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <atomic>
#include <time.h>
#include <sys/time.h>
#include "config.h"
#include "ble_scanner.h"
#include "display.h"
#include "weather_client.h"

// Deep sleep 後も維持される変数 (esp_deep_sleep_start 使用時のみ有効)
RTC_DATA_ATTR static uint32_t bootCount = 0;
RTC_DATA_ATTR static DisplaySensorData persistCO2 = {};
RTC_DATA_ATTR static DisplaySensorData persistOutdoor = {};
RTC_DATA_ATTR static bool ntpSyncPending = true;
RTC_DATA_ATTR static WeatherCache persistWeather = {};

// SNTPのコールバックはネットワーク側のタスクから呼ばれる。
static std::atomic<bool> ntpResponseReceived{false};

static void onNtpTimeSynced(struct timeval*) {
    ntpResponseReceived.store(true);
}

static bool connectWiFi() {
    Serial.println("[WiFi] Connecting...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 40) {
        delay(500);
        Serial.print(".");
        retries++;
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Connection failed");
        return false;
    }
    Serial.println("[WiFi] Connected");
    return true;
}

static bool syncNTP() {
    // getLocalTime()だけでは、スリープ前から残る時刻も成功扱いになる。
    // 今回のリクエストに対するSNTPの同期完了通知を待つ。
    esp_sntp_stop();
    ntpResponseReceived.store(false);
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_set_time_sync_notification_cb(onNtpTimeSynced);
    Serial.printf("[NTP] Waiting for fresh response from %s (system epoch=%ld)\n",
                  NTP_SERVER, (long)time(nullptr));
    const uint32_t syncStartedAt = millis();
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);

    while (!ntpResponseReceived.load()
           && (uint32_t)(millis() - syncStartedAt) < NTP_SYNC_TIMEOUT_MS) {
        delay(10);
    }
    esp_sntp_stop();
    sntp_set_time_sync_notification_cb(nullptr);

    bool ok = false;
    if (ntpResponseReceived.load()) {
        const time_t syncedTime = time(nullptr);
        struct tm timeinfo;
        localtime_r(&syncedTime, &timeinfo);
        Serial.printf("[NTP] Fresh response received after %lu ms\n",
                      (unsigned long)(millis() - syncStartedAt));
        Serial.printf("[NTP] Time: %04d/%02d/%02d %02d:%02d:%02d\n",
                      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
                      timeinfo.tm_mday, timeinfo.tm_hour,
                      timeinfo.tm_min, timeinfo.tm_sec);

        rtc_time_t rtcTime;
        rtcTime.hour = timeinfo.tm_hour;
        rtcTime.min  = timeinfo.tm_min;
        rtcTime.sec  = timeinfo.tm_sec;

        rtc_date_t rtcDate;
        rtcDate.year = timeinfo.tm_year + 1900;
        rtcDate.mon  = timeinfo.tm_mon + 1;
        rtcDate.day  = timeinfo.tm_mday;
        rtcDate.week = timeinfo.tm_wday;
        ok = M5.RTC.setTime(&rtcTime) && M5.RTC.setDate(&rtcDate);
        Serial.println(ok ? "[NTP] RTC updated" : "[NTP] RTC update failed");
    } else {
        Serial.println("[NTP] No fresh response; keeping RTC unchanged");
    }

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

static time_t rtcToEpoch(const rtc_date_t& date, const rtc_time_t& time) {
    if (date.year < 2024 || date.mon < 1 || date.mon > 12 || date.day < 1 ||
        date.day > 31 || time.hour > 23 || time.min > 59 || time.sec > 59) return 0;
    struct tm t = {};
    t.tm_year = date.year - 1900;
    t.tm_mon  = date.mon - 1;
    t.tm_mday = date.day;
    t.tm_hour = time.hour;
    t.tm_min  = time.min;
    t.tm_sec  = time.sec;
    t.tm_isdst = -1;
    return mktime(&t);
}

void setup() {
    M5.begin(false, false, true, true, true);
    bootCount++;

    setenv("TZ", "JST-9", 1);
    tzset();

    Serial.printf("\n==== M5Paper Home Sensors Viewer (boot #%d) ====\n", bootCount);

    displayInit(bootCount == 1);

    // Restore the hardware RTC before HTTPS so certificate dates can be checked,
    // including boots that do not need a fresh NTP synchronization.
    rtc_time_t initialTime = {};
    rtc_date_t initialDate = {};
    M5.RTC.getTime(&initialTime);
    M5.RTC.getDate(&initialDate);
    const time_t initialEpoch = rtcToEpoch(initialDate, initialTime);
    if (weatherTimeValid(initialEpoch)) {
        struct timeval tv = {initialEpoch, 0};
        settimeofday(&tv, nullptr);
    }
    weatherCheckLocation(persistWeather);
    bool needNtpSync = ntpSyncPending || (bootCount == 1)
                    || (bootCount % NTP_SYNC_EVERY_N_BOOTS == 0)
                    || !weatherTimeValid(initialEpoch);
    const bool needWeather = weatherConfigured() && weatherNeedsRefresh(persistWeather, initialEpoch);

    if (needNtpSync || needWeather) {
        if (connectWiFi()) {
            if (needNtpSync) ntpSyncPending = !syncNTP();
            if (weatherConfigured() && weatherNeedsRefresh(persistWeather, time(nullptr))) {
                if (!weatherFetch(persistWeather)) Serial.println("[Weather] Will retry on next wake");
            }
        } else if (needNtpSync) {
            ntpSyncPending = true;
        }
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        Serial.println("[WiFi] Disconnected");
    }
    if (ntpSyncPending) Serial.println("[NTP] Will retry on next wake");

    uint32_t battVoltage = M5.getBatteryVoltage();
    int batteryPct = constrain((int)(battVoltage - 3300) * 100 / 1050, 0, 100);

    bleInit();
    bleScan(BLE_SCAN_DURATION_SEC);
    bleStop();

    // 描画用の現在日時はスキャン後に取得する。
    rtc_time_t rtcTime = {};
    rtc_date_t rtcDate = {};
    M5.RTC.getTime(&rtcTime);
    M5.RTC.getDate(&rtcDate);

    int year   = rtcDate.year;
    int month  = rtcDate.mon;
    int day    = rtcDate.day;
    int wday   = rtcDate.week;

    time_t nowEpoch = rtcToEpoch(rtcDate, rtcTime);
    const uint32_t nowMillis = millis();

    Serial.printf("[Info] %04d/%02d/%02d %02d:%02d:%02d JST (epoch=%ld), Battery: %d%%\n",
                  year, month, day, rtcTime.hour, rtcTime.min, rtcTime.sec,
                  (long)nowEpoch, batteryPct);

    SensorData& co2Scan = getCO2SensorData();
    SensorData& outdoorScan = getOutdoorMeterData();

    if (co2Scan.hasData) {
        persistCO2.temperature     = co2Scan.temperature;
        persistCO2.humidity        = co2Scan.humidity;
        persistCO2.co2             = co2Scan.co2;
        persistCO2.hasData         = true;
        persistCO2.updateTimestamp = nowEpoch
            - (time_t)((uint32_t)(nowMillis - co2Scan.receivedAtMs) / 1000);
        Serial.printf("[Data] CO2: temp=%.1f, hum=%.1f, co2=%d (updated, age=%lds)\n",
                      co2Scan.temperature, co2Scan.humidity, co2Scan.co2,
                      (long)(nowEpoch - persistCO2.updateTimestamp));
    } else {
        Serial.printf("[Data] CO2: no new data, using cached (hasData=%d, age=%lds)\n",
                      persistCO2.hasData,
                      persistCO2.updateTimestamp ? (long)(nowEpoch - persistCO2.updateTimestamp) : -1L);
    }

    if (outdoorScan.hasData) {
        persistOutdoor.temperature     = outdoorScan.temperature;
        persistOutdoor.humidity        = outdoorScan.humidity;
        persistOutdoor.co2             = 0;
        persistOutdoor.hasData         = true;
        persistOutdoor.updateTimestamp = nowEpoch
            - (time_t)((uint32_t)(nowMillis - outdoorScan.receivedAtMs) / 1000);
        Serial.printf("[Data] Outdoor: temp=%.1f, hum=%.1f (updated, age=%lds)\n",
                      outdoorScan.temperature, outdoorScan.humidity,
                      (long)(nowEpoch - persistOutdoor.updateTimestamp));
    } else {
        Serial.printf("[Data] Outdoor: no new data, using cached (hasData=%d, age=%lds)\n",
                      persistOutdoor.hasData,
                      persistOutdoor.updateTimestamp ? (long)(nowEpoch - persistOutdoor.updateTimestamp) : -1L);
    }

    const DisplayWeatherData weather = weatherForDisplay(persistWeather, nowEpoch);
    displayDraw(persistCO2, persistOutdoor, weather, nowEpoch,
                year, month, day, wday, batteryPct);

    delay(2000);
    enterDeepSleep();
}

void loop() {
}
