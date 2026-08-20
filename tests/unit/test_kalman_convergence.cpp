/**
 * @file test_kalman_convergence.cpp
 * @brief Unit test suite for 1D and 2D discrete Kalman filter convergence.
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include "include/kore_kalman.h"

// Define Kalman functions directly or link with kore_kalman.cpp
int main() {
    std::cout << "[TEST] Running discrete Kalman filter convergence tests..." << std::endl;

    KalmanFilter1D kf;
    kf1d_init(&kf, 0.0f);

    float true_p = 100.0f;
    float dt = 0.033f;
    float q = 450.0f;
    float R = 25.0f;

    // Feed 50 noisy measurements centered around 100.0f
    for (int step = 0; step < 50; ++step) {
        kf1d_predict(&kf, dt, q);
        float noise = (float)((step % 5) - 2) * 0.5f;
        kf1d_update(&kf, true_p + noise, R);
    }

    // Residual error should be less than 1.0 pixel
    float residual = std::fabs(kf.p - true_p);
    std::cout << "[INFO] Final filtered position: " << kf.p << " (residual: " << residual << " px)" << std::endl;
    assert(residual < 1.0f);
    assert(!std::isnan(kf.p) && !std::isinf(kf.p));

    std::cout << "[PASS] Discrete Kalman filter convergence tests passed." << std::endl;
    return 0;
}
