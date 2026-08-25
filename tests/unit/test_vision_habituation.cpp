/**
 * @file test_vision_habituation.cpp
 * @brief Unit test suite for Vision Pipeline Temporal Habituation & Cream Surface Rejection.
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <cstring>
#include <cstdint>

#define VISION_HABITUATION_FRAMES     45
#define VISION_STATIC_DELTA_THRESH    3.5f
#define VISION_TEXTURE_MIN_GRADIENT   2.0f
#define VISION_MIN_ACQUIRE_SKIN_PX    6
#define VISION_MIN_ACQUIRE_ENERGY     20.0f
#define VISION_CLUSTER_HABITUATION_FRAMES  90
#define VISION_CLUSTER_CENTROID_RESET_PX   15.0f
#define HUMAN_CLUSTER_SUPPRESS_THRESH 0.30f

struct VisionHabituationSim {
    uint8_t static_skin_age[1200];
    uint8_t mhi_buf[1200];
    bool skin_mask[1200];

    VisionHabituationSim() {
        reset();
    }

    void reset() {
        std::memset(static_skin_age, 0, sizeof(static_skin_age));
        std::memset(mhi_buf, 0, sizeof(mhi_buf));
        std::memset(skin_mask, 0, sizeof(skin_mask));
    }

    int process_frame(bool is_static_cream_wall, float local_delta, float texture_val) {
        int skin_pixel_count = 0;
        for (int idx = 0; idx < 1200; idx++) {
            bool raw_skin = is_static_cream_wall;

            if (raw_skin) {
                if (local_delta <= VISION_STATIC_DELTA_THRESH && mhi_buf[idx] == 0) {
                    if (static_skin_age[idx] < 255) static_skin_age[idx]++;
                } else {
                    static_skin_age[idx] = 0;
                }

                bool is_habituated = (static_skin_age[idx] >= VISION_HABITUATION_FRAMES);
                bool is_flat_surface = (texture_val < VISION_TEXTURE_MIN_GRADIENT && static_skin_age[idx] >= 10);

                if (!is_habituated && !is_flat_surface) {
                    skin_mask[idx] = true;
                    skin_pixel_count++;
                } else {
                    skin_mask[idx] = false;
                }
            } else {
                static_skin_age[idx] = 0;
                skin_mask[idx] = false;
            }
        }
        return skin_pixel_count;
    }
};

int main() {
    std::cout << "[TEST] Running Vision Pipeline Temporal Habituation & Cream Rejection suite..." << std::endl;

    VisionHabituationSim sim;

    /* Test 1: Planar static surface suppression via texture variance */
    int initial_skins = sim.process_frame(true, 0.5f, 1.0f);
    std::cout << "[INFO] Frame 1 static cream skin count: " << initial_skins << std::endl;
    assert(initial_skins == 1200);

    for (int f = 2; f <= 15; f++) {
        sim.process_frame(true, 0.5f, 1.0f);
    }
    int post_flat_check = sim.process_frame(true, 0.5f, 1.0f);
    std::cout << "[INFO] Frame 16 static flat cream skin count: " << post_flat_check << std::endl;
    assert(post_flat_check == 0);
    std::cout << "[PASS] Flat cream background successfully suppressed by texture variance filter." << std::endl;

    /* Test 2: Full temporal habituation cycle */
    sim.reset();
    for (int f = 1; f <= 30; f++) {
        sim.process_frame(true, 0.5f, 4.0f);
    }
    int mid_hab = sim.process_frame(true, 0.5f, 4.0f);
    assert(mid_hab > 0);

    for (int f = 32; f <= 45; f++) {
        sim.process_frame(true, 0.5f, 4.0f);
    }
    int post_hab = sim.process_frame(true, 0.5f, 4.0f);
    std::cout << "[INFO] Frame 46 habituated skin count: " << post_hab << std::endl;
    assert(post_hab == 0);
    std::cout << "[PASS] Static cream surface fully habituated and rejected after " << VISION_HABITUATION_FRAMES << " frames." << std::endl;

    /* Test 3: Motion-triggered instant de-habituation */
    int dynamic_skins = sim.process_frame(true, 8.0f, 4.0f);
    std::cout << "[INFO] Dynamic motion skin count: " << dynamic_skins << std::endl;
    assert(dynamic_skins == 1200);
    std::cout << "[PASS] Human motion immediately resets habituation and restores detection." << std::endl;

    /* Cluster-level habituation: low composite score on a spatially stable blob
     * is treated as an inanimate false lock, independent of pixel-level age. */
    struct ClusterHabituationSim {
        uint8_t low_lik_frames = 0;
        bool suppressed = false;
        float cx = 0.0f;
        float cy = 0.0f;

        static bool moved(float x, float y, float rx, float ry) {
            float dx = x - rx;
            float dy = y - ry;
            float lim = VISION_CLUSTER_CENTROID_RESET_PX;
            return (dx * dx + dy * dy) > (lim * lim);
        }

        bool tick(int skin_px, float likelihood, float x, float y) {
            /* Acquire-gate equivalent: a spatial jump means a different object. */
            if (suppressed && moved(x, y, cx, cy)) {
                suppressed = false;
                low_lik_frames = 0;
                cx = x;
                cy = y;
            }

            if (skin_px < VISION_MIN_ACQUIRE_SKIN_PX) {
                low_lik_frames = 0;
                suppressed = false;
            } else if (likelihood >= HUMAN_CLUSTER_SUPPRESS_THRESH) {
                low_lik_frames = 0;
                suppressed = false;
                cx = x;
                cy = y;
            } else if (!suppressed) {
                if (low_lik_frames == 0 || moved(x, y, cx, cy)) {
                    low_lik_frames = 1;
                    cx = x;
                    cy = y;
                } else if (low_lik_frames < 255) {
                    low_lik_frames++;
                    if (low_lik_frames >= VISION_CLUSTER_HABITUATION_FRAMES) {
                        suppressed = true;
                    }
                }
            }
            return suppressed;
        }
    };

    ClusterHabituationSim cluster;
    for (int f = 0; f < VISION_CLUSTER_HABITUATION_FRAMES - 1; f++) {
        assert(!cluster.tick(12, 0.12f, 320.0f, 240.0f));
    }
    assert(cluster.tick(12, 0.12f, 320.0f, 240.0f));
    std::cout << "[PASS] Persistent low-likelihood cluster suppressed after "
              << VISION_CLUSTER_HABITUATION_FRAMES << " frames." << std::endl;

    /* Same blob stays suppressed; a >15px centroid jump is a new object. */
    assert(cluster.tick(12, 0.12f, 322.0f, 241.0f));
    cluster.tick(12, 0.12f, 340.0f, 240.0f);
    assert(!cluster.suppressed);
    std::cout << "[PASS] Cluster habituation resets when centroid shifts beyond "
              << VISION_CLUSTER_CENTROID_RESET_PX << "px." << std::endl;

    ClusterHabituationSim rising;
    for (int f = 0; f < 40; f++) {
        rising.tick(20, 0.10f, 200.0f, 180.0f);
    }
    assert(!rising.tick(20, 0.62f, 200.0f, 180.0f));
    assert(rising.low_lik_frames == 0);
    std::cout << "[PASS] Rising human likelihood clears cluster habituation before suppress." << std::endl;

    std::cout << "[SUCCESS] All Vision Temporal Habituation tests passed!" << std::endl;
    return 0;
}
