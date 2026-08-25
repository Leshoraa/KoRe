/**
 * @file kore_affective.cpp
 * @brief 2D Russell Circumplex affective emotion dynamics and stochastic Langevin model implementation.
 */

#include "include/kore_affective.h"
#include "include/kore_ai.h"
#include "include/kore_personality.h"
#include "include/kore_config.h"
#include "include/kore_types.h"
#include "include/kore_kinematics.h"
#include <math.h>
#include <Arduino.h>
#include <esp_random.h>

/* Global Affective State */
Expression g_currentExpr = EXPR_IDLE;
float g_animFrame = 0.0f;
BlinkState g_blinkState = BLINK_IDLE_STATE;
float g_blinkEyeHeight = 1.0f;
uint32_t g_nextBlinkTime = 0;

static float s_emotion_valence = 0.05f;
static float s_emotion_arousal = 0.15f;
static uint32_t s_last_mood_update = 0;
static uint32_t s_mood_lock_until = 0;
static uint32_t s_nextMoodShiftTime = 0;
static bool s_lastTargetDetectedState = false;
static float s_target_presence_ema = 0.0f;
static volatile bool s_manual_override = false;
static volatile int s_manual_expr_code = -1;
static volatile bool s_manual_expr_pending = false;
static volatile int s_pending_expr_code = -1;
static uint32_t s_manual_expr_start_ms = 0;

extern void transitionExpression(Expression fromExpr, Expression toExpr, float durationMs);

float getEmotionValence(void) {
    return s_emotion_valence;
}

float getEmotionArousal(void) {
    return s_emotion_arousal;
}

void setManualExpression(int expr_code) {
    s_pending_expr_code = expr_code;
    s_manual_expr_pending = true;
}

int getManualExpression(void) {
    return s_manual_expr_code;
}

bool isManualExpressionActive(void) {
    return s_manual_override;
}

const char* getExpressionName(Expression expr) {
    switch (expr) {
        case EXPR_IDLE:     return "IDLE";
        case EXPR_JOY:      return "JOY";
        case EXPR_ANGRY:    return "ANGRY";
        case EXPR_SMIRK:    return "SMIRK";
        case EXPR_SHOCK:    return "SHOCK";
        case EXPR_OVERLOAD: return "OVERLOAD";
        case EXPR_SAD:      return "SAD";
        case EXPR_DEADPAN:  return "DEADPAN";
        default:            return "IDLE";
    }
}

void setNextExpression(Expression newExpr) {
    if (g_currentExpr != newExpr && !g_is_transitioning) {
        g_is_transitioning = true;
        transitionExpression(g_currentExpr, newExpr, 170.0f);
        g_animFrame = 0.0f;
        g_blinkState = BLINK_IDLE_STATE;
        g_blinkEyeHeight = 1.0f;
        uint32_t blink_iv = getPersonalityBlinkInterval();
        if (newExpr == EXPR_ANGRY) blink_iv = (uint32_t)(blink_iv * 1.20f);
        g_nextBlinkTime = millis() + blink_iv;
        g_is_transitioning = false;
    }
}

void updateBiologicalMoodEngine(void) {
    unsigned long now = millis();

    if (s_manual_expr_pending) {
        s_manual_expr_pending = false;
        int code = s_pending_expr_code;
        if (code < 0 || code > 7) {
            s_manual_override = false;
            s_manual_expr_code = -1;
            s_mood_lock_until = 0;
            s_nextMoodShiftTime = now + 4000;
            setNextExpression(EXPR_IDLE);
        } else {
            s_manual_override = true;
            s_manual_expr_code = code;
            s_manual_expr_start_ms = now;
            setNextExpression((Expression)code);
        }
        return;
    }

    if (s_manual_override) {
        /* Auto-revert manual expression to autonomous mood to prevent OLED burn-in */
        if (now - s_manual_expr_start_ms > MANUAL_EXPR_TIMEOUT_MS) {
            s_manual_override = false;
            s_manual_expr_code = -1;
            s_mood_lock_until = 0;
            s_nextMoodShiftTime = now + 3000;
            setNextExpression(EXPR_IDLE);
        }
        return;
    }

    if (g_is_transitioning || now < s_mood_lock_until) return;

    TrackTarget target;
    portENTER_CRITICAL(&g_target_mutex);
    target = g_current_target;
    portEXIT_CRITICAL(&g_target_mutex);

    float raw_pres = (target.detected && target.human_likelihood > HUMAN_LIKELIHOOD_THRESHOLD) ? 1.0f : 0.0f;
    s_target_presence_ema = s_target_presence_ema * 0.88f + raw_pres * 0.12f;
    bool is_detected = (s_target_presence_ema > 0.58f);

    float dt = (s_last_mood_update > 0) ? (float)(now - s_last_mood_update) * 0.001f : 0.050f;
    dt = constrain(dt, 0.01f, 0.20f);
    s_last_mood_update = now;

    /* Advance circadian rhythm state before computing emotional targets,
     * so that energy and mood baseline are current for this tick. */
    updateCircadianCycle();
    CircadianState circa = getCircadianState();

    /* 1. Advance Affective Continuous Dynamics (Langevin SDE)
     * Circadian mood_baseline shifts the emotional equilibrium point,
     * and circadian energy_level scales arousal responsiveness. */
    float target_v = 0.05f + circa.mood_baseline;
    float target_a = 0.12f * circa.energy_level;

    if (is_detected) {
        float bonding = getBrainBondingLevel();
        float motion_factor = fminf(1.0f, (fabsf(target.vx) + fabsf(target.vy)) * 0.02f);
        target_a = (0.18f + 0.12f * target.proximity + 0.35f * motion_factor) * circa.energy_level;
        target_v = 0.20f + 0.40f * bonding + circa.mood_baseline;
    }

    float tau_v = AFFECTIVE_TAU_VALENCE_S;
    float tau_a = AFFECTIVE_TAU_AROUSAL_S;
    s_emotion_valence += ((target_v - s_emotion_valence) / tau_v) * dt;
    s_emotion_arousal += ((target_a - s_emotion_arousal) / tau_a) * dt;

    /* Stochastic Langevin diffusion term: sigma scaled by personality volatility.
     * Volatile personalities experience larger mood fluctuations; stable
     * personalities maintain steadier emotional trajectories. */
    float sigma_v = getPersonalityValenceSigma();
    float sigma_a = getPersonalityArousalSigma();
    float sqrt_dt = sqrtf(dt);
    float noise_v = ((float)(esp_random() % 2000) - 1000.0f) * 0.001f;
    float noise_a = ((float)(esp_random() % 2000) - 1000.0f) * 0.001f;
    s_emotion_valence += sigma_v * sqrt_dt * noise_v;
    s_emotion_arousal += sigma_a * sqrt_dt * noise_a;

    /* Clamp emotional state to valid ranges */
    s_emotion_valence = constrain(s_emotion_valence, -1.0f, 1.0f);
    s_emotion_arousal = constrain(s_emotion_arousal, 0.0f, 1.0f);

    /* 2. Advance TinyML Micro-Brain Homeostatic Drives & Neural Policy Inference */
    updateBrainEngine(dt);

    /* 3. Event-Driven Biological State Transitions */
    if (is_detected && !s_lastTargetDetectedState) {
        s_lastTargetDetectedState = true;
        Expression reactExpr = sampleBrainExpressionPolicy();
        setNextExpression(reactExpr);
        s_mood_lock_until = now + (esp_random() % 2500 + 4000);
        s_nextMoodShiftTime = s_mood_lock_until + (esp_random() % 4000 + 4000);
        return;
    }

    if (!is_detected && s_lastTargetDetectedState && s_target_presence_ema < 0.15f) {
        s_lastTargetDetectedState = false;
        Expression reactExpr = sampleBrainExpressionPolicy();
        setNextExpression(reactExpr);
        s_mood_lock_until = now + (esp_random() % 2500 + 4000);
        s_nextMoodShiftTime = s_mood_lock_until + (esp_random() % 4000 + 4000);
        return;
    }

    if (s_nextMoodShiftTime == 0) {
        s_nextMoodShiftTime = now + (esp_random() % 6000 + 8000);
    }

    /* 4. Stochastic Periodic Neural Policy Re-Sampling */
    if (now >= s_nextMoodShiftTime) {
        Expression nextMood = sampleBrainExpressionPolicy();
        setNextExpression(nextMood);
        s_mood_lock_until = now + (esp_random() % 3000 + 5000);
        if (nextMood == EXPR_IDLE) {
            s_nextMoodShiftTime = s_mood_lock_until + (esp_random() % 7000 + 7000);
        } else {
            s_nextMoodShiftTime = s_mood_lock_until + (esp_random() % 4000 + 4000);
        }
    }
}
