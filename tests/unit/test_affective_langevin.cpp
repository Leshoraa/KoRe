/**
 * @file test_affective_langevin.cpp
 * @brief Unit test suite for 2D Russell Circumplex Langevin stochastic differential emotion model.
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <algorithm>

struct AffectiveSim {
    float valence = 0.05f;
    float arousal = 0.15f;
    float tau_v = 6.0f;
    float tau_a = 4.5f;

    void step(float target_v, float target_a, float dt, float noise_norm_v, float noise_norm_a) {
        float sigma_v = 0.035f * std::sqrt(dt);
        float sigma_a = 0.035f * std::sqrt(dt);

        float d_noise_v = noise_norm_v * sigma_v;
        float d_noise_a = noise_norm_a * sigma_a;

        valence += ((target_v - valence) / tau_v) * dt + d_noise_v;
        arousal += ((target_a - arousal) / tau_a) * dt + d_noise_a;

        valence = std::max(-1.0f, std::min(1.0f, valence));
        arousal = std::max(0.0f, std::min(1.0f, arousal));
    }
};

int main() {
    std::cout << "[TEST] Running 2D Russell Circumplex Langevin affective model validation tests..." << std::endl;

    AffectiveSim sim;
    float dt = 0.033f; // 30 FPS update rate

    // Test 1: Homeostatic convergence to active target (V=0.60, A=0.70)
    for (int step = 0; step < 800; ++step) {
        float nv = (float)((step * 13) % 2001 - 1000) * 0.001f;
        float na = (float)((step * 29) % 2001 - 1000) * 0.001f;
        sim.step(0.60f, 0.70f, dt, nv, na);
    }

    std::cout << "[INFO] Converged active state: Valence = " << sim.valence << ", Arousal = " << sim.arousal << std::endl;
    assert(std::fabs(sim.valence - 0.60f) < 0.08f);
    assert(std::fabs(sim.arousal - 0.70f) < 0.08f);
    assert(!std::isnan(sim.valence) && !std::isinf(sim.valence));
    assert(!std::isnan(sim.arousal) && !std::isinf(sim.arousal));
    std::cout << "[PASS] Continuous Langevin integration converged to active target state." << std::endl;

    // Test 2: Relaxation to resting baseline (V0=0.05, A0=0.15) upon target loss
    for (int step = 0; step < 800; ++step) {
        float nv = (float)((step * 19) % 2001 - 1000) * 0.001f;
        float na = (float)((step * 37) % 2001 - 1000) * 0.001f;
        sim.step(0.05f, 0.15f, dt, nv, na);
    }

    std::cout << "[INFO] Relaxed resting state: Valence = " << sim.valence << ", Arousal = " << sim.arousal << std::endl;
    assert(std::fabs(sim.valence - 0.05f) < 0.10f);
    assert(std::fabs(sim.arousal - 0.15f) < 0.10f);
    assert(sim.valence >= -1.0f && sim.valence <= 1.0f);
    assert(sim.arousal >= 0.0f && sim.arousal <= 1.0f);
    std::cout << "[PASS] Affective homeostatic relaxation verified." << std::endl;

    std::cout << "[SUCCESS] All affective Langevin model tests passed." << std::endl;
    return 0;
}
