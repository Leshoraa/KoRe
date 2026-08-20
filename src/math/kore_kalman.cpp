/**
 * @file kore_kalman.cpp
 * @brief Discrete 2D linear Kalman tracking filter implementation.
 */

#include "include/kore_kalman.h"
#include <math.h>

#define KALMAN_EPSILON 1e-6f

void kf1d_init(KalmanFilter1D *kf, float init_p) {
    if (!kf) return;
    kf->p   = init_p;
    kf->v   = 0.0f;
    kf->P00 = 100.0f;
    kf->P01 = 0.0f;
    kf->P11 = 100.0f;
}

void kf1d_predict(KalmanFilter1D *kf, float dt, float q_accel) {
    if (!kf || dt <= 0.0f || isnan(dt) || isinf(dt)) return;

    /* State transition: p_new = p + v * dt */
    kf->p = kf->p + kf->v * dt;

    /* Process noise covariance matrix Q for continuous acceleration variance q */
    float dt2 = dt * dt;
    float dt3 = dt2 * dt;
    float dt4 = dt3 * dt;
    float Q00 = 0.25f * dt4 * q_accel;
    float Q01 = 0.50f * dt3 * q_accel;
    float Q11 = dt2 * q_accel;

    float P00_new = kf->P00 + dt * (kf->P01 + kf->P01) + dt2 * kf->P11 + Q00;
    float P01_new = kf->P01 + dt * kf->P11 + Q01;
    float P11_new = kf->P11 + Q11;

    kf->P00 = P00_new;
    kf->P01 = P01_new;
    kf->P11 = P11_new;
}

void kf1d_update(KalmanFilter1D *kf, float z, float R) {
    if (!kf || isnan(z) || isinf(z)) return;

    float y = z - kf->p;              /* Innovation residual */
    float S = kf->P00 + R;            /* Innovation covariance */
    if (S < KALMAN_EPSILON) S = KALMAN_EPSILON;
    float invS = 1.0f / S;

    float K0 = kf->P00 * invS;        /* Position Kalman gain */
    float K1 = kf->P01 * invS;        /* Velocity Kalman gain */

    kf->p = kf->p + K0 * y;
    kf->v = kf->v + K1 * y;

    /* Joseph-stabilized covariance update formulation */
    float P00_temp = (1.0f - K0) * kf->P00;
    float P01_temp = (1.0f - K0) * kf->P01;
    float P11_temp = kf->P11 - K1 * kf->P01;

    /* Enforce positive semi-definiteness */
    kf->P00 = fmaxf(1e-3f, P00_temp);
    kf->P01 = P01_temp;
    kf->P11 = fmaxf(1e-3f, P11_temp);
}

void kf2d_tracker_init(KalmanTracker2D *tracker) {
    if (!tracker) return;
    kf1d_init(&tracker->kf_x, 320.0f);
    kf1d_init(&tracker->kf_y, 240.0f);
    tracker->w = 160.0f;
    tracker->h = 200.0f;
    tracker->active = false;
    tracker->last_update_us = 0;
}

float kf2d_compute_dynamic_r(float dist_innovation, int skin_pixel_cnt, float lock_conf) {
    float R_base = 25.0f;
    float R_dynamic;

    if (skin_pixel_cnt >= 4) {
        if (dist_innovation < 6.0f) {
            R_dynamic = R_base * (1.5f + 1.5f * lock_conf);
        } else {
            R_dynamic = R_base / (1.0f + 0.12f * (dist_innovation - 6.0f));
        }
    } else {
        R_dynamic = R_base * 2.0f;
    }

    if (R_dynamic < 3.0f) R_dynamic = 3.0f;
    if (R_dynamic > 150.0f) R_dynamic = 150.0f;
    return R_dynamic;
}
