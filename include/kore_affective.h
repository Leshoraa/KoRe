/**
 * @file kore_affective.h
 * @brief 2D Russell Circumplex affective emotion dynamics and stochastic Langevin model.
 */

#ifndef KORE_AFFECTIVE_H
#define KORE_AFFECTIVE_H

#include <stdint.h>
#include <stdbool.h>
#include "include/kore_config.h"
#include "include/kore_types.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Expression g_currentExpr;
extern float g_animFrame;
extern BlinkState g_blinkState;
extern float g_blinkEyeHeight;
extern uint32_t g_nextBlinkTime;

void updateBiologicalMoodEngine(void);
void setNextExpression(Expression newExpr);
float getEmotionValence(void);
float getEmotionArousal(void);

#ifdef __cplusplus
}
#endif

#endif /* KORE_AFFECTIVE_H */
