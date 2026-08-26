/**
 * @file ble_manager.cpp
 * @brief Bluetooth Low Energy (BLE) Nordic UART Service (NUS) GATT server implementation.
 */

#include "src/net/ble_manager.h"
#include "src/net/notification_client.h"
#include "src/core/display_engine.h"
#include "src/net/wifi_manager.h"
#include "include/kore_config.h"
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

static BLEServer *s_pServer = nullptr;
static BLECharacteristic *s_pRxCharacteristic = nullptr;
static BLECharacteristic *s_pTxCharacteristic = nullptr;
static volatile bool s_device_connected = false;

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
    }

    void onDisconnect(BLEServer* pServer) override {
        s_device_connected = false;
        KORE_LOG_INF("BLE", "Phone disconnected; restarting advertising");
        pServer->startAdvertising();
    }
};

class CharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        String incoming = pCharacteristic->getValue();
        if (incoming.length() > 0) {
            processIncomingBleData(incoming);
        }
    }
};

void initBleNotificationServer(void) {
    KORE_LOG_INF("BLE", "Initializing BLE GATT Server: %s", BLE_DEVICE_NAME);

    BLEDevice::init(BLE_DEVICE_NAME);
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
    pAdvertising->setMinInterval(160); /* 100 ms interval for Wi-Fi 2.4GHz RF coexistence */
    pAdvertising->setMaxInterval(320); /* 200 ms interval */
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();

    KORE_LOG_INF("BLE", "BLE advertising started as '%s'", BLE_DEVICE_NAME);
}

bool isBleConnected(void) {
    return s_device_connected;
}
