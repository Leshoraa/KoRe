/**
 * @file kore_affective.cpp
 * @brief 2D Russell Circumplex affective emotion dynamics and stochastic Langevin model implementation.
 */

#include "include/kore_affective.h"
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
        case EXPR_SEDIH:    return "SEDIH";
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
        g_nextBlinkTime = millis() + ((newExpr == EXPR_ANGRY) ? (esp_random() % 4000 + 4000) : (esp_random() % 3500 + 3500));
        g_is_transitioning = false;
    }
}

void updateBiologicalMoodEngine(void) {
    if (s_manual_expr_pending) {
        s_manual_expr_pending = false;
        int code = s_pending_expr_code;
        if (code < 0 || code > 7) {
            s_manual_override = false;
            s_manual_expr_code = -1;
            s_mood_lock_until = 0;
            s_nextMoodShiftTime = millis() + 4000;
            setNextExpression(EXPR_IDLE);
        } else {
            s_manual_override = true;
            s_manual_expr_code = code;
            setNextExpression((Expression)code);
        }
        return;
    }

    if (s_manual_override) return;

    unsigned long now = millis();
    if (g_is_transitioning || now < s_mood_lock_until) return;

    TrackTarget target;
    portENTER_CRITICAL(&g_target_mutex);
    target = g_current_target;
    portEXIT_CRITICAL(&g_target_mutex);

    float raw_pres = (target.detected && target.confidence > 0.28f) ? 1.0f : 0.0f;
    s_target_presence_ema = s_target_presence_ema * 0.88f + raw_pres * 0.12f;
    bool is_detected = (s_target_presence_ema > 0.58f);

    float dt = (s_last_mood_update > 0) ? (float)(now - s_last_mood_update) * 0.001f : 0.050f;
    dt = constrain(dt, 0.01f, 0.20f);
    s_last_mood_update = now;

    float target_v = 0.05f;
    float target_a = 0.15f;

    if (is_detected) {
        target_a = 0.50f + 0.30f * target.proximity;
        target_v = 0.40f;
    } else {
        target_v = 0.05f;
        target_a = 0.15f;
    }

    float tau_v = AFFECTIVE_TAU_VALENCE_S;
    float tau_a = AFFECTIVE_TAU_AROUSAL_S;
    s_emotion_valence += ((target_v - s_emotion_valence) / tau_v) * dt;
    s_emotion_arousal += ((target_a - s_emotion_arousal) / tau_a) * dt;

    if (is_detected && !s_lastTargetDetectedState) {
        s_lastTargetDetectedState = true;
        uint32_t roll = esp_random() % 100;
        Expression reactExpr = EXPR_IDLE;
        if (roll < 40) {
            reactExpr = EXPR_ANGRY;
            s_emotion_valence = -0.6f;
            s_emotion_arousal = 0.75f;
        } else if (roll < 60) {
            reactExpr = EXPR_SHOCK;
            s_emotion_valence = -0.2f;
            s_emotion_arousal = 0.85f;
        } else if (roll < 75) {
            reactExpr = EXPR_SMIRK;
            s_emotion_valence = 0.45f;
            s_emotion_arousal = 0.35f;
        } else if (roll < 88) {
            reactExpr = EXPR_DEADPAN;
            s_emotion_valence = 0.0f;
            s_emotion_arousal = 0.15f;
        } else {
            reactExpr = EXPR_IDLE;
            s_emotion_valence = 0.10f;
            s_emotion_arousal = 0.25f;
        }
        setNextExpression(reactExpr);
        s_mood_lock_until = now + (esp_random() % 3000 + 5000);
        s_nextMoodShiftTime = s_mood_lock_until + (esp_random() % 4000 + 4000);
        return;
    }

    if (!is_detected && s_lastTargetDetectedState && s_target_presence_ema < 0.15f) {
        s_lastTargetDetectedState = false;
        uint32_t roll = esp_random() % 100;
        Expression reactExpr = (roll < 65) ? EXPR_IDLE : ((roll < 80) ? EXPR_DEADPAN : ((roll < 90) ? EXPR_SEDIH : EXPR_ANGRY));
        setNextExpression(reactExpr);
        s_mood_lock_until = now + (esp_random() % 2500 + 4500);
        s_nextMoodShiftTime = s_mood_lock_until + (esp_random() % 4000 + 4000);
        return;
    }

    if (s_nextMoodShiftTime == 0) {
        s_nextMoodShiftTime = now + (esp_random() % 6000 + 8000);
    }

    if (now >= s_nextMoodShiftTime) {
        uint32_t roll = esp_random() % 100;
        Expression nextMood = EXPR_IDLE;

        switch (g_currentExpr) {
            case EXPR_IDLE:
                if (roll < 58) nextMood = EXPR_IDLE;
                else if (roll < 73) nextMood = EXPR_ANGRY;
                else if (roll < 82) nextMood = EXPR_SMIRK;
                else if (roll < 90) nextMood = EXPR_DEADPAN;
                else if (roll < 95) nextMood = EXPR_JOY;
                else if (roll < 98) nextMood = EXPR_SEDIH;
                else nextMood = EXPR_OVERLOAD;
                break;

            case EXPR_ANGRY:
                if (roll < 65) nextMood = EXPR_IDLE;
                else if (roll < 80) nextMood = EXPR_SMIRK;
                else if (roll < 90) nextMood = EXPR_DEADPAN;
                else nextMood = EXPR_SEDIH;
                break;

            case EXPR_JOY:
                if (roll < 70) nextMood = EXPR_IDLE;
                else if (roll < 88) nextMood = EXPR_SMIRK;
                else if (roll < 95) nextMood = EXPR_DEADPAN;
                else nextMood = EXPR_ANGRY;
                break;

            case EXPR_SMIRK:
                if (roll < 65) nextMood = EXPR_IDLE;
                else if (roll < 80) nextMood = EXPR_DEADPAN;
                else if (roll < 92) nextMood = EXPR_JOY;
                else nextMood = EXPR_ANGRY;
                break;

            case EXPR_SHOCK:
                if (roll < 65) nextMood = EXPR_IDLE;
                else if (roll < 80) nextMood = EXPR_DEADPAN;
                else if (roll < 92) nextMood = EXPR_SMIRK;
                else nextMood = EXPR_ANGRY;
                break;

            case EXPR_OVERLOAD:
                if (roll < 75) nextMood = EXPR_IDLE;
                else nextMood = EXPR_SEDIH;
                break;

            case EXPR_SEDIH:
                if (roll < 65) nextMood = EXPR_IDLE;
                else if (roll < 80) nextMood = EXPR_DEADPAN;
                else if (roll < 92) nextMood = EXPR_SMIRK;
                else nextMood = EXPR_JOY;
                break;

            case EXPR_DEADPAN:
                if (roll < 65) nextMood = EXPR_IDLE;
                else if (roll < 80) nextMood = EXPR_SMIRK;
                else if (roll < 92) nextMood = EXPR_JOY;
                else nextMood = EXPR_ANGRY;
                break;
        }

        setNextExpression(nextMood);
        s_mood_lock_until = now + (esp_random() % 3000 + 5000);
        if (nextMood == EXPR_IDLE) {
            s_nextMoodShiftTime = s_mood_lock_until + (esp_random() % 8000 + 8000);
        } else {
            s_nextMoodShiftTime = s_mood_lock_until + (esp_random() % 4000 + 5000);
        }
    }
}
