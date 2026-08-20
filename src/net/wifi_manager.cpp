/**
 * @file wifi_manager.cpp
 * @brief NVS Preferences Wi-Fi configuration, STA/AP management, and DNS captive portal implementation.
 */

#include "src/net/wifi_manager.h"
#include "include/kore_config.h"
#include "src/core/display_engine.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <Arduino.h>

static const char* s_ap_ssid_default     = "KoRe";
static const char* s_ap_password_default = "12345678";

static const char* s_sta_ssid_default     = "";
static const char* s_sta_password_default = "";

static char s_sta_ssid[64] = {0};
static char s_sta_password[64] = {0};
static char s_ap_ssid[64] = {0};
static char s_ap_password[64] = {0};

static bool s_is_ap_mode = false;
static DNSServer s_dnsServer;

bool isWiFiAPMode(void) {
    return s_is_ap_mode;
}

const char* getWiFiStaSSID(void) {
    return s_sta_ssid;
}

const char* getWiFiStaPass(void) {
    return s_sta_password;
}

const char* getWiFiApSSID(void) {
    return s_ap_ssid;
}

const char* getWiFiApPass(void) {
    return s_ap_password;
}

static void restartTask(void *pvParameters) {
    uint32_t delay_ms = (uint32_t)(uintptr_t)pvParameters;
    if (delay_ms == 0) delay_ms = 1500;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    esp_restart();
}

void scheduleSystemRestart(uint32_t delay_ms) {
    xTaskCreate(restartTask, "restart_task", 2048, (void*)(uintptr_t)delay_ms, 1, NULL);
}

void saveWiFiCredentials(const char* sta_s, const char* sta_p, const char* ap_s, const char* ap_p) {
    Preferences prefs;
    prefs.begin("kore_cfg", false);
    if (sta_s && strlen(sta_s) > 0) {
        prefs.putString("sta_ssid", sta_s);
        prefs.putString("ssid", sta_s);
        prefs.putString("wifi_mode", "STA");
        strncpy(s_sta_ssid, sta_s, sizeof(s_sta_ssid) - 1);
    }
    if (sta_p) {
        prefs.putString("sta_pass", sta_p);
        prefs.putString("pass", sta_p);
        strncpy(s_sta_password, sta_p, sizeof(s_sta_password) - 1);
    }
    if (ap_s && strlen(ap_s) > 0) {
        prefs.putString("ap_ssid", ap_s);
        strncpy(s_ap_ssid, ap_s, sizeof(s_ap_ssid) - 1);
    }
    if (ap_p) {
        prefs.putString("ap_pass", ap_p);
        strncpy(s_ap_password, ap_p, sizeof(s_ap_password) - 1);
    }
    prefs.end();
    scheduleSystemRestart(1500);
}

void switchWiFiMode(const char* target_mode) {
    Preferences prefs;
    prefs.begin("kore_cfg", false);
    if (strcasecmp(target_mode, "AP") == 0) {
        prefs.putString("wifi_mode", "AP");
    } else if (strcasecmp(target_mode, "STA") == 0) {
        prefs.putString("wifi_mode", "STA");
    }
    prefs.end();
    scheduleSystemRestart(1500);
}

bool initWiFiAndNetwork(void) {
    Preferences prefs;
    prefs.begin("kore_cfg", false);
    String stored_wifi_mode = prefs.getString("wifi_mode", "AUTO");
    String stored_sta_ssid  = prefs.getString("sta_ssid", prefs.getString("ssid", ""));
    String stored_sta_pass  = prefs.getString("sta_pass", prefs.getString("pass", ""));
    String stored_ap_ssid   = prefs.getString("ap_ssid", "");
    String stored_ap_pass   = prefs.getString("ap_pass", "");

    if (stored_sta_ssid.length() > 0) {
        strncpy(s_sta_ssid, stored_sta_ssid.c_str(), sizeof(s_sta_ssid) - 1);
        strncpy(s_sta_password, stored_sta_pass.c_str(), sizeof(s_sta_password) - 1);
    } else {
        strncpy(s_sta_ssid, s_sta_ssid_default, sizeof(s_sta_ssid) - 1);
        strncpy(s_sta_password, s_sta_password_default, sizeof(s_sta_password) - 1);
    }

    if (stored_ap_ssid.length() > 0) {
        strncpy(s_ap_ssid, stored_ap_ssid.c_str(), sizeof(s_ap_ssid) - 1);
        strncpy(s_ap_password, stored_ap_pass.c_str(), sizeof(s_ap_password) - 1);
    } else {
        strncpy(s_ap_ssid, s_ap_ssid_default, sizeof(s_ap_ssid) - 1);
        strncpy(s_ap_password, s_ap_password_default, sizeof(s_ap_password) - 1);
    }
    prefs.end();

    bool force_ap = (stored_wifi_mode == "AP");
    bool connected = false;

    if (!force_ap && strlen(s_sta_ssid) > 0) {
        WiFi.mode(WIFI_STA);
        delay(100);
        WiFi.disconnect(true);
        delay(150);
        WiFi.setSleep(WIFI_PS_NONE);
        WiFi.setAutoReconnect(true);
        WiFi.setTxPower(WIFI_POWER_19_5dBm);

        char wifi_msg[40];
        snprintf(wifi_msg, sizeof(wifi_msg), "WiFi: %s", s_sta_ssid);
        showBootStatus(wifi_msg, "Connecting...");

        if (strlen(s_sta_password) > 0) {
            WiFi.begin(s_sta_ssid, s_sta_password);
        } else {
            WiFi.begin(s_sta_ssid);
        }

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 50) {
            delay(300);
            if (attempts % 4 == 0) {
                char dots[16] = "Connecting";
                int d = (attempts / 4) % 3;
                for (int i = 0; i <= d; i++) strcat(dots, ".");
                showBootStatus(wifi_msg, dots);
            }
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            connected = true;
        }
    }

    if (connected) {
        s_is_ap_mode = false;
        if (MDNS.begin("kore")) {
            MDNS.addService("http", "tcp", 80);
        }
        char sta_ip_buf[40];
        snprintf(sta_ip_buf, sizeof(sta_ip_buf), "IP: %s", WiFi.localIP().toString().c_str());
        showBootStatus("WiFi Connected!", sta_ip_buf);
        delay(1500);
        return true;
    } else {
        WiFi.disconnect(true);
        delay(100);
        WiFi.mode(WIFI_AP);
        WiFi.setSleep(WIFI_PS_NONE);
        WiFi.setTxPower(WIFI_POWER_19_5dBm);

        if (strlen(s_ap_password) > 0 && strlen(s_ap_password) < 8) {
            WiFi.softAP(s_ap_ssid, NULL);
        } else if (strlen(s_ap_password) == 0) {
            WiFi.softAP(s_ap_ssid, NULL);
        } else {
            WiFi.softAP(s_ap_ssid, s_ap_password);
        }

        s_dnsServer.start(53, "*", WiFi.softAPIP());
        s_is_ap_mode = true;

        xTaskCreate([](void* arg) {
            (void)arg;
            while (true) {
                if (s_is_ap_mode) {
                    s_dnsServer.processNextRequest();
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }, "AP_DNS_Task", 2048, NULL, 1, NULL);

        char ap_ip_buf[40];
        if (!force_ap && strlen(s_sta_ssid) > 0) {
            showBootStatus("STA Fail -> AP Mode", WiFi.softAPIP().toString().c_str());
        } else {
            snprintf(ap_ip_buf, sizeof(ap_ip_buf), "AP IP: %s", WiFi.softAPIP().toString().c_str());
            showBootStatus("AP Mode Active", ap_ip_buf);
        }
        delay(1500);
        return false;
    }
}
