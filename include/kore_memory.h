/**
 * @file kore_memory.h
 * @brief Affective & Episodic Vector Memory Embedding Engine declarations.
 */

#ifndef KORE_MEMORY_H
#define KORE_MEMORY_H

#include "include/kore_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EPISODIC_MEMORY_CAPACITY 32
#define EPISODIC_EMBEDDING_DIM 8
#define MEMORY_RECENCY_TAU_SEC 3600.0f
#define MEMORY_SIMILARITY_EPSILON 1e-6f

/**
 * @struct EpisodicMemoryEntry
 * @brief Represents an individual recorded emotional/interaction episode in latent vector space.
 */
typedef struct {
    float embedding[EPISODIC_EMBEDDING_DIM]; /* [V, A, Cur, Soc, Bor, Fat, Mis, Prox] */
    Expression expression;                   /* Associated dominant expression during event */
    float salience;                          /* Importance/weight of memory [0.0, 1.0] */
    uint32_t timestamp_s;                    /* Device uptime second when recorded */
    float valence_outcome;                   /* Emotional outcome valence [-1.0, +1.0] */
} EpisodicMemoryEntry;

/**
 * @struct EpisodicRecallResult
 * @brief Represents associative memory resonance and the resulting logit bias delta vector.
 */
typedef struct {
    float resonance_score;              /* Top cosine similarity score [0.0, 1.0] */
    Expression dominant_memory_expr;    /* Expression of the most resonant memory */
    float memory_logits_delta[8];       /* Additive logit bias vector across 8 expressions */
    char recall_context[48];            /* Semantic descriptive context for monologue */
    uint8_t top_k_count;                /* Number of resonant memories contributing to bias */
} EpisodicRecallResult;

/* Episodic Memory Lifecycle & Operations */
void initMemoryEngine(void);
void recordEpisodicMemory(const float state_vector[EPISODIC_EMBEDDING_DIM], Expression expr, float salience, float valence_outcome);
EpisodicRecallResult queryMemoryResonance(const float state_vector[EPISODIC_EMBEDDING_DIM]);
uint8_t getEpisodicMemoryCount(void);
float getLatestMemoryResonance(void);
Expression getLatestRecalledExpression(void);
void saveMemoryNVS(void);
void loadMemoryNVS(void);
void clearEpisodicMemory(void);

#ifdef __cplusplus
}
#endif

#endif /* KORE_MEMORY_H */
