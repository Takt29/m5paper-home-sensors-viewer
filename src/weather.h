#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

constexpr size_t WEATHER_DAYS = 2;
constexpr size_t WEATHER_HOURS = 48;
constexpr int WEATHER_REFRESH_SEC = 3600;
constexpr int WEATHER_STALE_SEC = 6 * 3600;

enum class WeatherIcon { Unknown, Sun, PartlyCloudy, Cloud, Fog, Rain, Snow, Thunder };

struct WeatherDay {
    time_t date;  // JST midnight
    float maxTemp;
    float minTemp;
    int16_t code;
    int16_t probability;  // -1 means missing, including JSON null
    bool hasMaxTemp;
    bool hasMinTemp;
};

struct WeatherHour {
    time_t end;  // Probability applies to the preceding hour.
    int16_t probability;
};

// Pointer-free data that can live in RTC memory across deep sleep.
struct WeatherCache {
    WeatherDay days[WEATHER_DAYS];
    WeatherHour hours[WEATHER_HOURS];
    time_t updatedAt;
    double latitude;
    double longitude;
    uint8_t dayCount;
    uint8_t hourCount;
    bool hasData;
};
static_assert(sizeof(WeatherCache) <= 1024, "Keep the forecast within the RTC memory budget");

struct DisplayWeatherData {
    WeatherIcon icon;
    int maxTemp;
    int minTemp;
    int probability;
    int windowProbability[2];
    char windowLabel[2][20];
    time_t updatedAt;
    bool tomorrow;
    bool hasMaxTemp;
    bool hasMinTemp;
};

bool weatherTimeValid(time_t now);
bool weatherNeedsRefresh(const WeatherCache& cache, time_t now);
// Commit only after both complete responses validate. Failure leaves cache intact.
bool weatherUpdateFromJson(WeatherCache& cache, const char* jmaJson,
                           const char* probabilityJson, time_t receivedAt);
DisplayWeatherData weatherForDisplay(const WeatherCache& cache, time_t now);
WeatherIcon weatherIconForCode(int code);
