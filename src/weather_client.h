#pragma once

#include "weather.h"

bool weatherConfigured();
void weatherCheckLocation(WeatherCache& cache);
// Requires connected Wi-Fi and a valid system clock for TLS verification.
bool weatherFetch(WeatherCache& cache);
