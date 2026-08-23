/**
 * @file test_kinematics_feedforward.cpp
 * @brief Unit test suite for predictive velocity feedforward, Ornstein-Uhlenbeck drift, and minimum-jerk kinematics.
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>

/* Standalone Horner polynomial evaluation */
static float eval_minimum_jerk_spline(float p) {
    if (p <= 0.0f) return 0.0f;
    if (p >= 1.0f) return 1.0f;
    return p * p * p * (10.0f + p * (-15.0f + 6.0f * p));
}

/* Mass-spring-damper gaze tracking with velocity feedforward simulation */
struct GazeSim {
    float gaze_x = 0.0f;
    float gaze_vx = 0.0f;
    float omega_n = 56.0f;
    float lead_time = 0.035f;

    void step(float target_pos, float target_vel, float dt) {
        float effective_target = target_pos + lead_time * target_vel;
        float decay = std::exp(-omega_n * dt);
        float w_dt = omega_n * dt;

        float err = gaze_x - effective_target;
        float err_next = decay * ((1.0f + w_dt) * err + dt * gaze_vx);
        float v_next = decay * (-omega_n * w_dt * err + (1.0f - w_dt) * gaze_vx);

        gaze_x = effective_target + err_next;
        gaze_vx = v_next;
    }
};

/* Ornstein-Uhlenbeck stochastic drift simulation */
struct OUDriftSim {
    float drift = 0.0f;
    float theta = 3.0f;
    float sigma = 0.20f;

    void step(float dt, float rand_uniform) {
        float decay = std::exp(-theta * dt);
        float vol = sigma * std::sqrt(std::max(0.0f, (1.0f - decay * decay) / (2.0f * theta)));
        drift = drift * decay + rand_uniform * vol;
    }
};

int main() {
    std::cout << "[TEST] Running kinematics velocity feedforward and OU drift validation tests..." << std::endl;

    // Test 1: Minimum-Jerk Spline properties
    assert(std::fabs(eval_minimum_jerk_spline(0.0f) - 0.0f) < 1e-6f);
    assert(std::fabs(eval_minimum_jerk_spline(1.0f) - 1.0f) < 1e-6f);
    assert(std::fabs(eval_minimum_jerk_spline(0.5f) - 0.5f) < 1e-6f);

    float prev_s = 0.0f;
    for (int i = 1; i <= 100; ++i) {
        float p = (float)i / 100.0f;
        float s = eval_minimum_jerk_spline(p);
        assert(s >= prev_s);
        assert(!std::isnan(s) && !std::isinf(s));
        prev_s = s;
    }
    std::cout << "[PASS] 5th-order minimum-jerk spline passed monotonicity and boundary checks." << std::endl;

    // Test 2: Predictive Velocity Feedforward eliminates steady-state ramp lag
    GazeSim sim_with_ff;
    GazeSim sim_without_ff;
    sim_without_ff.lead_time = 0.0f;

    float dt = 0.016666f;
    float target_velocity = 50.0f; // 50 px/s constant target velocity ramp

    for (int step = 0; step < 200; ++step) {
        float target_pos = target_velocity * (step * dt);
        sim_with_ff.step(target_pos, target_velocity, dt);
        sim_without_ff.step(target_pos, 0.0f, dt);
    }

    float final_pos = target_velocity * (200 * dt);
    float lag_with_ff = std::fabs(final_pos - sim_with_ff.gaze_x);
    float lag_without_ff = std::fabs(final_pos - sim_without_ff.gaze_x);

    std::cout << "[INFO] Steady-state lag WITHOUT feedforward : " << lag_without_ff << " px" << std::endl;
    std::cout << "[INFO] Steady-state lag WITH feedforward    : " << lag_with_ff << " px" << std::endl;

    assert(lag_with_ff < lag_without_ff);
    assert(lag_with_ff < 1.0f); // Steady-state tracking error under 1.0 pixel with velocity feedforward
    std::cout << "[PASS] Predictive velocity feedforward significantly reduced steady-state tracking lag." << std::endl;

    // Test 3: Ornstein-Uhlenbeck drift variance boundedness
    OUDriftSim ou_sim;
    std::vector<float> samples;
    float max_drift = 0.0f;

    for (int step = 0; step < 5000; ++step) {
        // Deterministic pseudo-random sequence within [-1.0, 1.0]
        float rand_val = (float)((step * 17) % 2001 - 1000) * 0.001f;
        ou_sim.step(dt, rand_val);
        samples.push_back(ou_sim.drift);
        if (std::fabs(ou_sim.drift) > max_drift) {
            max_drift = std::fabs(ou_sim.drift);
        }
    }

    float sum = 0.0f;
    for (float s : samples) sum += s;
    float mean = sum / samples.size();

    float sum_sq = 0.0f;
    for (float s : samples) sum_sq += (s - mean) * (s - mean);
    float variance = sum_sq / samples.size();

    std::cout << "[INFO] OU Drift mean: " << mean << ", variance: " << variance << ", max abs: " << max_drift << std::endl;
    assert(std::fabs(mean) < 0.10f); // Mean-reverts around zero
    assert(variance < 0.05f);        // Variance is mathematically bounded
    assert(max_drift < 0.50f);       // Never blows up into large drift

    std::cout << "[PASS] Ornstein-Uhlenbeck stochastic drift satisfies mean-reverting bounded variance requirements." << std::endl;

    std::cout << "[SUCCESS] All kinematics and dynamic model tests passed." << std::endl;
    return 0;
}
