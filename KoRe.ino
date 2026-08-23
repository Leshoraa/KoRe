/**
 * @file KoRe.ino
 * @brief Master firmware entrypoint and dual-core FreeRTOS task orchestrator.
 * @details Target Platform: Seeed Studio XIAO ESP32-S3 Sense
 *          Architecture: FreeRTOS Dual-Core Asynchronous Task Split
 *                        Core 0: Computer Vision Pipeline, Spatial Clustering, HTTP Server, DFS Power Scaling
 *                        Core 1: 60 FPS Biomechanical Gaze Kinematics & 1-Bit LGFX Sprite Engine
 */

#include "include/kore_config.h"
#include "include/kore_types.h"
#include "include/kore_kalman.h"
#include "include/kore_kinematics.h"
#include "include/kore_affective.h"
#include "src/core/camera_pipeline.h"
#include "src/core/display_engine.h"
#include "src/net/wifi_manager.h"
#include "src/net/http_server.h"
#include "src/net/weather_client.h"
#include <Arduino.h>

void setup() {
    /* Set CPU clock to active compute frequency (240 MHz) */
    setCpuFrequencyMhz(CPU_FREQ_ACTIVE_MHZ);

    /* Initialize OLED display to present boot progress */
    lcd.init();
    lcd.setRotation(2);
    lcd.setBrightness(OLED_DEFAULT_BRIGHTNESS);
    showBootStatus("KoRe Starting...");

    KORE_LOG_INF("MAIN", "KoRe Biomechanical Face Tracker Starting");

    /* Initialize camera sensor hardware */
    showBootStatus("Init Camera...");
    if (!initCamera()) {
        g_camera_init_ok = false;
        showBootStatus("Camera Offline", "Web Mode Active");
        KORE_LOG_ERR("MAIN", "Camera initialization failed; running web mode only");
        delay(1200);
    } else {
        g_camera_init_ok = true;
        KORE_LOG_INF("MAIN", "Camera hardware initialized successfully");
    }

    /* Allocate working arrays in internal SRAM and external PSRAM */
    bool vision_buffers_ok = allocateVisionBuffers();
    if (!vision_buffers_ok) {
        KORE_LOG_ERR("MAIN", "Vision buffer allocation failed; camera task disabled");
        showBootStatus("Mem Alloc Fail", "Web Mode Only");
        delay(2000);
    }

    /* Initialize Wi-Fi subsystem and NVS configuration */
    initWiFiAndNetwork();

    /* Initialize background Open-Meteo weather client */
    initWeatherClient();

    /* Start HTTP web dashboard (Port 80) and MJPEG video stream (Port 81) */
    startWebServer();
    KORE_LOG_INF("MAIN", "HTTP services active");

    /* Dispatch camera task if vision buffers are allocated (task handles automatic sensor recovery if offline) */
    if (vision_buffers_ok) {
        xTaskCreatePinnedToCore(
            cameraTask,
            "Camera_AI_Task",
            8192,
            NULL,
            2,
            NULL,
            0
        );
    } else {
        KORE_LOG_ERR("MAIN", "Camera task not started: vision buffer allocation failed");
    }

    /* Dispatch FreeRTOS Core 1 Display & Ocular Kinematics Task (Priority 1) */
    xTaskCreatePinnedToCore(
        oledTask,
        "OLED_Task",
        8192,
        NULL,
        1,
        NULL,
        1
    );

    KORE_LOG_INF("MAIN", "All FreeRTOS tasks dispatched successfully");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(5000));
}
