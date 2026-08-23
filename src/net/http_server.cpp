/**
 * @file http_server.cpp
 * @brief Embedded asynchronous HTTP web server, JSON telemetry, and MJPEG streamer implementation.
 */

#include "src/net/http_server.h"
#include "src/net/web_ui.h"
#include "src/net/wifi_manager.h"
#include "include/kore_config.h"
#include "include/kore_types.h"
#include "include/kore_affective.h"
#include "esp_camera.h"
#include <Arduino.h>
#include "esp_heap_caps.h"
#include <WiFi.h>

httpd_handle_t g_stream_httpd = NULL;
httpd_handle_t g_camera_httpd = NULL;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t index_handler(httpd_req_t *req) {
    g_last_web_activity_ms = millis();
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, HTML_PAGE, strlen(HTML_PAGE));
}

static esp_err_t telemetry_handler(httpd_req_t *req) {
    g_last_web_activity_ms = millis();
    char json[1024];
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

    snprintf(json, sizeof(json),
        "{\"detected\":%s,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"cx\":%d,\"cy\":%d,\"err_x\":%.1f,\"err_y\":%.1f,\"conf\":%.2f,\"fps_ai\":%.1f,\"fw\":%d,\"fh\":%d,\"vx\":%.1f,\"vy\":%.1f,\"prox\":%.2f,\"num_cands\":%d,\"insp_idx\":%d,\"c0_cx\":%d,\"c0_cy\":%d,\"c0_w\":%d,\"c0_h\":%d,\"c0_p\":%.1f,\"c1_cx\":%d,\"c1_cy\":%d,\"c1_w\":%d,\"c1_h\":%d,\"c1_p\":%.1f,\"c2_cx\":%d,\"c2_cy\":%d,\"c2_w\":%d,\"c2_h\":%d,\"c2_p\":%.1f,\"expr\":%d,\"expr_name\":\"%s\",\"is_manual\":%s,\"valence\":%.2f,\"arousal\":%.2f,\"heap_free\":%u,\"psram_free\":%u,\"uptime_s\":%lu,\"cpu_mhz\":%d}",
        target.detected ? "true" : "false",
        target.x, target.y, target.w, target.h,
        target.cx, target.cy,
        target.error_x, target.error_y,
        target.confidence,
        g_fps_ai,
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
        (unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned long)(millis() / 1000),
        ESP.getCpuFreqMHz()
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, strlen(json));
}

static esp_err_t stream_handler(httpd_req_t *req) {
    esp_err_t res = ESP_OK;
    char part_buf[64];
    uint32_t last_stream_time = millis();

    if (!g_camera_init_ok) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera Offline");
        return ESP_OK;
    }

    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) return res;
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    uint8_t* stream_buf = (uint8_t*)ps_malloc(STREAM_BUFFER_SIZE_BYTES);
    if (!stream_buf) stream_buf = (uint8_t*)malloc(STREAM_BUFFER_SIZE_BYTES);
    if (!stream_buf) return ESP_FAIL;

    portENTER_CRITICAL(&g_stream_mutex);
    g_stream_clients++;
    portEXIT_CRITICAL(&g_stream_mutex);
    g_last_web_activity_ms = millis();

    while (true) {
        if (!g_camera_init_ok) break;

        if (g_frame_sem && xSemaphoreTake(g_frame_sem, pdMS_TO_TICKS(200)) == pdTRUE) {
            size_t len = 0;
            if (stream_buf && g_latest_jpeg_buf) {
                portENTER_CRITICAL(&g_stream_mutex);
                memcpy(stream_buf, g_latest_jpeg_buf, g_latest_jpeg_len);
                len = g_latest_jpeg_len;
                portEXIT_CRITICAL(&g_stream_mutex);
            }

            if (len > 0 && stream_buf) {
                size_t hlen = snprintf(part_buf, 64, STREAM_PART, len);
                res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
                if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_buf, hlen);
                if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)stream_buf, len);

                if (res != ESP_OK) break;

                uint32_t now = millis();
                g_last_web_activity_ms = now;
                if (now - last_stream_time > 0) {
                    g_fps_stream = 1000.0f / (float)(now - last_stream_time);
                }
                last_stream_time = now;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    portENTER_CRITICAL(&g_stream_mutex);
    g_stream_clients--;
    if (g_stream_clients < 0) g_stream_clients = 0;
    portEXIT_CRITICAL(&g_stream_mutex);

    if (stream_buf) {
        free(stream_buf);
    }
    return res;
}

static bool extract_json_value(const char* json, const char* key, char* out, size_t max_len) {
    if (!json || !key || !out || max_len == 0) return false;

    char key_pattern[64];
    snprintf(key_pattern, sizeof(key_pattern), "\"%s\"", key);
    const char* p = strstr(json, key_pattern);

    if (p) {
        p += strlen(key_pattern);
        while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        if (*p == ':') {
            p++;
            while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
            if (*p == '"') {
                p++;
                const char* end = strchr(p, '"');
                if (!end) return false;
                size_t len = end - p;
                if (len >= max_len) len = max_len - 1;
                strncpy(out, p, len);
                out[len] = '\0';
                return true;
            } else {
                size_t i = 0;
                while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && i < max_len - 1) {
                    out[i++] = *p++;
                }
                out[i] = '\0';
                return (i > 0);
            }
        }
    }

    snprintf(key_pattern, sizeof(key_pattern), "%s=", key);
    p = strstr(json, key_pattern);
    if (p) {
        p += strlen(key_pattern);
        size_t i = 0;
        while (*p && *p != '&' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && i < max_len - 1) {
            out[i++] = *p++;
        }
        out[i] = '\0';
        return (i > 0);
    }

    return false;
}

static esp_err_t get_wifi_handler(httpd_req_t *req) {
    g_last_web_activity_ms = millis();
    char json[320];
    snprintf(json, sizeof(json),
        "{\"sta_ssid\":\"%s\",\"sta_pass\":\"********\",\"ap_ssid\":\"%s\",\"ap_pass\":\"********\",\"is_ap\":%s}",
        getWiFiStaSSID(), getWiFiApSSID(), isWiFiAPMode() ? "true" : "false"
    );
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, strlen(json));
}

static esp_err_t switch_mode_handler(httpd_req_t *req) {
    g_last_web_activity_ms = millis();
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) return ESP_FAIL;
    buf[ret] = '\0';

    char target_mode[16] = {0};
    extract_json_value(buf, "mode", target_mode, sizeof(target_mode));

    const char* resp = "{\"status\":\"ok\",\"message\":\"Mode beralih. ESP reboot...\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, resp, strlen(resp));

    switchWiFiMode(target_mode);
    return ESP_OK;
}

static esp_err_t save_wifi_handler(httpd_req_t *req) {
    g_last_web_activity_ms = millis();
    char buf[512];
    int ret, remaining = req->content_len;
    if (remaining >= sizeof(buf)) remaining = sizeof(buf) - 1;

    ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    char new_sta_ssid[64] = {0};
    char new_sta_pass[64] = {0};
    char new_ap_ssid[64]  = {0};
    char new_ap_pass[64]  = {0};

    extract_json_value(buf, "sta_ssid", new_sta_ssid, sizeof(new_sta_ssid));
    extract_json_value(buf, "sta_pass", new_sta_pass, sizeof(new_sta_pass));
    extract_json_value(buf, "ap_ssid", new_ap_ssid, sizeof(new_ap_ssid));
    extract_json_value(buf, "ap_pass", new_ap_pass, sizeof(new_ap_pass));

    const char* resp = "{\"status\":\"ok\",\"message\":\"Pengaturan disimpan. ESP reboot...\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, resp, strlen(resp));

    saveWiFiCredentials(new_sta_ssid, new_sta_pass, new_ap_ssid, new_ap_pass);
    return ESP_OK;
}

static esp_err_t set_expression_handler(httpd_req_t *req) {
    g_last_web_activity_ms = millis();
    char buf[128];
    int remaining = req->content_len;
    if (remaining >= (int)sizeof(buf)) remaining = sizeof(buf) - 1;

    int ret = httpd_req_recv(req, buf, remaining);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    char expr_val[16] = {0};
    if (extract_json_value(buf, "expr", expr_val, sizeof(expr_val))) {
        if (strcmp(expr_val, "auto") == 0 || strcmp(expr_val, "-1") == 0) {
            setManualExpression(-1);
        } else {
            int code = atoi(expr_val);
            if (code >= 0 && code <= 7) {
                setManualExpression(code);
            }
        }
    }

    const char* resp = "{\"status\":\"ok\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, resp, strlen(resp));
}

static esp_err_t scan_wifi_handler(httpd_req_t *req) {
    g_last_web_activity_ms = millis();
    WiFi.scanDelete();
    int n = WiFi.scanNetworks(false, false, false, 300);
    
    char json[1024];
    int pos = 0;
    pos += snprintf(json + pos, sizeof(json) - pos, "{\"networks\":[");
    
    for (int i = 0; i < n && i < 15 && pos < (int)sizeof(json) - 80; i++) {
        if (i > 0) pos += snprintf(json + pos, sizeof(json) - pos, ",");
        const char* enc = "OPEN";
        switch (WiFi.encryptionType(i)) {
            case WIFI_AUTH_WPA_PSK:       enc = "WPA"; break;
            case WIFI_AUTH_WPA2_PSK:      enc = "WPA2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK:  enc = "WPA/WPA2"; break;
            case WIFI_AUTH_WPA3_PSK:      enc = "WPA3"; break;
            case WIFI_AUTH_WEP:           enc = "WEP"; break;
            default:                       enc = "OTHER"; break;
        }
        pos += snprintf(json + pos, sizeof(json) - pos,
            "{\"ssid\":\"%s\",\"rssi\":%d,\"enc\":\"%s\"}",
            WiFi.SSID(i).c_str(), WiFi.RSSI(i), enc);
    }
    pos += snprintf(json + pos, sizeof(json) - pos, "],\"count\":%d}", n);
    
    WiFi.scanDelete();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, strlen(json));
}

static esp_err_t system_info_handler(httpd_req_t *req) {
    g_last_web_activity_ms = millis();
    char json[512];
    
    uint32_t uptime_s = millis() / 1000;
    int32_t rssi = (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
    
    snprintf(json, sizeof(json),
        "{\"firmware\":\"2.4.0\",\"compiled\":\"%s %s\",\"chip\":\"%s\",\"cores\":%d,\"cpu_mhz\":%d,"
        "\"heap_free\":%u,\"heap_min\":%u,\"psram_free\":%u,\"psram_total\":%u,"
        "\"uptime_s\":%lu,\"wifi_rssi\":%d,\"wifi_mode\":\"%s\",\"ip\":\"%s\","
        "\"camera_ok\":%s,\"stream_clients\":%d}",
        __DATE__, __TIME__,
        ESP.getChipModel(), ESP.getChipCores(), ESP.getCpuFreqMHz(),
        (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
        (unsigned long)uptime_s, (int)rssi,
        isWiFiAPMode() ? "AP" : "STA",
        isWiFiAPMode() ? WiFi.softAPIP().toString().c_str() : WiFi.localIP().toString().c_str(),
        g_camera_init_ok ? "true" : "false",
        g_stream_clients
    );
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json, strlen(json));
}

void startWebServer(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 24;
    config.server_port = HTTP_PORT_WEB_CONTROL;

    httpd_uri_t index_uri          = { .uri = "/",                   .method = HTTP_GET,  .handler = index_handler,          .user_ctx = NULL };
    httpd_uri_t telemetry_uri      = { .uri = "/telemetry",           .method = HTTP_GET,  .handler = telemetry_handler,      .user_ctx = NULL };
    httpd_uri_t set_expression_uri = { .uri = "/set_expression",      .method = HTTP_POST, .handler = set_expression_handler, .user_ctx = NULL };
    httpd_uri_t get_wifi_uri       = { .uri = "/get_wifi",            .method = HTTP_GET,  .handler = get_wifi_handler,       .user_ctx = NULL };
    httpd_uri_t save_wifi_uri      = { .uri = "/save_wifi",           .method = HTTP_POST, .handler = save_wifi_handler,      .user_ctx = NULL };
    httpd_uri_t switch_mode_uri    = { .uri = "/switch_mode",         .method = HTTP_POST, .handler = switch_mode_handler,    .user_ctx = NULL };
    httpd_uri_t captive_1          = { .uri = "/generate_204",        .method = HTTP_GET,  .handler = index_handler,          .user_ctx = NULL };
    httpd_uri_t captive_2          = { .uri = "/gen_204",             .method = HTTP_GET,  .handler = index_handler,          .user_ctx = NULL };
    httpd_uri_t captive_3          = { .uri = "/hotspot-detect.html", .method = HTTP_GET,  .handler = index_handler,          .user_ctx = NULL };
    httpd_uri_t captive_4          = { .uri = "/connecttest.txt",     .method = HTTP_GET,  .handler = index_handler,          .user_ctx = NULL };
    httpd_uri_t captive_5          = { .uri = "/ncsi.txt",            .method = HTTP_GET,  .handler = index_handler,          .user_ctx = NULL };
    httpd_uri_t captive_6          = { .uri = "/canonical.html",      .method = HTTP_GET,  .handler = index_handler,          .user_ctx = NULL };
    httpd_uri_t captive_7          = { .uri = "/success.txt",         .method = HTTP_GET,  .handler = index_handler,          .user_ctx = NULL };
    httpd_uri_t scan_wifi_uri      = { .uri = "/scan_wifi",           .method = HTTP_GET,  .handler = scan_wifi_handler,      .user_ctx = NULL };
    httpd_uri_t system_info_uri    = { .uri = "/system_info",         .method = HTTP_GET,  .handler = system_info_handler,    .user_ctx = NULL };

    if (httpd_start(&g_camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(g_camera_httpd, &index_uri);
        httpd_register_uri_handler(g_camera_httpd, &telemetry_uri);
        httpd_register_uri_handler(g_camera_httpd, &set_expression_uri);
        httpd_register_uri_handler(g_camera_httpd, &get_wifi_uri);
        httpd_register_uri_handler(g_camera_httpd, &save_wifi_uri);
        httpd_register_uri_handler(g_camera_httpd, &switch_mode_uri);
        httpd_register_uri_handler(g_camera_httpd, &captive_1);
        httpd_register_uri_handler(g_camera_httpd, &captive_2);
        httpd_register_uri_handler(g_camera_httpd, &captive_3);
        httpd_register_uri_handler(g_camera_httpd, &captive_4);
        httpd_register_uri_handler(g_camera_httpd, &captive_5);
        httpd_register_uri_handler(g_camera_httpd, &captive_6);
        httpd_register_uri_handler(g_camera_httpd, &captive_7);
        httpd_register_uri_handler(g_camera_httpd, &scan_wifi_uri);
        httpd_register_uri_handler(g_camera_httpd, &system_info_uri);
    }

    config.server_port = HTTP_PORT_STREAM;
    config.ctrl_port   = 32769;
    httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };

    if (httpd_start(&g_stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(g_stream_httpd, &stream_uri);
    }
}
