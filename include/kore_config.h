/**
 * @file kore_config.h
 * @brief System hardware definitions, power profiles, and logging macros.
 * @details Target Platform: Seeed Studio XIAO ESP32-S3 Sense
 */

#ifndef KORE_CONFIG_H
#define KORE_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Logging Architecture --- */
#ifndef ENABLE_SERIAL_DEBUG
#define ENABLE_SERIAL_DEBUG 0
#endif

#if defined(ENABLE_SERIAL_DEBUG) && (ENABLE_SERIAL_DEBUG == 1)
    #define KORE_LOG_DBG(tag, fmt, ...) printf("[%s][DBG] %s:%d: " fmt "\n", tag, __func__, __LINE__, ##__VA_ARGS__)
    #define KORE_LOG_INF(tag, fmt, ...) printf("[%s][INF] " fmt "\n", tag, ##__VA_ARGS__)
#else
    #define KORE_LOG_DBG(tag, fmt, ...) ((void)0)
    #define KORE_LOG_INF(tag, fmt, ...) ((void)0)
#endif
#define KORE_LOG_ERR(tag, fmt, ...)     fprintf(stderr, "[%s][ERR] %s:%d: " fmt "\n", tag, __func__, __LINE__, ##__VA_ARGS__)

/* --- Hardware Pin Configuration --- */
/* Note: Touch sensor support removed in v2.4.0 (was unused) */

/* OLED Display (SSD1306) Hardware I2C Pins */
#define PIN_OLED_SCL            5
#define PIN_OLED_SDA            6
#define OLED_I2C_PORT           0
#define OLED_I2C_FREQ_WRITE_HZ  1000000  /* 1.0 MHz Fast-Mode Plus I2C Overclock */
#define OLED_I2C_FREQ_READ_HZ   400000
#define OLED_PANEL_WIDTH_PX     128
#define OLED_PANEL_HEIGHT_PX    64
#define OLED_DEFAULT_BRIGHTNESS 128
#define OLED_SLEEP_BRIGHTNESS   16       /* Reduced brightness in standby to prevent OLED burn-in */

/* Camera Sensor DVP Pin Mapping (XIAO ESP32-S3 Sense) */
#define CAM_PIN_PWDN            -1
#define CAM_PIN_RESET           -1
#define CAM_PIN_XCLK            10
#define CAM_PIN_SIOD            40
#define CAM_PIN_SIOC            39

#define CAM_PIN_Y9              48
#define CAM_PIN_Y8              11
#define CAM_PIN_Y7              12
#define CAM_PIN_Y6              14
#define CAM_PIN_Y5              16
#define CAM_PIN_Y4              18
#define CAM_PIN_Y3              17
#define CAM_PIN_Y2              15
#define CAM_PIN_VSYNC           38
#define CAM_PIN_HREF            47
#define CAM_PIN_PCLK            13

#define CAM_XCLK_FREQ_HZ        10000000 /* 10 MHz XCLK */

/* --- Power and Dynamic Frequency Scaling (DFS) Configuration --- */
#define CPU_FREQ_ACTIVE_MHZ     240      /* Full compute frequency for vision AI and MJPEG streaming */
#define CPU_FREQ_SLEEP_MHZ      80       /* Minimum frequency keeping Wi-Fi stack stable */
#define ACTIVE_STATE_TIMEOUT_MS 5000     /* Time with no target before transitioning to standby */

/* --- Real-Time Execution Budgets --- */
#define TARGET_FPS_ACTIVE       60.0f
#define TARGET_FPS_SLEEP        30.0f
#define FRAME_BUDGET_ACTIVE_US  16666    /* 16.66 ms frame period (60 FPS) */
#define FRAME_BUDGET_SLEEP_US   33333    /* 33.33 ms frame period (30 FPS) */

/* --- Biomechanical Dynamic Modeling Parameters --- */
#define GAZE_NATURAL_FREQUENCY_RAD_S  38.0f    /* Ocular natural frequency omega_n */
#define GAZE_DAMPING_RATIO            1.00f    /* Critical damping zeta */
#define GAZE_GAIN_X                   1.75f    /* Horizontal tracking gain */
#define GAZE_GAIN_Y                   1.45f    /* Vertical tracking gain */
#define GAZE_DEADBAND_RADIUS_PX       1.35f    /* Sub-pixel noise gate threshold */
#define SACCADE_D0_MS                 20.0f    /* Flash & Hogan base duration */
#define SACCADE_K_MS_PER_DEG          2.5f     /* Flash & Hogan duration slope */
#define PX_TO_DEG_FACTOR              1.714f   /* 30 deg / 17.5 px */

/* --- Affective Dynamics Parameters --- */
#define AFFECTIVE_TAU_VALENCE_S       6.0f     /* Langevin valence relaxation constant */
#define AFFECTIVE_TAU_AROUSAL_S       4.5f     /* Langevin arousal relaxation constant */

/* --- Network Streaming Buffers --- */
#define STREAM_BUFFER_SIZE_BYTES      (64 * 1024)
#define HTTP_PORT_WEB_CONTROL         80
#define HTTP_PORT_STREAM              81

/* --- Stream Client Limits --- */
#define MAX_STREAM_CLIENTS        2        /* Maximum concurrent MJPEG stream clients */

/* --- Firmware Version --- */
#define KORE_FIRMWARE_VERSION     "2.5.0"

#ifdef __cplusplus
}
#endif

#endif /* KORE_CONFIG_H */
