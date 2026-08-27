/**
 * @file kore_memory.cpp
 * @brief Affective & Episodic Vector Memory Embedding Engine implementation.
 */

#include "include/kore_memory.h"
#include "include/kore_types.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <Preferences.h>
#else
#include <chrono>
static uint32_t s_mock_uptime_s = 0;
static inline uint32_t get_host_uptime_s() {
    return s_mock_uptime_s;
}
#endif

static inline uint32_t getCurrentUptimeSec(void) {
#ifdef ARDUINO
    return (uint32_t)(millis() / 1000);
#else
    return get_host_uptime_s();
#endif
}

/* In-Memory Memory Bank (Allocated statically, ~1.5 KB) */
static EpisodicMemoryEntry s_memory_entries[EPISODIC_MEMORY_CAPACITY];
static uint8_t s_memory_count = 0;
static uint8_t s_memory_head = 0;
static float s_latest_resonance = 0.0f;
static Expression s_latest_recalled_expr = EXPR_IDLE;

/* Associative Expression Reinforcement Matrix [Past Expr 0..7][Logit Target 0..7] */
static const float s_associative_matrix[8][8] = {
    /* 0: IDLE recalled     -> IDLE,   JOY,  ANGRY, SMIRK, SHOCK, OVERLOAD,  SAD, DEADPAN */
    {                          0.4f,  0.1f, -0.2f,  0.1f, -0.5f,  -0.4f,   -0.2f,  0.1f },
    /* 1: JOY recalled      -> Reinforce JOY & SMIRK, suppress negative */
    {                         -0.2f,  1.4f, -0.6f,  0.6f, -0.8f,  -0.5f,   -1.0f, -0.6f },
    /* 2: ANGRY recalled    -> Reinforce ANGRY & Tsundere */
    {                         -0.1f, -0.6f,  1.2f,  0.2f,  0.3f,   0.2f,   -0.2f,  0.2f },
    /* 3: SMIRK recalled    -> Reinforce mischievous SMIRK & JOY */
    {                          0.1f,  0.6f, -0.1f,  1.3f, -0.4f,  -0.3f,   -0.8f, -0.4f },
    /* 4: SHOCK recalled    -> Vigilance / cautious startle response */
    {                         -0.4f, -0.6f,  0.5f, -0.3f,  1.1f,   0.6f,    0.1f,  0.0f },
    /* 5: OVERLOAD recalled -> High fatigue / sensitivity to chaos */
    {                         -0.5f, -0.6f,  0.4f, -0.4f,  0.6f,   1.2f,    0.2f,  0.3f },
    /* 6: SAD recalled      -> Melancholy / solitude resonance */
    {                         -0.2f, -1.0f, -0.3f, -0.7f, -0.2f,  -0.1f,    1.3f,  0.4f },
    /* 7: DEADPAN recalled  -> Apathy / boredom resonance */
    {                          0.2f, -0.7f, -0.1f, -0.4f, -0.4f,  -0.3f,    0.3f,  1.2f }
};

static float computeVectorNorm(const float vec[EPISODIC_EMBEDDING_DIM]) {
    float sum_sq = 0.0f;
    for (int i = 0; i < EPISODIC_EMBEDDING_DIM; i++) {
        sum_sq += vec[i] * vec[i];
    }
    return sqrtf(sum_sq);
}

static float computeCosineSimilarity(const float a[EPISODIC_EMBEDDING_DIM], const float b[EPISODIC_EMBEDDING_DIM]) {
    float dot = 0.0f;
    float norm_a_sq = 0.0f;
    float norm_b_sq = 0.0f;

    for (int i = 0; i < EPISODIC_EMBEDDING_DIM; i++) {
        dot += a[i] * b[i];
        norm_a_sq += a[i] * a[i];
        norm_b_sq += b[i] * b[i];
    }

    float denom = (sqrtf(norm_a_sq) * sqrtf(norm_b_sq)) + MEMORY_SIMILARITY_EPSILON;
    float sim = dot / denom;
    if (sim < -1.0f) sim = -1.0f;
    if (sim > 1.0f) sim = 1.0f;
    return sim;
}

void initMemoryEngine(void) {
    clearEpisodicMemory();
    loadMemoryNVS();
}

void clearEpisodicMemory(void) {
    memset(s_memory_entries, 0, sizeof(s_memory_entries));
    s_memory_count = 0;
    s_memory_head = 0;
    s_latest_resonance = 0.0f;
    s_latest_recalled_expr = EXPR_IDLE;
}

void recordEpisodicMemory(const float state_vector[EPISODIC_EMBEDDING_DIM], Expression expr, float salience, float valence_outcome) {
    if (!state_vector) return;

    salience = fmaxf(0.05f, fminf(1.0f, salience));
    valence_outcome = fmaxf(-1.0f, fminf(1.0f, valence_outcome));
    uint32_t now_s = getCurrentUptimeSec();

    /* Deduplication: If latest memory is identical in emotion and recorded < 10s ago, update salience */
    if (s_memory_count > 0) {
        uint8_t prev_idx = (s_memory_head == 0) ? (EPISODIC_MEMORY_CAPACITY - 1) : (s_memory_head - 1);
        if (s_memory_entries[prev_idx].expression == expr && (now_s - s_memory_entries[prev_idx].timestamp_s) < 10) {
            s_memory_entries[prev_idx].salience = fmaxf(s_memory_entries[prev_idx].salience, salience);
            s_memory_entries[prev_idx].valence_outcome = valence_outcome;
            s_memory_entries[prev_idx].timestamp_s = now_s;
            return;
        }
    }

    uint8_t insert_idx;
    if (s_memory_count < EPISODIC_MEMORY_CAPACITY) {
        insert_idx = s_memory_head;
        s_memory_head = (s_memory_head + 1) % EPISODIC_MEMORY_CAPACITY;
        s_memory_count++;
    } else {
        /* Eviction strategy: find entry with lowest effective retained salience */
        uint8_t lowest_idx = s_memory_head;
        float min_retained_score = 999.0f;

        for (uint8_t i = 0; i < EPISODIC_MEMORY_CAPACITY; i++) {
            float dt_s = (float)(now_s - s_memory_entries[i].timestamp_s);
            if (dt_s < 0.0f) dt_s = 0.0f;
            float decay = expf(-dt_s / MEMORY_RECENCY_TAU_SEC);
            float score = s_memory_entries[i].salience * decay;
            if (score < min_retained_score) {
                min_retained_score = score;
                lowest_idx = i;
            }
        }
        insert_idx = lowest_idx;
    }

    /* Store entry */
    memcpy(s_memory_entries[insert_idx].embedding, state_vector, sizeof(float) * EPISODIC_EMBEDDING_DIM);
    s_memory_entries[insert_idx].expression = expr;
    s_memory_entries[insert_idx].salience = salience;
    s_memory_entries[insert_idx].timestamp_s = now_s;
    s_memory_entries[insert_idx].valence_outcome = valence_outcome;
}

EpisodicRecallResult queryMemoryResonance(const float state_vector[EPISODIC_EMBEDDING_DIM]) {
    EpisodicRecallResult result;
    memset(&result, 0, sizeof(result));
    result.dominant_memory_expr = EXPR_IDLE;
    snprintf(result.recall_context, sizeof(result.recall_context), "Observing present moment...");

    if (!state_vector || s_memory_count == 0) {
        s_latest_resonance = 0.0f;
        s_latest_recalled_expr = EXPR_IDLE;
        return result;
    }

    uint32_t now_s = getCurrentUptimeSec();
    float best_sim = -1.0f;
    float best_weighted_score = 0.0f;
    Expression best_expr = EXPR_IDLE;

    for (uint8_t i = 0; i < s_memory_count; i++) {
        float raw_sim = computeCosineSimilarity(state_vector, s_memory_entries[i].embedding);
        if (raw_sim <= 0.0f) continue; /* Ignore orthogonal/opposite states */

        float dt_s = (float)(now_s - s_memory_entries[i].timestamp_s);
        if (dt_s < 0.0f) dt_s = 0.0f;
        float recency_weight = expf(-dt_s / MEMORY_RECENCY_TAU_SEC);
        float effective_weight = raw_sim * s_memory_entries[i].salience * recency_weight;

        if (effective_weight > 0.15f) {
            result.top_k_count++;
            Expression mem_expr = s_memory_entries[i].expression;
            uint8_t expr_idx = (uint8_t)mem_expr;
            if (expr_idx < 8) {
                for (int k = 0; k < 8; k++) {
                    result.memory_logits_delta[k] += effective_weight * s_associative_matrix[expr_idx][k];
                }
            }
        }

        if (effective_weight > best_weighted_score) {
            best_weighted_score = effective_weight;
            best_sim = raw_sim;
            best_expr = s_memory_entries[i].expression;
        }
    }

    result.resonance_score = (best_sim > 0.0f) ? best_sim : 0.0f;
    result.dominant_memory_expr = best_expr;
    s_latest_resonance = result.resonance_score;
    s_latest_recalled_expr = best_expr;

    /* Clamp maximum logit biases to preserve base neural stability */
    for (int k = 0; k < 8; k++) {
        result.memory_logits_delta[k] = fmaxf(-1.5f, fminf(1.5f, result.memory_logits_delta[k]));
    }

    /* Formulate human-readable associative memory context */
    if (result.resonance_score > 0.65f && result.top_k_count > 0) {
        switch (best_expr) {
            case EXPR_JOY:
                snprintf(result.recall_context, sizeof(result.recall_context), "Remembering fun moments! (Joy resonance)");
                break;
            case EXPR_SMIRK:
                snprintf(result.recall_context, sizeof(result.recall_context), "Remembering playful teasing (Smirk resonance)");
                break;
            case EXPR_SHOCK:
                snprintf(result.recall_context, sizeof(result.recall_context), "Cautious from past startle (Shock resonance)");
                break;
            case EXPR_OVERLOAD:
                snprintf(result.recall_context, sizeof(result.recall_context), "Sensitive to chaotic motion (Overload memory)");
                break;
            case EXPR_SAD:
                snprintf(result.recall_context, sizeof(result.recall_context), "Recalling lonely quiet (Sadness resonance)");
                break;
            case EXPR_ANGRY:
                snprintf(result.recall_context, sizeof(result.recall_context), "Still pouting from before (Angry resonance)");
                break;
            case EXPR_DEADPAN:
                snprintf(result.recall_context, sizeof(result.recall_context), "Remembering dull wait (Boredom resonance)");
                break;
            default:
                snprintf(result.recall_context, sizeof(result.recall_context), "Familiar peaceful vibe (Idle resonance)");
                break;
        }
    }

    return result;
}

uint8_t getEpisodicMemoryCount(void) {
    return s_memory_count;
}

float getLatestMemoryResonance(void) {
    return s_latest_resonance;
}

Expression getLatestRecalledExpression(void) {
    return s_latest_recalled_expr;
}

void saveMemoryNVS(void) {
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("kore_memory", false)) {
        prefs.putUChar("count", s_memory_count);
        prefs.putUChar("head", s_memory_head);
        if (s_memory_count > 0) {
            size_t bytes_to_write = s_memory_count * sizeof(EpisodicMemoryEntry);
            prefs.putBytes("bank", (const uint8_t*)s_memory_entries, bytes_to_write);
        }
        prefs.end();
    }
#endif
}

void loadMemoryNVS(void) {
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("kore_memory", true)) {
        uint8_t count = prefs.getUChar("count", 0);
        uint8_t head = prefs.getUChar("head", 0);
        if (count > 0 && count <= EPISODIC_MEMORY_CAPACITY) {
            size_t bytes_to_read = count * sizeof(EpisodicMemoryEntry);
            size_t read_bytes = prefs.getBytes("bank", (uint8_t*)s_memory_entries, bytes_to_read);
            if (read_bytes == bytes_to_read) {
                s_memory_count = count;
                s_memory_head = (head < EPISODIC_MEMORY_CAPACITY) ? head : 0;
            } else {
                clearEpisodicMemory();
            }
        }
        prefs.end();
    }
#endif
}
