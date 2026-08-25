/**
 * @file kore_personality.cpp
 * @brief Persistent personality trait system and circadian rhythm engine implementation.
 * @details Trait values are loaded from NVS on boot and slowly drift over the device
 *          lifetime based on cumulative interaction patterns. The circadian engine uses
 *          a sinusoidal energy cycle derived from millis() uptime, producing smooth
 *          behavioral variation without requiring wall-clock synchronization.
 *
 *          All modulation functions centralize the mapping from raw trait values to
 *          downstream behavioral parameters, preventing trait logic from scattering
 *          across kinematics, affective, and display modules.
 */

#include "include/kore_personality.h"
#include "include/kore_config.h"
#include <math.h>
#include <Arduino.h>
#include <esp_random.h>
#ifdef ARDUINO
#include <Preferences.h>
#endif

/* Persistent Trait State */
static PersonalityTraits s_traits = {
    .boldness    = PERSONALITY_DEFAULT_BOLDNESS,
    .volatility  = PERSONALITY_DEFAULT_VOLATILITY,
    .playfulness = PERSONALITY_DEFAULT_PLAYFULNESS,
    .attachment  = PERSONALITY_DEFAULT_ATTACHMENT
};

/* Circadian Cycle State */
static CircadianState s_circadian = {
    .energy_level  = 0.60f,
    .mood_baseline = 0.0f,
    .activity_drive = 0.50f,
    .phase_pct     = 0.0f
};

static uint32_t s_circadian_epoch_ms = 0;

/* ------------------------------------------------------------------
 * NVS Persistence
 * Traits are stored in a separate NVS namespace from the brain memory
 * to allow independent reset of either subsystem without data loss.
 * ------------------------------------------------------------------ */

void initPersonalityEngine(void) {
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("kore_persona", true)) {
        s_traits.boldness    = prefs.getFloat("bold", PERSONALITY_DEFAULT_BOLDNESS);
        s_traits.volatility  = prefs.getFloat("volat", PERSONALITY_DEFAULT_VOLATILITY);
        s_traits.playfulness = prefs.getFloat("play", PERSONALITY_DEFAULT_PLAYFULNESS);
        s_traits.attachment  = prefs.getFloat("attach", PERSONALITY_DEFAULT_ATTACHMENT);
        prefs.end();

        s_traits.boldness    = constrain(s_traits.boldness, 0.0f, 1.0f);
        s_traits.volatility  = constrain(s_traits.volatility, 0.0f, 1.0f);
        s_traits.playfulness = constrain(s_traits.playfulness, 0.0f, 1.0f);
        s_traits.attachment  = constrain(s_traits.attachment, 0.0f, 1.0f);
    }
#endif
    s_circadian_epoch_ms = millis();
}

void savePersonalityNVS(void) {
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("kore_persona", false)) {
        prefs.putFloat("bold", s_traits.boldness);
        prefs.putFloat("volat", s_traits.volatility);
        prefs.putFloat("play", s_traits.playfulness);
        prefs.putFloat("attach", s_traits.attachment);
        prefs.end();
    }
#endif
}

/* ------------------------------------------------------------------
 * Circadian Rhythm Engine
 * Models a smooth internal energy cycle using a piecewise sinusoidal
 * waveform. The cycle has 4 phases of equal duration (25% each):
 *   Phase 0 (Rising):    Energy 0.30 -> 0.80, mood lifting
 *   Phase 1 (Peak):      Energy 0.80 -> 1.00, most alert and reactive
 *   Phase 2 (Declining): Energy 1.00 -> 0.50, gradually mellowing
 *   Phase 3 (Rest):      Energy 0.50 -> 0.30, drowsy, slow gaze
 *
 * The sinusoidal mapping avoids abrupt transitions between phases
 * and produces naturalistic energy curves matching ultradian rhythms.
 * ------------------------------------------------------------------ */

void updateCircadianCycle(void) {
    uint32_t elapsed = millis() - s_circadian_epoch_ms;
    float phase_raw = fmodf((float)elapsed, (float)CIRCADIAN_CYCLE_PERIOD_MS) / (float)CIRCADIAN_CYCLE_PERIOD_MS;
    s_circadian.phase_pct = phase_raw * 100.0f;

    /*
     * Energy follows a shifted cosine: peaks at 37.5% of cycle, troughs at 87.5%.
     * E(p) = 0.65 + 0.35 * cos(2*PI*(p - 0.375))
     * This places the energy peak slightly past the midpoint of the rising phase,
     * matching the biological pattern where alertness peaks mid-morning.
     */
    float theta = 6.2831853f * (phase_raw - 0.375f);
    s_circadian.energy_level = constrain(0.65f + 0.35f * cosf(theta), 0.20f, 1.0f);

    /*
     * Mood baseline follows a similar curve but with smaller amplitude and phase lead.
     * Positive mood correlates with rising energy, slight negative during rest trough.
     */
    float mood_theta = 6.2831853f * (phase_raw - 0.30f);
    s_circadian.mood_baseline = constrain(0.025f + 0.20f * cosf(mood_theta), -0.20f, 0.25f);

    /*
     * Activity drive scales with energy but has a faster onset (sharper rise)
     * to model the behavioral observation that desire for activity precedes
     * full physiological alertness.
     */
    float act_theta = 6.2831853f * (phase_raw - 0.30f);
    s_circadian.activity_drive = constrain(0.55f + 0.40f * cosf(act_theta), 0.10f, 1.0f);
}

PersonalityTraits getPersonalityTraits(void) {
    return s_traits;
}

CircadianState getCircadianState(void) {
    return s_circadian;
}

/* ------------------------------------------------------------------
 * Behavioral Modulation Functions
 * Each function maps one or more traits plus circadian state into a
 * single scalar consumed by exactly one downstream subsystem.
 * ------------------------------------------------------------------ */

/* --- Langevin Diffusion Coefficients (kore_affective.cpp) ---
 * Volatile personalities have larger stochastic noise amplitude,
 * causing more frequent spontaneous mood shifts and less predictable
 * emotional trajectories. Circadian rest phase dampens volatility
 * to model the observation that tired individuals are less reactive. */

float getPersonalityValenceSigma(void) {
    float base = 0.012f + 0.020f * s_traits.volatility;
    return base * (0.70f + 0.30f * s_circadian.energy_level);
}

float getPersonalityArousalSigma(void) {
    float base = 0.009f + 0.015f * s_traits.volatility;
    return base * (0.70f + 0.30f * s_circadian.energy_level);
}

/* --- Gaze Kinematics (kore_kinematics.cpp) ---
 * Bold personalities produce snappier eye movements (higher omega_n)
 * with slight underdamping (lower zeta) creating a more confident,
 * assertive gaze. Shy personalities use overdamped kinematics producing
 * sluggish, hesitant eye movements that avoid direct engagement.
 * Circadian energy scales the overall responsiveness. */

float getPersonalityGazeOmega(void) {
    float base = GAZE_NATURAL_FREQUENCY_RAD_S;
    float bold_factor = 0.85f + 0.30f * s_traits.boldness;
    float energy_factor = 0.80f + 0.20f * s_circadian.energy_level;
    return base * bold_factor * energy_factor;
}

float getPersonalityGazeDamping(void) {
    /*
     * Shy: zeta -> 0.90 (overdamped, sluggish settling, no bounce)
     * Bold: zeta -> 0.62 (underdamped, quick snap with slight overshoot)
     * This maps boldness inversely to damping ratio within the stable range.
     */
    return 0.90f - 0.28f * s_traits.boldness;
}

float getPersonalityIdleGazeYBias(void) {
    /*
     * Returns an additive Y offset applied to idle saccade targets.
     * Shy: positive bias (looks downward/away from engagement).
     * Bold: zero or slight negative (holds center or looks upward).
     * Rest phase amplifies downward bias to model drowsy drooping gaze.
     */
    float shyness = 1.0f - s_traits.boldness;
    float rest_factor = 1.0f + 0.5f * (1.0f - s_circadian.energy_level);
    return shyness * 2.5f * rest_factor;
}

float getPersonalityIdleIntervalScale(void) {
    /*
     * Multiplier on idle saccade inter-interval timing.
     * Playful + high energy: shorter intervals (0.65x, darting gaze).
     * Shy + low energy: longer intervals (1.4x, lingering gaze).
     */
    float play_speed = 1.0f - 0.35f * s_traits.playfulness * s_circadian.activity_drive;
    float shy_slow = 1.0f + 0.40f * (1.0f - s_traits.boldness) * (1.0f - s_circadian.energy_level);
    return constrain(play_speed * shy_slow, 0.55f, 1.50f);
}

/* --- Blink Patterns (display_engine.cpp) ---
 * Anxious/volatile personalities blink more frequently.
 * Low energy phases produce slower, heavier blinks.
 * Double-blink chance correlates with playfulness (expressive flutter). */

uint32_t getPersonalityBlinkInterval(void) {
    float base_min = 3500.0f;
    float base_range = 3500.0f;
    float volatility_speedup = 1.0f - 0.25f * s_traits.volatility;
    float energy_slowdown = 1.0f + 0.30f * (1.0f - s_circadian.energy_level);
    float total_scale = volatility_speedup * energy_slowdown;
    uint32_t random_offset = esp_random() % (uint32_t)(base_range * total_scale);
    return (uint32_t)(base_min * total_scale) + random_offset;
}

uint32_t getPersonalityDoubleBlinkChance(void) {
    /* Playful: up to 22% chance. Stoic: down to 8% chance. */
    float chance = 8.0f + 14.0f * s_traits.playfulness;
    return (uint32_t)chance;
}

/* --- Homeostatic Drive Gains (kore_brain.cpp) ---
 * Attachment amplifies social drive sensitivity: clingy personalities
 * develop social needs faster and feel loneliness sooner.
 * Playfulness amplifies mischief drive: playful personalities accumulate
 * teasing energy faster but also get bored faster without stimulation. */

float getPersonalitySocialGain(void) {
    return 0.15f + 0.20f * s_traits.attachment;
}

float getPersonalityMischiefGain(void) {
    return 0.08f + 0.14f * s_traits.playfulness;
}

float getPersonalityBoredomRate(void) {
    /*
     * Playful personalities accumulate boredom faster during monotony
     * because they have a higher baseline need for stimulation.
     * Low energy phases also increase boredom susceptibility.
     */
    float play_factor = 1.0f + 0.40f * s_traits.playfulness;
    float energy_factor = 1.0f + 0.25f * (1.0f - s_circadian.energy_level);
    return 0.12f * play_factor * energy_factor;
}

/* --- Ambient Glance Frequency (display_engine.cpp) ---
 * Returns a multiplier on the ambient glance interval.
 * Curious/playful personalities glance more often (lower multiplier).
 * High circadian activity drive also increases glance frequency.
 * This directly addresses the issue where the brain engine's engagement
 * detection suppresses clock/weather displays: by making the personality
 * actively "want" to check the time and weather. */

float getPersonalityAmbientGlanceScale(void) {
    float curiosity_factor = 1.0f - 0.30f * s_traits.playfulness;
    float activity_factor = 1.0f - 0.20f * s_circadian.activity_drive;
    return constrain(curiosity_factor * activity_factor, 0.40f, 1.20f);
}
