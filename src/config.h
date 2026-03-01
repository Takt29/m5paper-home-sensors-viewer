#pragma once

#include "secrets.h"

// --- NTP ---
#define NTP_SERVER "ntp.nict.jp"
#define GMT_OFFSET_SEC (9 * 3600)  // JST = UTC+9
#define DAYLIGHT_OFFSET_SEC 0
#define NTP_SYNC_EVERY_N_BOOTS 12  // 12回に1回 (5分sleep × 12 = 約1時間)

// --- BLE scan ---
#define BLE_SCAN_DURATION_SEC 30

// --- Deep sleep ---
#define SLEEP_DURATION_SEC 300  // 5分

// --- CO2 thresholds for face icon ---
#define CO2_EXCELLENT 600
#define CO2_GOOD 1000
#define CO2_FAIR 1500
