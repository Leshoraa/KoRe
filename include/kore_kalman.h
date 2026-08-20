/**
 * @file kore_kalman.h
 * @brief Discrete 2D linear Kalman tracking filter with dynamic measurement covariance.
 */

#ifndef KORE_KALMAN_H
#define KORE_KALMAN_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct KalmanFilter1D
 * @brief Discrete 1D linear Kalman filter state vector [p, v]^T.
 */
typedef struct {
    float p;       /* Estimated position (pixels) */
    float v;       /* Estimated velocity (pixels/sec) */
    float P00;     /* State covariance P[0,0] */
    float P01;     /* State covariance P[0,1] */
    float P11;     /* State covariance P[1,1] */
} KalmanFilter1D;

/**
 * @struct KalmanTracker2D
 * @brief 2D Cartesian target tracking container managing X and Y Kalman channels.
 */
typedef struct {
    KalmanFilter1D kf_x;
    KalmanFilter1D kf_y;
    float w;
    float h;
    bool active;
    uint32_t last_update_us;
} KalmanTracker2D;

/**
 * @brief Initializes a 1D Kalman filter state vector and prior covariance matrix.
 * @param[in,out] kf      Pointer to Kalman filter 1D structure.
 * @param[in]     init_p  Initial position estimate in pixels.
 *
 * @pre kf != NULL.
 * @post kf->p == init_p && kf->v == 0.0f.
 * @complexity Time: O(1), Space: O(1).
 * @thread_safety Requires external mutex protection when shared across tasks.
 */
void kf1d_init(KalmanFilter1D *kf, float init_p);

/**
 * @brief Executes Kalman prediction step using constant velocity state transition.
 * @param[in,out] kf       Pointer to Kalman filter 1D structure.
 * @param[in]     dt       Delta time in seconds since previous filter update.
 * @param[in]     q_accel  Process noise acceleration variance (px/s^2).
 *
 * @pre kf != NULL && dt > 0.0f && !isnan(dt) && !isinf(dt).
 * @complexity Time: O(1), Space: O(1).
 * @thread_safety Requires external mutex protection.
 */
void kf1d_predict(KalmanFilter1D *kf, float dt, float q_accel);

/**
 * @brief Updates 1D Kalman state with measurement and Joseph-stabilized covariance.
 * @param[in,out] kf  Pointer to Kalman filter 1D structure.
 * @param[in]     z   Measurement position in pixels.
 * @param[in]     R   Measurement noise covariance variance.
 *
 * @pre kf != NULL && !isnan(z) && !isinf(z) && R > 0.0f.
 * @complexity Time: O(1), Space: O(1).
 * @thread_safety Requires external mutex protection.
 */
void kf1d_update(KalmanFilter1D *kf, float z, float R);

/**
 * @brief Initializes full 2D Cartesian Kalman tracker state.
 * @param[in,out] tracker Pointer to 2D tracker structure.
 */
void kf2d_tracker_init(KalmanTracker2D *tracker);

/**
 * @brief Computes dynamic measurement noise covariance R based on target innovation and skin ratio.
 * @param[in] dist_innovation Innovation distance between measurement and prior estimate.
 * @param[in] skin_pixel_cnt  Number of skin classified pixels.
 * @param[in] lock_conf       Current tracking lock confidence in range [0.0, 1.0].
 * @return Dynamic R measurement noise variance clamped within [3.0, 150.0].
 */
float kf2d_compute_dynamic_r(float dist_innovation, int skin_pixel_cnt, float lock_conf);

#ifdef __cplusplus
}
#endif

#endif /* KORE_KALMAN_H */
