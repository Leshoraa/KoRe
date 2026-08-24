/**
 * @file display_engine.cpp
 * @brief LovyanGFX SSD1306 display driver and rigid facial rig composition engine implementation.
 */

#include "src/core/display_engine.h"
#include "src/net/wifi_manager.h"
#include "include/kore_config.h"
#include "include/kore_types.h"
#include "include/kore_kinematics.h"
#include "include/kore_affective.h"
#include <Arduino.h>
#include <esp_random.h>
#include <time.h>
#include <sys/time.h>

LGFX lcd;
LGFX_Sprite canvas(&lcd);

static ReconState s_prev_recon_state = STATE_ACTIVE;
static bool s_isDoubleBlinkPending = false;
static int s_burn_shift_x = 0;
static int s_burn_shift_y = 0;
static uint32_t s_last_burn_shift_ms = 0;

void showBootStatus(const char* line1, const char* line2) {
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextColor(TFT_WHITE);
    lcd.setTextSize(1);
    lcd.setCursor(4, 20);
    if (line1) lcd.print(line1);
    if (line2) {
        lcd.setCursor(4, 36);
        lcd.print(line2);
    }
}

static void drawDualEyes(float leftHeightFactor, float rightHeightFactor, int ox, int oy, uint16_t color, float vergence, float scale) {
    (void)vergence;
    int lx = 32 + ox;
    int rx = 96 + ox;
    int ly = 28 + oy;
    int ry = 28 + oy;

    float sx = (g_currentEyeScaleX > 0.01f) ? (scale * g_currentEyeScaleX) : scale;
    float sy = (g_currentEyeScaleY > 0.01f) ? (scale * g_currentEyeScaleY) : scale;

    int maxEyeWidth = (int)roundf(28.0f * sx);
    int maxEyeHeight = (int)roundf(38.0f * sy);

    int leftHeight = (int)roundf((float)maxEyeHeight * leftHeightFactor);
    int rightHeight = (int)roundf((float)maxEyeHeight * rightHeightFactor);

    if (leftHeight <= 3) {
        canvas.fillRoundRect(lx - maxEyeWidth / 2, ly - 1, maxEyeWidth, 3, 1, color);
    } else {
        int radius = (leftHeight < 24) ? leftHeight / 2 : (int)roundf(12.0f * sx);
        if (radius > maxEyeWidth / 2) radius = maxEyeWidth / 2;
        if (radius < 1) radius = 1;
        canvas.fillRoundRect(lx - maxEyeWidth / 2, ly - leftHeight / 2, maxEyeWidth, leftHeight, radius, color);
    }

    if (rightHeight <= 3) {
        canvas.fillRoundRect(rx - maxEyeWidth / 2, ry - 1, maxEyeWidth, 3, 1, color);
    } else {
        int radius = (rightHeight < 24) ? rightHeight / 2 : (int)roundf(12.0f * sx);
        if (radius > maxEyeWidth / 2) radius = maxEyeWidth / 2;
        if (radius < 1) radius = 1;
        canvas.fillRoundRect(rx - maxEyeWidth / 2, ry - rightHeight / 2, maxEyeWidth, rightHeight, radius, color);
    }
}

static void drawEyes(float eyeHeightFactor, int ox, int oy, uint16_t color, float vergence, float scale) {
    drawDualEyes(eyeHeightFactor, eyeHeightFactor, ox, oy, color, vergence, scale);
}

static void drawFumoEye(int cx, int cy, int w, int h, int r, uint16_t color) {
    if (h <= 3) {
        canvas.fillRoundRect(cx - w / 2, cy - 1, w, 3, 1, color);
        return;
    }
    int topY = cy - h / 2;
    int leftX = cx - w / 2;
    int rad = (r > h) ? h : ((r > w / 2) ? w / 2 : r);
    if (rad < 1) rad = 1;

    if (h > rad) {
        canvas.fillRect(leftX, topY, w, h - rad, color);
    }
    canvas.fillRoundRect(leftX, topY + h - rad * 2, w, rad * 2, rad, color);
}

static void drawFumoEyes(float eyeHeightFactor, int ox, int oy, uint16_t color, float vergence, float scale) {
    (void)vergence;
    int lx = 32 + ox;
    int rx = 96 + ox;
    int ly = 28 + oy;
    int ry = 28 + oy;

    float sx = (g_currentEyeScaleX > 0.01f) ? (scale * g_currentEyeScaleX) : scale;
    float sy = (g_currentEyeScaleY > 0.01f) ? (scale * g_currentEyeScaleY) : scale;

    int maxEyeWidth = (int)roundf(28.0f * sx);
    int maxEyeHeight = (int)roundf(34.0f * sy);
    int eyeHeight = (int)roundf((float)maxEyeHeight * eyeHeightFactor);
    int topRad = (int)roundf(14.0f * sx);

    drawFumoEye(lx, ly, maxEyeWidth, eyeHeight, topRad, color);
    drawFumoEye(rx, ry, maxEyeWidth, eyeHeight, topRad, color);
}

static void drawJoyEyes(int ox, int oy, float joyScale, uint16_t color, float vergence, float scale) {
    (void)vergence;
    (void)scale;
    if (joyScale <= 0.05f) return;
    int lx = 32 + ox;
    int rx = 96 + ox;
    int ly = 31 + oy;

    float eyeWidth = 24.0f * joyScale;
    float archHeight = 9.0f * joyScale;

    for (int eye = 0; eye < 2; eye++) {
        int cx = (eye == 0) ? lx : rx;
        for (float x = -eyeWidth / 2.0f; x <= eyeWidth / 2.0f; x += 0.3f) {
            float angle = (x / (eyeWidth / 2.0f)) * (3.14159265f / 2.0f);
            float y = (float)ly - archHeight * cosf(angle);
            canvas.fillCircle(cx + (int)roundf(x), (int)roundf(y), (joyScale < 0.5f) ? 1 : 2, color);
        }
    }
}

static void drawAngryBrows(float eyeHeightFactor, int ox, int oy, float browAlpha, uint16_t color, float vergence, float scale) {
    (void)vergence;
    (void)scale;
    if (eyeHeightFactor <= 0.15f || browAlpha <= 0.01f) return;

    int lx = 32 + ox;
    int rx = 96 + ox;
    int ly = 28 + oy;

    int maxEyeWidth = 28;
    int maxEyeHeight = 38;
    int eyeHeight = (int)roundf((float)maxEyeHeight * eyeHeightFactor);

    int browCutX = (int)((maxEyeWidth / 2 + 4) * browAlpha);
    int browCutY = (int)((eyeHeight / 2 + 3) * browAlpha);
    int eyeTop = ly - eyeHeight / 2;

    canvas.fillTriangle(
        lx - 2, eyeTop - 1,
        lx - 2 + browCutX, eyeTop - 1,
        lx - 2 + browCutX, eyeTop - 1 + browCutY,
        color
    );

    canvas.fillTriangle(
        rx + 2, eyeTop - 1,
        rx + 2 - browCutX, eyeTop - 1,
        rx + 2 - browCutX, eyeTop - 1 + browCutY,
        color
    );
}

static void drawShockEyes(int ox, int oy, uint16_t color, float vergence, float scale) {
    (void)vergence;
    int lx = 32 + ox;
    int rx = 96 + ox;
    int ly = 28 + oy;
    int ry = 28 + oy;

    int w = 28;
    int h = (int)roundf(36.0f * scale);
    if (h <= 3) {
        canvas.fillRoundRect(lx - w / 2, ly - 1, w, 3, 1, color);
        canvas.fillRoundRect(rx - w / 2, ry - 1, w, 3, 1, color);
        return;
    }
    int rad = (h < 24) ? h / 2 : 12;
    canvas.drawRoundRect(lx - w / 2, ly - h / 2, w, h, rad, color);
    if (h > 12) {
        canvas.drawRoundRect(lx - w / 2 + 1, ly - h / 2 + 1, w - 2, h - 2, (rad > 1 ? rad - 1 : 1), color);
    }
    int pupilRad = (h > 18) ? 3 : (h > 8 ? 2 : 1);
    canvas.fillCircle(lx, ly, pupilRad, color);

    canvas.drawRoundRect(rx - w / 2, ry - h / 2, w, h, rad, color);
    if (h > 12) {
        canvas.drawRoundRect(rx - w / 2 + 1, ry - h / 2 + 1, w - 2, h - 2, (rad > 1 ? rad - 1 : 1), color);
    }
    canvas.fillCircle(rx, ry, pupilRad, color);
}

static void drawSpiralEye(int cx, int cy, float rotAngle, uint16_t color, float scale) {
    float prevX = cx;
    float prevY = cy;
    for (float theta = 0.2f; theta <= 13.5f; theta += 0.35f) {
        float r = 0.85f * theta * scale;
        float angle = theta + rotAngle;
        float x = cx + r * cosf(angle);
        float y = cy + r * sinf(angle);
        canvas.drawLine((int)prevX, (int)prevY, (int)x, (int)y, color);
        prevX = x;
        prevY = y;
    }
}

static void drawSpiralEyes(int ox, int oy, float rotAngle, uint16_t color, float vergence, float scale) {
    (void)vergence;
    int lx = 32 + ox;
    int rx = 96 + ox;
    int ly = 28 + oy;
    int ry = 28 + oy;

    if (scale <= 0.05f) {
        canvas.fillRoundRect(lx - 14, ly - 1, 28, 3, 1, color);
        canvas.fillRoundRect(rx - 14, ry - 1, 28, 3, 1, color);
        return;
    }
    drawSpiralEye(lx, ly, rotAngle, color, scale);
    drawSpiralEye(rx, ry, -rotAngle, color, scale);
}

static void drawSadEyes(int ox, int oy, float animFrame, uint16_t color, float vergence, float scale) {
    (void)vergence;
    int lx = 32 + ox;
    int rx = 96 + ox;
    int ly = 25 + oy;
    int ry = 25 + oy;

    int barW = (int)roundf(26.0f * scale);
    int barH = 3;

    canvas.fillRoundRect(lx - barW / 2, ly - 1, barW, barH, 1, color);
    canvas.fillRoundRect(rx - barW / 2, ry - 1, barW, barH, 1, color);

    int tearOffsets[2] = { -6, 6 };
    int eyesX[2] = { lx, rx };

    for (int e = 0; e < 2; e++) {
        int cx = eyesX[e];
        for (int t = 0; t < 2; t++) {
            int tx = cx + (int)roundf((float)tearOffsets[t] * scale);
            int startY = ly + 2;
            int endY = 62;

            for (int ty = startY; ty <= endY; ty += 2) {
                float phase = ((float)(ty - startY) * 0.35f) - (animFrame * 3.5f);
                float wave = sinf(phase);
                if (wave > -0.65f) {
                    int thickness = (wave > 0.3f) ? 1 : 0;
                    canvas.drawFastVLine(tx, ty, 2, color);
                    if (thickness > 0) {
                        canvas.drawFastVLine(tx - 1, ty, 2, color);
                    }
                }
            }
        }
    }
}

static void drawMouthCustom(int ox, int oy, float curve, float baseY, float width, float asym, uint16_t color, float scale) {
    int mx = 64 + ox;
    int my = (int)roundf(baseY + (float)oy);
    float scaledWidth = width * scale;
    for (float x = -scaledWidth; x <= scaledWidth; x += 0.4f) {
        float normX = (scale > 0.01f) ? (x / scale) : x;
        float y = (float)my + (curve * normX * normX + asym * normX) * scale;
        canvas.fillCircle(mx + (int)roundf(x), (int)roundf(y), 1, color);
    }
}

static void drawCatMouth(int ox, int oy, uint16_t color, float scale) {
    int mx = 64 + ox;
    int my = (int)roundf(43.5f + (float)oy);
    float halfW = 8.5f * scale;
    float depth = 3.0f * scale;

    for (float x = -halfW; x <= halfW; x += 0.35f) {
        float normX;
        if (x < 0.0f) {
            normX = (x + halfW * 0.5f) / (halfW * 0.5f);
        } else {
            normX = (x - halfW * 0.5f) / (halfW * 0.5f);
        }
        float y = (float)my + depth * (1.0f - normX * normX);
        canvas.fillCircle(mx + (int)roundf(x), (int)roundf(y), 1, color);
    }
}

static void drawDeadpanMouth(int ox, int oy, uint16_t color, float scale) {
    int mx = 64 + ox;
    int my = (int)roundf(44.0f + (float)oy);
    int halfW = (int)roundf(7.5f * scale);
    canvas.fillRoundRect(mx - halfW, my - 1, halfW * 2, 3, 1, color);
}

static void drawJoyMouth(int ox, int oy, float joyScale, uint16_t color, float scale) {
    if (joyScale <= 0.05f) return;
    int mx = 64 + ox;
    float width = 11.0f * joyScale * scale;
    int baseY = (int)roundf(39.0f + (float)oy);

    for (float x = -width; x <= width; x += 0.4f) {
        float normX = (width > 0) ? (x / width) : 0;
        float yTop = (float)baseY - (0.02f * (x / scale) * (x / scale)) * scale;
        float yBottom = yTop + (11.0f * joyScale * scale) * (1.0f - normX * normX);

        for (float y = yTop; y <= yBottom; y += 0.6f) {
            canvas.fillCircle(mx + (int)roundf(x), (int)roundf(y), 1, color);
        }
    }
}

static void drawShockMouth(int ox, int oy, uint16_t color, float scale) {
    int mx = 64 + ox;
    int my = (int)roundf(39.0f + (float)oy);
    int w = (int)roundf(16.0f * scale);
    int h = (int)roundf(14.0f * scale);
    canvas.fillRoundRect(mx - w / 2, my, w, h, (int)roundf(5.0f * scale), color);
}

static void drawOverloadMouth(int ox, int oy, uint16_t color, float scale) {
    int mx = 64 + ox;
    int my = (int)roundf(45.0f + (float)oy);
    canvas.fillEllipse(mx, my, (int)roundf(7.0f * scale), (int)roundf(5.0f * scale), color);
}

static void drawSadMouth(int ox, int oy, float animFrame, uint16_t color, float scale) {
    int mx = 64 + ox;
    int my = (int)roundf(45.0f + (float)oy);
    float halfW = 9.5f * scale;
    float waveAmp = 1.8f * scale;
    float waveFreq = 0.75f;
    float phase = animFrame * 4.5f;

    for (float x = -halfW; x <= halfW; x += 0.35f) {
        float norm = x / halfW;
        float taper = 1.0f - norm * norm * norm * norm;
        float y = (float)my + waveAmp * sinf(waveFreq * (x / scale) + phase) * taper;
        canvas.fillCircle(mx + (int)roundf(x), (int)roundf(y), 1, color);
    }
}

static void renderFaceState(float eyeHeightFactor, int ox, int oy, float mouthCurve, float mouthY, float mouthWidth, float browAlpha, bool inverted, float vergence, float scale) {
    uint16_t bgColor = inverted ? TFT_WHITE : TFT_BLACK;
    uint16_t fgColor = inverted ? TFT_BLACK : TFT_WHITE;

    canvas.fillScreen(bgColor);
    drawEyes(eyeHeightFactor, ox, oy, fgColor, vergence, scale);
    drawAngryBrows(eyeHeightFactor, ox, oy, browAlpha, bgColor, vergence, scale);
    drawMouthCustom(ox, oy, mouthCurve, mouthY, mouthWidth, 0.0f, fgColor, scale);
    canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
}

void drawFace(Expression expr, float eyeHeightFactor, float offsetX, float offsetY, float frame, float vergence, float scale) {
    int ox = getFilteredOx(offsetX);
    int oy = getFilteredOy(offsetY);
    switch (expr) {
        case EXPR_IDLE:
            renderFaceState(eyeHeightFactor, ox, oy, -0.030f, 44.0f, 7.5f, 0.0f, false, vergence, scale);
            break;
        case EXPR_JOY:
            canvas.fillScreen(TFT_BLACK);
            drawJoyEyes(ox, oy, 1.0f, TFT_WHITE, vergence, scale);
            drawJoyMouth(ox, oy, 1.0f, TFT_WHITE, scale);
            canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
            break;
        case EXPR_ANGRY:
            renderFaceState(eyeHeightFactor, ox, oy, 0.042f, 43.0f, 7.0f, 1.0f, true, vergence, scale);
            break;
        case EXPR_SMIRK:
            canvas.fillScreen(TFT_BLACK);
            drawFumoEyes(eyeHeightFactor, ox, oy, TFT_WHITE, vergence, scale);
            drawCatMouth(ox, oy, TFT_WHITE, scale);
            canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
            break;
        case EXPR_SHOCK:
            canvas.fillScreen(TFT_BLACK);
            if (eyeHeightFactor > 0.3f) {
                drawShockEyes(ox, oy, TFT_WHITE, vergence, scale);
            } else {
                drawEyes(eyeHeightFactor, ox, oy, TFT_WHITE, vergence, scale);
            }
            drawShockMouth(ox, oy, TFT_WHITE, scale);
            canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
            break;
        case EXPR_OVERLOAD:
            canvas.fillScreen(TFT_BLACK);
            drawSpiralEyes(ox, oy, frame, TFT_WHITE, vergence, scale);
            drawOverloadMouth(ox, oy, TFT_WHITE, scale);
            canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
            break;
        case EXPR_SAD:
            canvas.fillScreen(TFT_BLACK);
            drawSadEyes(ox, oy, frame, TFT_WHITE, vergence, scale);
            drawSadMouth(ox, oy, frame, TFT_WHITE, scale);
            canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
            break;
        case EXPR_DEADPAN:
            canvas.fillScreen(TFT_BLACK);
            drawFumoEyes(eyeHeightFactor, ox, oy, TFT_WHITE, vergence, scale);
            drawDeadpanMouth(ox, oy, TFT_WHITE, scale);
            canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
            break;
    }
}

void transitionExpression(Expression fromExpr, Expression toExpr, float durationMs) {
    if (fromExpr == toExpr) return;

    float startLeftEyeH  = 1.0f;
    float endLeftEyeH    = 1.0f;
    float startRightEyeH = 1.0f;
    float endRightEyeH   = 1.0f;

    float startCurve = (fromExpr == EXPR_IDLE) ? -0.030f : ((fromExpr == EXPR_ANGRY) ? 0.042f : 0.0f);
    float endCurve   = (toExpr == EXPR_IDLE)   ? -0.030f : ((toExpr == EXPR_ANGRY)   ? 0.042f : 0.0f);

    float startY = (fromExpr == EXPR_IDLE) ? 44.0f : ((fromExpr == EXPR_ANGRY) ? 43.0f : ((fromExpr == EXPR_SHOCK) ? 39.0f : ((fromExpr == EXPR_OVERLOAD) ? 45.0f : 44.0f)));
    float endY   = (toExpr == EXPR_IDLE)   ? 44.0f : ((toExpr == EXPR_ANGRY)   ? 43.0f : ((toExpr == EXPR_SHOCK)   ? 39.0f : ((toExpr == EXPR_OVERLOAD)   ? 45.0f : 44.0f)));

    float startW = (fromExpr == EXPR_IDLE) ? 7.5f : ((fromExpr == EXPR_ANGRY) ? 7.0f : 8.0f);
    float endW   = (toExpr == EXPR_IDLE)   ? 7.5f : ((toExpr == EXPR_ANGRY)   ? 7.0f : 8.0f);

    float startAsym = 0.0f;
    float endAsym   = 0.0f;

    float startBrow = (fromExpr == EXPR_ANGRY) ? 1.0f : 0.0f;
    float endBrow   = (toExpr == EXPR_ANGRY)   ? 1.0f : 0.0f;

    int steps = 14;
    float stepDelay = durationMs / (float)steps;
    float arousal = getEmotionArousal();

    for (int i = 0; i <= steps; i++) {
        updateGazeSystem();

        float t = (float)i / (float)steps;

        float curLeftEyeH;
        float curRightEyeH;
        if (t <= 0.35f) {
            float p = t / 0.35f;
            float blinkFactor = fmaxf(0.04f, 1.0f - p * p);
            curLeftEyeH = startLeftEyeH * blinkFactor;
            curRightEyeH = startRightEyeH * blinkFactor;
        } else if (t <= 0.50f) {
            curLeftEyeH = 0.04f;
            curRightEyeH = 0.04f;
        } else {
            float p = (t - 0.50f) / 0.50f;
            /* Biological eyelid opening with elastic pop */
            float blinkFactor = fmaxf(0.04f, blinkOpenEase(p));
            curLeftEyeH = endLeftEyeH * blinkFactor;
            curRightEyeH = endRightEyeH * blinkFactor;
        }

        /* Viscoelastic underdamped elastic bounce easing */
        float elasticT = eval_elastic_bounce_ease(t);
        float curCurve = customLerp(startCurve, endCurve, elasticT);
        float curY     = customLerp(startY, endY, elasticT);
        float curW     = customLerp(startW, endW, elasticT);
        float curAsym  = customLerp(startAsym, endAsym, elasticT);
        float curBrow  = customLerp(startBrow, endBrow, elasticT);

        /* Biological volume-conserving Squash & Stretch */
        float squashX = 1.0f, squashY = 1.0f;
        compute_squash_stretch_factors(t, arousal, &squashX, &squashY);
        g_currentEyeScaleX = squashX;
        g_currentEyeScaleY = squashY;

        float joyScale = (t < 0.5f) ? fmaxf(0.0f, 1.0f - t * 2.0f) : fmaxf(0.0f, (t - 0.5f) * 2.0f);
        joyScale *= squashY;

        bool inverted = (t < 0.5f) ? (fromExpr == EXPR_ANGRY) : (toExpr == EXPR_ANGRY);
        uint16_t bgColor = inverted ? TFT_WHITE : TFT_BLACK;
        uint16_t fgColor = inverted ? TFT_BLACK : TFT_WHITE;

        int ox = getFilteredOx(g_currentOffsetX);
        int oy = getFilteredOy(g_currentOffsetY);

        canvas.fillScreen(bgColor);

        Expression activeExpr = (t < 0.5f) ? fromExpr : toExpr;
        if (activeExpr == EXPR_IDLE || activeExpr == EXPR_ANGRY) {
            drawDualEyes(curLeftEyeH, curRightEyeH, ox, oy, fgColor, g_currentVergence, g_currentEyeScale);
        } else if (activeExpr == EXPR_SMIRK || activeExpr == EXPR_DEADPAN) {
            drawFumoEyes(curLeftEyeH, ox, oy, fgColor, g_currentVergence, g_currentEyeScale);
        } else if (activeExpr == EXPR_JOY) {
            drawJoyEyes(ox, oy, joyScale, fgColor, g_currentVergence, g_currentEyeScale);
        } else if (activeExpr == EXPR_SHOCK) {
            drawShockEyes(ox, oy, fgColor, g_currentVergence, g_currentEyeScale * curLeftEyeH * squashY);
        } else if (activeExpr == EXPR_OVERLOAD) {
            drawSpiralEyes(ox, oy, t * 2.0f, fgColor, g_currentVergence, g_currentEyeScale * curLeftEyeH * squashY);
        } else if (activeExpr == EXPR_SAD) {
            drawSadEyes(ox, oy, t * 4.0f, fgColor, g_currentVergence, g_currentEyeScale * curLeftEyeH * squashY);
        }

        if (curBrow > 0.01f) {
            drawAngryBrows(curLeftEyeH, ox, oy, curBrow, bgColor, g_currentVergence, g_currentEyeScale);
        }

        bool fromCurveMouth = (fromExpr == EXPR_IDLE || fromExpr == EXPR_ANGRY);
        bool toCurveMouth   = (toExpr == EXPR_IDLE || toExpr == EXPR_ANGRY);

        if (fromCurveMouth && toCurveMouth) {
            drawMouthCustom(ox, oy, curCurve, curY, curW, curAsym, fgColor, g_currentEyeScale * squashX);
        } else {
            Expression mouthExpr = (t < 0.5f) ? fromExpr : toExpr;
            if (mouthExpr == EXPR_IDLE || mouthExpr == EXPR_ANGRY) {
                drawMouthCustom(ox, oy, curCurve, curY, curW, curAsym, fgColor, g_currentEyeScale * squashX);
            } else if (mouthExpr == EXPR_SMIRK) {
                drawCatMouth(ox, oy, fgColor, g_currentEyeScale * squashX);
            } else if (mouthExpr == EXPR_DEADPAN) {
                drawDeadpanMouth(ox, oy, fgColor, g_currentEyeScale * squashX);
            } else if (mouthExpr == EXPR_JOY) {
                drawJoyMouth(ox, oy, joyScale, fgColor, g_currentEyeScale);
            } else if (mouthExpr == EXPR_SHOCK) {
                drawShockMouth(ox, oy, fgColor, g_currentEyeScale * squashY);
            } else if (mouthExpr == EXPR_OVERLOAD) {
                drawOverloadMouth(ox, oy, fgColor, g_currentEyeScale * squashX);
            } else if (mouthExpr == EXPR_SAD) {
                drawSadMouth(ox, oy, t * 4.0f, fgColor, g_currentEyeScale * squashX);
            }
        }

        canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
        vTaskDelay(pdMS_TO_TICKS((int)stepDelay));
    }
    g_currentEyeScaleX = 1.0f;
    g_currentEyeScaleY = 1.0f;
    g_currentExpr = toExpr;
}

void renderClockToCanvas(float animFrame, int offsetY = 0) {
    time_t now_sec;
    time(&now_sec);
    struct tm timeinfo;
    bool time_synced = (now_sec > 1700000000);
    if (time_synced) {
        localtime_r(&now_sec, &timeinfo);
    } else {
        memset(&timeinfo, 0, sizeof(timeinfo));
    }

    /* 1. Hero Center: Clean, Large 3x Digital Time */
    if (time_synced) {
        char hm_buf[8];
        bool colon_on = (timeinfo.tm_sec % 2 == 0) || ((int)(animFrame * 2.5f) % 2 == 0);
        snprintf(hm_buf, sizeof(hm_buf), "%02d%c%02d", timeinfo.tm_hour, colon_on ? ':' : ' ', timeinfo.tm_min);

        int time_w = 5 * 18;
        int time_x = (OLED_PANEL_WIDTH_PX - time_w) / 2;
        canvas.setTextSize(3);
        canvas.setTextColor(TFT_WHITE, TFT_BLACK);
        canvas.setCursor(time_x, 15 + offsetY);
        canvas.print(hm_buf);
    } else {
        bool colon_on = ((int)(animFrame * 2.5f) % 2 == 0);
        char hm_buf[8];
        snprintf(hm_buf, sizeof(hm_buf), "--%c--", colon_on ? ':' : ' ');
        int time_w = 5 * 18;
        int time_x = (OLED_PANEL_WIDTH_PX - time_w) / 2;
        canvas.setTextSize(3);
        canvas.setTextColor(TFT_WHITE, TFT_BLACK);
        canvas.setCursor(time_x, 15 + offsetY);
        canvas.print(hm_buf);
    }

    /* 2. Split Bottom Bar: Day on Left, Date on Right */
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);

    if (time_synced) {
        static const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        
        const char* day_str = days[timeinfo.tm_wday % 7];
        char date_str[16];
        snprintf(date_str, sizeof(date_str), "%d %s %d",
            timeinfo.tm_mday,
            months[timeinfo.tm_mon % 12],
            1900 + timeinfo.tm_year
        );

        // Bottom Left: Day Name
        canvas.setCursor(6, 50 + offsetY);
        canvas.print(day_str);

        // Bottom Right: Date
        int date_w = strlen(date_str) * 6;
        int date_x = OLED_PANEL_WIDTH_PX - 6 - date_w;
        if (date_x < 60) date_x = 60;
        canvas.setCursor(date_x, 50 + offsetY);
        canvas.print(date_str);
    } else {
        canvas.setCursor(6, 50 + offsetY);
        canvas.print("Syncing");

        const char* r_str = "NTP Clock";
        int rw = strlen(r_str) * 6;
        canvas.setCursor(OLED_PANEL_WIDTH_PX - 6 - rw, 50 + offsetY);
        canvas.print(r_str);
    }
}

void drawClockScreen(float animFrame) {
    canvas.fillScreen(TFT_BLACK);
    renderClockToCanvas(animFrame, 0);
    canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
}

void renderWeatherToCanvas(const WeatherInfo& weather, float animFrame, int offsetY = 0) {
    /* 1. City Name on Top (Centered, clean 1x font) */
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);

    char city_buf[24];
    snprintf(city_buf, sizeof(city_buf), "%s", (weather.city[0] != '\0') ? weather.city : "Weather");
    int city_w = strlen(city_buf) * 6;
    int city_x = (OLED_PANEL_WIDTH_PX - city_w) / 2;
    if (city_x < 0) city_x = 0;
    canvas.setCursor(city_x, 6 + offsetY);
    canvas.print(city_buf);

    /* 2. Hero Center: Minimalist Weather Icon on Left + Large Temperature on Right */
    int code = weather.weather_code;
    int icx = 34;
    int icy = 31 + offsetY;

    if (code == 0 || code == 1) {
        /* Clear / Sun */
        canvas.fillCircle(icx, icy, 6, TFT_WHITE);
        for (int i = 0; i < 6; i++) {
            float angle = (float)i * (2.0f * (float)M_PI / 6.0f) + animFrame * 0.2f;
            int x1 = icx + (int)roundf(cosf(angle) * 8.0f);
            int y1 = icy + (int)roundf(sinf(angle) * 8.0f);
            int x2 = icx + (int)roundf(cosf(angle) * 11.0f);
            int y2 = icy + (int)roundf(sinf(angle) * 11.0f);
            canvas.drawLine(x1, y1, x2, y2, TFT_WHITE);
        }
    } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        /* Rain / Drizzle */
        canvas.fillCircle(icx - 6, icy - 3, 4, TFT_WHITE);
        canvas.fillCircle(icx, icy - 6, 6, TFT_WHITE);
        canvas.fillCircle(icx + 6, icy - 3, 4, TFT_WHITE);
        canvas.fillRect(icx - 10, icy - 3, 20, 5, TFT_WHITE);

        int dropShift = ((int)(animFrame * 6.0f)) % 4;
        for (int r = -6; r <= 6; r += 6) {
            int rx = icx + r;
            int ry = icy + 4 + dropShift;
            canvas.drawLine(rx, ry, rx - 1, ry + 3, TFT_WHITE);
        }
    } else if (code >= 95 && code <= 99) {
        /* Thunderstorm */
        canvas.fillCircle(icx - 6, icy - 4, 4, TFT_WHITE);
        canvas.fillCircle(icx, icy - 7, 6, TFT_WHITE);
        canvas.fillCircle(icx + 6, icy - 4, 4, TFT_WHITE);
        canvas.fillRect(icx - 10, icy - 4, 20, 5, TFT_WHITE);

        canvas.drawLine(icx, icy + 2, icx - 2, icy + 7, TFT_WHITE);
        canvas.drawLine(icx - 2, icy + 7, icx + 1, icy + 7, TFT_WHITE);
        canvas.drawLine(icx + 1, icy + 7, icx - 1, icy + 12, TFT_WHITE);
    } else if (code == 45 || code == 48) {
        /* Fog */
        canvas.drawFastHLine(icx - 12, icy - 4, 24, TFT_WHITE);
        canvas.drawFastHLine(icx - 8, icy, 16, TFT_WHITE);
        canvas.drawFastHLine(icx - 12, icy + 4, 24, TFT_WHITE);
    } else {
        /* Cloudy / Overcast */
        if (code == 2) {
            canvas.drawCircle(icx + 7, icy - 7, 3, TFT_WHITE);
        }
        canvas.fillCircle(icx - 6, icy - 2, 5, TFT_WHITE);
        canvas.fillCircle(icx, icy - 6, 7, TFT_WHITE);
        canvas.fillCircle(icx + 6, icy - 2, 5, TFT_WHITE);
        canvas.fillRect(icx - 10, icy - 2, 20, 6, TFT_WHITE);
    }

    // Temperature text on right (TextSize 2, e.g. "28°")
    char temp_buf[16];
    if (weather.valid) {
        snprintf(temp_buf, sizeof(temp_buf), "%.1f", weather.temperature);
    } else {
        snprintf(temp_buf, sizeof(temp_buf), "--.-");
    }

    canvas.setTextSize(2);
    canvas.setCursor(62, 24 + offsetY);
    canvas.print(temp_buf);
    int deg_x = 62 + strlen(temp_buf) * 12;
    canvas.drawCircle(deg_x + 3, 25 + offsetY, 2, TFT_WHITE);

    /* 3. Minimalist Footer: Condition & Humidity */
    canvas.setTextSize(1);
    char cond_buf[36];
    if (weather.valid) {
        snprintf(cond_buf, sizeof(cond_buf), "%s  •  %d%%",
            weather.condition[0] != '\0' ? weather.condition : "OK",
            weather.humidity
        );
    } else {
        snprintf(cond_buf, sizeof(cond_buf), "Updating Forecast...");
    }
    int cond_w = strlen(cond_buf) * 6;
    int cond_x = (OLED_PANEL_WIDTH_PX - cond_w) / 2;
    if (cond_x < 0) cond_x = 0;
    canvas.setCursor(cond_x, 49 + offsetY);
    canvas.print(cond_buf);
}

void drawWeatherScreen(const WeatherInfo& weather, float animFrame) {
    canvas.fillScreen(TFT_BLACK);
    renderWeatherToCanvas(weather, animFrame, 0);
    canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
}

void setOledBrightnessLive(uint8_t brightness) {
    g_oled_brightness = brightness;
}

static AmbientScreenMode s_active_ambient_mode = AMBIENT_NONE;
static uint32_t s_ambient_popup_until_ms = 0;
static uint32_t s_next_random_ambient_check_ms = 0;
static uint8_t s_last_applied_brightness = 0;
static volatile bool s_is_manual_ambient = false;
static volatile AmbientScreenMode s_pending_ambient_mode = AMBIENT_NONE;
static volatile uint32_t s_pending_ambient_duration = 0;

void transitionToAmbient(AmbientScreenMode toMode, float durationMs) {
    if (toMode == AMBIENT_NONE) return;

    int steps = 11;
    float stepDelay = durationMs / (float)steps;
    Expression startExpr = g_currentExpr;

    WeatherInfo local_weather;
    if (toMode == AMBIENT_WEATHER) {
        portENTER_CRITICAL(&g_weather_mutex);
        local_weather = g_weather_info;
        portEXIT_CRITICAL(&g_weather_mutex);
    }

    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;

        if (t <= 0.45f) {
            /* Phase 1: Face squashes down and blinks closed */
            float p = t / 0.45f;
            float eyeH = fmaxf(0.04f, 1.0f - p * p);
            float squashX = 1.0f + 0.16f * p;
            float squashY = fmaxf(0.04f, 1.0f - 0.96f * p);
            g_currentEyeScaleX = squashX;
            g_currentEyeScaleY = squashY;

            drawFace(startExpr, eyeH, g_currentOffsetX, g_currentOffsetY, 0.0f, g_currentVergence, 1.0f);
        } else {
            /* Phase 2: Ambient screen elements pop up with underdamped elastic bounce */
            float p = (t - 0.45f) / 0.55f;
            float bounceT = eval_elastic_bounce_ease(p);
            int offsetY = (int)roundf((1.0f - bounceT) * 32.0f);

            canvas.fillScreen(TFT_BLACK);
            if (toMode == AMBIENT_CLOCK) {
                renderClockToCanvas(g_animFrame, offsetY);
            } else if (toMode == AMBIENT_WEATHER) {
                renderWeatherToCanvas(local_weather, g_animFrame, offsetY);
            }
            canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
        }

        vTaskDelay(pdMS_TO_TICKS((int)stepDelay));
    }
    g_currentEyeScaleX = 1.0f;
    g_currentEyeScaleY = 1.0f;
}

void transitionFromAmbientToFace(AmbientScreenMode fromMode, Expression toExpr, float durationMs) {
    if (fromMode == AMBIENT_NONE) return;

    int steps = 11;
    float stepDelay = durationMs / (float)steps;

    WeatherInfo local_weather;
    if (fromMode == AMBIENT_WEATHER) {
        portENTER_CRITICAL(&g_weather_mutex);
        local_weather = g_weather_info;
        portEXIT_CRITICAL(&g_weather_mutex);
    }

    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;

        if (t <= 0.40f) {
            /* Phase 1: Ambient screen squashes / drops down */
            float p = t / 0.40f;
            int offsetY = (int)roundf((p * p) * 32.0f);

            canvas.fillScreen(TFT_BLACK);
            if (fromMode == AMBIENT_CLOCK) {
                renderClockToCanvas(g_animFrame, offsetY);
            } else if (fromMode == AMBIENT_WEATHER) {
                renderWeatherToCanvas(local_weather, g_animFrame, offsetY);
            }
            canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
        } else {
            /* Phase 2: Face pops open with fast-twitch elastic rebound */
            float p = (t - 0.40f) / 0.60f;
            float eyeH = fmaxf(0.04f, blinkOpenEase(p));
            float squashX = 1.0f, squashY = 1.0f;
            compute_squash_stretch_factors(p, getEmotionArousal(), &squashX, &squashY);
            g_currentEyeScaleX = squashX;
            g_currentEyeScaleY = squashY;

            drawFace(toExpr, eyeH, g_currentOffsetX, g_currentOffsetY, 0.0f, g_currentVergence, 1.0f);
        }

        vTaskDelay(pdMS_TO_TICKS((int)stepDelay));
    }
    g_currentEyeScaleX = 1.0f;
    g_currentEyeScaleY = 1.0f;
}

void triggerAmbientDisplay(AmbientScreenMode mode, uint32_t duration_ms) {
    s_pending_ambient_mode = mode;
    s_pending_ambient_duration = duration_ms;
    s_is_manual_ambient = true;
}

void triggerClockDisplay(uint32_t duration_ms) {
    triggerAmbientDisplay(AMBIENT_CLOCK, duration_ms);
}

void triggerWeatherDisplay(uint32_t duration_ms) {
    triggerAmbientDisplay(AMBIENT_WEATHER, duration_ms);
}

void oledTask(void *pvParameters) {
    (void)pvParameters;

    s_last_applied_brightness = g_oled_brightness;
    lcd.setBrightness(g_oled_brightness);

    canvas.setColorDepth(1);
    canvas.createSprite(OLED_PANEL_WIDTH_PX, OLED_PANEL_HEIGHT_PX);

    g_currentExpr = EXPR_IDLE;
    drawFace(g_currentExpr, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);

    while (true) {
        uint32_t frame_start_us = micros();
        unsigned long now = millis();

        /* Keep user brightness setting applied without auto-dimming */
        if (s_last_applied_brightness != g_oled_brightness) {
            s_last_applied_brightness = g_oled_brightness;
            lcd.setBrightness(g_oled_brightness);
        }

        if (g_recon_state != s_prev_recon_state) {
            s_prev_recon_state = g_recon_state;
            if (g_recon_state == STATE_ACTIVE) {
                s_burn_shift_x = 0;
                s_burn_shift_y = 0;
            }
        }

        if (g_recon_state == STATE_SLEEP_RECON && (now - s_last_burn_shift_ms > 60000)) {
            s_last_burn_shift_ms = now;
            s_burn_shift_x = (int)(esp_random() % 3) - 1;
            s_burn_shift_y = (int)(esp_random() % 3) - 1;
        }

        /* Check if target face is actively engaged */
        bool target_engaged = false;
        portENTER_CRITICAL(&g_target_mutex);
        target_engaged = (g_recon_state == STATE_ACTIVE && g_current_target.detected && (now - g_current_target.last_seen_ms < 600));
        portEXIT_CRITICAL(&g_target_mutex);

        /* Check manual preview request from Web UI */
        if (s_pending_ambient_mode != AMBIENT_NONE) {
            AmbientScreenMode target = s_pending_ambient_mode;
            uint32_t dur = s_pending_ambient_duration;
            s_pending_ambient_mode = AMBIENT_NONE;
            if (s_active_ambient_mode != target) {
                transitionToAmbient(target, 160.0f);
            }
            s_active_ambient_mode = target;
            s_ambient_popup_until_ms = millis() + dur;
            s_is_manual_ambient = true;
        }

        /* Spontaneous organic ambient glance (Personality & Attention-Driven):
         * - Only triggers when robot is NOT engaged with a person and manual expression is inactive.
         * - True stochastic weighted selection: Clock vs Weather vs skipping to staying in eye gaze.
         * - Natural organic interval: 45s - 120s between spontaneous glances.
         * - Brief glance duration: 3.5s - 5.5s. */
        if (s_next_random_ambient_check_ms == 0) {
            uint32_t interval_span = (AMBIENT_INTERVAL_MAX_MS > AMBIENT_INTERVAL_MIN_MS) ? (AMBIENT_INTERVAL_MAX_MS - AMBIENT_INTERVAL_MIN_MS) : 60000;
            s_next_random_ambient_check_ms = now + (esp_random() % interval_span) + AMBIENT_INTERVAL_MIN_MS;
        }

        if (!target_engaged && !isManualExpressionActive() && now >= s_next_random_ambient_check_ms) {
            uint32_t interval_span = (AMBIENT_INTERVAL_MAX_MS > AMBIENT_INTERVAL_MIN_MS) ? (AMBIENT_INTERVAL_MAX_MS - AMBIENT_INTERVAL_MIN_MS) : 60000;
            s_next_random_ambient_check_ms = now + (esp_random() % interval_span) + AMBIENT_INTERVAL_MIN_MS;

            if (s_ambient_popup_until_ms < now) {
                bool weather_valid = false;
                portENTER_CRITICAL(&g_weather_mutex);
                weather_valid = g_weather_info.valid;
                portEXIT_CRITICAL(&g_weather_mutex);

                bool weather_available = isWeatherEnabled() && weather_valid;
                uint32_t roll = esp_random() % 100;
                AmbientScreenMode chosen_mode = AMBIENT_NONE;

                if (weather_available) {
                    if (roll < 45) {
                        /* 45% chance: Glances at clock */
                        chosen_mode = AMBIENT_CLOCK;
                    } else if (roll < 85) {
                        /* 40% chance: Glances at weather */
                        chosen_mode = AMBIENT_WEATHER;
                    }
                } else {
                    if (roll < 60) {
                        /* 60% chance: Glances at clock */
                        chosen_mode = AMBIENT_CLOCK;
                    }
                }

                if (chosen_mode != AMBIENT_NONE) {
                    s_is_manual_ambient = false;
                    transitionToAmbient(chosen_mode, 160.0f);
                    s_active_ambient_mode = chosen_mode;
                    uint32_t duration_span = (AMBIENT_POPUP_DURATION_MAX_MS > AMBIENT_POPUP_DURATION_MIN_MS) ? (AMBIENT_POPUP_DURATION_MAX_MS - AMBIENT_POPUP_DURATION_MIN_MS) : 2000;
                    uint32_t random_duration_ms = (esp_random() % duration_span) + AMBIENT_POPUP_DURATION_MIN_MS;
                    s_ambient_popup_until_ms = now + random_duration_ms;
                }
            }
        }

        /* Check if ambient screen popup is active */
        if (now < s_ambient_popup_until_ms && s_active_ambient_mode != AMBIENT_NONE) {
            g_animFrame += 0.04f;
            if (s_active_ambient_mode == AMBIENT_CLOCK) {
                drawClockScreen(g_animFrame);
            } else if (s_active_ambient_mode == AMBIENT_WEATHER) {
                WeatherInfo local_weather;
                portENTER_CRITICAL(&g_weather_mutex);
                local_weather = g_weather_info;
                portEXIT_CRITICAL(&g_weather_mutex);
                drawWeatherScreen(local_weather, g_animFrame);
            }
        } else {
            if (s_active_ambient_mode != AMBIENT_NONE) {
                AmbientScreenMode prev_mode = s_active_ambient_mode;
                s_active_ambient_mode = AMBIENT_NONE;
                s_is_manual_ambient = false;
                transitionFromAmbientToFace(prev_mode, g_currentExpr, 140.0f);
            }
            Expression prevExpr = g_currentExpr;
            updateBiologicalMoodEngine();

            if (g_currentExpr != prevExpr) {
                frame_start_us = micros();
            } else {
                updateGazeSystem();

                bool canBlink = (g_currentExpr != EXPR_OVERLOAD && g_currentExpr != EXPR_SAD && g_currentExpr != EXPR_JOY);

                if (!canBlink) {
                    g_blinkState = BLINK_IDLE_STATE;
                    g_blinkEyeHeight = 1.0f;
                } else {
                    if (g_blinkState == BLINK_IDLE_STATE) {
                        if (g_nextBlinkTime == 0) {
                            uint32_t initInterval = (g_currentExpr == EXPR_ANGRY) ? (esp_random() % 4000 + 4000) : (esp_random() % 3500 + 3500);
                            g_nextBlinkTime = now + initInterval;
                        }
                        if (now >= g_nextBlinkTime) {
                            g_blinkState = BLINK_CLOSING_STATE;
                            g_nextBlinkTime = now;
                            if (s_isDoubleBlinkPending) {
                                s_isDoubleBlinkPending = false;
                            } else if ((esp_random() % 100) < 14) {
                                s_isDoubleBlinkPending = true;
                            }
                        }
                    }

                    if (g_blinkState == BLINK_CLOSING_STATE) {
                        float elapsed = (float)(now - g_nextBlinkTime);
                        float duration = (g_currentExpr == EXPR_ANGRY) ? 35.0f : 50.0f;
                        if (elapsed >= duration) {
                            g_blinkEyeHeight = 0.0f;
                            g_blinkState = BLINK_OPENING_STATE;
                            g_nextBlinkTime = now;
                        } else {
                            float t = elapsed / duration;
                            g_blinkEyeHeight = blinkCloseEase(t);
                        }
                    } else if (g_blinkState == BLINK_OPENING_STATE) {
                        float elapsed = (float)(now - g_nextBlinkTime);
                        float duration = (g_currentExpr == EXPR_ANGRY) ? 80.0f : 110.0f;
                        if (elapsed >= duration) {
                            g_blinkEyeHeight = 1.0f;
                            g_blinkState = BLINK_IDLE_STATE;
                            if (s_isDoubleBlinkPending) {
                                g_nextBlinkTime = now + 120;
                            } else {
                                uint32_t interval = (g_currentExpr == EXPR_ANGRY) ? (esp_random() % 4000 + 4000) : (esp_random() % 3500 + 3500);
                                g_nextBlinkTime = now + interval;
                            }
                        } else {
                            float t = elapsed / duration;
                            g_blinkEyeHeight = blinkOpenEase(t);
                        }
                    } else {
                        g_blinkEyeHeight = 1.0f;
                    }
                }

                if (g_currentExpr == EXPR_OVERLOAD || g_currentExpr == EXPR_SAD) {
                    g_animFrame += 0.025f;
                }

                drawFace(g_currentExpr, g_blinkEyeHeight, g_currentOffsetX, g_currentOffsetY, g_animFrame, g_currentVergence, g_currentEyeScale);
            }
        }

        uint32_t frame_budget_us = (g_recon_state == STATE_SLEEP_RECON) ? FRAME_BUDGET_SLEEP_US : FRAME_BUDGET_ACTIVE_US;
        uint32_t frame_elapsed_us = micros() - frame_start_us;
        if (frame_elapsed_us < frame_budget_us) {
            uint32_t wait_us = frame_budget_us - frame_elapsed_us;
            if (wait_us > 2000) {
                vTaskDelay(pdMS_TO_TICKS((wait_us - 500) / 1000));
            }
            /* Fine-grained sub-ms timing: yield once then accept frame */
            uint32_t remaining_us = frame_budget_us - (micros() - frame_start_us);
            if (remaining_us > 0 && remaining_us < 2000) {
                delayMicroseconds(remaining_us);
            }
        } else {
            vTaskDelay(1);
        }
    }
}
