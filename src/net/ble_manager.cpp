/**
 * @file ble_manager.cpp
 * @brief Bluetooth Low Energy (BLE) Nordic UART Service (NUS) GATT server implementation.
 */

#include "src/net/ble_manager.h"
#include "src/net/notification_client.h"
#include "src/core/display_engine.h"
#include "src/net/wifi_manager.h"
#include "src/net/weather_client.h"
#include "include/kore_affective.h"
#include "include/kore_config.h"
#include "include/kore_types.h"
#include "include/kore_ai.h"
#include "include/kore_personality.h"

#include <Arduino.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

static BLEServer *s_pServer = nullptr;
static BLECharacteristic *s_pRxCharacteristic = nullptr;
static BLECharacteristic *s_pTxCharacteristic = nullptr;
static volatile bool s_device_connected = false;
static volatile bool s_ble_telemetry_streaming = false;
static volatile uint32_t s_ble_telemetry_interval_ms = 500;
static TaskHandle_t s_ble_telemetry_task_handle = NULL;

void formatTelemetryJson(char *json, size_t max_len) {
    TrackTarget target;
    int num_cands = 0;
    int insp_idx = 0;
    ObjectCandidate cands[3];

    portENTER_CRITICAL(&g_target_mutex);
    target = g_current_target;
    num_cands = g_num_candidates;
    insp_idx = g_inspected_candidate_idx;
    for (int i = 0; i < 3; i++) cands[i] = g_object_candidates[i];
    portEXIT_CRITICAL(&g_target_mutex);

    BrainTelemetry brain = getBrainTelemetry();
    PersonalityTraits traits = getPersonalityTraits();
    CircadianState circa = getCircadianState();

    bool is_cam_sleeping = (g_recon_state == STATE_SLEEP_RECON || !g_camera_init_ok);
    float current_fps = is_cam_sleeping ? 0.0f : g_fps_ai;
    bool is_detected = is_cam_sleeping ? false : target.detected;

    snprintf(json, max_len,
        "{\"type\":\"telemetry\",\"detected\":%s,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"cx\":%d,\"cy\":%d,\"err_x\":%.1f,\"err_y\":%.1f,\"conf\":%.2f,\"human_likelihood\":%.2f,\"fps_ai\":%.1f,\"fw\":%d,\"fh\":%d,\"vx\":%.1f,\"vy\":%.1f,\"prox\":%.2f,\"num_cands\":%d,\"insp_idx\":%d,\"c0_cx\":%d,\"c0_cy\":%d,\"c0_w\":%d,\"c0_h\":%d,\"c0_p\":%.1f,\"c1_cx\":%d,\"c1_cy\":%d,\"c1_w\":%d,\"c1_h\":%d,\"c1_p\":%.1f,\"c2_cx\":%d,\"c2_cy\":%d,\"c2_w\":%d,\"c2_h\":%d,\"c2_p\":%.1f,\"expr\":%d,\"expr_name\":\"%s\",\"is_manual\":%s,\"valence\":%.2f,\"arousal\":%.2f,\"curiosity\":%.2f,\"social\":%.2f,\"boredom\":%.2f,\"fatigue\":%.2f,\"mischief\":%.2f,\"thought\":\"%s\",\"interact_s\":%u,\"solitude_s\":%u,\"bonding\":%.2f,\"life_s\":%u,\"mem_count\":%u,\"mem_res\":%.2f,\"mem_expr\":%d,\"heap_free\":%u,\"psram_free\":%u,\"uptime_s\":%lu,\"cpu_mhz\":%d,\"cam_sleep\":%s,\"cam_online\":%s,\"personality\":{\"boldness\":%.2f,\"volatility\":%.2f,\"playfulness\":%.2f,\"attachment\":%.2f},\"circadian\":{\"energy\":%.2f,\"mood_offset\":%.2f,\"phase_pct\":%.1f}}",
        is_detected ? "true" : "false",
        target.x, target.y, target.w, target.h,
        target.cx, target.cy,
        target.error_x, target.error_y,
        is_cam_sleeping ? 0.0f : target.confidence,
        is_cam_sleeping ? 0.0f : target.human_likelihood,
        current_fps,
        640, 480,
        target.vx, target.vy,
        target.proximity,
        num_cands, insp_idx,
        cands[0].cx, cands[0].cy, cands[0].w, cands[0].h, cands[0].priority_score,
        cands[1].cx, cands[1].cy, cands[1].w, cands[1].h, cands[1].priority_score,
        cands[2].cx, cands[2].cy, cands[2].w, cands[2].h, cands[2].priority_score,
        (int)g_currentExpr,
        getExpressionName(g_currentExpr),
        isManualExpressionActive() ? "true" : "false",
        getEmotionValence(), getEmotionArousal(),
        brain.drives.curiosity, brain.drives.social, brain.drives.boredom, brain.drives.fatigue, brain.drives.mischief,
        brain.thought_summary,
        brain.interaction_sec, brain.solitude_sec,
        brain.bonding_level, brain.lifetime_sec,
        (unsigned)brain.memory_count, brain.memory_resonance, (int)brain.last_recalled_expr,
        (unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned long)(millis() / 1000),
        ESP.getCpuFreqMHz(),
        is_cam_sleeping ? "true" : "false",
        g_camera_init_ok ? "true" : "false",
        traits.boldness, traits.volatility, traits.playfulness, traits.attachment,
        circa.energy_level, circa.mood_baseline, circa.phase_pct
    );
}

static void sendBleData(const char* data, size_t len) {
    if (!s_pTxCharacteristic || !s_device_connected || len == 0) return;

    const size_t CHUNK_SIZE = 180;
    size_t offset = 0;
    while (offset < len && s_device_connected) {
        size_t to_send = len - offset;
        if (to_send > CHUNK_SIZE) to_send = CHUNK_SIZE;
        s_pTxCharacteristic->setValue((uint8_t*)(data + offset), to_send);
        s_pTxCharacteristic->notify();
        offset += to_send;
        if (offset < len) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static portMUX_TYPE s_ble_telem_mux = portMUX_INITIALIZER_UNLOCKED;
static char s_ble_telemetry_buf[1400];

void sendBleTelemetryNow(void) {
    if (!s_pTxCharacteristic || !s_device_connected) return;
    portENTER_CRITICAL(&s_ble_telem_mux);
    formatTelemetryJson(s_ble_telemetry_buf, sizeof(s_ble_telemetry_buf));
    portEXIT_CRITICAL(&s_ble_telem_mux);
    sendBleData(s_ble_telemetry_buf, strlen(s_ble_telemetry_buf));
}

void setBleTelemetryStreaming(bool enable, uint32_t interval_ms) {
    s_ble_telemetry_streaming = enable;
    if (interval_ms >= 100) {
        s_ble_telemetry_interval_ms = interval_ms;
    }
    KORE_LOG_INF("BLE", "BLE telemetry streaming %s (interval=%ums)",
                 enable ? "ENABLED" : "DISABLED", (unsigned)s_ble_telemetry_interval_ms);
}

bool isBleTelemetryStreaming(void) {
    return s_ble_telemetry_streaming && s_device_connected;
}

static void bleTelemetryStreamTask(void *pvParameters) {
    (void)pvParameters;
    while (true) {
        if (s_device_connected && s_ble_telemetry_streaming) {
            sendBleTelemetryNow();
            uint32_t wait_time = s_ble_telemetry_interval_ms;
            if (wait_time < 100) wait_time = 100;
            vTaskDelay(pdMS_TO_TICKS(wait_time));
        } else {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

static bool extractJsonField(const String& json, const char* key, char* out, size_t max_len) {
    // 1. Try quoted string: "key":"value"
    String pattern = String("\"") + key + "\":\"";
    int start = json.indexOf(pattern);
    if (start >= 0) {
        start += pattern.length();
        int end = json.indexOf("\"", start);
        if (end > start) {
            String val = json.substring(start, end);
            strncpy(out, val.c_str(), max_len - 1);
            out[max_len - 1] = '\0';
            return true;
        }
    }

    // 2. Try unquoted literal or number: "key": 123 or "key":true
    pattern = String("\"") + key + "\":";
    start = json.indexOf(pattern);
    if (start >= 0) {
        start += pattern.length();
        while (start < (int)json.length() && (json[start] == ' ' || json[start] == '\t')) {
            start++;
        }
        if (start < (int)json.length() && json[start] == '\"') {
            start++;
            int end = json.indexOf("\"", start);
            if (end > start) {
                String val = json.substring(start, end);
                strncpy(out, val.c_str(), max_len - 1);
                out[max_len - 1] = '\0';
                return true;
            }
        } else {
            int end = start;
            while (end < (int)json.length() && json[end] != ',' && json[end] != '}' && 
                   json[end] != '\r' && json[end] != '\n' && json[end] != ' ' && json[end] != '\"') {
                end++;
            }
            if (end > start) {
                String val = json.substring(start, end);
                val.trim();
                strncpy(out, val.c_str(), max_len - 1);
                out[max_len - 1] = '\0';
                return true;
            }
        }
    }
    return false;
}

static void processIncomingBleData(const String& raw_input) {
    String input = raw_input;
    input.trim();
    if (input.length() == 0) return;

    /* A. Check for Brightness adjustment command via JSON */
    if (input.startsWith("{") && input.endsWith("}")) {
        char cmd[32] = {0};
        char bright_str[32] = {0};
        char save_str[16] = {0};

        extractJsonField(input, "cmd", cmd, sizeof(cmd));
        if (cmd[0] == '\0') {
            extractJsonField(input, "type", cmd, sizeof(cmd));
        }

        bool is_brightness_cmd = (strcmp(cmd, "set_brightness") == 0 || strcmp(cmd, "brightness") == 0);
        bool has_brightness_field = extractJsonField(input, "brightness", bright_str, sizeof(bright_str));
        if (!has_brightness_field) {
            has_brightness_field = extractJsonField(input, "val", bright_str, sizeof(bright_str));
        }
        if (!has_brightness_field) {
            has_brightness_field = extractJsonField(input, "value", bright_str, sizeof(bright_str));
        }

        if (is_brightness_cmd || (has_brightness_field && cmd[0] != '\0' && strcmp(cmd, "notification") != 0)) {
            if (bright_str[0] != '\0') {
                int b = atoi(bright_str);
                b = constrain(b, 0, 255);
                setOledBrightnessLive((uint8_t)b);

                extractJsonField(input, "save", save_str, sizeof(save_str));
                bool should_save = (save_str[0] == '\0') || (strcmp(save_str, "true") == 0) || (strcmp(save_str, "1") == 0);
                if (should_save) {
                    saveOledBrightness((uint8_t)b);
                }

                KORE_LOG_INF("BLE", "OLED brightness updated via BLE: %d (save=%d)", b, should_save ? 1 : 0);

                if (s_pTxCharacteristic && s_device_connected) {
                    char resp[64];
                    snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"brightness\":%d}", b);
                    s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
                    s_pTxCharacteristic->notify();
                }
                return;
            }
        }

        /* B. Parse Navigation command payload: {"cmd":"nav", "active":true, "icon":"turn_right", "dist":"200 m", "inst":"Turn right", "street":"Main St"} */
        if (strcmp(cmd, "nav") == 0 || strcmp(cmd, "navigation") == 0) {
            char active_str[16] = {0};
            extractJsonField(input, "active", active_str, sizeof(active_str));
            bool is_active = (active_str[0] == '\0') || (strcmp(active_str, "true") == 0) || (strcmp(active_str, "1") == 0);

            if (!is_active) {
                KORE_LOG_INF("BLE", "Navigation deactivated via BLE");
                dismissNavigationDisplay();
                return;
            }

            char icon_str[32] = {0};
            char dist_str[16] = {0};
            char inst_str[32] = {0};
            char street_str[48] = {0};
            char eta_str[16] = {0};
            char dur_str[16] = {0};
            char tot_dist_str[16] = {0};

            extractJsonField(input, "icon", icon_str, sizeof(icon_str));
            extractJsonField(input, "dist", dist_str, sizeof(dist_str));
            if (dist_str[0] == '\0') extractJsonField(input, "distance", dist_str, sizeof(dist_str));
            extractJsonField(input, "inst", inst_str, sizeof(inst_str));
            if (inst_str[0] == '\0') extractJsonField(input, "instruction", inst_str, sizeof(inst_str));
            extractJsonField(input, "street", street_str, sizeof(street_str));
            extractJsonField(input, "eta", eta_str, sizeof(eta_str));
            extractJsonField(input, "dur", dur_str, sizeof(dur_str));
            if (dur_str[0] == '\0') extractJsonField(input, "duration", dur_str, sizeof(dur_str));
            extractJsonField(input, "tot_dist", tot_dist_str, sizeof(tot_dist_str));
            if (tot_dist_str[0] == '\0') extractJsonField(input, "total_dist", tot_dist_str, sizeof(tot_dist_str));

            NavIconType icon_type = NAV_ICON_STRAIGHT;
            if (strcmp(icon_str, "turn_right") == 0 || strcmp(icon_str, "right") == 0) {
                icon_type = NAV_ICON_TURN_RIGHT;
            } else if (strcmp(icon_str, "turn_left") == 0 || strcmp(icon_str, "left") == 0) {
                icon_type = NAV_ICON_TURN_LEFT;
            } else if (strcmp(icon_str, "slight_right") == 0) {
                icon_type = NAV_ICON_SLIGHT_RIGHT;
            } else if (strcmp(icon_str, "slight_left") == 0) {
                icon_type = NAV_ICON_SLIGHT_LEFT;
            } else if (strcmp(icon_str, "sharp_right") == 0) {
                icon_type = NAV_ICON_SHARP_RIGHT;
            } else if (strcmp(icon_str, "sharp_left") == 0) {
                icon_type = NAV_ICON_SHARP_LEFT;
            } else if (strcmp(icon_str, "uturn") == 0 || strcmp(icon_str, "u_turn") == 0) {
                icon_type = NAV_ICON_UTURN;
            } else if (strcmp(icon_str, "roundabout") == 0) {
                icon_type = NAV_ICON_ROUNDABOUT;
            } else if (strcmp(icon_str, "arrive") == 0 || strcmp(icon_str, "destination") == 0) {
                icon_type = NAV_ICON_ARRIVE;
            }

            NavigationInfo nav;
            memset(&nav, 0, sizeof(nav));
            nav.icon = icon_type;
            strncpy(nav.distance, dist_str, sizeof(nav.distance) - 1);
            strncpy(nav.instruction, inst_str, sizeof(nav.instruction) - 1);
            strncpy(nav.street, street_str, sizeof(nav.street) - 1);
            strncpy(nav.eta, eta_str, sizeof(nav.eta) - 1);
            strncpy(nav.duration, dur_str, sizeof(nav.duration) - 1);
            strncpy(nav.total_dist, tot_dist_str, sizeof(nav.total_dist) - 1);
            nav.active = true;
            nav.valid = true;
            nav.updated_ms = millis();

            KORE_LOG_INF("BLE", "Navigation HUD updated: [%s] dist=%s inst=%s eta=%s dur=%s tot=%s", icon_str, nav.distance, nav.instruction, nav.eta, nav.duration, nav.total_dist);
            triggerNavigationDisplay(nav, 8000);
            return;
        }

        /* C. Parse Expression command payload: {"cmd":"set_expression", "expr":0} or {"cmd":"set_expression", "expr":"auto"} */
        if (strcmp(cmd, "set_expression") == 0 || strcmp(cmd, "expression") == 0 || strcmp(cmd, "set_expr") == 0) {
            char expr_str[16] = {0};
            bool has_expr = extractJsonField(input, "expr", expr_str, sizeof(expr_str));
            if (!has_expr) {
                has_expr = extractJsonField(input, "expression", expr_str, sizeof(expr_str));
            }
            if (!has_expr) {
                has_expr = extractJsonField(input, "val", expr_str, sizeof(expr_str));
            }

            if (has_expr && expr_str[0] != '\0') {
                if (strcmp(expr_str, "auto") == 0 || strcmp(expr_str, "-1") == 0) {
                    setManualExpression(-1);
                    KORE_LOG_INF("BLE", "Expression reset to autonomous Auto Mood via BLE");
                } else {
                    int code = atoi(expr_str);
                    if (code >= 0 && code <= 7) {
                        setManualExpression(code);
                        KORE_LOG_INF("BLE", "Expression set to %d (%s) via BLE", code, getExpressionName((Expression)code));
                    }
                }

                if (s_pTxCharacteristic && s_device_connected) {
                    char resp[64];
                    snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"expr\":\"%s\"}", expr_str);
                    s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
                    s_pTxCharacteristic->notify();
                }
                return;
            }
        }

        /* D. Query IP and WiFi status: {"cmd":"get_ip"} or {"cmd":"status"} */
        if (strcmp(cmd, "get_ip") == 0 || strcmp(cmd, "status") == 0 || strcmp(cmd, "info") == 0) {
            if (s_pTxCharacteristic && s_device_connected) {
                char resp[128];
                String current_ip = isWiFiAPMode() ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
                snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"ip\":\"%s\",\"mode\":\"%s\"}", current_ip.c_str(), isWiFiAPMode() ? "AP" : "STA");
                s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
                s_pTxCharacteristic->notify();
                KORE_LOG_INF("BLE", "Reported Wi-Fi IP %s via BLE", current_ip.c_str());
            }
            return;
        }

        /* E. Query full device configuration */
        if (strcmp(cmd, "get_config") == 0 || strcmp(cmd, "get_network") == 0 || strcmp(cmd, "get_device_config") == 0) {
            if (s_pTxCharacteristic && s_device_connected) {
                char resp[300];
                String current_ip = isWiFiAPMode() ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
                snprintf(resp, sizeof(resp),
                    "{\"status\":\"ok\",\"sta_ssid\":\"%s\",\"sta_pass\":\"%s\",\"ap_ssid\":\"%s\",\"ap_pass\":\"%s\",\"ble_name\":\"%s\",\"ip\":\"%s\",\"is_ap\":%s}",
                    getWiFiStaSSID(), getWiFiStaPass(), getWiFiApSSID(), getWiFiApPass(), getBleDeviceName(),
                    current_ip.c_str(), isWiFiAPMode() ? "true" : "false");
                s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
                s_pTxCharacteristic->notify();
                KORE_LOG_INF("BLE", "Reported full device config via BLE");
            }
            return;
        }

        /* F. Save all device configurations: Wi-Fi STA, AP Hotspot, and Bluetooth Name */
        if (strcmp(cmd, "save_device_config") == 0 || strcmp(cmd, "save_config") == 0) {
            char sta_s[64] = {0};
            char sta_p[64] = {0};
            char ap_s[64] = {0};
            char ap_p[64] = {0};
            char ble_n[64] = {0};

            extractJsonField(input, "sta_ssid", sta_s, sizeof(sta_s));
            extractJsonField(input, "sta_pass", sta_p, sizeof(sta_p));
            extractJsonField(input, "ap_ssid", ap_s, sizeof(ap_s));
            extractJsonField(input, "ap_pass", ap_p, sizeof(ap_p));
            extractJsonField(input, "ble_name", ble_n, sizeof(ble_n));

            if (s_pTxCharacteristic && s_device_connected) {
                char resp[128] = "{\"status\":\"ok\",\"message\":\"Config saved. ESP rebooting...\"}";
                s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
                s_pTxCharacteristic->notify();
            }

            saveAllDeviceConfig(sta_s, sta_p, ap_s, ap_p, ble_n);
            return;
        }

        /* G. Save WiFi only */
        if (strcmp(cmd, "save_wifi") == 0) {
            char sta_s[64] = {0};
            char sta_p[64] = {0};
            char ap_s[64] = {0};
            char ap_p[64] = {0};
            extractJsonField(input, "sta_ssid", sta_s, sizeof(sta_s));
            extractJsonField(input, "sta_pass", sta_p, sizeof(sta_p));
            extractJsonField(input, "ap_ssid", ap_s, sizeof(ap_s));
            extractJsonField(input, "ap_pass", ap_p, sizeof(ap_p));

            if (s_pTxCharacteristic && s_device_connected) {
                char resp[128] = "{\"status\":\"ok\",\"message\":\"WiFi saved. ESP rebooting...\"}";
                s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
                s_pTxCharacteristic->notify();
            }

            saveWiFiCredentials(sta_s, sta_p, ap_s, ap_p);
            return;
        }

        /* H. Save BLE only */
        if (strcmp(cmd, "save_ble") == 0) {
            char ble_n[64] = {0};
            extractJsonField(input, "ble_name", ble_n, sizeof(ble_n));
            if (ble_n[0] == '\0') extractJsonField(input, "name", ble_n, sizeof(ble_n));

            if (s_pTxCharacteristic && s_device_connected) {
                char resp[128] = "{\"status\":\"ok\",\"message\":\"BLE saved. ESP rebooting...\"}";
                s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
                s_pTxCharacteristic->notify();
            }

            saveBleConfig(ble_n);
            return;
        }

        /* I. Show Clock Glance on OLED: {"cmd":"show_clock"} or {"cmd":"clock"} */
        if (strcmp(cmd, "show_clock") == 0 || strcmp(cmd, "clock") == 0) {
            triggerClockDisplay(WEATHER_POPUP_DURATION_MS);
            KORE_LOG_INF("BLE", "Clock display triggered via BLE");

            if (s_pTxCharacteristic && s_device_connected) {
                char resp[64] = "{\"status\":\"ok\",\"message\":\"Clock triggered\"}";
                s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
                s_pTxCharacteristic->notify();
            }
            return;
        }

        /* J. Show Weather Glance on OLED: {"cmd":"show_weather"} or {"cmd":"weather"} */
        if (strcmp(cmd, "show_weather") == 0 || strcmp(cmd, "weather") == 0) {
            triggerWeatherDisplay(WEATHER_POPUP_DURATION_MS);
            KORE_LOG_INF("BLE", "Weather display triggered via BLE");

            if (s_pTxCharacteristic && s_device_connected) {
                char resp[64] = "{\"status\":\"ok\",\"message\":\"Weather triggered\"}";
                s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
                s_pTxCharacteristic->notify();
            }
            return;
        }

        /* K. Query Weather & Location configuration: {"cmd":"get_weather"} */
        if (strcmp(cmd, "get_weather") == 0 || strcmp(cmd, "get_weather_config") == 0) {
            if (s_pTxCharacteristic && s_device_connected) {
                char resp[300];
                portENTER_CRITICAL(&g_weather_mutex);
                snprintf(resp, sizeof(resp),
                    "{\"status\":\"ok\",\"city\":\"%s\",\"lat\":%.4f,\"lon\":%.4f,\"enabled\":%s,\"valid\":%s,"
                    "\"tz\":%ld,\"temp\":%.1f,\"humidity\":%d,\"code\":%d,\"condition\":\"%s\"}",
                    getWeatherCity(), getWeatherLat(), getWeatherLon(),
                    isWeatherEnabled() ? "true" : "false",
                    g_weather_info.valid ? "true" : "false",
                    (long)getTimezoneOffsetSec(),
                    g_weather_info.temperature,
                    g_weather_info.humidity,
                    g_weather_info.weather_code,
                    g_weather_info.condition
                );
                portEXIT_CRITICAL(&g_weather_mutex);
                s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
                s_pTxCharacteristic->notify();
                KORE_LOG_INF("BLE", "Reported weather config via BLE");
            }
            return;
        }

        /* L. Save Weather & Location configuration: {"cmd":"save_weather", "city":"Jakarta", "lat":-6.2088, "lon":106.8456, "enabled":true, "tz":25200} */
        if (strcmp(cmd, "save_weather") == 0 || strcmp(cmd, "set_weather") == 0) {
            char city[32] = {0};
            char lat_s[32] = {0};
            char lon_s[32] = {0};
            char en_s[16] = {0};
            char tz_s[32] = {0};

            extractJsonField(input, "city", city, sizeof(city));
            extractJsonField(input, "lat", lat_s, sizeof(lat_s));
            extractJsonField(input, "lon", lon_s, sizeof(lon_s));
            extractJsonField(input, "enabled", en_s, sizeof(en_s));
            extractJsonField(input, "tz", tz_s, sizeof(tz_s));
            if (tz_s[0] == '\0') extractJsonField(input, "tz_offset_sec", tz_s, sizeof(tz_s));

            float lat = (lat_s[0] != '\0') ? (float)atof(lat_s) : getWeatherLat();
            float lon = (lon_s[0] != '\0') ? (float)atof(lon_s) : getWeatherLon();
            bool enabled = (en_s[0] == '\0' || strcmp(en_s, "true") == 0 || strcmp(en_s, "1") == 0);

            saveWeatherConfig(city, lat, lon, enabled);
            if (tz_s[0] != '\0') {
                saveTimezoneOffsetSec((int32_t)atol(tz_s));
                applyTimezoneConfig();
            }
            triggerWeatherFetch();

            KORE_LOG_INF("BLE", "Saved weather config via BLE: city=%s, lat=%.4f, lon=%.4f, en=%d, tz=%s", city, lat, lon, enabled ? 1 : 0, tz_s);

            if (s_pTxCharacteristic && s_device_connected) {
                char resp[128] = "{\"status\":\"ok\",\"message\":\"Weather settings updated\"}";
                s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
                s_pTxCharacteristic->notify();
            }
            return;
        }

        /* M. Live Weather Push from Phone over BLE (Works completely offline without Wi-Fi on KoRe):
              {"cmd":"push_weather", "city":"Jakarta", "temp":28.5, "hum":70, "code":1, "cond":"MAINLY CLEAR"} */
        if (strcmp(cmd, "push_weather") == 0 || strcmp(cmd, "sync_weather") == 0) {
            char city[32] = {0};
            char temp_s[16] = {0};
            char hum_s[16] = {0};
            char code_s[16] = {0};
            char cond[32] = {0};

            extractJsonField(input, "city", city, sizeof(city));
            extractJsonField(input, "temp", temp_s, sizeof(temp_s));
            extractJsonField(input, "hum", hum_s, sizeof(hum_s));
            extractJsonField(input, "code", code_s, sizeof(code_s));
            extractJsonField(input, "cond", cond, sizeof(cond));

            float temp = atof(temp_s);
            int hum = atoi(hum_s);
            int code = atoi(code_s);

            portENTER_CRITICAL(&g_weather_mutex);
            g_weather_info.temperature = temp;
            g_weather_info.humidity = hum;
            g_weather_info.weather_code = code;
            if (city[0] != '\0') {
                strncpy(g_weather_info.city, city, sizeof(g_weather_info.city) - 1);
                g_weather_info.city[sizeof(g_weather_info.city) - 1] = '\0';
            }
            if (cond[0] != '\0') {
                strncpy(g_weather_info.condition, cond, sizeof(g_weather_info.condition) - 1);
                g_weather_info.condition[sizeof(g_weather_info.condition) - 1] = '\0';
            }
            g_weather_info.valid = true;
            g_weather_info.last_sync_ms = millis();
            portEXIT_CRITICAL(&g_weather_mutex);

            KORE_LOG_INF("BLE", "Weather pushed directly from phone BLE: %s, %.1fC, %d%%, code=%d (%s)",
                g_weather_info.city, temp, hum, code, g_weather_info.condition);

            if (s_pTxCharacteristic && s_device_connected) {
                char resp[64] = "{\"status\":\"ok\",\"message\":\"Weather updated\"}";
                s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
                s_pTxCharacteristic->notify();
            }
            return;
        }

        /* N. Accurate Time Synchronization from Phone over BLE:
              {"cmd":"sync_time", "epoch":1724745000, "tz":25200} */
        if (strcmp(cmd, "sync_time") == 0 || strcmp(cmd, "time") == 0) {
            char epoch_s[32] = {0};
            char tz_s[16] = {0};
            extractJsonField(input, "epoch", epoch_s, sizeof(epoch_s));
            extractJsonField(input, "tz", tz_s, sizeof(tz_s));

            time_t epoch = (time_t)strtoull(epoch_s, NULL, 10);
            if (epoch > 1700000000) {
                struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
                settimeofday(&tv, NULL);
                KORE_LOG_INF("BLE", "RTC Time synced from phone over BLE: epoch=%llu", (unsigned long long)epoch);
            }
            if (tz_s[0] != '\0') {
                saveTimezoneOffsetSec((int32_t)atol(tz_s));
                applyTimezoneConfig();
            }

            if (s_pTxCharacteristic && s_device_connected) {
                char resp[64] = "{\"status\":\"ok\",\"message\":\"Time synced\"}";
                s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
                s_pTxCharacteristic->notify();
            }
            return;
        }

        /* O. Query Telemetry snapshot: {"cmd":"get_telemetry"} or {"cmd":"telemetry"} */
        if (strcmp(cmd, "get_telemetry") == 0 || strcmp(cmd, "telemetry") == 0 || strcmp(cmd, "get_tele") == 0) {
            KORE_LOG_INF("BLE", "Telemetry snapshot requested via BLE JSON command");
            sendBleTelemetryNow();
            return;
        }

        /* P. Background Telemetry Streaming: {"cmd":"stream_telemetry", "enable":true, "interval":500} */
        if (strcmp(cmd, "stream_telemetry") == 0 || strcmp(cmd, "start_telemetry") == 0 || strcmp(cmd, "stop_telemetry") == 0) {
            char en_s[16] = {0};
            char int_s[16] = {0};
            extractJsonField(input, "enable", en_s, sizeof(en_s));
            if (en_s[0] == '\0') extractJsonField(input, "active", en_s, sizeof(en_s));
            extractJsonField(input, "interval", int_s, sizeof(int_s));
            if (int_s[0] == '\0') extractJsonField(input, "interval_ms", int_s, sizeof(int_s));

            bool enable = true;
            if (strcmp(cmd, "stop_telemetry") == 0) {
                enable = false;
            } else if (en_s[0] != '\0') {
                enable = (strcmp(en_s, "true") == 0 || strcmp(en_s, "1") == 0);
            }

            uint32_t interval = (int_s[0] != '\0') ? (uint32_t)atoi(int_s) : s_ble_telemetry_interval_ms;
            setBleTelemetryStreaming(enable, interval);

            if (s_pTxCharacteristic && s_device_connected) {
                char resp[96];
                snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"streaming\":%s,\"interval_ms\":%u}",
                         enable ? "true" : "false", (unsigned)s_ble_telemetry_interval_ms);
                sendBleData(resp, strlen(resp));
            }
            return;
        }
    }

    /* B. Check for Brightness adjustment command via Raw Text: BRIGHTNESS:180 */
    if (input.startsWith("BRIGHTNESS:") || input.startsWith("brightness:") || input.startsWith("SET_BRIGHTNESS:")) {
        int colon = input.indexOf(':');
        int b = input.substring(colon + 1).toInt();
        b = constrain(b, 0, 255);
        setOledBrightnessLive((uint8_t)b);
        saveOledBrightness((uint8_t)b);

        KORE_LOG_INF("BLE", "OLED brightness set via BLE text command: %d", b);

        if (s_pTxCharacteristic && s_device_connected) {
            char resp[64];
            snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"brightness\":%d}", b);
            s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
            s_pTxCharacteristic->notify();
        }
        return;
    }

    /* C. Check for Expression adjustment command via Raw Text: EXPR:0 or EXPR:auto */
    if (input.startsWith("EXPR:") || input.startsWith("expr:") || input.startsWith("SET_EXPRESSION:")) {
        int colon = input.indexOf(':');
        String val = input.substring(colon + 1);
        val.trim();
        if (val.equalsIgnoreCase("auto") || val == "-1") {
            setManualExpression(-1);
            KORE_LOG_INF("BLE", "Expression reset to autonomous Auto Mood via text command");
        } else {
            int code = val.toInt();
            if (code >= 0 && code <= 7) {
                setManualExpression(code);
                KORE_LOG_INF("BLE", "Expression set to %d (%s) via BLE text command", code, getExpressionName((Expression)code));
            }
        }

        if (s_pTxCharacteristic && s_device_connected) {
            char resp[64];
            snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"expr\":\"%s\"}", val.c_str());
            s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
            s_pTxCharacteristic->notify();
        }
        return;
    }

    /* D. Check for Telemetry query via Raw Text: TELEMETRY or GET_TELEMETRY */
    if (input.equalsIgnoreCase("TELEMETRY") || input.equalsIgnoreCase("GET_TELEMETRY") || input.startsWith("TELEMETRY:")) {
        KORE_LOG_INF("BLE", "Telemetry snapshot requested via BLE text command");
        sendBleTelemetryNow();
        return;
    }

    /* E. Check for Telemetry Streaming via Raw Text: STREAM_TELEMETRY:1 or STREAM_TELEMETRY:0 */
    if (input.startsWith("STREAM_TELEMETRY:") || input.startsWith("stream_telemetry:")) {
        int colon = input.indexOf(':');
        String val = input.substring(colon + 1);
        val.trim();
        bool enable = (val == "1" || val.equalsIgnoreCase("true") || val.equalsIgnoreCase("on"));
        int interval = val.toInt();
        if (interval > 1) {
            setBleTelemetryStreaming(true, (uint32_t)interval);
        } else {
            setBleTelemetryStreaming(enable, s_ble_telemetry_interval_ms);
        }

        if (s_pTxCharacteristic && s_device_connected) {
            char resp[96];
            snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"streaming\":%s,\"interval_ms\":%u}",
                     s_ble_telemetry_streaming ? "true" : "false", (unsigned)s_ble_telemetry_interval_ms);
            sendBleData(resp, strlen(resp));
        }
        return;
    }


    char app[16] = {0};
    char title[36] = {0};
    char message[96] = {0};

    /* 1. Format JSON: {"app":"WA","title":"Budi","message":"Halo"} */
    if (input.startsWith("{") && input.endsWith("}")) {
        extractJsonField(input, "app", app, sizeof(app));
        extractJsonField(input, "title", title, sizeof(title));
        if (title[0] == '\0') {
            extractJsonField(input, "sender", title, sizeof(title));
        }
        extractJsonField(input, "message", message, sizeof(message));
        if (message[0] == '\0') {
            extractJsonField(input, "msg", message, sizeof(message));
        }
        if (message[0] == '\0') {
            extractJsonField(input, "text", message, sizeof(message));
        }
    }
    /* 2. Format with App tag: [WA] Budi: Halo or [Telegram] Budi: Halo */
    else if (input.startsWith("[")) {
        int close_bracket = input.indexOf(']');
        if (close_bracket > 1) {
            String app_tag = input.substring(1, close_bracket);
            strncpy(app, app_tag.c_str(), sizeof(app) - 1);
            String rest = input.substring(close_bracket + 1);
            rest.trim();

            int sep_idx = rest.indexOf(':');
            if (sep_idx < 0) sep_idx = rest.indexOf('|');

            if (sep_idx >= 0) {
                String t = rest.substring(0, sep_idx);
                t.trim();
                String m = rest.substring(sep_idx + 1);
                m.trim();
                strncpy(title, t.c_str(), sizeof(title) - 1);
                strncpy(message, m.c_str(), sizeof(message) - 1);
            } else {
                strncpy(title, app_tag.c_str(), sizeof(title) - 1);
                strncpy(message, rest.c_str(), sizeof(message) - 1);
            }
        }
    }
    /* 3. Format Pipe or Colon: Budi|Halo or Budi: Halo */
    else {
        int sep_idx = input.indexOf('|');
        if (sep_idx < 0) sep_idx = input.indexOf(':');

        if (sep_idx >= 0) {
            String t = input.substring(0, sep_idx);
            t.trim();
            String m = input.substring(sep_idx + 1);
            m.trim();
            strncpy(title, t.c_str(), sizeof(title) - 1);
            strncpy(message, m.c_str(), sizeof(message) - 1);
        } else {
            strncpy(title, "Notification", sizeof(title) - 1);
            strncpy(message, input.c_str(), sizeof(message) - 1);
        }
    }

    /* Auto-detect app name from keywords if not explicitly specified */
    if (app[0] == '\0') {
        if (strstr(title, "WhatsApp") || strstr(title, "[WA]") || strstr(message, "WhatsApp")) {
            strncpy(app, "WhatsApp", sizeof(app) - 1);
        } else if (strstr(title, "Telegram") || strstr(title, "[TG]") || strstr(message, "Telegram")) {
            strncpy(app, "Telegram", sizeof(app) - 1);
        } else if (strstr(title, "Gmail") || strstr(title, "Email")) {
            strncpy(app, "Gmail", sizeof(app) - 1);
        } else if (strstr(title, "SMS") || strstr(title, "Pesan")) {
            strncpy(app, "SMS", sizeof(app) - 1);
        } else if (strstr(title, "Discord")) {
            strncpy(app, "Discord", sizeof(app) - 1);
        } else {
            strncpy(app, "Notice", sizeof(app) - 1);
        }
    }

    if (title[0] == '\0') {
        strncpy(title, "BLE Alert", sizeof(title) - 1);
    }

    if (strlen(message) > 0) {
        KORE_LOG_INF("BLE", "Notification received from BLE [%s] %s: %s", app, title, message);
        pushLocalNotification(app, title, message);
    }
}

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        s_device_connected = true;
        KORE_LOG_INF("BLE", "Phone connected via BLE GATT");

        if (s_pTxCharacteristic) {
            char resp[300];
            String current_ip = isWiFiAPMode() ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
            snprintf(resp, sizeof(resp),
                "{\"status\":\"ok\",\"ip\":\"%s\",\"mode\":\"%s\",\"sta_ssid\":\"%s\",\"sta_pass\":\"%s\",\"ap_ssid\":\"%s\",\"ap_pass\":\"%s\",\"ble_name\":\"%s\"}",
                current_ip.c_str(), isWiFiAPMode() ? "AP" : "STA",
                getWiFiStaSSID(), getWiFiStaPass(), getWiFiApSSID(), getWiFiApPass(), getBleDeviceName()
            );
            s_pTxCharacteristic->setValue((uint8_t*)resp, strlen(resp));
            s_pTxCharacteristic->notify();
        }
    }

    void onDisconnect(BLEServer* pServer) override {
        s_device_connected = false;
        s_ble_telemetry_streaming = false;
        KORE_LOG_INF("BLE", "Phone disconnected; restarting advertising");
        delay(30);
        BLEDevice::startAdvertising();
    }
};

static String s_ble_rx_buffer = "";
static unsigned long s_last_rx_ms = 0;

class CharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        String incoming = pCharacteristic->getValue();
        if (incoming.length() == 0) return;

        unsigned long now = millis();
        if (s_ble_rx_buffer.length() > 0 && (now - s_last_rx_ms > 1500)) {
            s_ble_rx_buffer = "";
        }
        s_last_rx_ms = now;

        s_ble_rx_buffer += incoming;
        s_ble_rx_buffer.trim();

        /* Robust parser: extract and process every complete JSON object { ... } */
        while (true) {
            int start_brace = s_ble_rx_buffer.indexOf('{');
            if (start_brace < 0) {
                /* Non-JSON raw command or notification */
                if (s_ble_rx_buffer.length() > 0 && (s_ble_rx_buffer.indexOf('\n') >= 0 || s_ble_rx_buffer.indexOf(':') >= 0 || s_ble_rx_buffer.indexOf('|') >= 0)) {
                    String complete_msg = s_ble_rx_buffer;
                    s_ble_rx_buffer = "";
                    processIncomingBleData(complete_msg);
                }
                break;
            }

            int end_brace = s_ble_rx_buffer.indexOf('}', start_brace);
            if (end_brace < 0) {
                /* Incomplete JSON chunk, wait for next onWrite */
                break;
            }

            String single_json = s_ble_rx_buffer.substring(start_brace, end_brace + 1);
            s_ble_rx_buffer = s_ble_rx_buffer.substring(end_brace + 1);
            s_ble_rx_buffer.trim();

            processIncomingBleData(single_json);
        }
    }
};

void initBleNotificationServer(void) {
    const char* dev_name = getBleDeviceName();
    KORE_LOG_INF("BLE", "Initializing BLE GATT Server: %s", dev_name);

    BLEDevice::init(dev_name);
    BLEDevice::setMTU(517);

    /* Set Bluetooth RF TX power to maximum (+9 dBm) on ESP32-S3 */
    BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
    BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
    BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_SCAN);

    s_pServer = BLEDevice::createServer();
    s_pServer->setCallbacks(new ServerCallbacks());

    BLEService *pService = s_pServer->createService(BLE_NUS_SERVICE_UUID);

    s_pTxCharacteristic = pService->createCharacteristic(
        BLE_NUS_CHAR_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    s_pTxCharacteristic->addDescriptor(new BLE2902());

    s_pRxCharacteristic = pService->createCharacteristic(
        BLE_NUS_CHAR_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    s_pRxCharacteristic->setCallbacks(new CharacteristicCallbacks());

    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_NUS_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06); /* 7.5 ms preferred interval */
    pAdvertising->setMinPreferred(0x12); /* 22.5 ms preferred interval */
    pAdvertising->setMinInterval(32);    /* 20 ms */
    pAdvertising->setMaxInterval(64);    /* 40 ms */
    BLEDevice::startAdvertising();

    if (!s_ble_telemetry_task_handle) {
        xTaskCreatePinnedToCore(
            bleTelemetryStreamTask,
            "BLE_Telem_Task",
            6144,
            NULL,
            1,
            &s_ble_telemetry_task_handle,
            0
        );
    }

    KORE_LOG_INF("BLE", "BLE advertising started as '%s'", dev_name);
}

bool isBleConnected(void) {
    return s_device_connected;
}
