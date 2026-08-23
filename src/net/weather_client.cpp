/**
 * @file weather_client.cpp
 * @brief Open-Meteo background weather fetcher and JSON parser implementation.
 */

#include "src/net/weather_client.h"
#include "src/net/wifi_manager.h"
#include "include/kore_config.h"
#include "include/kore_types.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino.h>

static TaskHandle_t s_weather_task_handle = NULL;

static void mapWmoCodeToCondition(int code, char* out_str, size_t max_len) {
    const char* cond = "UNKNOWN";
    if (code == 0) {
        cond = "CLEAR";
    } else if (code == 1) {
        cond = "MAINLY CLEAR";
    } else if (code == 2) {
        cond = "PARTLY CLOUDY";
    } else if (code == 3) {
        cond = "OVERCAST";
    } else if (code == 45 || code == 48) {
        cond = "FOG";
    } else if (code >= 51 && code <= 55) {
        cond = "DRIZZLE";
    } else if (code >= 56 && code <= 57) {
        cond = "FREEZING DRIZZLE";
    } else if (code >= 61 && code <= 65) {
        cond = "RAIN";
    } else if (code >= 66 && code <= 67) {
        cond = "FREEZING RAIN";
    } else if (code >= 71 && code <= 77) {
        cond = "SNOW";
    } else if (code >= 80 && code <= 82) {
        cond = "SHOWERS";
    } else if (code >= 85 && code <= 86) {
        cond = "SNOW SHOWERS";
    } else if (code >= 95 && code <= 99) {
        cond = "THUNDERSTORM";
    } else {
        cond = "CLOUDY";
    }
    strncpy(out_str, cond, max_len - 1);
    out_str[max_len - 1] = '\0';
}

static bool parseOpenMeteoJson(const String& payload, float* out_temp, int* out_humidity, int* out_code) {
    int cur_idx = payload.indexOf("\"current\":");
    if (cur_idx < 0) cur_idx = 0;
    
    int temp_idx = payload.indexOf("\"temperature_2m\":", cur_idx);
    if (temp_idx < 0) return false;
    temp_idx += 17;
    int temp_end = payload.indexOf(",", temp_idx);
    if (temp_end < 0) temp_end = payload.indexOf("}", temp_idx);
    if (temp_end < 0) return false;
    *out_temp = payload.substring(temp_idx, temp_end).toFloat();

    int hum_idx = payload.indexOf("\"relative_humidity_2m\":", cur_idx);
    if (hum_idx < 0) return false;
    hum_idx += 23;
    int hum_end = payload.indexOf(",", hum_idx);
    if (hum_end < 0) hum_end = payload.indexOf("}", hum_idx);
    if (hum_end < 0) return false;
    *out_humidity = payload.substring(hum_idx, hum_end).toInt();

    int code_idx = payload.indexOf("\"weather_code\":", cur_idx);
    if (code_idx < 0) return false;
    code_idx += 15;
    int code_end = payload.indexOf(",", code_idx);
    if (code_end < 0) code_end = payload.indexOf("}", code_idx);
    if (code_end < 0) return false;
    *out_code = payload.substring(code_idx, code_end).toInt();

    return true;
}

bool fetchWeatherSync(const char* city, float lat, float lon) {
    if (WiFi.status() != WL_CONNECTED || isWiFiAPMode()) {
        return false;
    }

    HTTPClient http;
    char url[256];
    snprintf(url, sizeof(url),
        "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,weather_code",
        lat, lon
    );

    http.setTimeout(4000);
    if (!http.begin(url)) {
        return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    float temp = 0.0f;
    int humidity = 0;
    int code = 0;

    if (!parseOpenMeteoJson(payload, &temp, &humidity, &code)) {
        return false;
    }

    portENTER_CRITICAL(&g_weather_mutex);
    g_weather_info.temperature = temp;
    g_weather_info.humidity = humidity;
    g_weather_info.weather_code = code;
    strncpy(g_weather_info.city, city ? city : WEATHER_DEFAULT_CITY, sizeof(g_weather_info.city) - 1);
    g_weather_info.city[sizeof(g_weather_info.city) - 1] = '\0';
    mapWmoCodeToCondition(code, g_weather_info.condition, sizeof(g_weather_info.condition));
    g_weather_info.valid = true;
    g_weather_info.last_sync_ms = millis();
    portEXIT_CRITICAL(&g_weather_mutex);

    return true;
}

static void weatherTask(void* pvParameters) {
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(4000)); /* Wait for initial network settling */

    while (true) {
        if (isWeatherEnabled() && WiFi.status() == WL_CONNECTED && !isWiFiAPMode()) {
            const char* city = getWeatherCity();
            float lat = getWeatherLat();
            float lon = getWeatherLon();
            fetchWeatherSync(city, lat, lon);
        }

        /* Wait for periodic interval (30 min) or immediate task notification */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WEATHER_FETCH_INTERVAL_MS));
    }
}

void triggerWeatherFetch(void) {
    if (s_weather_task_handle != NULL) {
        xTaskNotifyGive(s_weather_task_handle);
    }
}

void initWeatherClient(void) {
    if (s_weather_task_handle == NULL) {
        xTaskCreatePinnedToCore(
            weatherTask,
            "Weather_Task",
            8192,
            NULL,
            1,
            &s_weather_task_handle,
            0 /* Core 0 */
        );
    }
}
