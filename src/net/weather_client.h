/**
 * @file weather_client.h
 * @brief Open-Meteo background weather fetcher and JSON parser declarations.
 */

#ifndef WEATHER_CLIENT_H
#define WEATHER_CLIENT_H

#include "include/kore_config.h"
#include "include/kore_types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void initWeatherClient(void);
void triggerWeatherFetch(void);
bool fetchWeatherSync(const char* city, float lat, float lon);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_CLIENT_H */
