/**
 * @file test_minimum_jerk.cpp
 * @brief Unit test suite for 5th-order minimum-jerk trajectory polynomial.
 */

#include <iostream>
#include <cassert>
#include <cmath>

static float eval_minimum_jerk_spline(float p) {
    if (p <= 0.0f) return 0.0f;
    if (p >= 1.0f) return 1.0f;
    return p * p * p * (10.0f + p * (-15.0f + 6.0f * p));
}

int main() {
    std::cout << "[TEST] Running minimum-jerk boundary and smoothness tests..." << std::endl;

    // Test 1: Boundary values
    assert(std::fabs(eval_minimum_jerk_spline(0.0f) - 0.0f) < 1e-6f);
    assert(std::fabs(eval_minimum_jerk_spline(1.0f) - 1.0f) < 1e-6f);
    assert(std::fabs(eval_minimum_jerk_spline(0.5f) - 0.5f) < 1e-6f);

    // Test 2: Monotonic increase
    float prev = 0.0f;
    for (int i = 1; i <= 100; ++i) {
        float p = (float)i / 100.0f;
        float val = eval_minimum_jerk_spline(p);
        assert(val >= prev);
        assert(val >= 0.0f && val <= 1.0f);
        prev = val;
    }

    std::cout << "[PASS] 5th-order minimum-jerk polynomial tests passed." << std::endl;
    return 0;
}
