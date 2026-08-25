/**
 * @file kore_types.h
 * @brief Common data structures, enumerations, and cross-core state declarations.
 */

#ifndef KORE_TYPES_H
#define KORE_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#else
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
typedef void* SemaphoreHandle_t;
#define portENTER_CRITICAL(m) ((void)0)
#define portEXIT_CRITICAL(m) ((void)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum Expression
 * @brief Discrete 2D facial emotional expressions rendered on the OLED display.
 */
typedef enum {
    EXPR_IDLE = 0,
    EXPR_JOY,
    EXPR_ANGRY,
    EXPR_SMIRK,
    EXPR_SHOCK,
    EXPR_OVERLOAD,
    EXPR_SAD,
    EXPR_DEADPAN
} Expression;

/**
 * @enum BlinkState
 * @brief Non-blocking state machine states for eyelid blinking animations.
 */
typedef enum {
    BLINK_IDLE_STATE = 0,
    BLINK_CLOSING_STATE,
    BLINK_OPENING_STATE
} BlinkState;

/**
 * @enum ReconState
 * @brief Intermittent reconnaissance duty cycle operational states.
 */
typedef enum {
    STATE_ACTIVE = 0,       /* Full 30 FPS vision tracking @ 240 MHz CPU */
    STATE_SLEEP_RECON,      /* Camera in software standby, CPU @ 80 MHz, random saccades */
    STATE_SAMPLING          /* High-speed verification pulse @ 240 MHz CPU */
} ReconState;

/**
 * @struct TrackTarget
 * @brief Primary target tracking state passed across FreeRTOS cores.
 * @note Access must be protected with target_mutex spinlock.
 */
typedef struct {
    bool detected;
    int x;
    int y;
    int w;
    int h;
    int cx;
    int cy;
    float error_x;
    float error_y;
    float confidence;
    float human_likelihood;  /* Composite multi-signal human probability [0.0, 1.0] fusing skin consistency,
                              * motion pattern, spatial coherence, and temporal persistence. Used by the
                              * brain engine to gate bonding growth and social drive accumulation. */
    float total_energy;
    float vx;
    float vy;
    float proximity;        /* Normalized proximity Z in range [0.0 (far), 1.0 (near)] */
    uint32_t last_seen_ms;
} TrackTarget;

/**
 * @struct ObjectCandidate
 * @brief Candidate descriptor for multi-object spatial sector tracking.
 */
typedef struct {
    bool active;
    int cx;
    int cy;
    int w;
    int h;
    float priority_score;
    float error_x;
    float error_y;
    int skin_px;
    float motion_energy;
    float proximity;
} ObjectCandidate;

/**
 * @struct WeatherInfo
 * @brief Current weather observation payload for OLED visualization and Web telemetry.
 */
typedef struct {
    float temperature;
    int humidity;
    int weather_code;
    char city[32];
    char condition[24];
    bool valid;
    uint32_t last_sync_ms;
} WeatherInfo;

#define MAX_OBJECT_CANDIDATES 3

/* Cross-Core Synchronization Handles */
extern portMUX_TYPE g_target_mutex;
extern portMUX_TYPE g_stream_mutex;
extern portMUX_TYPE g_weather_mutex;
extern SemaphoreHandle_t g_frame_sem;

/* Global Cross-Core State Variables */
extern TrackTarget g_current_target;
extern ObjectCandidate g_object_candidates[MAX_OBJECT_CANDIDATES];
extern int g_num_candidates;
extern int g_inspected_candidate_idx;
extern volatile ReconState g_recon_state;
extern volatile float g_fps_ai;
extern volatile float g_fps_stream;
extern volatile int g_stream_clients;
extern volatile uint32_t g_last_web_activity_ms;
extern uint8_t* g_latest_jpeg_buf;
extern size_t g_latest_jpeg_len;
extern volatile bool g_camera_init_ok;
extern volatile uint8_t g_oled_brightness;
extern WeatherInfo g_weather_info;

#ifdef __cplusplus
}
#endif

#endif /* KORE_TYPES_H */
