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

/* Weather Configuration & Open-Meteo Integration */
#define WEATHER_DEFAULT_CITY        "Jakarta"
#define WEATHER_DEFAULT_LAT         -6.2088f
#define WEATHER_DEFAULT_LON         106.8456f
#define AMBIENT_POPUP_DURATION_MIN_MS 3500     /* 3.5 seconds brief glance on OLED */
#define AMBIENT_POPUP_DURATION_MAX_MS 5500     /* 5.5 seconds maximum glance on OLED */
#define AMBIENT_INTERVAL_MIN_MS       30000    /* 30 seconds minimum between spontaneous glances */
#define AMBIENT_INTERVAL_MAX_MS       75000    /* 75 seconds (~1.25 mins) maximum between glances */
#define WEATHER_POPUP_DURATION_MS     5000     /* 5 seconds manual trigger display on OLED */
#define WEATHER_FETCH_INTERVAL_MS   1800000  /* 30 minutes periodic sync */

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
#define GAZE_NATURAL_FREQUENCY_RAD_S  32.0f    /* Ocular natural frequency omega_n (viscoelastic soft tissue) */
#define GAZE_DAMPING_RATIO            0.72f    /* Underdamped compliance zeta: optimal 4.3% biological bounce */
#define BOUNCE_SQUASH_STRETCH_GAIN    0.14f    /* Volume-conserving biological squash/stretch amplitude */
#define GLISSADE_REBOUND_GAIN         0.045f   /* Saccadic landing ocular micro-glissade rebound */
#define GAZE_GAIN_X                   1.75f    /* Horizontal tracking gain */
#define GAZE_GAIN_Y                   1.45f    /* Vertical tracking gain */
#define GAZE_DEADBAND_RADIUS_PX       1.35f    /* Sub-pixel noise gate threshold */
#define SACCADE_D0_MS                 20.0f    /* Flash & Hogan base duration */
#define SACCADE_K_MS_PER_DEG          2.5f     /* Flash & Hogan duration slope */
#define PX_TO_DEG_FACTOR              1.714f   /* 30 deg / 17.5 px */

/* --- Affective Dynamics Parameters --- */
#define AFFECTIVE_TAU_VALENCE_S       6.0f     /* Langevin valence relaxation constant */
#define AFFECTIVE_TAU_AROUSAL_S       4.5f     /* Langevin arousal relaxation constant */

/* --- Vision Anti-False-Positive & Temporal Habituation --- */
#define VISION_HABITUATION_FRAMES     45       /* Number of static frames (~1.5-2.0s) before static cream surfaces are habituated */
#define VISION_STATIC_DELTA_THRESH    3.5f     /* Maximum luminance delta considered stationary */
#define VISION_TEXTURE_MIN_GRADIENT   2.0f     /* Minimum local edge texture variance for skin verification */
#define VISION_MIN_ACQUIRE_SKIN_PX    6        /* Minimum dynamic skin pixels to acquire a new candidate */
#define VISION_MIN_ACQUIRE_ENERGY     20.0f    /* Minimum dynamic energy required to lock on initial acquisition */
#define VISION_MIN_R_G_DIFF           12       /* Minimum Red over Green margin (hemoglobin absorption vs yellow/mustard fabric) */
#define VISION_CLUSTER_HABITUATION_FRAMES  90  /* Low-likelihood frames (~3s at 30fps) before the whole cluster is treated as inanimate */
#define VISION_CLUSTER_CENTROID_RESET_PX   15.0f /* Centroid jump that means a new object, not the habituated blob */
#define HUMAN_CLUSTER_SUPPRESS_THRESH 0.30f    /* Composite below this with persistent skin is a false-positive cluster */

/* --- Network Streaming Buffers --- */
#define STREAM_BUFFER_SIZE_BYTES      (64 * 1024)
#define HTTP_PORT_WEB_CONTROL         80
#define HTTP_PORT_STREAM              81

/* --- Stream Client Limits --- */
#define MAX_STREAM_CLIENTS        2        /* Maximum concurrent MJPEG stream clients */

/* --- Human Likelihood Composite Scoring Weights ---
 * Weighted fusion of 4 independent discrimination signals to estimate the probability
 * that a tracked target is a real human rather than a skin-colored inanimate object.
 * Weights sum to 1.0; tuned empirically against false-positive rejection performance. */
#define HUMAN_W_SKIN_CONSISTENCY    0.30f  /* Temporal stability of skin cluster area (low variance = static object) */
#define HUMAN_W_MOTION_PATTERN      0.25f  /* Autocorrelation of velocity: smooth human motion vs random noise */
#define HUMAN_W_SPATIAL_COHERENCE   0.25f  /* Cluster compactness ratio: face/hand (compact) vs scattered pixels */
#define HUMAN_W_TEMPORAL_PERSIST    0.20f  /* Saturating duration of continuous tracking above quality threshold */
#define HUMAN_LIKELIHOOD_THRESHOLD  0.55f  /* Minimum composite score to gate bonding growth and social drive */
#define HUMAN_TEMPORAL_TAU_HALF_S   3.0f   /* Half-saturation time constant for temporal persistence sigmoid */

/* --- Personality Trait Defaults ---
 * Permanent character disposition stored in NVS. These define KoRe's behavioral baseline
 * and modulate gaze kinematics, Langevin diffusion, blink timing, and drive sensitivity. */
#define PERSONALITY_DEFAULT_BOLDNESS      0.55f  /* [0,1]: Shy/avoidant (0) to confident/direct gaze holder (1) */
#define PERSONALITY_DEFAULT_VOLATILITY    0.40f  /* [0,1]: Emotionally stable (0) to moody/reactive (1) */
#define PERSONALITY_DEFAULT_PLAYFULNESS   0.65f  /* [0,1]: Serious/stoic (0) to mischievous/teasing (1) */
#define PERSONALITY_DEFAULT_ATTACHMENT    0.50f  /* [0,1]: Independent/aloof (0) to clingy/social (1) */

/* --- Circadian Rhythm Configuration ---
 * Internal energy cycle that creates natural behavioral variation over time.
 * Uses millis() relative uptime; not wall-clock dependent. */
#define CIRCADIAN_CYCLE_PERIOD_MS   21600000  /* 6 hours per full energy cycle (rising -> peak -> declining -> rest) */

/* --- Firmware Version --- */
#define KORE_FIRMWARE_VERSION     "2.6.0"

#ifdef __cplusplus
}
#endif

#endif /* KORE_CONFIG_H */
