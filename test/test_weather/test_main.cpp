#include <unity.h>
#include <ArduinoJson.h>
#include "weather.h"
#include <cstdlib>
#include <cstring>
#include <string>

void setUp() {}
void tearDown() {}

static time_t epoch(const char* date, int hour = 0, int minute = 0) {
    struct tm t = {};
    int year, month, day;
    sscanf(date, "%d-%d-%d", &year, &month, &day);
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    return mktime(&t) - 9 * 3600;  // Test process TZ is UTC.
}

struct ForecastFixture {
    JsonDocument jma;
    JsonDocument rain;
    explicit ForecastFixture(const char* date = "2026-10-18") {
        jma["utc_offset_seconds"] = rain["utc_offset_seconds"] = 32400;
        const time_t start = epoch(date) + 9 * 3600;
        for (int i = 0; i < 2; ++i) {
            const time_t day = start + i * 86400;
            struct tm t;
            gmtime_r(&day, &t);
            char text[20];
            strftime(text, sizeof(text), "%Y-%m-%d", &t);
            jma["daily"]["time"][i] = text;
            rain["daily"]["time"][i] = text;
            jma["daily"]["weather_code"][i] = i == 0 ? 61 : 3;
            jma["daily"]["temperature_2m_max"][i] = i == 0 ? 24.4 : 22.5;
            jma["daily"]["temperature_2m_min"][i] = i == 0 ? -15.5 : -10.4;
            rain["daily"]["precipitation_probability_max"][i] = i == 0 ? 70 : 100;
        }
        for (int i = 0; i < 48; ++i) {
            const time_t end = start + i * 3600;
            struct tm t;
            gmtime_r(&end, &t);
            char text[20];
            strftime(text, sizeof(text), "%Y-%m-%dT%H:%M", &t);
            rain["hourly"]["time"][i] = text;
            rain["hourly"]["precipitation_probability"][i] = i;
        }
    }
    bool update(WeatherCache& cache, time_t now) {
        std::string first, second;
        serializeJson(jma, first);
        serializeJson(rain, second);
        return weatherUpdateFromJson(cache, first.c_str(), second.c_str(), now);
    }
};

static void switches_daily_forecast_at_21() {
    ForecastFixture fixture;
    WeatherCache cache = {};
    TEST_ASSERT_TRUE(fixture.update(cache, epoch("2026-10-18", 20, 59)));
    auto today = weatherForDisplay(cache, epoch("2026-10-18", 20, 59));
    TEST_ASSERT_FALSE(today.tomorrow);
    TEST_ASSERT_EQUAL(24, today.maxTemp);
    TEST_ASSERT_EQUAL(-16, today.minTemp);
    TEST_ASSERT_EQUAL(70, today.probability);
    TEST_ASSERT_EQUAL(static_cast<int>(WeatherIcon::Rain), static_cast<int>(today.icon));
    auto tomorrow = weatherForDisplay(cache, epoch("2026-10-18", 21));
    TEST_ASSERT_TRUE(tomorrow.tomorrow);
    TEST_ASSERT_EQUAL(23, tomorrow.maxTemp);
    TEST_ASSERT_EQUAL(-10, tomorrow.minTemp);
    TEST_ASSERT_EQUAL(100, tomorrow.probability);
    TEST_ASSERT_EQUAL(static_cast<int>(WeatherIcon::Cloud), static_cast<int>(tomorrow.icon));
    TEST_ASSERT_EQUAL_STRING("21-01時", tomorrow.windowLabel[0]);
    TEST_ASSERT_EQUAL_STRING("01-05時", tomorrow.windowLabel[1]);
    TEST_ASSERT_EQUAL(25, tomorrow.windowProbability[0]);
    TEST_ASSERT_EQUAL(29, tomorrow.windowProbability[1]);
}

static void midnight_keeps_forecast_date_and_updates_labels() {
    ForecastFixture fixture;
    WeatherCache cache = {};
    TEST_ASSERT_TRUE(fixture.update(cache, epoch("2026-10-18", 23)));
    const auto before = weatherForDisplay(cache, epoch("2026-10-18", 23, 59));
    const auto after = weatherForDisplay(cache, epoch("2026-10-19"));
    TEST_ASSERT_TRUE(before.tomorrow);
    TEST_ASSERT_FALSE(after.tomorrow);
    TEST_ASSERT_EQUAL(before.maxTemp, after.maxTemp);
    TEST_ASSERT_EQUAL(before.probability, after.probability);
    TEST_ASSERT_EQUAL_STRING("00-04時", after.windowLabel[0]);
    TEST_ASSERT_EQUAL_STRING("04-08時", after.windowLabel[1]);
    TEST_ASSERT_EQUAL(28, after.windowProbability[0]);
    TEST_ASSERT_EQUAL(32, after.windowProbability[1]);
}

static void month_year_and_leap_day_boundaries() {
    for (const auto date : {"2026-10-31", "2026-12-31", "2028-02-28", "2028-02-29"}) {
        ForecastFixture fixture(date);
        WeatherCache cache = {};
        const time_t now = epoch(date, 21);
        TEST_ASSERT_TRUE(fixture.update(cache, now));
        const auto before = weatherForDisplay(cache, now);
        const auto after = weatherForDisplay(cache, now + 3 * 3600);
        TEST_ASSERT_EQUAL(100, before.probability);
        TEST_ASSERT_EQUAL(before.probability, after.probability);
        TEST_ASSERT_FALSE(after.tomorrow);
    }
}

static void windows_use_four_preceding_hour_values_without_overlap() {
    ForecastFixture fixture;
    // The current-hour value and the next-window value must not leak into the first window.
    fixture.rain["hourly"]["precipitation_probability"][7] = 100;
    fixture.rain["hourly"]["precipitation_probability"][12] = 99;
    WeatherCache cache = {};
    TEST_ASSERT_TRUE(fixture.update(cache, epoch("2026-10-18", 7)));
    const auto view = weatherForDisplay(cache, epoch("2026-10-18", 7, 30));
    TEST_ASSERT_EQUAL_STRING("07-11時", view.windowLabel[0]);
    TEST_ASSERT_EQUAL_STRING("11-15時", view.windowLabel[1]);
    TEST_ASSERT_EQUAL(11, view.windowProbability[0]);
    TEST_ASSERT_EQUAL(99, view.windowProbability[1]);
}

static void labels_omit_next_day_and_keep_24_at_midnight() {
    WeatherCache cache = {};
    auto view = weatherForDisplay(cache, epoch("2026-10-18", 20));
    TEST_ASSERT_EQUAL_STRING("20-24時", view.windowLabel[0]);
    TEST_ASSERT_EQUAL_STRING("00-04時", view.windowLabel[1]);
    view = weatherForDisplay(cache, epoch("2026-10-18", 23, 30));
    TEST_ASSERT_EQUAL_STRING("23-03時", view.windowLabel[0]);
    TEST_ASSERT_EQUAL_STRING("03-07時", view.windowLabel[1]);
    TEST_ASSERT_NULL(strstr(view.windowLabel[0], "翌"));
    TEST_ASSERT_NULL(strstr(view.windowLabel[1], "翌"));
}

static void nulls_are_missing_not_zero() {
    ForecastFixture fixture;
    fixture.jma["daily"]["temperature_2m_max"][0] = nullptr;
    fixture.jma["daily"]["weather_code"][0] = nullptr;
    fixture.rain["daily"]["precipitation_probability_max"][0] = nullptr;
    fixture.rain["hourly"]["precipitation_probability"][10] = nullptr;
    WeatherCache cache = {};
    TEST_ASSERT_TRUE(fixture.update(cache, epoch("2026-10-18", 7)));
    const auto view = weatherForDisplay(cache, epoch("2026-10-18", 7));
    TEST_ASSERT_FALSE(view.hasMaxTemp);
    TEST_ASSERT_TRUE(view.hasMinTemp);
    TEST_ASSERT_EQUAL(-1, view.probability);
    TEST_ASSERT_EQUAL(-1, view.windowProbability[0]);
    TEST_ASSERT_EQUAL(15, view.windowProbability[1]);
    TEST_ASSERT_EQUAL(static_cast<int>(WeatherIcon::Unknown), static_cast<int>(view.icon));
}

static void missing_hour_makes_only_its_window_unavailable() {
    ForecastFixture fixture;
    fixture.rain["hourly"]["time"].as<JsonArray>().remove(24);
    fixture.rain["hourly"]["precipitation_probability"].as<JsonArray>().remove(24);
    WeatherCache cache = {};
    TEST_ASSERT_TRUE(fixture.update(cache, epoch("2026-10-18", 21)));
    const auto view = weatherForDisplay(cache, epoch("2026-10-18", 21));
    TEST_ASSERT_EQUAL(-1, view.windowProbability[0]);
    TEST_ASSERT_EQUAL(29, view.windowProbability[1]);
}

static void zero_is_valid_and_invalid_values_stay_missing() {
    ForecastFixture fixture;
    fixture.jma["daily"]["temperature_2m_max"][0] = 0;
    fixture.jma["daily"]["temperature_2m_min"][0] = "0";
    fixture.jma["daily"]["weather_code"][0] = 65536;
    fixture.rain["daily"]["precipitation_probability_max"][0] = 0;
    for (int i = 8; i <= 11; ++i) fixture.rain["hourly"]["precipitation_probability"][i] = 0;
    fixture.rain["hourly"]["precipitation_probability"][12] = 101;
    WeatherCache cache = {};
    TEST_ASSERT_TRUE(fixture.update(cache, epoch("2026-10-18", 7)));
    const auto view = weatherForDisplay(cache, epoch("2026-10-18", 7));
    TEST_ASSERT_TRUE(view.hasMaxTemp);
    TEST_ASSERT_EQUAL(0, view.maxTemp);
    TEST_ASSERT_FALSE(view.hasMinTemp);
    TEST_ASSERT_EQUAL(0, view.probability);
    TEST_ASSERT_EQUAL(0, view.windowProbability[0]);
    TEST_ASSERT_EQUAL(-1, view.windowProbability[1]);
    TEST_ASSERT_EQUAL(static_cast<int>(WeatherIcon::Unknown), static_cast<int>(view.icon));
}

static void failed_updates_preserve_cache_and_timestamp() {
    ForecastFixture fixture;
    WeatherCache cache = {};
    TEST_ASSERT_TRUE(fixture.update(cache, epoch("2026-10-18", 7)));
    const WeatherCache previous = cache;
    std::string jma, rain;
    serializeJson(fixture.jma, jma);
    serializeJson(fixture.rain, rain);
    const time_t later = epoch("2026-10-18", 8);
    TEST_ASSERT_FALSE(weatherUpdateFromJson(cache, jma.c_str(), nullptr, later));
    TEST_ASSERT_FALSE(weatherUpdateFromJson(cache, nullptr, rain.c_str(), later));
    TEST_ASSERT_FALSE(weatherUpdateFromJson(cache, jma.c_str(), "{broken", later));
    TEST_ASSERT_FALSE(weatherUpdateFromJson(cache, "{broken", rain.c_str(), later));
    TEST_ASSERT_FALSE(weatherUpdateFromJson(cache, jma.c_str(), "{\"error\":true}", later));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &cache, sizeof(cache));
    TEST_ASSERT_TRUE(weatherNeedsRefresh(cache, later));
}

static void structural_errors_do_not_commit() {
    ForecastFixture fixture;
    WeatherCache cache = {};
    TEST_ASSERT_TRUE(fixture.update(cache, epoch("2026-10-18", 7)));
    const WeatherCache previous = cache;
    fixture.rain["daily"]["time"][0] = "2026-10-19";
    TEST_ASSERT_FALSE(fixture.update(cache, epoch("2026-10-18", 8)));
    fixture.rain["daily"]["time"][0] = "2026-10-18";
    fixture.rain["hourly"]["time"][10] = "2026-10-18T09:00";
    TEST_ASSERT_FALSE(fixture.update(cache, epoch("2026-10-18", 8)));
    fixture.rain["hourly"]["time"][10] = "2026-10-18T10:00";
    fixture.jma["daily"]["temperature_2m_min"].as<JsonArray>().remove(1);
    TEST_ASSERT_FALSE(fixture.update(cache, epoch("2026-10-18", 8)));
    TEST_ASSERT_EQUAL_MEMORY(&previous, &cache, sizeof(cache));
}

static void expired_and_future_cache_is_hidden_but_timestamp_retained() {
    ForecastFixture fixture;
    WeatherCache cache = {};
    const time_t updated = epoch("2026-10-18", 7);
    TEST_ASSERT_TRUE(fixture.update(cache, updated));
    TEST_ASSERT_EQUAL(70, weatherForDisplay(cache, updated + WEATHER_STALE_SEC).probability);
    for (const time_t now : {updated + WEATHER_STALE_SEC + 1, updated - 1}) {
        const auto view = weatherForDisplay(cache, now);
        TEST_ASSERT_FALSE(view.hasMaxTemp);
        TEST_ASSERT_EQUAL(-1, view.probability);
        TEST_ASSERT_EQUAL(-1, view.windowProbability[0]);
        TEST_ASSERT_EQUAL(updated, view.updatedAt);
    }
}

static void absent_target_day_is_not_replaced_with_another_day() {
    ForecastFixture fixture;
    WeatherCache cache = {};
    TEST_ASSERT_TRUE(fixture.update(cache, epoch("2026-10-18", 20)));
    cache.dayCount = 1;
    const auto view = weatherForDisplay(cache, epoch("2026-10-18", 21));
    TEST_ASSERT_TRUE(view.tomorrow);
    TEST_ASSERT_FALSE(view.hasMaxTemp);
    TEST_ASSERT_EQUAL(-1, view.probability);
    TEST_ASSERT_EQUAL(25, view.windowProbability[0]);
}

static void refresh_is_hourly_and_unset_clock_is_not_used() {
    ForecastFixture fixture;
    WeatherCache cache = {};
    const time_t now = epoch("2026-10-18", 7);
    TEST_ASSERT_TRUE(weatherNeedsRefresh(cache, now));
    TEST_ASSERT_FALSE(weatherNeedsRefresh(cache, 0));
    TEST_ASSERT_TRUE(fixture.update(cache, now));
    TEST_ASSERT_FALSE(weatherNeedsRefresh(cache, now + 3599));
    TEST_ASSERT_TRUE(weatherNeedsRefresh(cache, now + 3600));
    TEST_ASSERT_TRUE(weatherNeedsRefresh(cache, now - 1));
    const auto unknown = weatherForDisplay(cache, 0);
    TEST_ASSERT_EQUAL_STRING("--:--時", unknown.windowLabel[0]);
    TEST_ASSERT_EQUAL(-1, unknown.probability);
}

static void icon_mapping_covers_weather_families_and_unknown() {
    const int codes[] = {0, 2, 3, 48, 67, 86, 99, 999};
    const WeatherIcon icons[] = {WeatherIcon::Sun, WeatherIcon::PartlyCloudy, WeatherIcon::Cloud,
        WeatherIcon::Fog, WeatherIcon::Rain, WeatherIcon::Snow, WeatherIcon::Thunder, WeatherIcon::Unknown};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        TEST_ASSERT_EQUAL(static_cast<int>(icons[i]), static_cast<int>(weatherIconForCode(codes[i])));
    }
}

int main() {
    setenv("TZ", "UTC", 1);
    tzset();
    UNITY_BEGIN();
    RUN_TEST(switches_daily_forecast_at_21);
    RUN_TEST(midnight_keeps_forecast_date_and_updates_labels);
    RUN_TEST(month_year_and_leap_day_boundaries);
    RUN_TEST(windows_use_four_preceding_hour_values_without_overlap);
    RUN_TEST(labels_omit_next_day_and_keep_24_at_midnight);
    RUN_TEST(nulls_are_missing_not_zero);
    RUN_TEST(missing_hour_makes_only_its_window_unavailable);
    RUN_TEST(zero_is_valid_and_invalid_values_stay_missing);
    RUN_TEST(failed_updates_preserve_cache_and_timestamp);
    RUN_TEST(structural_errors_do_not_commit);
    RUN_TEST(expired_and_future_cache_is_hidden_but_timestamp_retained);
    RUN_TEST(absent_target_day_is_not_replaced_with_another_day);
    RUN_TEST(refresh_is_hourly_and_unset_clock_is_not_used);
    RUN_TEST(icon_mapping_covers_weather_families_and_unknown);
    return UNITY_END();
}
