/**
 * @file sample_motion_frames.h
 * @brief Synthetic downscaled frame test vectors for vision algorithm benchmarking.
 * @details Four test scenarios covering rest, skin detection, shadow, and glare conditions.
 */

#ifndef SAMPLE_MOTION_FRAMES_H
#define SAMPLE_MOTION_FRAMES_H

#include <stdint.h>

/**
 * @brief 40x30 luminance frame: uniform background at ~100 luminance (no target).
 */
static const uint8_t SYNTHETIC_FRAME_40X30_REST[1200] = {
    [0 ... 1199] = 100
};

/**
 * @brief 40x30 luminance frame: skin-colored blob at center (rows 10-20, cols 15-25).
 * Background ~95 luminance, skin region ~130 luminance.
 */
static uint8_t SYNTHETIC_FRAME_40X30_CENTER_SKIN[1200];

static inline void init_center_skin_frame(void) {
    for (int i = 0; i < 1200; i++) {
        int y = i / 40;
        int x = i % 40;
        if (y >= 10 && y <= 20 && x >= 15 && x <= 25) {
            SYNTHETIC_FRAME_40X30_CENTER_SKIN[i] = 130;  /* Skin-like luminance */
        } else {
            SYNTHETIC_FRAME_40X30_CENTER_SKIN[i] = 95;   /* Background */
        }
    }
}

/**
 * @brief 40x30 luminance frame: dark/shadow scenario, uniform ~40 luminance.
 */
static const uint8_t SYNTHETIC_FRAME_40X30_DARK[1200] = {
    [0 ... 1199] = 40
};

/**
 * @brief 40x30 luminance frame: over-exposed/glare scenario, uniform ~220 luminance.
 */
static const uint8_t SYNTHETIC_FRAME_40X30_BRIGHT[1200] = {
    [0 ... 1199] = 220
};

#endif /* SAMPLE_MOTION_FRAMES_H */
