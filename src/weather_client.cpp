#include "weather_client.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cmath>

// ESP-IDF's bundled root certificates are linked from libmbedtls.
extern const uint8_t rootBundle[] asm("_binary_x509_crt_bundle_start");

bool weatherConfigured() {
#if defined(WEATHER_LATITUDE) && defined(WEATHER_LONGITUDE)
    return std::isfinite(WEATHER_LATITUDE) && std::isfinite(WEATHER_LONGITUDE) &&
           WEATHER_LATITUDE >= -90 && WEATHER_LATITUDE <= 90 &&
           WEATHER_LONGITUDE >= -180 && WEATHER_LONGITUDE <= 180;
#else
    return false;
#endif
}

void weatherCheckLocation(WeatherCache& cache) {
#if defined(WEATHER_LATITUDE) && defined(WEATHER_LONGITUDE)
    if (!weatherConfigured() || cache.latitude != WEATHER_LATITUDE || cache.longitude != WEATHER_LONGITUDE) {
        cache = {};
        cache.latitude = WEATHER_LATITUDE;
        cache.longitude = WEATHER_LONGITUDE;
    }
#else
    cache = {};
#endif
}

static bool getForecast(const String& url, String& body) {
    WiFiClientSecure client;
    client.setCACertBundle(rootBundle);
    client.setHandshakeTimeout(10);
    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(10000);
    http.useHTTP10(true);
    if (!http.begin(client, url)) return false;
    const int status = http.GET();
    bool ok = status == HTTP_CODE_OK && http.getSize() <= 16384;
    if (ok) {
        body = http.getString();
        ok = !body.isEmpty() && body.length() <= 16384 &&
             (http.getSize() < 0 || body.length() == static_cast<size_t>(http.getSize()));
    }
    Serial.printf("[Weather] HTTP %d, bytes=%u, success=%d\n", status, body.length(), ok);
    http.end();
    return ok;
}

bool weatherFetch(WeatherCache& cache) {
#if defined(WEATHER_LATITUDE) && defined(WEATHER_LONGITUDE)
    if (!weatherConfigured() || !weatherTimeValid(time(nullptr))) return false;
    const String base = String("https://api.open-meteo.com/v1/forecast?latitude=") +
        String(static_cast<double>(WEATHER_LATITUDE), 6) + "&longitude=" +
        String(static_cast<double>(WEATHER_LONGITUDE), 6) +
        "&timezone=Asia%2FTokyo&forecast_days=2";
    String jma;
    String rain;
    if (!getForecast(base + "&models=jma_seamless&daily=weather_code,temperature_2m_max,temperature_2m_min", jma) ||
        !getForecast(base + "&models=best_match&daily=precipitation_probability_max&hourly=precipitation_probability", rain)) return false;
    const bool ok = weatherUpdateFromJson(cache, jma.c_str(), rain.c_str(), time(nullptr));
    Serial.printf("[Weather] Forecast %s (cache=%u bytes)\n", ok ? "updated" : "invalid; keeping cache", sizeof(cache));
    return ok;
#else
    return false;
#endif
}
