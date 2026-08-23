/**
 * @file display_engine.cpp
 * @brief LovyanGFX SSD1306 display driver and rigid facial rig composition engine implementation.
 */

#include "src/core/display_engine.h"
#include "include/kore_config.h"
#include "include/kore_types.h"
#include "include/kore_kinematics.h"
#include "include/kore_affective.h"
#include "src/net/wifi_manager.h"
#include <WiFi.h>
#include <Arduino.h>
#include <esp_random.h>

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
    (void)scale;
    int lx = 32 + ox;
    int rx = 96 + ox;
    int ly = 28 + oy;
    int ry = 28 + oy;

    int maxEyeWidth = 28;
    int maxEyeHeight = 38;

    int leftHeight = (int)roundf((float)maxEyeHeight * leftHeightFactor);
    int rightHeight = (int)roundf((float)maxEyeHeight * rightHeightFactor);

    if (leftHeight <= 3) {
        canvas.fillRoundRect(lx - maxEyeWidth / 2, ly - 1, maxEyeWidth, 3, 1, color);
    } else {
        int radius = (leftHeight < 24) ? leftHeight / 2 : 12;
        canvas.fillRoundRect(lx - maxEyeWidth / 2, ly - leftHeight / 2, maxEyeWidth, leftHeight, radius, color);
    }

    if (rightHeight <= 3) {
        canvas.fillRoundRect(rx - maxEyeWidth / 2, ry - 1, maxEyeWidth, 3, 1, color);
    } else {
        int radius = (rightHeight < 24) ? rightHeight / 2 : 12;
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

    int maxEyeWidth = (int)roundf(28.0f * scale);
    int maxEyeHeight = (int)roundf(34.0f * scale);
    int eyeHeight = (int)roundf((float)maxEyeHeight * eyeHeightFactor);
    int topRad = (int)roundf(14.0f * scale);

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

static void drawSedihEyes(int ox, int oy, float animFrame, uint16_t color, float vergence, float scale) {
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

static void drawSedihMouth(int ox, int oy, float animFrame, uint16_t color, float scale) {
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

static void drawWiFiStatusOverlay(uint16_t color) {
    bool is_ap = isWiFiAPMode();
    bool connected = (WiFi.status() == WL_CONNECTED);
    int8_t rssi = connected ? WiFi.RSSI() : 0;

    int bx = 117;
    int by = 3;

    if (is_ap) {
        /* AP Mode indicator */
        canvas.drawCircle(bx + 4, by + 3, 2, color);
        canvas.drawPixel(bx + 4, by + 3, color);
    } else if (connected) {
        /* 3-bar signal strength indicator */
        canvas.fillRect(bx, by + 4, 2, 2, color);
        if (rssi > -80) {
            canvas.fillRect(bx + 3, by + 2, 2, 4, color);
        }
        if (rssi > -65) {
            canvas.fillRect(bx + 6, by, 2, 6, color);
        }
    } else {
        /* Disconnected 'x' glyph */
        canvas.drawLine(bx + 2, by + 1, bx + 6, by + 5, color);
        canvas.drawLine(bx + 6, by + 1, bx + 2, by + 5, color);
    }
}

static void renderFaceState(float eyeHeightFactor, int ox, int oy, float mouthCurve, float mouthY, float mouthWidth, float browAlpha, bool inverted, float vergence, float scale) {
    uint16_t bgColor = inverted ? TFT_WHITE : TFT_BLACK;
    uint16_t fgColor = inverted ? TFT_BLACK : TFT_WHITE;

    canvas.fillScreen(bgColor);
    drawEyes(eyeHeightFactor, ox, oy, fgColor, vergence, scale);
    drawAngryBrows(eyeHeightFactor, ox, oy, browAlpha, bgColor, vergence, scale);
    drawMouthCustom(ox, oy, mouthCurve, mouthY, mouthWidth, 0.0f, fgColor, scale);
    drawWiFiStatusOverlay(fgColor);
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
            drawWiFiStatusOverlay(TFT_WHITE);
            canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
            break;
        case EXPR_ANGRY:
            renderFaceState(eyeHeightFactor, ox, oy, 0.042f, 43.0f, 7.0f, 1.0f, true, vergence, scale);
            break;
        case EXPR_SMIRK:
            canvas.fillScreen(TFT_BLACK);
            drawFumoEyes(eyeHeightFactor, ox, oy, TFT_WHITE, vergence, scale);
            drawCatMouth(ox, oy, TFT_WHITE, scale);
            drawWiFiStatusOverlay(TFT_WHITE);
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
            drawWiFiStatusOverlay(TFT_WHITE);
            canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
            break;
        case EXPR_OVERLOAD:
            canvas.fillScreen(TFT_BLACK);
            drawSpiralEyes(ox, oy, frame, TFT_WHITE, vergence, scale);
            drawOverloadMouth(ox, oy, TFT_WHITE, scale);
            drawWiFiStatusOverlay(TFT_WHITE);
            canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
            break;
        case EXPR_SEDIH:
            canvas.fillScreen(TFT_BLACK);
            drawSedihEyes(ox, oy, frame, TFT_WHITE, vergence, scale);
            drawSedihMouth(ox, oy, frame, TFT_WHITE, scale);
            drawWiFiStatusOverlay(TFT_WHITE);
            canvas.pushSprite(s_burn_shift_x, s_burn_shift_y);
            break;
        case EXPR_DEADPAN:
            canvas.fillScreen(TFT_BLACK);
            drawFumoEyes(eyeHeightFactor, ox, oy, TFT_WHITE, vergence, scale);
            drawDeadpanMouth(ox, oy, TFT_WHITE, scale);
            drawWiFiStatusOverlay(TFT_WHITE);
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

    int steps = 10;
    float stepDelay = durationMs / steps;

    for (int i = 0; i <= steps; i++) {
        updateGazeSystem();

        float t = (float)i / (float)steps;

        float curLeftEyeH;
        float curRightEyeH;
        if (t <= 0.40f) {
            float p = t / 0.40f;
            float blinkFactor = fmaxf(0.04f, 1.0f - p * p);
            curLeftEyeH = startLeftEyeH * blinkFactor;
            curRightEyeH = startRightEyeH * blinkFactor;
        } else if (t <= 0.60f) {
            curLeftEyeH = 0.04f;
            curRightEyeH = 0.04f;
        } else {
            float p = (t - 0.60f) / 0.40f;
            float blinkFactor = fmaxf(0.04f, sinf(p * 1.5707963f));
            curLeftEyeH = endLeftEyeH * blinkFactor;
            curRightEyeH = endRightEyeH * blinkFactor;
        }

        float easedT = easeInOutCubic(t);
        float curCurve = customLerp(startCurve, endCurve, easedT);
        float curY     = customLerp(startY, endY, easedT);
        float curW     = customLerp(startW, endW, easedT);
        float curAsym  = customLerp(startAsym, endAsym, easedT);
        float curBrow  = customLerp(startBrow, endBrow, easedT);

        float joyScale = (t < 0.5f) ? fmaxf(0.0f, 1.0f - t * 2.0f) : fmaxf(0.0f, (t - 0.5f) * 2.0f);

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
            drawShockEyes(ox, oy, fgColor, g_currentVergence, g_currentEyeScale * curLeftEyeH);
        } else if (activeExpr == EXPR_OVERLOAD) {
            drawSpiralEyes(ox, oy, t * 2.0f, fgColor, g_currentVergence, g_currentEyeScale * curLeftEyeH);
        } else if (activeExpr == EXPR_SEDIH) {
            drawSedihEyes(ox, oy, t * 4.0f, fgColor, g_currentVergence, g_currentEyeScale * curLeftEyeH);
        }

        if (curBrow > 0.01f) {
            drawAngryBrows(curLeftEyeH, ox, oy, curBrow, bgColor, g_currentVergence, g_currentEyeScale);
        }

        bool fromCurveMouth = (fromExpr == EXPR_IDLE || fromExpr == EXPR_ANGRY);
        bool toCurveMouth   = (toExpr == EXPR_IDLE || toExpr == EXPR_ANGRY);

        if (fromCurveMouth && toCurveMouth) {
            drawMouthCustom(ox, oy, curCurve, curY, curW, curAsym, fgColor, g_currentEyeScale);
        } else {
            Expression mouthExpr = (t < 0.5f) ? fromExpr : toExpr;
            if (mouthExpr == EXPR_IDLE || mouthExpr == EXPR_ANGRY) {
                drawMouthCustom(ox, oy, curCurve, curY, curW, curAsym, fgColor, g_currentEyeScale);
            } else if (mouthExpr == EXPR_SMIRK) {
                drawCatMouth(ox, oy, fgColor, g_currentEyeScale);
            } else if (mouthExpr == EXPR_DEADPAN) {
                drawDeadpanMouth(ox, oy, fgColor, g_currentEyeScale);
            } else if (mouthExpr == EXPR_JOY) {
                drawJoyMouth(ox, oy, joyScale, fgColor, g_currentEyeScale);
            } else if (mouthExpr == EXPR_SHOCK) {
                drawShockMouth(ox, oy, fgColor, g_currentEyeScale);
            } else if (mouthExpr == EXPR_OVERLOAD) {
                drawOverloadMouth(ox, oy, fgColor, g_currentEyeScale);
            } else if (mouthExpr == EXPR_SEDIH) {
                drawSedihMouth(ox, oy, t * 4.0f, fgColor, g_currentEyeScale);
            }
        }

        canvas.pushSprite(0, 0);
        vTaskDelay(pdMS_TO_TICKS((int)stepDelay));
    }
    g_currentExpr = toExpr;
}

void oledTask(void *pvParameters) {
    (void)pvParameters;

    lcd.init();
    lcd.setRotation(2);
    lcd.setBrightness(OLED_DEFAULT_BRIGHTNESS);

    canvas.setColorDepth(1);
    canvas.createSprite(OLED_PANEL_WIDTH_PX, OLED_PANEL_HEIGHT_PX);

    g_currentExpr = EXPR_IDLE;
    drawFace(g_currentExpr, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);

    while (true) {
        uint32_t frame_start_us = micros();
        unsigned long now = millis();

        Expression prevExpr = g_currentExpr;
        updateBiologicalMoodEngine();

        if (g_recon_state != s_prev_recon_state) {
            s_prev_recon_state = g_recon_state;
            if (g_recon_state == STATE_SLEEP_RECON) {
                lcd.setBrightness(OLED_SLEEP_BRIGHTNESS);
            } else {
                lcd.setBrightness(OLED_DEFAULT_BRIGHTNESS);
                s_burn_shift_x = 0;
                s_burn_shift_y = 0;
            }
        }

        if (g_recon_state == STATE_SLEEP_RECON && (now - s_last_burn_shift_ms > 60000)) {
            s_last_burn_shift_ms = now;
            s_burn_shift_x = (int)(esp_random() % 3) - 1;
            s_burn_shift_y = (int)(esp_random() % 3) - 1;
        }

        if (g_currentExpr != prevExpr) {
            frame_start_us = micros();
        } else {
            updateGazeSystem();

            bool canBlink = (g_currentExpr != EXPR_OVERLOAD && g_currentExpr != EXPR_SEDIH && g_currentExpr != EXPR_JOY);

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

            if (g_currentExpr == EXPR_OVERLOAD || g_currentExpr == EXPR_SEDIH) {
                g_animFrame += 0.025f;
            }

            drawFace(g_currentExpr, g_blinkEyeHeight, g_currentOffsetX, g_currentOffsetY, g_animFrame, g_currentVergence, g_currentEyeScale);
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
        }
    }
}
