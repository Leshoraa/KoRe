/**
 * @file test_episodic_memory.cpp
 * @brief Host unit test suite for Affective & Episodic Vector Memory Embedding Engine.
 */

#include "include/kore_memory.h"
#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>

int main() {
    std::cout << "[TEST] Running Affective & Episodic Vector Memory test suite..." << std::endl;

    initMemoryEngine();
    clearEpisodicMemory();

    // Test 1: Empty Memory Bank Baseline
    assert(getEpisodicMemoryCount() == 0);
    float state_zero[8] = {0.0f, 0.0f, 0.5f, 0.5f, 0.1f, 0.1f, 0.2f, 0.3f};
    EpisodicRecallResult recall_empty = queryMemoryResonance(state_zero);
    assert(recall_empty.resonance_score == 0.0f);
    assert(recall_empty.top_k_count == 0);
    for (int i = 0; i < 8; i++) {
        assert(std::fabs(recall_empty.memory_logits_delta[i]) < 1e-6f);
    }
    std::cout << "[PASS] Empty memory bank returns zero resonance and zero delta logits." << std::endl;

    // Test 2: Ingestion and Perfect Cosine Similarity Recall
    float happy_state[8] = { 0.8f, 0.6f, 0.4f, 0.9f, 0.0f, 0.1f, 0.3f, 0.7f };
    recordEpisodicMemory(happy_state, EXPR_JOY, 0.90f, 0.8f);
    assert(getEpisodicMemoryCount() == 1);

    EpisodicRecallResult recall_joy = queryMemoryResonance(happy_state);
    assert(recall_joy.resonance_score > 0.98f);
    assert(recall_joy.dominant_memory_expr == EXPR_JOY);
    assert(recall_joy.top_k_count == 1);
    assert(recall_joy.memory_logits_delta[EXPR_JOY] > 0.50f);
    assert(recall_joy.memory_logits_delta[EXPR_SAD] < 0.0f);
    std::cout << "[PASS] Happy episode ingestion yields near 1.0 cosine similarity and boosts JOY logits." << std::endl;

    // Test 3: Multiple Distinct Memories & Orthogonal Queries
    float shock_state[8] = { -0.7f, 0.95f, 0.9f, 0.1f, 0.0f, 0.2f, 0.1f, 0.95f };
    recordEpisodicMemory(shock_state, EXPR_SHOCK, 0.95f, -0.7f);
    assert(getEpisodicMemoryCount() == 2);

    // Querying with shock-like state should resonate with SHOCK, not JOY
    float query_shock[8] = { -0.6f, 0.90f, 0.85f, 0.15f, 0.0f, 0.2f, 0.1f, 0.90f };
    EpisodicRecallResult recall_shock = queryMemoryResonance(query_shock);
    assert(recall_shock.resonance_score > 0.90f);
    assert(recall_shock.dominant_memory_expr == EXPR_SHOCK);
    assert(recall_shock.memory_logits_delta[EXPR_SHOCK] > 0.40f);
    std::cout << "[PASS] Multi-memory separation and shock resonance successfully validated." << std::endl;

    // Test 4: Memory Ring Buffer Capacity & Eviction Stability
    clearEpisodicMemory();
    for (int i = 0; i < EPISODIC_MEMORY_CAPACITY; i++) {
        float dummy_state[8] = { (float)i * 0.02f, 0.2f, 0.3f, 0.4f, 0.1f, 0.1f, 0.2f, 0.5f };
        recordEpisodicMemory(dummy_state, (Expression)(i % 8), 0.50f, 0.0f);
    }
    assert(getEpisodicMemoryCount() == EPISODIC_MEMORY_CAPACITY);

    // Inserting 33rd element should evict and maintain capacity without overflow
    float overflow_state[8] = { 0.9f, 0.9f, 0.9f, 0.9f, 0.1f, 0.1f, 0.9f, 0.9f };
    recordEpisodicMemory(overflow_state, EXPR_JOY, 1.0f, 0.9f);
    assert(getEpisodicMemoryCount() == EPISODIC_MEMORY_CAPACITY);

    EpisodicRecallResult recall_overflow = queryMemoryResonance(overflow_state);
    assert(recall_overflow.resonance_score > 0.98f);
    std::cout << "[PASS] Memory capacity limit (32 slots) and eviction maintain system stability." << std::endl;

    // Test 5: Clamping of Logit Deltas
    for (int k = 0; k < 8; k++) {
        assert(recall_overflow.memory_logits_delta[k] >= -1.5f);
        assert(recall_overflow.memory_logits_delta[k] <= 1.5f);
    }
    std::cout << "[PASS] Associative memory logit deltas strictly clamped within [-1.5, +1.5]." << std::endl;

    std::cout << "[SUCCESS] All Affective & Episodic Vector Memory tests passed successfully!" << std::endl;
    return 0;
}
