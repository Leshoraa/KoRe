/**
 * @file kore_personality.h
 * @brief Persistent personality trait system and circadian rhythm engine declarations.
 * @details Personality traits define KoRe's behavioral disposition and are stored
 *          persistently in NVS flash. These modulate all downstream behavioral
 *          subsystems: Langevin affective diffusion, gaze kinematics, blink timing,
 *          homeostatic drive sensitivity, and idle saccade target distribution.
 *
 *          The circadian engine provides a slow internal energy cycle (default 6 hours)
 *          that creates natural behavioral variation over time, independent of wall-clock.
 */

#ifndef KORE_PERSONALITY_H
#define KORE_PERSONALITY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct PersonalityTraits
 * @brief Four permanent character dimensions stored in NVS. These form the
 *        dispositional baseline that shapes all reactive and spontaneous behavior.
 */
typedef struct {
    float boldness;     /* [0.0, 1.0]: Shy/avoidant gaze (0) to direct/confident gaze holder (1).
                         * Modulates gaze omega_n, idle saccade Y-bias, and gaze hold duration. */
    float volatility;   /* [0.0, 1.0]: Emotionally stable (0) to moody/reactive (1).
                         * Modulates Langevin diffusion sigma_v, sigma_a and mood shift frequency. */
    float playfulness;  /* [0.0, 1.0]: Serious/stoic (0) to mischievous/teasing (1).
                         * Modulates mischief drive gain, boredom accumulation rate, and
                         * spontaneous ambient glance frequency. */
    float attachment;   /* [0.0, 1.0]: Independent/aloof (0) to clingy/social (1).
                         * Modulates social drive sensitivity, bonding growth rate, and
                         * loneliness onset threshold. */
} PersonalityTraits;

/**
 * @struct CircadianState
 * @brief Snapshot of the internal energy cycle state at a given moment.
 *        Provides multiplicative and additive modifiers to downstream subsystems.
 */
typedef struct {
    float energy_level;     /* [0.2, 1.0]: Overall alertness/responsiveness scaling factor */
    float mood_baseline;    /* [-0.20, +0.25]: Additive valence offset applied to Langevin target */
    float activity_drive;   /* [0.1, 1.0]: Desire for engagement, scales saccade frequency */
    float phase_pct;        /* [0.0, 100.0]: Current position in the cycle for telemetry */
} CircadianState;

/* --- Lifecycle --- */
void initPersonalityEngine(void);
void savePersonalityNVS(void);

/* --- Circadian Update (call once per mood engine tick) --- */
void updateCircadianCycle(void);

/* --- State Accessors --- */
PersonalityTraits getPersonalityTraits(void);
CircadianState getCircadianState(void);

/* --- Behavioral Modulation Functions ---
 * Each function fuses the relevant personality trait(s) with the current circadian
 * state to produce a single scalar that the calling subsystem uses directly.
 * This keeps modulation logic centralized and prevents trait constants from
 * scattering across unrelated modules. */

/* Langevin diffusion coefficient scaling (kore_affective.cpp) */
float getPersonalityValenceSigma(void);
float getPersonalityArousalSigma(void);

/* Gaze kinematics parameter modulation (kore_kinematics.cpp) */
float getPersonalityGazeOmega(void);
float getPersonalityGazeDamping(void);
float getPersonalityIdleGazeYBias(void);
float getPersonalityIdleIntervalScale(void);

/* Blink pattern modulation (display_engine.cpp) */
uint32_t getPersonalityBlinkInterval(void);
uint32_t getPersonalityDoubleBlinkChance(void);

/* Homeostatic drive gain modulation (kore_brain.cpp) */
float getPersonalitySocialGain(void);
float getPersonalityMischiefGain(void);
float getPersonalityBoredomRate(void);

/* Ambient glance frequency modulation (display_engine.cpp) */
float getPersonalityAmbientGlanceScale(void);

#ifdef __cplusplus
}
#endif

#endif /* KORE_PERSONALITY_H */
