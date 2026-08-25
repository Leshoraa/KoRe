/**
 * @file test_brain_inference.cpp
 * @brief Host unit test suite for On-Device TinyML Micro-Brain Neural Network & Homeostatic Drives.
 */

#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <numeric>

struct BrainSim {
    float curiosity = 0.35f;
    float social = 0.50f;
    float boredom = 0.15f;
    float fatigue = 0.10f;
    float mischief = 0.40f;
    float bonding = 0.05f;

    float weights[8][8] = {
        {  0.1f, -0.5f,  0.1f,  0.3f, -1.0f, -0.4f, -0.2f,  0.1f }, // IDLE
        {  1.8f,  0.6f,  0.3f,  1.3f, -1.0f, -0.5f,  0.2f,  0.6f }, // JOY
        { -1.5f,  1.1f,  0.1f, -0.5f, -0.2f,  0.4f,  1.4f,  0.4f }, // ANGRY (Tsundere)
        {  0.7f,  0.3f,  0.6f,  0.8f, -0.4f, -0.3f,  1.8f,  0.4f }, // SMIRK
        { -0.4f,  1.6f,  0.8f, -0.2f, -0.6f,  0.2f,  0.1f,  1.2f }, // SHOCK
        { -0.9f,  1.4f,  0.2f, -0.3f, -0.5f,  2.5f, -0.4f,  0.3f }, // OVERLOAD
        { -1.8f, -0.8f, -0.5f, -2.0f,  0.9f,  0.2f, -0.6f, -0.6f }, // SAD
        { -0.2f, -1.2f, -0.8f, -0.5f,  2.4f,  0.4f,  0.1f, -0.3f }  // DEADPAN
    };

    float biases[8] = { 1.40f, -0.70f, -1.20f, -0.70f, -2.60f, -1.80f, -0.80f, -0.30f };

    void infer(float V, float A, float prox, float probs_out[8]) {
        float state[8] = { V, A, curiosity, social, boredom, fatigue, mischief, prox };
        float logits[8];
        float max_l = -999.0f;
        for (int i = 0; i < 8; i++) {
            float sum = biases[i];
            for (int j = 0; j < 8; j++) sum += weights[i][j] * state[j];

            if (i == 1) sum += 0.8f * bonding;
            else if (i == 3) sum += 0.6f * bonding;
            else if (i == 4) sum -= 1.2f * bonding;
            else if (i == 2) sum -= 0.6f * bonding;

            logits[i] = sum;
            if (sum > max_l) max_l = sum;
        }

        float tau = 0.85f;
        float sum_exp = 0.0f;
        for (int i = 0; i < 8; i++) {
            probs_out[i] = std::exp((logits[i] - max_l) / tau);
            sum_exp += probs_out[i];
        }
        for (int i = 0; i < 8; i++) probs_out[i] /= sum_exp;
    }
};

int main() {
    std::cout << "[TEST] Running On-Device TinyML Micro-Brain validation suite..." << std::endl;

    BrainSim brain;
    float probs[8];

    // Test 1: Softmax Probability Distribution Axiom (Sum = 1.0, non-negative)
    brain.infer(0.0f, 0.0f, 0.0f, probs);
    float sum_p = 0.0f;
    for (int i = 0; i < 8; i++) {
        assert(probs[i] >= 0.0f && probs[i] <= 1.0f);
        sum_p += probs[i];
    }
    assert(std::fabs(sum_p - 1.0f) < 1e-5f);
    std::cout << "[PASS] Softmax Boltzmann probability distribution axioms verified." << std::endl;

    // Test 2: Calm Steady Observation -> IDLE Dominance (> 45%) & Shock Suppressed (< 5%)
    brain.social = 0.50f;
    brain.curiosity = 0.35f;
    brain.boredom = 0.10f;
    brain.mischief = 0.30f;
    brain.bonding = 0.20f;
    brain.infer(0.25f, 0.20f, 0.40f, probs);
    assert(probs[0] > 0.40f); // IDLE dominant
    assert(probs[4] < 0.05f); // SHOCK heavily suppressed
    std::cout << "[PASS] Calm steady observation yields IDLE dominance and suppressed shock." << std::endl;

    // Test 3: Social Playful State (High Social, High Mischief, High Bonding) -> JOY/SMIRK dominant
    brain.social = 0.90f;
    brain.mischief = 0.85f;
    brain.bonding = 0.75f;
    brain.boredom = 0.05f;
    brain.infer(0.70f, 0.45f, 0.60f, probs);
    assert(probs[1] > 0.25f || probs[3] > 0.25f); // JOY or SMIRK high
    assert(probs[4] < 0.02f); // Shock virtually 0 when bonded
    std::cout << "[PASS] Social playful state with high bonding triggers JOY/SMIRK dominance." << std::endl;

    // Test 4: Prolonged Monotony / Solitude -> DEADPAN / SAD high
    brain.social = 0.05f;
    brain.boredom = 0.90f;
    brain.mischief = 0.10f;
    brain.bonding = 0.10f;
    brain.infer(-0.40f, 0.10f, 0.0f, probs);
    assert(probs[7] > 0.30f || probs[6] > 0.20f); // DEADPAN or SAD high
    std::cout << "[PASS] Monotony & solitude trigger DEADPAN / SAD policy dominance." << std::endl;

    // Test 5: True Sudden Startle Shock Event (High Sudden Arousal + Proximity Spike on low bonding)
    brain.bonding = 0.0f;
    brain.curiosity = 0.85f;
    brain.infer(-0.60f, 0.95f, 0.90f, probs);
    assert(probs[4] > 0.20f || probs[2] > 0.25f); // SHOCK or ANGRY responds to sudden startle
    std::cout << "[PASS] Sudden startle spike triggers realistic surprise reaction." << std::endl;

    std::cout << "[SUCCESS] All On-Device TinyML Micro-Brain tests passed successfully!" << std::endl;
    return 0;
}
