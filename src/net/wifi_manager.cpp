/**
 * @file wifi_manager.cpp
 * @brief NVS Preferences Wi-Fi configuration, STA/AP management, and DNS captive portal implementation.
 */

#include "src/net/wifi_manager.h"
#include "include/kore_config.h"
#include "src/core/display_engine.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <NetBIOS.h>
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

static char s_weather_city[32] = WEATHER_DEFAULT_CITY;
static float s_weather_lat = WEATHER_DEFAULT_LAT;
static float s_weather_lon = WEATHER_DEFAULT_LON;
static bool s_weather_enabled = true;
static int32_t s_timezone_offset_sec = 7 * 3600; /* Default UTC+7 (Jakarta / WIB) */
static uint8_t s_oled_brightness = OLED_DEFAULT_BRIGHTNESS;

static bool s_is_ap_mode = false;
static DNSServer s_dnsServer;

int32_t getTimezoneOffsetSec(void) {
    return s_timezone_offset_sec;
}

void applyTimezoneConfig(void) {
    configTime(s_timezone_offset_sec, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

void saveTimezoneOffsetSec(int32_t offset_sec) {
    Preferences prefs;
    prefs.begin("kore_cfg", false);
    prefs.putLong("tz_offset", (long)offset_sec);
    prefs.end();
    s_timezone_offset_sec = offset_sec;
    applyTimezoneConfig();
}

uint8_t getSavedOledBrightness(void) {
    return s_oled_brightness;
}

void saveOledBrightness(uint8_t brightness) {
    Preferences prefs;
    prefs.begin("kore_cfg", false);
    prefs.putUChar("oled_bright", brightness);
    prefs.end();
    s_oled_brightness = brightness;
    g_oled_brightness = brightness;
}

const char* getWeatherCity(void) {
    return s_weather_city;
}

float getWeatherLat(void) {
    return s_weather_lat;
}

float getWeatherLon(void) {
    return s_weather_lon;
}

bool isWeatherEnabled(void) {
    return s_weather_enabled;
}

void saveWeatherConfig(const char* city, float lat, float lon, bool enabled) {
    Preferences prefs;
    prefs.begin("kore_cfg", false);
    if (city && strlen(city) > 0) {
        prefs.putString("w_city", city);
        strncpy(s_weather_city, city, sizeof(s_weather_city) - 1);
    }
    prefs.putFloat("w_lat", lat);
    prefs.putFloat("w_lon", lon);
    prefs.putBool("w_en", enabled);
    prefs.end();
    s_weather_lat = lat;
    s_weather_lon = lon;
    s_weather_enabled = enabled;
}

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
    xTaskCreate(restartTask, "restart_task", 3072, (void*)(uintptr_t)delay_ms, 1, NULL);
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
    if (sta_p && strlen(sta_p) > 0 && strcmp(sta_p, "********") != 0) {
        prefs.putString("sta_pass", sta_p);
        prefs.putString("pass", sta_p);
        strncpy(s_sta_password, sta_p, sizeof(s_sta_password) - 1);
    }
    if (ap_s && strlen(ap_s) > 0) {
        prefs.putString("ap_ssid", ap_s);
        strncpy(s_ap_ssid, ap_s, sizeof(s_ap_ssid) - 1);
    }
    if (ap_p && strlen(ap_p) > 0 && strcmp(ap_p, "********") != 0) {
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

    s_oled_brightness = prefs.getUChar("oled_bright", OLED_DEFAULT_BRIGHTNESS);
    g_oled_brightness = s_oled_brightness;

    String stored_w_city = prefs.getString("w_city", WEATHER_DEFAULT_CITY);
    strncpy(s_weather_city, stored_w_city.c_str(), sizeof(s_weather_city) - 1);
    s_weather_lat = prefs.getFloat("w_lat", WEATHER_DEFAULT_LAT);
    s_weather_lon = prefs.getFloat("w_lon", WEATHER_DEFAULT_LON);
    s_weather_enabled = prefs.getBool("w_en", true);
    s_timezone_offset_sec = prefs.getLong("tz_offset", 7 * 3600);

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
        while (WiFi.status() != WL_CONNECTED && attempts < 25) {
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

        // Synchronize NTP Real-Time Clock with configured Timezone
        applyTimezoneConfig();

        // 1. Multicast DNS (mDNS for Apple iOS/macOS, modern Linux, Android 12+)
        if (MDNS.begin("kore")) {
            MDNS.setInstanceName("KoRe Robot");
            MDNS.addService("http", "tcp", HTTP_PORT_WEB_CONTROL);
            MDNS.addService("stream", "tcp", HTTP_PORT_STREAM);
        }

        // 2. NetBIOS Name Service (Allows http://kore/ directly on Windows & Linux)
        NBNS.begin("kore");

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

        // Set AP IP to 192.168.18.16 so that AP and STA share the EXACT SAME IP URL
        IPAddress apIP(192, 168, 18, 16);
        IPAddress apGateway(192, 168, 18, 16);
        IPAddress apSubnet(255, 255, 255, 0);
        WiFi.softAPConfig(apIP, apGateway, apSubnet);

        if (strlen(s_ap_password) > 0 && strlen(s_ap_password) < 8) {
            WiFi.softAP(s_ap_ssid, NULL);
        } else if (strlen(s_ap_password) == 0) {
            WiFi.softAP(s_ap_ssid, NULL);
        } else {
            WiFi.softAP(s_ap_ssid, s_ap_password);
        }

        // 1. mDNS in AP Mode
        if (MDNS.begin("kore")) {
            MDNS.setInstanceName("KoRe Robot AP");
            MDNS.addService("http", "tcp", HTTP_PORT_WEB_CONTROL);
            MDNS.addService("stream", "tcp", HTTP_PORT_STREAM);
        }

        // 2. NetBIOS in AP Mode
        NBNS.begin("kore");

        // 3. DNS Captive Portal Server (Port 53 Catch-All for any domain -> AP IP)
        s_dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        s_dnsServer.start(53, "*", WiFi.softAPIP());
        s_is_ap_mode = true;

        xTaskCreatePinnedToCore([](void* arg) {
            (void)arg;
            while (true) {
                if (s_is_ap_mode) {
                    s_dnsServer.processNextRequest();
                    vTaskDelay(pdMS_TO_TICKS(5));
                } else {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            }
        }, "AP_DNS_Task", 4096, NULL, 1, NULL, 0);

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
