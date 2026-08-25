/**
 * @file notification_client.cpp
 * @brief Ntfy.sh persistent real-time streaming client and local notification dispatcher.
 */

#include "src/net/notification_client.h"
#include "src/net/wifi_manager.h"
#include "src/core/display_engine.h"
#include "include/kore_config.h"
#include "include/kore_types.h"
#include <WiFi.h>
#include <Arduino.h>
#ifdef ARDUINO
#include <Preferences.h>
#include <esp_mac.h>
#endif

NotificationInfo g_notification_info = {
    .title = "",
    .message = "",
    .app = "",
    .received_ms = 0,
    .active = false
};

portMUX_TYPE g_notification_mutex = portMUX_INITIALIZER_UNLOCKED;

static char s_ntfy_topic[64] = "";
static TaskHandle_t s_ntfy_task_handle = NULL;
static uint32_t s_last_msg_timestamp = 0;
static volatile bool s_is_connected = false;
static volatile bool s_reconnect_requested = false;

static void ensureDefaultTopic(void) {
    if (s_ntfy_topic[0] == '\0' || strcmp(s_ntfy_topic, "kore_notif_default") == 0) {
        uint8_t mac[6] = {0};
#ifdef ARDUINO
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
#endif
        snprintf(s_ntfy_topic, sizeof(s_ntfy_topic), "kore_%02x%02x%02x", mac[3], mac[4], mac[5]);
    }
}

static void loadNtfyConfigNVS(void) {
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("kore_notif", true)) {
        String saved_topic = prefs.getString("topic", "");
        if (saved_topic.length() > 0 && saved_topic != "kore_notif_default") {
            strncpy(s_ntfy_topic, saved_topic.c_str(), sizeof(s_ntfy_topic) - 1);
            s_ntfy_topic[sizeof(s_ntfy_topic) - 1] = '\0';
        }
        prefs.end();
    }
#endif
    ensureDefaultTopic();
}

static void saveNtfyConfigNVS(void) {
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("kore_notif", false)) {
        prefs.putString("topic", s_ntfy_topic);
        prefs.end();
    }
#endif
}

bool setNtfyTopic(const char* topic) {
    if (!topic || strlen(topic) == 0) return false;
    strncpy(s_ntfy_topic, topic, sizeof(s_ntfy_topic) - 1);
    s_ntfy_topic[sizeof(s_ntfy_topic) - 1] = '\0';
    saveNtfyConfigNVS();
    s_reconnect_requested = true;
    return true;
}

const char* getNtfyTopic(void) {
    ensureDefaultTopic();
    return s_ntfy_topic;
}

bool isNtfyConnected(void) {
    return s_is_connected;
}

uint32_t getNtfyLastMessageTime(void) {
    return s_last_msg_timestamp;
}

void pushLocalNotification(const char* app, const char* title, const char* message) {
    NotificationInfo local_notif;
    memset(&local_notif, 0, sizeof(local_notif));

    if (app && strlen(app) > 0) {
        strncpy(local_notif.app, app, sizeof(local_notif.app) - 1);
    } else {
        strncpy(local_notif.app, "Notice", sizeof(local_notif.app) - 1);
    }

    if (title && strlen(title) > 0) {
        strncpy(local_notif.title, title, sizeof(local_notif.title) - 1);
    } else {
        strncpy(local_notif.title, "Notification", sizeof(local_notif.title) - 1);
    }

    if (message && strlen(message) > 0) {
        strncpy(local_notif.message, message, sizeof(local_notif.message) - 1);
    } else {
        strncpy(local_notif.message, "New alert", sizeof(local_notif.message) - 1);
    }

    local_notif.received_ms = millis();
    local_notif.active = true;

    portENTER_CRITICAL(&g_notification_mutex);
    g_notification_info = local_notif;
    portEXIT_CRITICAL(&g_notification_mutex);

    /* Trigger OLED display engine to present the notification popup */
    triggerNotificationDisplay(local_notif, NOTIFICATION_POPUP_DURATION_MS);
}

static bool extractJsonField(const String& json, const char* key, char* out, size_t max_len) {
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
    return false;
}

static void ntfyTask(void *pvParameters) {
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(2500)); /* Network settling delay */

    ensureDefaultTopic();

    while (true) {
        if (WiFi.status() == WL_CONNECTED && !isWiFiAPMode() && strlen(s_ntfy_topic) > 0) {
            WiFiClient client;
            client.setTimeout(120); /* 120s stream timeout */

            KORE_LOG_INF("NTFY", "Connecting to ntfy.sh stream for topic: %s", s_ntfy_topic);

            if (client.connect("ntfy.sh", 80)) {
                char req[256];
                snprintf(req, sizeof(req),
                    "GET /%s/json HTTP/1.1\r\n"
                    "Host: ntfy.sh\r\n"
                    "User-Agent: KoRe-ESP32-Sense\r\n"
                    "Accept: application/x-ndjson\r\n"
                    "Connection: keep-alive\r\n\r\n",
                    s_ntfy_topic
                );
                client.print(req);

                // Wait for HTTP response status line
                unsigned long t_start = millis();
                while (!client.available() && client.connected() && millis() - t_start < 5000) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }

                if (client.available()) {
                    String status_line = client.readStringUntil('\n');
                    if (status_line.indexOf("200") >= 0) {
                        s_is_connected = true;
                        s_reconnect_requested = false;
                        KORE_LOG_INF("NTFY", "Ntfy stream connected successfully (HTTP 200)");

                        // Skip HTTP response headers until empty line "\r"
                        while (client.connected() && !s_reconnect_requested) {
                            String h = client.readStringUntil('\n');
                            h.trim();
                            if (h.length() == 0) break;
                        }

                        // Stream live events in real-time
                        while (client.connected() && !s_reconnect_requested && WiFi.status() == WL_CONNECTED) {
                            if (client.available()) {
                                String line = client.readStringUntil('\n');
                                line.trim();
                                if (line.length() < 10) continue;

                                if (line.indexOf("\"event\":\"message\"") >= 0) {
                                    int time_idx = line.indexOf("\"time\":");
                                    uint32_t msg_time = 0;
                                    if (time_idx >= 0) {
                                        msg_time = (uint32_t)line.substring(time_idx + 7).toInt();
                                    }
                                    s_last_msg_timestamp = msg_time;

                                    char title[36] = {0};
                                    char msg[96] = {0};
                                    char app[16] = {0};

                                    extractJsonField(line, "title", title, sizeof(title));
                                    extractJsonField(line, "message", msg, sizeof(msg));

                                    if (strstr(title, "WhatsApp") || strstr(title, "[WA]")) {
                                        strncpy(app, "WhatsApp", sizeof(app) - 1);
                                    } else if (strstr(title, "Telegram") || strstr(title, "[TG]")) {
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

                                    if (strlen(title) == 0) {
                                        strncpy(title, "Ntfy Alert", sizeof(title) - 1);
                                    }

                                    if (strlen(msg) > 0) {
                                        pushLocalNotification(app, title, msg);
                                    }
                                }
                            } else {
                                vTaskDelay(pdMS_TO_TICKS(25));
                            }
                        }
                    }
                }
                client.stop();
            }
            s_is_connected = false;
        } else {
            s_is_connected = false;
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void initNotificationClient(void) {
    loadNtfyConfigNVS();

    xTaskCreatePinnedToCore(
        ntfyTask,
        "Ntfy_Client_Task",
        4096,
        NULL,
        1,
        &s_ntfy_task_handle,
        0
    );
}
