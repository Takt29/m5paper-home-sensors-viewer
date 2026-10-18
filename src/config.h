#pragma once

#include "secrets.h"

// --- NTP ---
#ifndef NTP_SERVER
#define NTP_SERVER "ntp.nict.jp"
#endif
#define GMT_OFFSET_SEC (9 * 3600)  // JST = UTC+9
#define DAYLIGHT_OFFSET_SEC 0
#ifndef NTP_SYNC_EVERY_N_BOOTS
#define NTP_SYNC_EVERY_N_BOOTS 12  // 12回に1回 (5分sleep × 12 = 約1時間)
#endif
#ifndef NTP_SYNC_TIMEOUT_MS
#define NTP_SYNC_TIMEOUT_MS 10000
#endif

// --- BLE scan ---
#define BLE_SCAN_DURATION_SEC 15

// --- Deep sleep ---
#ifndef SLEEP_DURATION_SEC
#define SLEEP_DURATION_SEC 300  // 5分
#endif

// --- Data staleness ---
#define DATA_STALE_SEC 1800  // 30分間データ未取得でハイフン表示

// --- CO2 thresholds for face icon (3段階) ---
#define CO2_GOOD 1000
#define CO2_WARNING 1500
