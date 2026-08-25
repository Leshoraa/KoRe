/**
 * @file kore_ai.h
 * @brief On-Device TinyML / Micro-Brain Behavioral Neural Engine declarations.
 */

#ifndef KORE_AI_H
#define KORE_AI_H

#include "include/kore_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct HomeostaticDrives
 * @brief 5 Continuous biological & psychological homeostatic drives.
 */
typedef struct {
    float curiosity;    /* [0.0, 1.0]: Rises with novel motion/novelty; seeks new stimulation */
    float social;       /* [0.0, 1.0]: Rises with companionship; decays into loneliness when alone */
    float boredom;      /* [0.0, 1.0]: Rises during visual monotony; triggers ambient/glance/playfulness */
    float fatigue;      /* [0.0, 1.0]: Rises with continuous fast hyperactivity; prevents overstimulation */
    float mischief;     /* [0.0, 1.0]: Tsundere playfulness trait; triggers teasing smirks & faux-pout */
} HomeostaticDrives;

/**
 * @struct BrainTelemetry
 * @brief Comprehensive cognitive state snapshot for real-time Web UI telemetry & decision logs.
 */
typedef struct {
    float valence;              /* Affective valence [-1.0 (negative), +1.0 (positive)] */
    float arousal;              /* Affective arousal [0.0 (calm), 1.0 (excited)] */
    HomeostaticDrives drives;   /* 5 Homeostatic biological drives */
    float decision_logits[8];   /* Neural decision logits for each expression */
    float decision_probs[8];    /* Softmax probabilities for each expression */
    Expression dominant_expr;   /* Neural policy's top chosen expression */
    char thought_summary[48];   /* Human-readable inner monologue / cognitive state */
    uint32_t interaction_sec;   /* Current continuous session interaction seconds */
    uint32_t solitude_sec;      /* Current continuous session solitude seconds */
    float bonding_level;        /* [0.0, 1.0]: Cumulative companionship bonding level in NVS */
    uint32_t lifetime_sec;      /* Total cumulative lifetime interaction seconds in NVS */
} BrainTelemetry;

/* Micro-Brain Neural Network Lifecycle & Inference */
void initBrainEngine(void);
void updateBrainEngine(float dt_sec);
BrainTelemetry getBrainTelemetry(void);
Expression sampleBrainExpressionPolicy(void);
const char* getBrainThoughtSummary(void);
void saveBrainMemoryNVS(void);
void loadBrainMemoryNVS(void);
float getBrainBondingLevel(void);
uint32_t getBrainLifetimeSec(void);

#ifdef __cplusplus
}
#endif

#endif /* KORE_AI_H */
