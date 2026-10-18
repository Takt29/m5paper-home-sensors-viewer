#include "weather.h"
#include <ArduinoJson.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
constexpr time_t HOUR = 3600;
constexpr time_t DAY = 24 * HOUR;
constexpr time_t JST = 9 * HOUR;

time_t midnight(time_t now) {
    return (now + JST) / DAY * DAY - JST;
}

int hourOfDay(time_t now) {
    return (now + JST) / HOUR % 24;
}

// Parse the API's timezone=Asia/Tokyo ISO strings without relying on the host TZ.
bool parseTime(const char* text, bool dateOnly, time_t& result) {
    if (!text || strlen(text) != (dateOnly ? 10u : 16u)) return false;
    if (text[4] != '-' || text[7] != '-' ||
        (!dateOnly && (text[10] != 'T' || text[13] != ':'))) return false;
    for (size_t i = 0; text[i]; ++i) {
        if (i == 4 || i == 7 || (!dateOnly && (i == 10 || i == 13))) continue;
        if (text[i] < '0' || text[i] > '9') return false;
    }
    int year, month, day, hour = 0, minute = 0;
    if (sscanf(text, "%4d-%2d-%2d", &year, &month, &day) != 3) return false;
    if (!dateOnly && sscanf(text + 11, "%2d:%2d", &hour, &minute) != 2) return false;
    if (year < 2024 || year > 2099 || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour > 23 || minute != 0) return false;
    // Gregorian civil date -> days since 1970-01-01.
    int y = year - (month <= 2);
    const int era = y / 400;
    const int yoe = y - era * 400;
    const int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t seconds = (static_cast<int64_t>(era) * 146097 + doe - 719468) * DAY
                            + hour * HOUR - JST;
    if (seconds + JST > std::numeric_limits<time_t>::max()) return false;
    result = static_cast<time_t>(seconds);
    const time_t utcWallTime = result + JST;
    struct tm check;
    gmtime_r(&utcWallTime, &check);
    return check.tm_year + 1900 == year && check.tm_mon + 1 == month &&
           check.tm_mday == day && check.tm_hour == hour;
}

bool arraySize(JsonVariantConst value, size_t count) {
    return value.is<JsonArrayConst>() && value.size() == count;
}

int16_t probability(JsonVariantConst value) {
    if (!value.is<int>()) return -1;
    const int n = value.as<int>();
    return n >= 0 && n <= 100 ? static_cast<int16_t>(n) : -1;
}

bool temperature(JsonVariantConst value, float& output) {
    if (!value.is<float>()) return false;
    output = value.as<float>();
    return std::isfinite(output) && output >= -100 && output <= 80;
}

int windowMaximum(const WeatherCache& cache, time_t start) {
    int maximum = 0;
    for (int step = 1; step <= 4; ++step) {
        const time_t end = start + step * HOUR;
        int found = -1;
        for (size_t i = 0; i < cache.hourCount; ++i) {
            if (cache.hours[i].end == end) found = cache.hours[i].probability;
        }
        if (found < 0 || found > 100) return -1;
        if (found > maximum) maximum = found;
    }
    return maximum;
}
}  // namespace

bool weatherTimeValid(time_t now) {
    return now >= 1704067200;  // 2024-01-01 UTC; reject an unset RTC.
}

bool weatherNeedsRefresh(const WeatherCache& cache, time_t now) {
    return weatherTimeValid(now) && (!cache.hasData || now < cache.updatedAt ||
                                    now - cache.updatedAt >= WEATHER_REFRESH_SEC);
}

bool weatherUpdateFromJson(WeatherCache& cache, const char* jmaJson,
                           const char* probabilityJson, time_t receivedAt) {
    if (!weatherTimeValid(receivedAt) || !jmaJson || !probabilityJson) return false;
    JsonDocument jma;
    JsonDocument rain;
    if (deserializeJson(jma, jmaJson) || deserializeJson(rain, probabilityJson)) return false;
    if (jma["error"].as<bool>() || rain["error"].as<bool>() ||
        jma["utc_offset_seconds"].as<int>() != JST ||
        rain["utc_offset_seconds"].as<int>() != JST) return false;

    const JsonObjectConst daily = jma["daily"];
    const JsonObjectConst rainDaily = rain["daily"];
    const JsonObjectConst hourly = rain["hourly"];
    const size_t days = daily["time"].size();
    const size_t hours = hourly["time"].size();
    if (days == 0 || days > WEATHER_DAYS || hours == 0 || hours > WEATHER_HOURS ||
        !arraySize(daily["time"], days) || !arraySize(daily["weather_code"], days) ||
        !arraySize(daily["temperature_2m_max"], days) ||
        !arraySize(daily["temperature_2m_min"], days) ||
        !arraySize(rainDaily["time"], days) ||
        !arraySize(rainDaily["precipitation_probability_max"], days) ||
        !arraySize(hourly["time"], hours) ||
        !arraySize(hourly["precipitation_probability"], hours)) return false;

    WeatherCache next = {};
    next.latitude = cache.latitude;
    next.longitude = cache.longitude;
    next.dayCount = static_cast<uint8_t>(days);
    next.hourCount = static_cast<uint8_t>(hours);
    for (size_t i = 0; i < days; ++i) {
        WeatherDay& day = next.days[i];
        time_t rainDate;
        if (!parseTime(daily["time"][i], true, day.date) ||
            !parseTime(rainDaily["time"][i], true, rainDate) || rainDate != day.date ||
            (i > 0 && day.date != next.days[i - 1].date + DAY)) return false;
        const int code = daily["weather_code"][i].is<int>()
                         ? daily["weather_code"][i].as<int>() : -1;
        day.code = code >= 0 && code <= 99 ? static_cast<int16_t>(code) : -1;
        day.hasMaxTemp = temperature(daily["temperature_2m_max"][i], day.maxTemp);
        day.hasMinTemp = temperature(daily["temperature_2m_min"][i], day.minTemp);
        day.probability = probability(rainDaily["precipitation_probability_max"][i]);
    }
    for (size_t i = 0; i < hours; ++i) {
        WeatherHour& hour = next.hours[i];
        if (!parseTime(hourly["time"][i], false, hour.end) ||
            (i > 0 && hour.end <= next.hours[i - 1].end)) return false;
        hour.probability = probability(hourly["precipitation_probability"][i]);
    }
    // A request crossing midnight may start on yesterday, but not before that.
    const time_t today = midnight(receivedAt);
    if (next.days[0].date < today - DAY || next.days[0].date > today ||
        next.days[days - 1].date < today) return false;
    next.updatedAt = receivedAt;
    next.hasData = true;
    cache = next;
    return true;
}

WeatherIcon weatherIconForCode(int code) {
    switch (code) {
        case 0: case 1: return WeatherIcon::Sun;
        case 2: return WeatherIcon::PartlyCloudy;
        case 3: return WeatherIcon::Cloud;
        case 45: case 48: return WeatherIcon::Fog;
        case 51: case 53: case 55: case 56: case 57:
        case 61: case 63: case 65: case 66: case 67:
        case 80: case 81: case 82: return WeatherIcon::Rain;
        case 71: case 73: case 75: case 77: case 85: case 86: return WeatherIcon::Snow;
        case 95: case 96: case 97: case 99: return WeatherIcon::Thunder;
        default: return WeatherIcon::Unknown;
    }
}

DisplayWeatherData weatherForDisplay(const WeatherCache& cache, time_t now) {
    DisplayWeatherData view = {};
    view.icon = WeatherIcon::Unknown;
    view.probability = view.windowProbability[0] = view.windowProbability[1] = -1;
    view.updatedAt = cache.hasData ? cache.updatedAt : 0;
    if (!weatherTimeValid(now)) {
        for (auto& label : view.windowLabel) snprintf(label, sizeof(label), "--:--時");
        return view;
    }
    view.tomorrow = hourOfDay(now) >= 21;
    const time_t targetDay = midnight(now) + (view.tomorrow ? DAY : 0);
    const time_t start = now / HOUR * HOUR;
    for (int i = 0; i < 2; ++i) {
        const int from = hourOfDay(start + i * 4 * HOUR);
        const int to = hourOfDay(start + (i + 1) * 4 * HOUR);
        // GenSenRounded-R.ttf does not include U+2013 (en dash).
        snprintf(view.windowLabel[i], sizeof(view.windowLabel[i]), "%02d-%02d時", from, to == 0 ? 24 : to);
    }
    if (!cache.hasData || now < cache.updatedAt || now - cache.updatedAt > WEATHER_STALE_SEC ||
        cache.dayCount > WEATHER_DAYS || cache.hourCount > WEATHER_HOURS) return view;
    for (size_t i = 0; i < cache.dayCount; ++i) {
        const WeatherDay& day = cache.days[i];
        if (day.date != targetDay) continue;
        view.icon = weatherIconForCode(day.code);
        view.hasMaxTemp = day.hasMaxTemp;
        view.hasMinTemp = day.hasMinTemp;
        if (day.hasMaxTemp) view.maxTemp = static_cast<int>(std::lround(day.maxTemp));
        if (day.hasMinTemp) view.minTemp = static_cast<int>(std::lround(day.minTemp));
        view.probability = day.probability;
    }
    for (int i = 0; i < 2; ++i) view.windowProbability[i] = windowMaximum(cache, start + i * 4 * HOUR);
    return view;
}
