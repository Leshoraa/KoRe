/**
 * @file wifi_manager.h
 * @brief NVS Preferences Wi-Fi configuration, STA/AP management, and DNS captive portal.
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "include/kore_config.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool initWiFiAndNetwork(void);
bool isWiFiAPMode(void);
const char* getWiFiStaSSID(void);
const char* getWiFiStaPass(void);
const char* getWiFiApSSID(void);
const char* getWiFiApPass(void);
void saveWiFiCredentials(const char* sta_s, const char* sta_p, const char* ap_s, const char* ap_p);
void switchWiFiMode(const char* target_mode);
void scheduleSystemRestart(uint32_t delay_ms);

uint8_t getSavedOledBrightness(void);
void saveOledBrightness(uint8_t brightness);
const char* getWeatherCity(void);
float getWeatherLat(void);
float getWeatherLon(void);
bool isWeatherEnabled(void);
void saveWeatherConfig(const char* city, float lat, float lon, bool enabled);
int32_t getTimezoneOffsetSec(void);
void saveTimezoneOffsetSec(int32_t offset_sec);
void applyTimezoneConfig(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MANAGER_H */
