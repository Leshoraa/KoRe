/**
 * @file kore_kinematics.h
 * @brief Biomechanical ocular kinematics, mass-spring-damper, and minimum-jerk solver.
 */

#ifndef KORE_KINEMATICS_H
#define KORE_KINEMATICS_H

#include <stdint.h>
#include <stdbool.h>
#include "include/kore_config.h"
#include "include/kore_types.h"

#ifdef __cplusplus
extern "C" {
#endif

extern float g_currentOffsetX;
extern float g_currentOffsetY;
extern float g_currentVergence;
extern float g_currentEyeScale;
extern bool g_is_transitioning;

float eval_minimum_jerk_spline(float p);
uint32_t compute_saccade_duration_ms(float displacement_px);
float easeInOutCubic(float t);
float blinkCloseEase(float t);
float blinkOpenEase(float t);
float customLerp(float a, float b, float t);
int getFilteredOx(float rawOffsetX);
int getFilteredOy(float rawOffsetY);
void resetHysteresisFilter(void);
void updateGazeSystem(void);

#ifdef __cplusplus
}
#endif

#endif /* KORE_KINEMATICS_H */
