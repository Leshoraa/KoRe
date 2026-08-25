/**
 * @file kore_brain.cpp
 * @brief On-Device TinyML / Micro-Brain Behavioral Neural Engine implementation.
 */

#include "include/kore_ai.h"
#include "include/kore_affective.h"
#include "include/kore_personality.h"
#include "include/kore_config.h"
#include "include/kore_types.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <Arduino.h>
#include <esp_random.h>
#ifdef ARDUINO
#include <Preferences.h>
#endif

/* Persistent Bonding & Companionship State */
static float s_bonding_level = 0.05f;
static uint32_t s_lifetime_interaction_sec = 0;
static uint32_t s_last_nvs_save_ms = 0;
static uint32_t s_last_bonding_grow_ms = 0;

/* Internal Homeostatic State Variables */
static HomeostaticDrives s_drives = {
    .curiosity = 0.35f,
    .social    = 0.50f,
    .boredom   = 0.15f,
    .fatigue   = 0.10f,
    .mischief  = 0.40f
};

static float s_presence_ema = 0.0f;
static float s_motion_energy_ema = 0.0f;
static float s_proximity_smooth = 0.0f;
static uint32_t s_interaction_start_ms = 0;
static uint32_t s_solitude_start_ms = 0;
static uint32_t s_total_interaction_sec = 0;
static uint32_t s_total_solitude_sec = 0;
static uint32_t s_last_update_ms = 0;

static float s_decision_logits[8] = {0};
static float s_decision_probs[8] = {0};
static Expression s_dominant_expr = EXPR_IDLE;
static char s_thought_summary[48] = "Booting cognitive engine...";

/* 8x8 Neural Weight Matrix: Maps [V, A, Cur, Soc, Bor, Fat, Mis, Prox] -> 8 Expressions
 * Expressions: 0:IDLE, 1:JOY, 2:ANGRY, 3:SMIRK, 4:SHOCK, 5:OVERLOAD, 6:SAD, 7:DEADPAN */
static const float s_neural_weights[8][8] = {
    /* IDLE: Dominant calm resting baseline (suppressed when bored or fatigued) */
    {  0.1f, -0.5f,  0.1f,  0.3f, -1.0f, -0.4f, -0.2f,  0.1f },
    /* JOY: High Valence, High Social, High Proximity, Excited */
    {  1.8f,  0.6f,  0.3f,  1.3f, -1.0f, -0.5f,  0.2f,  0.6f },
    /* ANGRY: Tsundere pout (Low Valence, High Arousal, High Mischief) */
    { -1.5f,  1.1f,  0.1f, -0.5f, -0.2f,  0.4f,  1.4f,  0.4f },
    /* SMIRK: Playful mischief, moderate positive valence, social */
    {  0.7f,  0.3f,  0.6f,  0.8f, -0.4f, -0.3f,  1.8f,  0.4f },
    /* SHOCK: Intense startle reaction on sudden high-arousal spikes */
    { -0.4f,  1.6f,  0.8f, -0.2f, -0.6f,  0.2f,  0.1f,  1.2f },
    /* OVERLOAD: High Fatigue, High Arousal, Rapid motion */
    { -0.9f,  1.4f,  0.2f, -0.3f, -0.5f,  2.5f, -0.4f,  0.3f },
    /* SAD: Low Social, High Solitude, Negative Valence, Low Arousal */
    { -1.8f, -0.8f, -0.5f, -2.0f,  0.9f,  0.2f, -0.6f, -0.6f },
    /* DEADPAN: High Boredom, Low Arousal, Low Motion */
    { -0.2f, -1.2f, -0.8f, -0.5f,  2.4f,  0.4f,  0.1f, -0.3f }
};

static const float s_neural_biases[8] = {
    1.40f,  /* IDLE bias: High baseline for calm stability */
   -0.70f,  /* JOY bias */
   -1.20f,  /* ANGRY bias */
   -0.70f,  /* SMIRK bias */
   -2.60f,  /* SHOCK bias: Heavily suppressed from spurious triggering */
   -1.80f,  /* OVERLOAD bias */
   -0.80f,  /* SAD bias */
   -0.30f   /* DEADPAN bias */
};

void loadBrainMemoryNVS(void) {
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("kore_brain", true)) {
        s_bonding_level = prefs.getFloat("bonding", 0.05f);
        s_lifetime_interaction_sec = prefs.getULong("life_sec", 0);
        prefs.end();
        s_bonding_level = constrain(s_bonding_level, 0.0f, 1.0f);
    }
#endif
}

void saveBrainMemoryNVS(void) {
#ifdef ARDUINO
    Preferences prefs;
    if (prefs.begin("kore_brain", false)) {
        prefs.putFloat("bonding", s_bonding_level);
        prefs.putULong("life_sec", s_lifetime_interaction_sec);
        prefs.end();
        s_last_nvs_save_ms = millis();
    }
#endif
}

float getBrainBondingLevel(void) {
    return s_bonding_level;
}

uint32_t getBrainLifetimeSec(void) {
    return s_lifetime_interaction_sec;
}

void initBrainEngine(void) {
    loadBrainMemoryNVS();

    s_drives.curiosity = 0.35f;
    s_drives.social    = 0.45f + 0.25f * s_bonding_level;
    s_drives.boredom   = 0.10f;
    s_drives.fatigue   = 0.05f;
    s_drives.mischief  = 0.40f;

    s_last_update_ms = millis();
    s_solitude_start_ms = s_last_update_ms;
    s_last_nvs_save_ms = s_last_update_ms;
    s_last_bonding_grow_ms = s_last_update_ms;
    snprintf(s_thought_summary, sizeof(s_thought_summary), "Observing surroundings...");
}

void updateBrainEngine(float dt_sec) {
    if (dt_sec <= 0.001f) dt_sec = 0.050f;
    dt_sec = constrain(dt_sec, 0.01f, 0.50f);

    unsigned long now = millis();

    /* 1. Sensory Integration
     * Gate presence on composite human likelihood rather than raw skin-color
     * confidence, ensuring that bonding and social drives only respond to
     * targets that exhibit multi-signal human characteristics. */
    TrackTarget target;
    portENTER_CRITICAL(&g_target_mutex);
    target = g_current_target;
    portEXIT_CRITICAL(&g_target_mutex);

    float raw_pres = (target.detected && target.human_likelihood > HUMAN_LIKELIHOOD_THRESHOLD) ? 1.0f : 0.0f;
    s_presence_ema = s_presence_ema * 0.90f + raw_pres * 0.10f;
    bool is_detected = (s_presence_ema > 0.50f);

    float raw_motion = (target.detected) ? fminf(1.0f, (fabsf(target.vx) + fabsf(target.vy)) * 0.05f + target.total_energy * 0.02f) : 0.0f;
    s_motion_energy_ema = s_motion_energy_ema * 0.85f + raw_motion * 0.15f;

    if (target.detected) {
        s_proximity_smooth = s_proximity_smooth * 0.85f + target.proximity * 0.15f;
    } else {
        s_proximity_smooth *= 0.95f;
    }

    /* Track Timers & Bonding Growth */
    if (is_detected) {
        if (s_interaction_start_ms == 0) s_interaction_start_ms = now;
        s_total_interaction_sec = (now - s_interaction_start_ms) / 1000;
        s_solitude_start_ms = 0;
        s_total_solitude_sec = 0;

        /* Advance cumulative lifetime companionship & bonding level */
        if (now - s_last_bonding_grow_ms >= 10000) {
            uint32_t delta_s = (now - s_last_bonding_grow_ms) / 1000;
            s_last_bonding_grow_ms = now;
            s_lifetime_interaction_sec += delta_s;
            /* Growth rate: ~0.01 per 15 minutes of companion time */
            s_bonding_level = fminf(1.0f, s_bonding_level + (float)delta_s * 0.000011f);
        }
    } else {
        if (s_solitude_start_ms == 0) s_solitude_start_ms = now;
        s_total_solitude_sec = (now - s_solitude_start_ms) / 1000;
        s_interaction_start_ms = 0;
        s_total_interaction_sec = 0;
        s_last_bonding_grow_ms = now;
    }

    /* Periodic Flash Memory Wear-Leveling Save (Every 300 Seconds) */
    if (now - s_last_nvs_save_ms >= 300000) {
        saveBrainMemoryNVS();
        savePersonalityNVS();
    }

    /* 2. Homeostatic Biological Drive Evolution (Personality-Modulated Differential Equations) */
    /* Curiosity Drive: Rises with movement/novelty, decays naturally.
     * Not personality-gated: curiosity toward motion is a universal instinct. */
    float d_curiosity = (0.35f * s_motion_energy_ema * (1.0f - 0.4f * s_presence_ema) - 0.08f * s_drives.curiosity);
    s_drives.curiosity = constrain(s_drives.curiosity + d_curiosity * dt_sec, 0.0f, 1.0f);

    /* Social Drive: Gain scales with attachment trait.
     * High attachment: social need rises quickly with companionship and drops painfully in solitude.
     * Low attachment: more self-sufficient, slower social drive accumulation. */
    float social_gain = getPersonalitySocialGain();
    float d_social = (social_gain * s_presence_ema - 0.05f * (1.0f - s_presence_ema));
    s_drives.social = constrain(s_drives.social + d_social * dt_sec, 0.0f, 1.0f);

    /* Boredom Drive: Rate scales with playfulness and circadian energy.
     * Playful personalities get bored faster during monotony because they
     * have a higher baseline need for stimulation. Low circadian energy
     * also increases susceptibility to boredom. */
    float boredom_rate = getPersonalityBoredomRate();
    float d_boredom = (boredom_rate * (1.0f - s_motion_energy_ema) * (1.0f - 0.5f * s_presence_ema) - 0.20f * (s_motion_energy_ema + 0.3f * s_presence_ema));
    s_drives.boredom = constrain(s_drives.boredom + d_boredom * dt_sec, 0.0f, 1.0f);

    /* Fatigue Drive: Rises with continuous fast hyperactivity, recovers during calm */
    float d_fatigue = (0.30f * (s_motion_energy_ema * s_presence_ema) - 0.15f * (1.0f - s_motion_energy_ema));
    s_drives.fatigue = constrain(s_drives.fatigue + d_fatigue * dt_sec, 0.0f, 1.0f);

    /* Mischief Drive: Gain scales with playfulness trait.
     * Playful personalities accumulate teasing energy faster when socially
     * comfortable and not fatigued, driving more frequent SMIRK and ANGRY expressions. */
    float noise_m = ((float)(esp_random() % 1000) - 500.0f) * 0.0001f;
    float mischief_gain = getPersonalityMischiefGain();
    float d_mischief = (mischief_gain * (s_drives.social * (1.0f - s_drives.fatigue)) - 0.08f * s_drives.mischief + noise_m);
    s_drives.mischief = constrain(s_drives.mischief + d_mischief * dt_sec, 0.0f, 1.0f);

    /* 3. Neural Policy Feedforward Inference */
    float V = getEmotionValence();
    float A = getEmotionArousal();

    float state_vector[8] = {
        V,
        A,
        s_drives.curiosity,
        s_drives.social,
        s_drives.boredom,
        s_drives.fatigue,
        s_drives.mischief,
        s_proximity_smooth
    };

    /* Matrix Multiplication: Logits = W * State + Bias */
    float max_logit = -999.0f;
    for (int i = 0; i < 8; i++) {
        float sum = s_neural_biases[i];
        for (int j = 0; j < 8; j++) {
            sum += s_neural_weights[i][j] * state_vector[j];
        }

        /* Modulate Logits via Persistent Bonding Level */
        if (i == 1) sum += 0.8f * s_bonding_level;          /* JOY boosted by bonding */
        else if (i == 3) sum += 0.6f * s_bonding_level;     /* SMIRK boosted by bonding */
        else if (i == 4) sum -= 1.2f * s_bonding_level;     /* SHOCK heavily suppressed by bonding */
        else if (i == 2) sum -= 0.6f * s_bonding_level;     /* Tsundere pout gentled by bonding */

        s_decision_logits[i] = sum;
        if (sum > max_logit) max_logit = sum;
    }

    /* Softmax Boltzmann Probability Distribution with Temperature tau = 0.85 */
    float tau = 0.85f;
    float sum_exp = 0.0f;
    for (int i = 0; i < 8; i++) {
        s_decision_probs[i] = expf((s_decision_logits[i] - max_logit) / tau);
        sum_exp += s_decision_probs[i];
    }
    float best_prob = -1.0f;
    for (int i = 0; i < 8; i++) {
        s_decision_probs[i] /= sum_exp;
        if (s_decision_probs[i] > best_prob) {
            best_prob = s_decision_probs[i];
            s_dominant_expr = (Expression)i;
        }
    }

    /* 4. Update Cognitive Inner Monologue Summary
     * Combinatorial monologue generation using personality traits, circadian state,
     * and drive values to produce varied, character-consistent internal narration. */
    PersonalityTraits traits = getPersonalityTraits();
    CircadianState circa = getCircadianState();

    if (s_drives.fatigue > 0.70f) {
        if (circa.energy_level < 0.40f)
            snprintf(s_thought_summary, sizeof(s_thought_summary), "So tired... everything is heavy");
        else if (traits.playfulness > 0.60f)
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Sleepy but still wanna play...");
        else
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Need to rest for a bit...");
    } else if (circa.energy_level < 0.35f && !is_detected) {
        if (traits.boldness < 0.40f)
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Getting drowsy... quiet here");
        else
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Low energy. Conserving...");
    } else if (s_bonding_level > 0.40f && is_detected && s_total_interaction_sec > 15) {
        if (traits.attachment > 0.60f)
            snprintf(s_thought_summary, sizeof(s_thought_summary), "So glad you are here! Stay close");
        else if (traits.playfulness > 0.60f)
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Hey companion! Wanna play?");
        else
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Comfortable with you nearby");
    } else if (traits.boldness < 0.35f && is_detected && s_total_interaction_sec < 5) {
        snprintf(s_thought_summary, sizeof(s_thought_summary), "Someone appeared... (cautious)");
    } else if (traits.boldness > 0.65f && is_detected && s_total_interaction_sec > 10) {
        snprintf(s_thought_summary, sizeof(s_thought_summary), "Watching you directly. Curious.");
    } else if (s_drives.mischief > 0.65f && s_presence_ema > 0.4f) {
        if (traits.playfulness > 0.70f)
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Hehe... feeling mischievous!");
        else
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Feeling cheeky right now");
    } else if (s_drives.curiosity > 0.60f) {
        if (circa.energy_level > 0.70f)
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Alert! Tracking something...");
        else
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Curious about that movement...");
    } else if (s_drives.boredom > 0.65f) {
        if (traits.playfulness > 0.60f)
            snprintf(s_thought_summary, sizeof(s_thought_summary), "So bored... need excitement!");
        else
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Nothing happening. Waiting...");
    } else if (s_total_solitude_sec > 120 && s_drives.social < 0.25f) {
        if (traits.attachment > 0.60f)
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Lonely... missing company");
        else
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Alone. That is fine for now.");
    } else if (circa.energy_level > 0.85f && !is_detected) {
        snprintf(s_thought_summary, sizeof(s_thought_summary), "Full energy! Ready for anything");
    } else if (is_detected) {
        if (s_total_interaction_sec > 30 && traits.boldness > 0.50f)
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Settled in. Watching calmly.");
        else
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Observing companion...");
    } else {
        if (circa.activity_drive > 0.70f)
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Looking around for stimulation");
        else
            snprintf(s_thought_summary, sizeof(s_thought_summary), "Observing quietly...");
    }
}

Expression sampleBrainExpressionPolicy(void) {
    /* Stochastic sampling from Softmax Boltzmann distribution */
    float r = (float)(esp_random() % 10000) / 10000.0f;
    float cum = 0.0f;
    for (int i = 0; i < 8; i++) {
        cum += s_decision_probs[i];
        if (r <= cum || i == 7) {
            return (Expression)i;
        }
    }
    return s_dominant_expr;
}

BrainTelemetry getBrainTelemetry(void) {
    BrainTelemetry t;
    t.valence = getEmotionValence();
    t.arousal = getEmotionArousal();
    t.drives  = s_drives;
    memcpy(t.decision_logits, s_decision_logits, sizeof(s_decision_logits));
    memcpy(t.decision_probs, s_decision_probs, sizeof(s_decision_probs));
    t.dominant_expr = s_dominant_expr;
    snprintf(t.thought_summary, sizeof(t.thought_summary), "%s", s_thought_summary);
    t.interaction_sec = s_total_interaction_sec;
    t.solitude_sec = s_total_solitude_sec;
    t.bonding_level = s_bonding_level;
    t.lifetime_sec = s_lifetime_interaction_sec;
    return t;
}

const char* getBrainThoughtSummary(void) {
    return s_thought_summary;
}
