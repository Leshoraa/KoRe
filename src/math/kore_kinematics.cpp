/**
 * @file kore_kinematics.cpp
 * @brief Biomechanical ocular kinematics, mass-spring-damper, and minimum-jerk solver implementation.
 */

#include "include/kore_kinematics.h"
#include "include/kore_personality.h"
#include "include/kore_config.h"
#include "include/kore_types.h"
#include <math.h>
#include <Arduino.h>
#include <esp_random.h>

/* Global Kinematic State */
float g_currentOffsetX = 0.0f;
float g_currentOffsetY = 0.0f;
float g_currentVergence = 0.0f;
float g_currentEyeScale = 1.0f;
float g_currentEyeScaleX = 1.0f;
float g_currentEyeScaleY = 1.0f;
bool g_is_transitioning = false;

/* Internal Dynamic State */
static float s_eye_vx = 0.0f;
static float s_eye_vy = 0.0f;
static float s_smoothedTargetX = 0.0f;
static float s_smoothedTargetY = 0.0f;
static bool s_trackInSaccade = false;
static uint32_t s_trackSaccadeStart = 0;
static uint32_t s_trackSaccadeDuration = 60;
static float s_trackSaccadeStartX = 0.0f;
static float s_trackSaccadeStartY = 0.0f;
static float s_trackSaccadeTargetX = 0.0f;
static float s_trackSaccadeTargetY = 0.0f;

static float s_startOffsetX = 0.0f;
static float s_startOffsetY = 0.0f;
static float s_targetOffsetX = 0.0f;
static float s_targetOffsetY = 0.0f;
static unsigned long s_gazeStartTime = 0;
static unsigned long s_gazeDuration = 120;
static unsigned long s_nextGazeTime = 0;
static bool s_inSaccade = false;

static bool s_prevTargetDetected = false;
static float s_deadbandTargetX = 0.0f;
static float s_deadbandTargetY = 0.0f;
static bool s_hasTargetLock = false;

/* Coordinate Hysteresis Filter State */
static float s_stable_ox = 0.0f;
static float s_stable_oy = 0.0f;

float eval_minimum_jerk_spline(float p) {
    if (p <= 0.0f) return 0.0f;
    if (p >= 1.0f) return 1.0f;
    float base_spline = p * p * p * (10.0f + p * (-15.0f + 6.0f * p));
    /* Post-saccadic ocular glissade rebound upon target landing */
    if (p > 0.70f) {
        float norm_tail = (p - 0.70f) / 0.30f;
        float glissade = GLISSADE_REBOUND_GAIN * sinf(norm_tail * 3.14159265f) * expf(-3.5f * norm_tail);
        base_spline += glissade;
    }
    return constrain(base_spline, 0.0f, 1.06f);
}

uint32_t compute_saccade_duration_ms(float displacement_px) {
    float deg = fabsf(displacement_px) * PX_TO_DEG_FACTOR;
    float dur = SACCADE_D0_MS + SACCADE_K_MS_PER_DEG * deg;
    if (dur < 110.0f) dur = 110.0f;
    if (dur > 260.0f) dur = 260.0f;
    return (uint32_t)dur;
}

float easeInOutCubic(float t) {
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    } else {
        float f = (-2.0f * t + 2.0f);
        return 1.0f - (f * f * f) / 2.0f;
    }
}

float eval_elastic_bounce_ease(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    /* Analytical Kelvin-Voigt underdamped transient compliance (zeta = 0.72, omega = 7.2 rad/s) */
    const float zeta = 0.72f;
    const float omega = 7.2f;
    const float omega_d = 4.996f; // omega * sqrt(1 - zeta^2)
    const float decay = expf(-zeta * omega * t);
    float response = 1.0f - decay * (cosf(omega_d * t) + (zeta / 0.6939f) * sinf(omega_d * t));
    return response;
}

void compute_squash_stretch_factors(float progress, float arousal, float* scaleX, float* scaleY) {
    if (!scaleX || !scaleY) return;
    if (progress <= 0.0f || progress >= 1.0f) {
        *scaleX = 1.0f;
        *scaleY = 1.0f;
        return;
    }
    /* Biological incompressibility volume conservation: Sy * Sx^2 ≈ 1.0 -> Sx = 1.0 / sqrt(Sy) */
    float effective_arousal = constrain(arousal, 0.20f, 1.0f);
    float bounce_amp = BOUNCE_SQUASH_STRETCH_GAIN * effective_arousal;
    float phase = progress * 6.2831853f; // 2 * PI
    float decay = expf(-3.2f * progress);
    float delta_y = bounce_amp * sinf(phase) * decay;
    
    float sy = constrain(1.0f + delta_y, 0.80f, 1.25f);
    float sx = constrain(1.0f / sqrtf(sy), 0.80f, 1.25f);
    
    *scaleX = sx;
    *scaleY = sy;
}

float blinkCloseEase(float t) {
    return 1.0f - (t * t);
}

float blinkOpenEase(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    /* Fast-twitch muscular pop with 5.5% elastic overshoot */
    float base = sinf(t * 1.5707963f);
    float pop = 0.055f * sinf(t * 3.14159265f) * expf(-3.8f * t);
    return constrain(base + pop, 0.0f, 1.08f);
}

float customLerp(float a, float b, float t) {
    return a + t * (b - a);
}

int getFilteredOx(float rawOffsetX) {
    if (fabsf(rawOffsetX - s_stable_ox) >= 0.55f) {
        s_stable_ox = roundf(rawOffsetX);
    }
    return (int)s_stable_ox;
}

int getFilteredOy(float rawOffsetY) {
    if (fabsf(rawOffsetY - s_stable_oy) >= 0.55f) {
        s_stable_oy = roundf(rawOffsetY);
    }
    return (int)s_stable_oy;
}

void resetHysteresisFilter(void) {
    s_stable_ox = 0.0f;
    s_stable_oy = 0.0f;
}

void updateGazeSystem(void) {
    TrackTarget target;
    portENTER_CRITICAL(&g_target_mutex);
    target = g_current_target;
    portEXIT_CRITICAL(&g_target_mutex);

    unsigned long now = millis();
    static uint32_t lastGazeTimeUs = 0;
    uint32_t nowUs = micros();
    float dt = (lastGazeTimeUs > 0) ? (float)(nowUs - lastGazeTimeUs) * 0.000001f : 0.016666f;
    if (dt < 0.005f) dt = 0.005f;
    if (dt > 0.040f) dt = 0.040f;
    lastGazeTimeUs = nowUs;

    if (g_is_transitioning) {
        bool targetActive = (g_recon_state == STATE_ACTIVE) && (target.detected || ((now - target.last_seen_ms) < 300 && target.last_seen_ms > 0));
        if (targetActive) {
            float normX = constrain((target.error_x / 100.0f) * GAZE_GAIN_X, -1.0f, 1.0f);
            float normY = constrain((target.error_y / 100.0f) * GAZE_GAIN_Y, -1.0f, 1.0f);
            float rawTargetX = normX * 22.0f;
            float rawTargetY = normY * 14.0f;

            if (!s_hasTargetLock || !s_prevTargetDetected) {
                s_deadbandTargetX = rawTargetX;
                s_deadbandTargetY = rawTargetY;
                s_hasTargetLock = true;
                s_prevTargetDetected = true;
            } else {
                float deltaX = rawTargetX - s_deadbandTargetX;
                float deltaY = rawTargetY - s_deadbandTargetY;
                float deltaDist = sqrtf(deltaX * deltaX + deltaY * deltaY);
                if (deltaDist > GAZE_DEADBAND_RADIUS_PX) {
                    float excess = (deltaDist - GAZE_DEADBAND_RADIUS_PX) / (deltaDist + 1e-6f);
                    s_deadbandTargetX += deltaX * excess * 0.40f;
                    s_deadbandTargetY += deltaY * excess * 0.40f;
                }
            }

            float alpha = 1.0f - expf(-20.0f * dt);
            s_smoothedTargetX += (s_deadbandTargetX - s_smoothedTargetX) * alpha;
            s_smoothedTargetY += (s_deadbandTargetY - s_smoothedTargetY) * alpha;
        }
        s_eye_vx = 0.0f;
        s_eye_vy = 0.0f;
        s_trackInSaccade = false;
        s_inSaccade = false;
        return;
    }

    bool targetActive = (g_recon_state == STATE_ACTIVE) && target.detected;
    if (targetActive) {
        float normX = constrain((target.error_x / 100.0f) * GAZE_GAIN_X, -1.0f, 1.0f);
        float normY = constrain((target.error_y / 100.0f) * GAZE_GAIN_Y, -1.0f, 1.0f);
        float rawTargetX = normX * 22.0f;
        float rawTargetY = normY * 14.0f;

        if (!s_hasTargetLock || !s_prevTargetDetected) {
            s_deadbandTargetX = rawTargetX;
            s_deadbandTargetY = rawTargetY;
            s_hasTargetLock = true;
        } else {
            float deltaX = rawTargetX - s_deadbandTargetX;
            float deltaY = rawTargetY - s_deadbandTargetY;
            float deltaDist = sqrtf(deltaX * deltaX + deltaY * deltaY);
            if (deltaDist > GAZE_DEADBAND_RADIUS_PX) {
                float excess = (deltaDist - GAZE_DEADBAND_RADIUS_PX) / (deltaDist + 1e-6f);
                s_deadbandTargetX += deltaX * excess * 0.40f;
                s_deadbandTargetY += deltaY * excess * 0.40f;
            }
        }

        float effectiveTargetX = s_deadbandTargetX;
        float effectiveTargetY = s_deadbandTargetY;

        if (!s_prevTargetDetected) {
            s_prevTargetDetected = true;
            float dist_init = sqrtf((effectiveTargetX - g_currentOffsetX) * (effectiveTargetX - g_currentOffsetX) +
                                    (effectiveTargetY - g_currentOffsetY) * (effectiveTargetY - g_currentOffsetY));
            if (dist_init > 10.0f) {
                s_trackInSaccade = true;
                s_trackSaccadeStart = now;
                s_trackSaccadeStartX = g_currentOffsetX;
                s_trackSaccadeStartY = g_currentOffsetY;
                s_trackSaccadeTargetX = effectiveTargetX;
                s_trackSaccadeTargetY = effectiveTargetY;
                s_trackSaccadeDuration = compute_saccade_duration_ms(dist_init);
            } else {
                s_trackInSaccade = false;
            }
            s_smoothedTargetX = g_currentOffsetX;
            s_smoothedTargetY = g_currentOffsetY;
            s_eye_vx = 0.0f;
            s_eye_vy = 0.0f;
            s_inSaccade = false;
        }

        float alpha = 1.0f - expf(-20.0f * dt);
        s_smoothedTargetX += (effectiveTargetX - s_smoothedTargetX) * alpha;
        s_smoothedTargetY += (effectiveTargetY - s_smoothedTargetY) * alpha;

        g_currentVergence = 0.0f;
        g_currentEyeScale = 1.0f;

        float dx_eye = effectiveTargetX - g_currentOffsetX;
        float dy_eye = effectiveTargetY - g_currentOffsetY;
        float dist_eye = sqrtf(dx_eye * dx_eye + dy_eye * dy_eye);

        if (dist_eye > 15.0f && !s_trackInSaccade) {
            s_trackInSaccade = true;
            s_trackSaccadeStart = now;
            s_trackSaccadeDuration = compute_saccade_duration_ms(dist_eye);
            s_trackSaccadeStartX = g_currentOffsetX;
            s_trackSaccadeStartY = g_currentOffsetY;
            s_trackSaccadeTargetX = effectiveTargetX;
            s_trackSaccadeTargetY = effectiveTargetY;
            s_eye_vx = 0.0f;
            s_eye_vy = 0.0f;
        }

        if (s_trackInSaccade) {
            float elapsed = (float)(now - s_trackSaccadeStart);
            float progress = elapsed / (float)s_trackSaccadeDuration;

            if (progress >= 1.0f) {
                g_currentOffsetX = s_trackSaccadeTargetX;
                g_currentOffsetY = s_trackSaccadeTargetY;
                s_smoothedTargetX = s_trackSaccadeTargetX;
                s_smoothedTargetY = s_trackSaccadeTargetY;
                s_trackInSaccade = false;
            } else {
                float s = eval_minimum_jerk_spline(progress);
                float distX = s_trackSaccadeTargetX - s_trackSaccadeStartX;
                float distY = s_trackSaccadeTargetY - s_trackSaccadeStartY;
                g_currentOffsetX = s_trackSaccadeStartX + (distX * s);
                g_currentOffsetY = s_trackSaccadeStartY + (distY * s);
            }
        } else {
            /* Personality-modulated spring-damper parameters.
             * Bold + high energy: snappy underdamped (omega up, zeta down).
             * Shy + low energy: sluggish overdamped (omega down, zeta up). */
            float omega_n = getPersonalityGazeOmega();
            float zeta = getPersonalityGazeDamping();

            float ax = (omega_n * omega_n) * (s_smoothedTargetX - g_currentOffsetX) - (2.0f * zeta * omega_n) * s_eye_vx;
            float ay = (omega_n * omega_n) * (s_smoothedTargetY - g_currentOffsetY) - (2.0f * zeta * omega_n) * s_eye_vy;

            s_eye_vx += ax * dt;
            s_eye_vy += ay * dt;

            if (fabsf(s_smoothedTargetX - g_currentOffsetX) < 0.30f && fabsf(s_eye_vx) < 1.2f) {
                s_eye_vx *= 0.60f;
            }
            if (fabsf(s_smoothedTargetY - g_currentOffsetY) < 0.30f && fabsf(s_eye_vy) < 1.2f) {
                s_eye_vy *= 0.60f;
            }

            g_currentOffsetX += s_eye_vx * dt;
            g_currentOffsetY += s_eye_vy * dt;
        }

        g_currentOffsetX = constrain(g_currentOffsetX, -17.5f, 17.5f);
        g_currentOffsetY = constrain(g_currentOffsetY, -12.0f, 11.0f);

        s_inSaccade = false;
        s_nextGazeTime = now + 800;
        return;
    }

    /* On target loss: smoothly ease eyes back to center before resuming calm idle exploration */
    if (s_prevTargetDetected) {
        s_prevTargetDetected = false;
        s_hasTargetLock = false;
        s_trackInSaccade = false;
        s_inSaccade = false;
        s_eye_vx = 0.0f;
        s_eye_vy = 0.0f;
        /* Pause at center for 2.5 - 4.0 seconds before starting idle saccades */
        s_nextGazeTime = now + (uint32_t)(esp_random() % 1500 + 2500);
    }

    float alpha_decay = 1.0f - expf(-8.0f * dt);
    g_currentVergence += (0.0f - g_currentVergence) * alpha_decay;
    g_currentEyeScale += (1.0f - g_currentEyeScale) * alpha_decay;

    bool isSleep = (g_recon_state == STATE_SLEEP_RECON);
    float y_bias = getPersonalityIdleGazeYBias();

    if (!s_inSaccade && !g_is_transitioning && now >= s_nextGazeTime) {
        s_startOffsetX = g_currentOffsetX;
        s_startOffsetY = g_currentOffsetY;

        if (isSleep) {
            uint32_t pick = esp_random() % 100;
            float distFromCenter = sqrtf(s_startOffsetX * s_startOffsetX + s_startOffsetY * s_startOffsetY);

            if (pick < 35 && distFromCenter >= 3.0f) {
                s_targetOffsetX = ((float)(esp_random() % 20) - 10.0f) * 0.1f;
                s_targetOffsetY = ((float)(esp_random() % 16) - 8.0f) * 0.1f;
            } else if (pick < 75) {
                float signX = (s_startOffsetX > 1.0f) ? -1.0f : ((s_startOffsetX < -1.0f) ? 1.0f : ((esp_random() % 2 == 0) ? -1.0f : 1.0f));
                s_targetOffsetX = signX * (3.5f + (float)(esp_random() % 500) * 0.01f);
                s_targetOffsetY = ((float)(esp_random() % 400) - 200.0f) * 0.01f;
            } else {
                float signX = (esp_random() % 2 == 0) ? -1.0f : 1.0f;
                float signY = (esp_random() % 2 == 0) ? -1.0f : 1.0f;
                s_targetOffsetX = signX * (3.0f + (float)(esp_random() % 400) * 0.01f);
                s_targetOffsetY = signY * (2.0f + (float)(esp_random() % 300) * 0.01f);
            }

            s_targetOffsetX = constrain(s_targetOffsetX, -12.0f, 12.0f);
            s_targetOffsetY = constrain(s_targetOffsetY, -8.0f, 7.0f);

            float ds = sqrtf((s_targetOffsetX - s_startOffsetX) * (s_targetOffsetX - s_startOffsetX) +
                             (s_targetOffsetY - s_startOffsetY) * (s_targetOffsetY - s_startOffsetY));
            s_gazeDuration = compute_saccade_duration_ms(ds);
            s_nextGazeTime = now + s_gazeDuration + (esp_random() % 2000 + 3000);
            s_gazeStartTime = now;
            s_inSaccade = true;
        } else {
            /* Organic idle saccade distribution:
             * 60% center micro-shifts (calm attentive resting),
             * 28% gentle peripheral glances, 12% wider curiosities. */
            uint32_t pick = esp_random() % 100;
            if (pick < 60) {
                /* Subtle micro-shift around center */
                s_targetOffsetX = ((float)(esp_random() % 70) - 35.0f) * 0.1f;
                s_targetOffsetY = ((float)(esp_random() % 40) - 20.0f) * 0.1f + y_bias * 0.5f;
            } else if (pick < 88) {
                /* Gentle glance */
                float signX = (esp_random() % 2 == 0) ? -1.0f : 1.0f;
                s_targetOffsetX = signX * (4.0f + (float)(esp_random() % 40) * 0.1f);
                s_targetOffsetY = ((float)(esp_random() % 50) - 25.0f) * 0.1f + y_bias;
            } else {
                /* Wider exploratory glance */
                float signX = (esp_random() % 2 == 0) ? -1.0f : 1.0f;
                s_targetOffsetX = signX * (8.0f + (float)(esp_random() % 35) * 0.1f);
                s_targetOffsetY = ((float)(esp_random() % 60) - 30.0f) * 0.1f + y_bias;
            }

            s_targetOffsetX = constrain(s_targetOffsetX, -14.0f, 14.0f);
            s_targetOffsetY = constrain(s_targetOffsetY, -9.0f, 8.0f);

            float ds = sqrtf((s_targetOffsetX - s_startOffsetX) * (s_targetOffsetX - s_startOffsetX) +
                             (s_targetOffsetY - s_startOffsetY) * (s_targetOffsetY - s_startOffsetY));
            s_gazeDuration = compute_saccade_duration_ms(ds);
            /* Natural inter-saccade intervals (3.0s - 6.0s) scaled by personality */
            float interval_scale = getPersonalityIdleIntervalScale();
            s_nextGazeTime = now + s_gazeDuration + (uint32_t)(interval_scale * (float)(esp_random() % 2500 + 3200));
            s_gazeStartTime = now;
            s_inSaccade = true;
        }
    }

    if (s_inSaccade) {
        float elapsed = (float)(now - s_gazeStartTime);
        float progress = elapsed / (float)s_gazeDuration;

        if (progress >= 1.0f) {
            g_currentOffsetX = s_targetOffsetX;
            g_currentOffsetY = s_targetOffsetY;
            s_inSaccade = false;
        } else {
            float s = eval_minimum_jerk_spline(progress);
            float distX = s_targetOffsetX - s_startOffsetX;
            float distY = s_targetOffsetY - s_startOffsetY;
            g_currentOffsetX = s_startOffsetX + (distX * s);
            g_currentOffsetY = s_startOffsetY + (distY * s);
        }
    } else {
        float u1 = ((float)(esp_random() % 1000) - 500.0f) * 0.001f;
        float u2 = ((float)(esp_random() % 1000) - 500.0f) * 0.001f;
        float drift_sigma = 0.03f * sqrtf(dt);
        s_eye_vx += u1 * drift_sigma;
        s_eye_vy += u2 * drift_sigma;
        s_eye_vx *= 0.88f;
        s_eye_vy *= 0.88f;

        g_currentOffsetX += s_eye_vx;
        g_currentOffsetY += s_eye_vy;
    }

    g_currentOffsetX = constrain(g_currentOffsetX, -17.5f, 17.5f);
    g_currentOffsetY = constrain(g_currentOffsetY, -12.0f, 11.0f);
}
