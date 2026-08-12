/*
 * KoRe (Kinematic Optical Recognition Engine)
 * Standalone Embedded Biomechanical Human Face Tracker & Animated UI
 * Target: Seeed Studio XIAO ESP32-S3 Sense
 * Display: 0.96" SSD1306 OLED via LovyanGFX I2C (800kHz)
 * Architecture: Dual-Core FreeRTOS (Core 0: Vision & Stream, Core 1: OLED & UI Engine)
 */

#include "esp_camera.h"
#include "esp_http_server.h"
#include "img_converters.h"
#include <WiFi.h>
#include <math.h>
#include <esp_random.h>
#include <LovyanGFX.hpp>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// --- Hardware Configuration & Pins ---
#define ENABLE_TOUCH_PIN false // Set true if physical touch sensor is attached
#define TOUCH_PIN 2            // D1 (GPIO 2)

enum Expression {
  EXPR_IDLE,
  EXPR_JOY,
  EXPR_ANGRY,
  EXPR_SMIRK,
  EXPR_SHOCK,
  EXPR_OVERLOAD,
  EXPR_SEDIH
};

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_SSD1306 _panel_instance;
  lgfx::Bus_I2C _bus_instance;

public:
  LGFX() {
    {
      auto cfg = _bus_instance.config();
      cfg.i2c_port = 0;
      cfg.freq_write = 800000; // 800kHz Fast-mode I2C
      cfg.freq_read = 400000;
      cfg.pin_scl = 5; // D4 = GPIO 5
      cfg.pin_sda = 6; // D5 = GPIO 6
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.panel_width = 128;
      cfg.panel_height = 64;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    setPanel(&_panel_instance);
  }
};

LGFX lcd;

// --- Network Configuration ---
#define USE_AP_MODE false
const char* ap_ssid     = "KoRe-Tracker";
const char* ap_password = "12345678";

const char* sta_ssid     = "Kasminingsih";
const char* sta_password = "hidet4mp4n";

// --- Camera Hardware Configuration ---
#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  10
#define SIOD_GPIO_NUM  40
#define SIOC_GPIO_NUM  39

#define Y9_GPIO_NUM    48
#define Y8_GPIO_NUM    11
#define Y7_GPIO_NUM    12
#define Y6_GPIO_NUM    14
#define Y5_GPIO_NUM    16
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    17
#define Y2_GPIO_NUM    15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13

// --- Telemetry & Target State Data Structures ---
struct TrackTarget {
  bool detected;
  int x;
  int y;
  int w;
  int h;
  int cx;
  int cy;
  float error_x;
  float error_y;
  float confidence;
  float total_energy;
  float vx;
  float vy;
  uint32_t last_seen_ms;
};

static TrackTarget current_target = {false, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0};
static portMUX_TYPE target_mutex = portMUX_INITIALIZER_UNLOCKED;

static volatile float fps_ai = 0.0f;
static volatile float fps_stream = 0.0f;
static const int frame_w = 640;
static const int frame_h = 480;

// --- Framebuffer Processing Allocations ---
static uint8_t* small_rgb_buf = NULL;
static uint8_t* prev_lum_buf  = NULL;
static uint8_t* mhi_buf       = NULL;

// --- Core 0 to Core 1 IPC Streaming Sync ---
static volatile int g_stream_clients = 0;
static uint8_t* g_latest_jpeg_buf = NULL;
static size_t g_latest_jpeg_len = 0;
static portMUX_TYPE g_stream_mutex = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t g_frame_sem = NULL;

// --- Web Server Contexts ---
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// --- Animation & Mood Engine States ---
Expression currentExpr = EXPR_IDLE;

unsigned long lastBlink = 0;
float animFrame = 0.0f;
unsigned long lastAnimUpdate = 0;

bool lastTouchState = false;
unsigned long lastTouchCheck = 0;

float currentOffsetX = 0.0f;
float currentOffsetY = 0.0f;
float startOffsetX = 0.0f;
float startOffsetY = 0.0f;
float targetOffsetX = 0.0f;
float targetOffsetY = 0.0f;
unsigned long gazeStartTime = 0;
unsigned long gazeDuration = 120;
unsigned long nextGazeTime = 0;
bool inSaccade = false;

// --- Biomechanical Ocular Kinematics State ---
static float eye_vx = 0.0f;           // Velocity X (px/s)
static float eye_vy = 0.0f;           // Velocity Y (px/s)
static float smoothedTargetX = 0.0f;  // Deadzone-filtered target X
static float smoothedTargetY = 0.0f;  // Deadzone-filtered target Y
static bool trackInSaccade = false;   // Active tracking saccade flag
static uint32_t trackSaccadeStart = 0;
static uint32_t trackSaccadeDuration = 60;
static float trackSaccadeStartX = 0.0f;
static float trackSaccadeStartY = 0.0f;

// --- Non-Blocking Eyelid Blink Engine ---
enum BlinkState {
  BLINK_IDLE_STATE,
  BLINK_CLOSING_STATE,
  BLINK_OPENING_STATE
};

static BlinkState g_blinkState = BLINK_IDLE_STATE;
static uint32_t g_blinkStartTime = 0;
static uint32_t g_nextBlinkTime = 0;
static float g_blinkEyeHeight = 1.0f;
static bool g_isDoubleBlinkPending = false;

// --- Dynamic Biological Mood Engine ---
static uint32_t g_nextMoodShiftTime = 0;
static bool g_lastTargetDetectedState = false;

// --- Telemetry Dashboard Web UI ---
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
  <title>KoRe Telemetry Dashboard</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { background: #000; width: 100vw; height: 100vh; display: flex; justify-content: center; align-items: center; overflow: hidden; }
    .wrapper { position: relative; display: flex; justify-content: center; align-items: center; max-width: 100vw; max-height: 100vh; }
    #stream-img { display: block; max-width: 100vw; max-height: 100vh; width: auto; height: auto; object-fit: contain; user-select: none; }
    #hud-canvas { position: absolute; top: 0; left: 0; width: 100%; height: 100%; pointer-events: none; }
  </style>
</head>
<body>
  <div class="wrapper" id="wrapper">
    <img id="stream-img" src="" alt="" crossorigin="anonymous">
    <canvas id="hud-canvas"></canvas>
  </div>

  <script>
    const host = window.location.hostname;
    const img = document.getElementById('stream-img');
    const canvas = document.getElementById('hud-canvas');
    const ctx = canvas.getContext('2d');
    img.src = 'http://' + host + ':81/stream';

    function resizeCanvas() {
      if (img.clientWidth > 0 && img.clientHeight > 0) {
        canvas.width = img.clientWidth;
        canvas.height = img.clientHeight;
      }
    }
    window.addEventListener('resize', resizeCanvas);
    img.onload = resizeCanvas;

    let renderBox = null;
    async function updateTelemetry() {
      try {
        const res = await fetch('http://' + host + '/telemetry');
        const data = await res.json();
        ctx.clearRect(0, 0, canvas.width, canvas.height);

        if (data.detected && data.fw > 0 && data.fh > 0) {
          resizeCanvas();
          const scaleX = canvas.width / data.fw;
          const scaleY = canvas.height / data.fh;

          const targetBx = data.x * scaleX;
          const targetBy = data.y * scaleY;
          const targetBw = data.w * scaleX;
          const targetBh = data.h * scaleY;
          const targetCx = data.cx * scaleX;
          const targetCy = data.cy * scaleY;

          if (!renderBox) {
            renderBox = { bx: targetBx, by: targetBy, bw: targetBw, bh: targetBh, cx: targetCx, cy: targetCy };
          } else {
            renderBox.bx = renderBox.bx * 0.25 + targetBx * 0.75;
            renderBox.by = renderBox.by * 0.25 + targetBy * 0.75;
            renderBox.bw = renderBox.bw * 0.25 + targetBw * 0.75;
            renderBox.bh = renderBox.bh * 0.25 + targetBh * 0.75;
            renderBox.cx = renderBox.cx * 0.25 + targetCx * 0.75;
            renderBox.cy = renderBox.cy * 0.25 + targetCy * 0.75;
          }

          const bx = renderBox.bx, by = renderBox.by, bw = renderBox.bw, bh = renderBox.bh, cx = renderBox.cx, cy = renderBox.cy;

          ctx.strokeStyle = '#00ff88';
          ctx.lineWidth = 2;
          ctx.strokeRect(bx, by, bw, bh);

          const len = 14;
          ctx.lineWidth = 3.5;
          ctx.beginPath(); ctx.moveTo(bx, by + len); ctx.lineTo(bx, by); ctx.lineTo(bx + len, by); ctx.stroke();
          ctx.beginPath(); ctx.moveTo(bx + bw - len, by); ctx.lineTo(bx + bw, by); ctx.lineTo(bx + bw, by + len); ctx.stroke();
          ctx.beginPath(); ctx.moveTo(bx, by + bh - len); ctx.lineTo(bx, by + bh); ctx.lineTo(bx + len, by + bh); ctx.stroke();
          ctx.beginPath(); ctx.moveTo(bx + bw - len, by + bh); ctx.lineTo(bx + bw, by + bh); ctx.lineTo(bx + bw, by + bh - len); ctx.stroke();

          ctx.strokeStyle = '#00d8ff';
          ctx.lineWidth = 1.5;
          ctx.beginPath();
          ctx.moveTo(cx - 10, cy); ctx.lineTo(cx + 10, cy);
          ctx.moveTo(cx, cy - 10); ctx.lineTo(cx, cy + 10);
          ctx.stroke();
          ctx.beginPath(); ctx.arc(cx, cy, 3.5, 0, 2 * Math.PI); ctx.stroke();
        } else {
          renderBox = null;
        }
      } catch (e) {}
      setTimeout(updateTelemetry, 50);
    }
    updateTelemetry();
  </script>
</body>
</html>
)rawliteral";

// --- Easing & Math Utilities ---
float easeInOutCubic(float t) {
  return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

float blinkCloseEase(float t) {
  return 1.0f - (t * t);
}

float blinkOpenEase(float t) {
  return sinf(t * 1.5707963f);
}

float customLerp(float a, float b, float t) {
  return a + t * (b - a);
}

void drawSensorOverlay() {
  // Sensor text overlay explicitly disabled for clean UI
}

// --- Biomechanical 2D Gaze Controller ---
void updateGazeSystem() {
  TrackTarget target;
  portENTER_CRITICAL(&target_mutex);
  target = current_target;
  portEXIT_CRITICAL(&target_mutex);

  unsigned long now = millis();

  // Target tracking: 2nd-order damped smooth pursuit & minimum-jerk saccades
  if (target.detected) {
    float normX = constrain(target.error_x / 100.0f, -1.0f, 1.0f);
    float normY = constrain(target.error_y / 100.0f, -1.0f, 1.0f);

    float boostX = (normX >= 0.0f ? 1.0f : -1.0f) * sqrtf(fabsf(normX));
    float boostY = (normY >= 0.0f ? 1.0f : -1.0f) * sqrtf(fabsf(normY));

    float rawTargetX = boostX * 18.0f;
    float rawTargetY = boostY * 12.0f;

    // Foveal deadzone & noise suppression
    float dx_raw = rawTargetX - smoothedTargetX;
    float dy_raw = rawTargetY - smoothedTargetY;
    float dist_raw = sqrtf(dx_raw * dx_raw + dy_raw * dy_raw);

    if (dist_raw < 1.5f) {
      // Lock gaze within deadzone to eliminate sub-pixel jitter
      smoothedTargetX += dx_raw * 0.02f;
      smoothedTargetY += dy_raw * 0.02f;
    } else {
      // Adaptive hysteresis tracking
      float alpha = constrain((dist_raw - 1.5f) / 8.0f, 0.15f, 0.80f);
      smoothedTargetX += dx_raw * alpha;
      smoothedTargetY += dy_raw * alpha;
    }

    // Ballistic saccade detection (>3.5px offset)
    float dx_eye = smoothedTargetX - currentOffsetX;
    float dy_eye = smoothedTargetY - currentOffsetY;
    float dist_eye = sqrtf(dx_eye * dx_eye + dy_eye * dy_eye);

    if (dist_eye > 3.5f && !trackInSaccade) {
      trackInSaccade = true;
      trackSaccadeStart = now;
      trackSaccadeDuration = (uint32_t)constrain(40.0f + dist_eye * 3.0f, 50.0f, 90.0f);
      trackSaccadeStartX = currentOffsetX;
      trackSaccadeStartY = currentOffsetY;
      eye_vx = 0.0f;
      eye_vy = 0.0f;
    }

    if (trackInSaccade) {
      float elapsed = (float)(now - trackSaccadeStart);
      float progress = elapsed / (float)trackSaccadeDuration;

      if (progress >= 1.0f) {
        currentOffsetX = smoothedTargetX;
        currentOffsetY = smoothedTargetY;
        trackInSaccade = false;
      } else {
        // 5th-order minimum-jerk saccade trajectory with muscle overshoot
        float p = progress;
        float s = 10.0f * p * p * p - 15.0f * p * p * p * p + 6.0f * p * p * p * p * p;
        float distX = smoothedTargetX - trackSaccadeStartX;
        float distY = smoothedTargetY - trackSaccadeStartY;
        float overshootX = 0.10f * distX * sinf(3.14159265f * p) * expf(-3.5f * p);
        float overshootY = 0.10f * distY * sinf(3.14159265f * p) * expf(-3.5f * p);

        currentOffsetX = trackSaccadeStartX + (distX * s) + overshootX;
        currentOffsetY = trackSaccadeStartY + (distY * s) + overshootY;
      }
    } else {
      // Mass-spring-damper smooth pursuit (omega_n = 22.0 rad/s, zeta = 0.88)
      float dt = 0.016f; // ~60 FPS
      float omega_n = 22.0f;
      float zeta = 0.88f;

      float ax = (omega_n * omega_n) * (smoothedTargetX - currentOffsetX) - (2.0f * zeta * omega_n) * eye_vx;
      float ay = (omega_n * omega_n) * (smoothedTargetY - currentOffsetY) - (2.0f * zeta * omega_n) * eye_vy;

      eye_vx += ax * dt;
      eye_vy += ay * dt;

      currentOffsetX += eye_vx * dt;
      currentOffsetY += eye_vy * dt;

      // Physiological 4Hz micro-tremor
      if (dist_eye < 1.0f) {
        float tremorX = 0.25f * sinf(now * 0.025f);
        float tremorY = 0.18f * cosf(now * 0.031f);
        currentOffsetX += tremorX * 0.05f;
        currentOffsetY += tremorY * 0.05f;
      }
    }

    inSaccade = false;
    nextGazeTime = now + 600;
    return;
  }

  // Center decay for non-gaze expressions
  if (currentExpr != EXPR_IDLE && currentExpr != EXPR_SHOCK && currentExpr != EXPR_SEDIH) {
    targetOffsetX = 0.0f;
    targetOffsetY = 0.0f;
    if (fabsf(currentOffsetX) > 0.01f || fabsf(currentOffsetY) > 0.01f) {
      currentOffsetX *= 0.70f;
      currentOffsetY *= 0.70f;
    } else {
      currentOffsetX = 0.0f;
      currentOffsetY = 0.0f;
    }
    inSaccade = false;
    return;
  }

  // Idle gaze wander state machine
  if (!inSaccade && now >= nextGazeTime) {
    startOffsetX = currentOffsetX;
    startOffsetY = currentOffsetY;
    
    if (currentExpr == EXPR_IDLE) {
      uint32_t pick = esp_random() % 100;
      if (pick < 40) {
        targetOffsetX = 0.0f;
        targetOffsetY = 0.0f;
      } else if (pick < 70) {
        targetOffsetX = -1.0f * (float)(esp_random() % 9 + 6);
        targetOffsetY = (float)(esp_random() % 7) - 3.0f;
      } else {
        targetOffsetX = (float)(esp_random() % 9 + 6);
        targetOffsetY = (float)(esp_random() % 7) - 3.0f;
      }
      
      gazeDuration = esp_random() % 50 + 100;
      nextGazeTime = now + (esp_random() % 2400 + 1800);
    } 
    else if (currentExpr == EXPR_SHOCK) {
      float dir = (esp_random() % 2 == 0) ? -1.0f : 1.0f;
      targetOffsetX = dir * (float)(esp_random() % 9 + 10);
      targetOffsetY = (float)(esp_random() % 7) - 3.0f;
      
      gazeDuration = esp_random() % 40 + 50;
      nextGazeTime = now + (esp_random() % 600 + 300);
    } 
    else if (currentExpr == EXPR_SEDIH) {
      float dir = (esp_random() % 2 == 0) ? -1.0f : 1.0f;
      targetOffsetX = dir * (float)(esp_random() % 10);
      targetOffsetY = (float)(esp_random() % 6 + 3);
      
      gazeDuration = esp_random() % 80 + 180;
      nextGazeTime = now + (esp_random() % 2500 + 2500);
    }

    gazeStartTime = now;
    inSaccade = true;
  }

  // Idle saccadic trajectory
  if (inSaccade) {
    float elapsed = (float)(now - gazeStartTime);
    float progress = elapsed / (float)gazeDuration;

    if (progress >= 1.0f) {
      currentOffsetX = targetOffsetX;
      currentOffsetY = targetOffsetY;
      inSaccade = false;
    } else {
      float p = progress;
      float s = 10.0f * p * p * p - 15.0f * p * p * p * p + 6.0f * p * p * p * p * p;
      float distX = targetOffsetX - startOffsetX;
      float distY = targetOffsetY - startOffsetY;
      float overshootX = 0.12f * distX * sinf(3.14159265f * p) * expf(-3.0f * p);
      float overshootY = 0.12f * distY * sinf(3.14159265f * p) * expf(-3.0f * p);
      currentOffsetX = startOffsetX + (distX * s) + overshootX;
      currentOffsetY = startOffsetY + (distY * s) + overshootY;
    }
  }
}

// --- 2D Facial Primitives ---
void drawEyes(float eyeHeightFactor, float offsetX, float offsetY, uint16_t color) {
  int lx = 32 + (int)offsetX;
  int rx = 96 + (int)offsetX;
  int ly = 28 + (int)offsetY;
  int ry = 28 + (int)offsetY;

  int maxEyeWidth = 28;
  int maxEyeHeight = 38;
  int eyeHeight = (int)(maxEyeHeight * eyeHeightFactor);

  if (eyeHeight <= 3) {
    lcd.fillRoundRect(lx - maxEyeWidth / 2, ly - 1, maxEyeWidth, 3, 1, color);
    lcd.fillRoundRect(rx - maxEyeWidth / 2, ry - 1, maxEyeWidth, 3, 1, color);
  } else {
    int radius = (eyeHeight < 24) ? eyeHeight / 2 : 12;
    lcd.fillRoundRect(lx - maxEyeWidth / 2, ly - eyeHeight / 2, maxEyeWidth, eyeHeight, radius, color);
    lcd.fillRoundRect(rx - maxEyeWidth / 2, ry - eyeHeight / 2, maxEyeWidth, eyeHeight, radius, color);
  }
}

void drawJoyEyes(float offsetX, float offsetY, float scale, uint16_t color) {
  if (scale <= 0.05f) return;
  int lx = 32 + (int)offsetX;
  int rx = 96 + (int)offsetX;
  int ly = 31 + (int)offsetY;

  float eyeWidth = 24.0f * scale;
  float archHeight = 9.0f * scale;

  for (int eye = 0; eye < 2; eye++) {
    int cx = (eye == 0) ? lx : rx;
    for (float x = -eyeWidth / 2.0f; x <= eyeWidth / 2.0f; x += 0.3f) {
      float angle = (x / (eyeWidth / 2.0f)) * (3.14159265f / 2.0f);
      float y = ly - archHeight * cosf(angle);
      lcd.fillCircle(cx + (int)roundf(x), (int)roundf(y), (scale < 0.5f) ? 1 : 2, color);
    }
  }
}

void drawAngryBrows(float eyeHeightFactor, float offsetX, float offsetY, float browAlpha, uint16_t color) {
  if (eyeHeightFactor <= 0.2f || browAlpha <= 0.01f) return;

  int lx = 32 + (int)offsetX;
  int rx = 96 + (int)offsetX;
  int ly = 28 + (int)offsetY;
  int ry = 28 + (int)offsetY;

  int maxEyeWidth = 28;
  int maxEyeHeight = 38;

  int browCutX = (int)((maxEyeWidth / 2 + 4) * browAlpha);
  int browCutY = (int)((maxEyeHeight / 2 + 3) * browAlpha);

  lcd.fillTriangle(
    lx - 2, ly - maxEyeHeight / 2 - 1,
    lx - 2 + browCutX, ly - maxEyeHeight / 2 - 1,
    lx - 2 + browCutX, ly - maxEyeHeight / 2 - 1 + browCutY,
    color
  );

  lcd.fillTriangle(
    rx + 2, ry - maxEyeHeight / 2 - 1,
    rx + 2 - browCutX, ry - maxEyeHeight / 2 - 1,
    rx + 2 - browCutX, ry - maxEyeHeight / 2 - 1 + browCutY,
    color
  );
}

void drawShockEyes(float offsetX, float offsetY, uint16_t color) {
  int lx = 32 + (int)offsetX;
  int rx = 96 + (int)offsetX;
  int ly = 28 + (int)offsetY;
  int ry = 28 + (int)offsetY;

  lcd.drawRoundRect(lx - 14, ly - 18, 28, 36, 12, color);
  lcd.drawRoundRect(lx - 13, ly - 17, 26, 34, 11, color);
  lcd.fillCircle(lx, ly, 3, color);

  lcd.drawRoundRect(rx - 14, ry - 18, 28, 36, 12, color);
  lcd.drawRoundRect(rx - 13, ry - 17, 26, 34, 11, color);
  lcd.fillCircle(rx, ry, 3, color);
}

void drawSpiralEye(int cx, int cy, float rotAngle, uint16_t color) {
  float prevX = cx;
  float prevY = cy;
  for (float theta = 0.2f; theta <= 13.5f; theta += 0.35f) {
    float r = 0.85f * theta;
    float angle = theta + rotAngle;
    float x = cx + r * cosf(angle);
    float y = cy + r * sinf(angle);
    lcd.drawLine((int)prevX, (int)prevY, (int)x, (int)y, color);
    prevX = x;
    prevY = y;
  }
}

void drawSpiralEyes(float offsetX, float offsetY, float rotAngle, uint16_t color) {
  int lx = 32 + (int)offsetX;
  int rx = 96 + (int)offsetX;
  int ly = 28 + (int)offsetY;
  int ry = 28 + (int)offsetY;

  drawSpiralEye(lx, ly, rotAngle, color);
  drawSpiralEye(rx, ry, -rotAngle, color);
}

void drawSedihEyes(float offsetX, float offsetY, uint16_t color) {
  int lx = 32 + (int)offsetX;
  int rx = 96 + (int)offsetX;
  int ly = 28 + (int)offsetY;
  int ry = 28 + (int)offsetY;

  for (float x = -11.0f; x <= 11.0f; x += 0.4f) {
    float y = ly + 4.0f - 0.065f * x * x;
    lcd.fillCircle(lx + (int)roundf(x), (int)roundf(y), 1, color);
    lcd.fillCircle(rx + (int)roundf(x), (int)roundf(y), 1, color);
  }
}

// Synchronized mouth path rendering
void drawMouthCustom(float offsetX, float offsetY, float curve, float baseY, float width, float asym, uint16_t color) {
  int mx = 64 + (int)offsetX;
  float my = baseY + offsetY;
  for (float x = -width; x <= width; x += 0.4f) {
    float y = my + curve * x * x + asym * x;
    lcd.fillCircle(mx + (int)roundf(x), (int)roundf(y), 1, color);
  }
}

void drawJoyMouth(float offsetX, float offsetY, float scale, uint16_t color) {
  if (scale <= 0.05f) return;
  int mx = 64 + (int)offsetX;
  float width = 11.0f * scale;
  float baseY = 39.0f + offsetY;

  for (float x = -width; x <= width; x += 0.4f) {
    float normX = (width > 0) ? (x / width) : 0;
    float yTop = baseY - 0.02f * x * x;
    float yBottom = yTop + (11.0f * scale) * (1.0f - normX * normX);

    for (float y = yTop; y <= yBottom; y += 0.6f) {
      lcd.fillCircle(mx + (int)roundf(x), (int)roundf(y), 1, color);
    }
  }
}

void drawShockMouth(float offsetX, float offsetY, uint16_t color) {
  int mx = 64 + (int)offsetX;
  int my = 39 + (int)offsetY;
  lcd.fillRoundRect(mx - 8, my, 16, 14, 5, color);
}

void drawOverloadMouth(float offsetX, float offsetY, uint16_t color) {
  int mx = 64 + (int)offsetX;
  int my = 45 + (int)offsetY;
  lcd.fillEllipse(mx, my, 7, 5, color);
}

void drawSedihMouth(float offsetX, float offsetY, float phase, uint16_t color) {
  int mx = 64 + (int)offsetX;
  float baseY = 44.0f + offsetY;
  for (float x = -8.0f; x <= 8.0f; x += 0.4f) {
    float y = baseY + 1.2f * sinf(0.8f * x + phase);
    lcd.fillCircle(mx + (int)roundf(x), (int)roundf(y), 1, color);
  }
}

void renderFaceState(float eyeHeightFactor, float offsetX, float offsetY, float mouthCurve, float mouthY, float mouthWidth, float browAlpha, bool inverted) {
  uint16_t bgColor = inverted ? TFT_WHITE : TFT_BLACK;
  uint16_t fgColor = inverted ? TFT_BLACK : TFT_WHITE;

  lcd.startWrite();
  lcd.clear(bgColor);
  drawEyes(eyeHeightFactor, offsetX, offsetY, fgColor);
  drawAngryBrows(eyeHeightFactor, offsetX, offsetY, browAlpha, bgColor);
  drawMouthCustom(offsetX, offsetY, mouthCurve, mouthY, mouthWidth, 0.0f, fgColor);
  drawSensorOverlay();
  lcd.endWrite();
}

void drawFaceIdle(float eyeHeightFactor, float offsetX, float offsetY) {
  renderFaceState(eyeHeightFactor, offsetX, offsetY, -0.030f, 44.0f, 7.5f, 0.0f, false);
}

void drawFaceJoy(float offsetX, float offsetY) {
  lcd.startWrite();
  lcd.clear(TFT_BLACK);
  drawJoyEyes(offsetX, offsetY, 1.0f, TFT_WHITE);
  drawJoyMouth(offsetX, offsetY, 1.0f, TFT_WHITE);
  drawSensorOverlay();
  lcd.endWrite();
}

void drawFaceAngry(float eyeHeightFactor, float offsetX, float offsetY) {
  renderFaceState(eyeHeightFactor, offsetX, offsetY, 0.042f, 43.0f, 7.0f, 1.0f, true);
}

void drawFaceSmirk(float eyeHeightFactor, float offsetX, float offsetY) {
  lcd.startWrite();
  lcd.clear(TFT_BLACK);
  drawEyes(0.35f * eyeHeightFactor, offsetX, offsetY, TFT_WHITE);
  drawMouthCustom(offsetX, offsetY, -0.035f, 43.0f, 9.0f, 0.14f, TFT_WHITE);
  drawSensorOverlay();
  lcd.endWrite();
}

void drawFaceShock(float eyeHeightFactor, float offsetX, float offsetY) {
  lcd.startWrite();
  lcd.clear(TFT_BLACK);
  if (eyeHeightFactor > 0.3f) {
    drawShockEyes(offsetX, offsetY, TFT_WHITE);
  } else {
    drawEyes(eyeHeightFactor, offsetX, offsetY, TFT_WHITE);
  }
  drawShockMouth(offsetX, offsetY, TFT_WHITE);
  drawSensorOverlay();
  lcd.endWrite();
}

void drawFaceOverload(float eyeHeightFactor, float offsetX, float offsetY, float frame = 0.0f) {
  lcd.startWrite();
  lcd.clear(TFT_BLACK);
  drawSpiralEyes(offsetX, offsetY, frame, TFT_WHITE);
  drawOverloadMouth(offsetX, offsetY, TFT_WHITE);
  drawSensorOverlay();
  lcd.endWrite();
}

void drawFaceSedih(float eyeHeightFactor, float offsetX, float offsetY, float frame = 0.0f) {
  lcd.startWrite();
  lcd.clear(TFT_BLACK);
  drawSedihEyes(offsetX, offsetY, TFT_WHITE);
  drawSedihMouth(offsetX, offsetY, frame, TFT_WHITE);
  drawSensorOverlay();
  lcd.endWrite();
}

void drawFace(Expression expr, float eyeHeightFactor, float offsetX, float offsetY, float frame = 0.0f) {
  switch (expr) {
    case EXPR_IDLE: drawFaceIdle(eyeHeightFactor, offsetX, offsetY); break;
    case EXPR_JOY: drawFaceJoy(offsetX, offsetY); break;
    case EXPR_ANGRY: drawFaceAngry(eyeHeightFactor, offsetX, offsetY); break;
    case EXPR_SMIRK: drawFaceSmirk(eyeHeightFactor, offsetX, offsetY); break;
    case EXPR_SHOCK: drawFaceShock(eyeHeightFactor, offsetX, offsetY); break;
    case EXPR_OVERLOAD: drawFaceOverload(eyeHeightFactor, offsetX, offsetY, frame); break;
    case EXPR_SEDIH: drawFaceSedih(eyeHeightFactor, offsetX, offsetY, frame); break;
  }
}

// --- Expression Transition Engine ---
void transitionExpression(Expression fromExpr, Expression toExpr, float durationMs = 380.0f) {
  float startEyeH = (fromExpr == EXPR_SMIRK) ? 0.35f : 1.0f;
  float endEyeH   = (toExpr == EXPR_SMIRK)   ? 0.35f : 1.0f;

  float startCurve = (fromExpr == EXPR_IDLE) ? -0.030f : ((fromExpr == EXPR_ANGRY) ? 0.042f : (fromExpr == EXPR_SMIRK ? -0.035f : 0.0f));
  float endCurve   = (toExpr == EXPR_IDLE)   ? -0.030f : ((toExpr == EXPR_ANGRY)   ? 0.042f : (toExpr == EXPR_SMIRK ? -0.035f : 0.0f));

  float startY = (fromExpr == EXPR_IDLE) ? 44.0f : ((fromExpr == EXPR_ANGRY) ? 43.0f : (fromExpr == EXPR_SMIRK ? 43.0f : (fromExpr == EXPR_SHOCK ? 39.0f : (fromExpr == EXPR_OVERLOAD ? 45.0f : 44.0f))));
  float endY   = (toExpr == EXPR_IDLE)   ? 44.0f : ((toExpr == EXPR_ANGRY)   ? 43.0f : (toExpr == EXPR_SMIRK ? 43.0f : (toExpr == EXPR_SHOCK ? 39.0f : (toExpr == EXPR_OVERLOAD ? 45.0f : 44.0f))));

  float startW = (fromExpr == EXPR_IDLE) ? 7.5f : ((fromExpr == EXPR_ANGRY) ? 7.0f : (fromExpr == EXPR_SMIRK ? 9.0f : 8.0f));
  float endW   = (toExpr == EXPR_IDLE)   ? 7.5f : ((toExpr == EXPR_ANGRY)   ? 7.0f : (toExpr == EXPR_SMIRK ? 9.0f : 8.0f));

  float startAsym = (fromExpr == EXPR_SMIRK) ? 0.14f : 0.0f;
  float endAsym   = (toExpr == EXPR_SMIRK)   ? 0.14f : 0.0f;

  float startBrow = (fromExpr == EXPR_ANGRY) ? 1.0f : 0.0f;
  float endBrow   = (toExpr == EXPR_ANGRY)   ? 1.0f : 0.0f;

  int steps = 16;
  float stepDelay = durationMs / steps;

  for (int i = 0; i <= steps; i++) {
    // Update gaze tracking continuously during transition
    updateGazeSystem();

    float t = (float)i / steps;
    float easedT = easeInOutCubic(t);

    float squish = sinf(t * 3.14159265f);
    float curEyeH = customLerp(startEyeH, endEyeH, easedT) * (1.0f - 0.45f * squish);

    float curCurve = customLerp(startCurve, endCurve, easedT);
    float curY     = customLerp(startY, endY, easedT);
    float curW     = customLerp(startW, endW, easedT);
    float curAsym  = customLerp(startAsym, endAsym, easedT);
    float curBrow  = customLerp(startBrow, endBrow, easedT);

    float joyScale = 1.0f;
    if (fromExpr == EXPR_JOY) {
      joyScale = customLerp(1.0f, 0.0f, easedT * 2.0f);
      if (joyScale < 0.0f) joyScale = 0.0f;
    } else if (toExpr == EXPR_JOY) {
      joyScale = customLerp(0.0f, 1.0f, (easedT - 0.5f) * 2.0f);
      if (joyScale < 0.0f) joyScale = 0.0f;
    }

    bool inverted = (easedT >= 0.5f) ? (toExpr == EXPR_ANGRY) : (fromExpr == EXPR_ANGRY);
    uint16_t bgColor = inverted ? TFT_WHITE : TFT_BLACK;
    uint16_t fgColor = inverted ? TFT_BLACK : TFT_WHITE;

    lcd.startWrite();
    lcd.clear(bgColor);

    Expression activeExpr = (easedT < 0.5f) ? fromExpr : toExpr;
    if (activeExpr == EXPR_IDLE || activeExpr == EXPR_ANGRY || activeExpr == EXPR_SMIRK) {
      drawEyes(curEyeH, currentOffsetX, currentOffsetY, fgColor);
    } else if (activeExpr == EXPR_JOY) {
      drawJoyEyes(currentOffsetX, currentOffsetY, joyScale, fgColor);
    } else if (activeExpr == EXPR_SHOCK) {
      drawShockEyes(currentOffsetX, currentOffsetY, fgColor);
    } else if (activeExpr == EXPR_OVERLOAD) {
      drawSpiralEyes(currentOffsetX, currentOffsetY, t * 2.0f, fgColor);
    } else if (activeExpr == EXPR_SEDIH) {
      drawSedihEyes(currentOffsetX, currentOffsetY, fgColor);
    }

    if (curBrow > 0.01f) {
      drawAngryBrows(curEyeH, currentOffsetX, currentOffsetY, curBrow, bgColor);
    }

    bool fromCurveMouth = (fromExpr == EXPR_IDLE || fromExpr == EXPR_ANGRY || fromExpr == EXPR_SMIRK);
    bool toCurveMouth   = (toExpr == EXPR_IDLE || toExpr == EXPR_ANGRY || toExpr == EXPR_SMIRK);

    if (fromCurveMouth && toCurveMouth) {
      drawMouthCustom(currentOffsetX, currentOffsetY, curCurve, curY, curW, curAsym, fgColor);
    } else {
      Expression mouthExpr = (easedT < 0.5f) ? fromExpr : toExpr;
      if (mouthExpr == EXPR_IDLE || mouthExpr == EXPR_ANGRY || mouthExpr == EXPR_SMIRK) {
        drawMouthCustom(currentOffsetX, currentOffsetY, curCurve, curY, curW, curAsym, fgColor);
      } else if (mouthExpr == EXPR_JOY) {
        drawJoyMouth(currentOffsetX, currentOffsetY, joyScale, fgColor);
      } else if (mouthExpr == EXPR_SHOCK) {
        drawShockMouth(currentOffsetX, currentOffsetY, fgColor);
      } else if (mouthExpr == EXPR_OVERLOAD) {
        drawOverloadMouth(currentOffsetX, currentOffsetY, fgColor);
      } else if (mouthExpr == EXPR_SEDIH) {
        drawSedihMouth(currentOffsetX, currentOffsetY, t * 4.0f, fgColor);
      }
    }

    drawSensorOverlay();
    lcd.endWrite();
    vTaskDelay(pdMS_TO_TICKS((int)stepDelay));
  }
  currentExpr = toExpr;
  inSaccade = false;
}

void blink(Expression expr, float currentOffsetX = 0.0f, float currentOffsetY = 0.0f) {
  if (expr == EXPR_ANGRY || expr == EXPR_OVERLOAD || expr == EXPR_SEDIH || expr == EXPR_JOY) return;

  int closeSteps = 4;
  for (int i = 0; i <= closeSteps; i++) {
    float t = (float)i / closeSteps;
    float h = blinkCloseEase(t);
    drawFace(expr, h, currentOffsetX, currentOffsetY);
    vTaskDelay(pdMS_TO_TICKS(11));
  }

  int openSteps = 7;
  for (int i = 0; i <= openSteps; i++) {
    float t = (float)i / openSteps;
    float h = blinkOpenEase(t);
    drawFace(expr, h, currentOffsetX, currentOffsetY);
    vTaskDelay(pdMS_TO_TICKS(15));
  }
}

void setNextExpression(Expression newExpr) {
  if (currentExpr != newExpr) {
    transitionExpression(currentExpr, newExpr, 380.0f);
    animFrame = 0.0f;
  }
}

// --- Dynamic Biological Mood Engine ---
void updateBiologicalMoodEngine() {
  unsigned long now = millis();
  TrackTarget target;
  portENTER_CRITICAL(&target_mutex);
  target = current_target;
  portEXIT_CRITICAL(&target_mutex);

  // Event 1: Target acquired
  if (target.detected && !g_lastTargetDetectedState) {
    g_lastTargetDetectedState = true;
    uint32_t roll = esp_random() % 100;
    // 55% EXPR_ANGRY, 30% EXPR_SHOCK, 15% EXPR_IDLE
    Expression reactExpr = (roll < 55) ? EXPR_ANGRY : ((roll < 85) ? EXPR_SHOCK : EXPR_IDLE);
    setNextExpression(reactExpr);
    g_nextMoodShiftTime = now + (esp_random() % 4000 + 4000);
    return;
  }

  // Event 2: Target lost
  if (!target.detected && g_lastTargetDetectedState) {
    g_lastTargetDetectedState = false;
    uint32_t roll = esp_random() % 100;
    // 60% EXPR_IDLE, 30% EXPR_ANGRY, 10% EXPR_SEDIH
    Expression reactExpr = (roll < 60) ? EXPR_IDLE : ((roll < 90) ? EXPR_ANGRY : EXPR_SEDIH);
    setNextExpression(reactExpr);
    g_nextMoodShiftTime = now + (esp_random() % 4000 + 3500);
    return;
  }

  // Spontaneous Markov mood transitions
  if (g_nextMoodShiftTime == 0) {
    g_nextMoodShiftTime = now + (esp_random() % 6000 + 5000);
  }

  if (now >= g_nextMoodShiftTime) {
    uint32_t roll = esp_random() % 100;
    Expression nextMood = EXPR_IDLE;

    switch (currentExpr) {
      case EXPR_IDLE:
        if (roll < 48) nextMood = EXPR_IDLE;          // 48% EXPR_IDLE
        else if (roll < 76) nextMood = EXPR_ANGRY;    // 28% EXPR_ANGRY
        else if (roll < 86) nextMood = EXPR_OVERLOAD; // 10% EXPR_OVERLOAD
        else if (roll < 93) nextMood = EXPR_JOY;      // 7% EXPR_JOY
        else if (roll < 97) nextMood = EXPR_SEDIH;    // 4% EXPR_SEDIH
        else nextMood = EXPR_SMIRK;                   // 3% EXPR_SMIRK
        break;

      case EXPR_ANGRY:
        if (roll < 48) nextMood = EXPR_ANGRY;         // 48% EXPR_ANGRY
        else if (roll < 83) nextMood = EXPR_IDLE;     // 35% EXPR_IDLE
        else if (roll < 93) nextMood = EXPR_OVERLOAD; // 10% EXPR_OVERLOAD
        else nextMood = EXPR_SEDIH;                   // 7% EXPR_SEDIH
        break;

      case EXPR_JOY:
        if (roll < 65) nextMood = EXPR_IDLE;          // 65% EXPR_IDLE
        else if (roll < 85) nextMood = EXPR_ANGRY;    // 20% EXPR_ANGRY
        else if (roll < 93) nextMood = EXPR_OVERLOAD; // 8% EXPR_OVERLOAD
        else nextMood = EXPR_SMIRK;                   // 7% EXPR_SMIRK
        break;

      case EXPR_SMIRK:
        if (roll < 55) nextMood = EXPR_ANGRY;         // 55% EXPR_ANGRY
        else if (roll < 85) nextMood = EXPR_IDLE;     // 30% EXPR_IDLE
        else nextMood = EXPR_OVERLOAD;                // 15% EXPR_OVERLOAD
        break;

      case EXPR_SHOCK:
        if (roll < 50) nextMood = EXPR_ANGRY;         // 50% EXPR_ANGRY
        else if (roll < 80) nextMood = EXPR_IDLE;     // 30% EXPR_IDLE
        else nextMood = EXPR_OVERLOAD;                // 20% EXPR_OVERLOAD
        break;

      case EXPR_OVERLOAD:
        if (roll < 55) nextMood = EXPR_ANGRY;         // 55% EXPR_ANGRY
        else if (roll < 88) nextMood = EXPR_IDLE;     // 33% EXPR_IDLE
        else nextMood = EXPR_SEDIH;                   // 12% EXPR_SEDIH
        break;

      case EXPR_SEDIH:
        if (roll < 50) nextMood = EXPR_IDLE;          // 50% EXPR_IDLE
        else if (roll < 78) nextMood = EXPR_ANGRY;    // 28% EXPR_ANGRY
        else if (roll < 90) nextMood = EXPR_OVERLOAD; // 12% EXPR_OVERLOAD
        else nextMood = EXPR_JOY;                     // 10% EXPR_JOY
        break;
    }

    setNextExpression(nextMood);
    g_nextMoodShiftTime = now + (esp_random() % 7500 + 4500);
  }
}

// --- OLED Display & UI Task (Core 1) ---
void oledTask(void *pvParameters) {
  pinMode(TOUCH_PIN, INPUT_PULLDOWN);

  lcd.init();
  lcd.setRotation(2);
  lcd.setBrightness(128);

  currentExpr = EXPR_IDLE;
  drawFace(currentExpr, 1.0f, 0.0f, 0.0f);

  float lastDrawnX = 999.0f;
  float lastDrawnY = 999.0f;

  while (true) {
    unsigned long now = millis();

    #if ENABLE_TOUCH_PIN
    if (now - lastTouchCheck > 50) {
      lastTouchCheck = now;
      bool currentTouchState = digitalRead(TOUCH_PIN);
      if (currentTouchState && !lastTouchState) {
        int nextIndex = (static_cast<int>(currentExpr) + 1) % 7;
        setNextExpression(static_cast<Expression>(nextIndex));
      }
      lastTouchState = currentTouchState;
    }
    #endif

    updateBiologicalMoodEngine();
    updateGazeSystem();

    bool isTargetLocked = false;
    portENTER_CRITICAL(&target_mutex);
    isTargetLocked = current_target.detected;
    portEXIT_CRITICAL(&target_mutex);

    // Eyelid blink state machine
    bool canBlink = (currentExpr != EXPR_ANGRY && currentExpr != EXPR_OVERLOAD && currentExpr != EXPR_SEDIH && currentExpr != EXPR_JOY);

    if (!canBlink) {
      g_blinkState = BLINK_IDLE_STATE;
      g_blinkEyeHeight = 1.0f;
    } else {
      if (g_blinkState == BLINK_IDLE_STATE) {
        if (g_nextBlinkTime == 0) {
          g_nextBlinkTime = now + (esp_random() % 3500 + 3500);
        }
        if (now >= g_nextBlinkTime) {
          g_blinkState = BLINK_CLOSING_STATE;
          g_blinkStartTime = now;
          if (g_isDoubleBlinkPending) {
            g_isDoubleBlinkPending = false;
          } else if ((esp_random() % 100) < 14) { // 14% double-blink probability
            g_isDoubleBlinkPending = true;
          }
        }
      }

      if (g_blinkState == BLINK_CLOSING_STATE) {
        float elapsed = (float)(now - g_blinkStartTime);
        float duration = 50.0f; // Eyelid closure phase (50ms)
        if (elapsed >= duration) {
          g_blinkEyeHeight = 0.0f;
          g_blinkState = BLINK_OPENING_STATE;
          g_blinkStartTime = now;
        } else {
          float t = elapsed / duration;
          g_blinkEyeHeight = blinkCloseEase(t);
        }
      } else if (g_blinkState == BLINK_OPENING_STATE) {
        float elapsed = (float)(now - g_blinkStartTime);
        float duration = 110.0f; // Eyelid opening phase (110ms)
        if (elapsed >= duration) {
          g_blinkEyeHeight = 1.0f;
          g_blinkState = BLINK_IDLE_STATE;
          if (g_isDoubleBlinkPending) {
            g_nextBlinkTime = now + 140;
          } else {
            g_nextBlinkTime = now + (esp_random() % 3500 + 3500); // 3.5s - 7.0s natural interval
          }
        } else {
          float t = elapsed / duration;
          g_blinkEyeHeight = blinkOpenEase(t);
        }
      } else {
        g_blinkEyeHeight = 1.0f;
      }
    }

    // Display render pipeline
    if (currentExpr == EXPR_OVERLOAD || currentExpr == EXPR_SEDIH) {
      if (now - lastAnimUpdate > 25) {
        lastAnimUpdate = now;
        animFrame += 0.035f;
        drawFace(currentExpr, g_blinkEyeHeight, currentOffsetX, currentOffsetY, animFrame);
      }
    } else {
      bool isBlinking = (g_blinkState != BLINK_IDLE_STATE);
      bool needRedraw = isTargetLocked || inSaccade || isBlinking ||
                         fabsf(currentOffsetX - lastDrawnX) > 0.08f ||
                         fabsf(currentOffsetY - lastDrawnY) > 0.08f;
      if (needRedraw) {
        drawFace(currentExpr, g_blinkEyeHeight, currentOffsetX, currentOffsetY);
        lastDrawnX = currentOffsetX;
        lastDrawnY = currentOffsetY;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(16));
  }
}

// --- Kinematic State Predictor ---
struct KinematicState2D {
  float x;
  float y;
  float vx;
  float vy;
  float ax;
  float ay;
  float w;
  float h;
  bool active;
  uint32_t last_update_us;
};

static KinematicState2D k_state = { 320.0f, 240.0f, 0.0f, 0.0f, 0.0f, 0.0f, 160.0f, 200.0f, false, 0 };
static float lock_confidence = 0.0f;
static uint32_t last_valid_human_time = 0;
static float ema_global_luminance = 100.0f;

static float debug_m00 = 0.0f;
static int debug_skin_px = 0;
static float debug_lock_conf = 0.0f;

// --- Differential Computer Vision Engine ---
void processFrameAI(camera_fb_t *fb) {
  if (!fb || !fb->buf || fb->len < 1024 || !small_rgb_buf || !prev_lum_buf || !mhi_buf || fb->format != PIXFORMAT_JPEG) return;

  bool ok = jpg2rgb565(fb->buf, fb->len, small_rgb_buf, JPG_SCALE_8X);
  if (!ok) return;

  uint16_t* pixels = (uint16_t*)small_rgb_buf;

  static uint8_t cur_40x30[1200];
  static bool skin_mask_40x30[1200];
  static uint8_t texture_40x30[1200];
  int skin_pixel_count = 0;
  uint32_t total_luminance_sum = 0;

  float delta_y_lux = fmaxf(0.0f, fminf(12.0f, (90.0f - ema_global_luminance) * 0.5f));
  int min_cb = (int)(77.0f - delta_y_lux);
  int max_cb = (int)(127.0f + delta_y_lux);
  int min_cr = (int)(128.0f - delta_y_lux);
  int max_cr = (int)(178.0f + delta_y_lux);

  uint8_t* p_cur = cur_40x30;
  bool* p_skin = skin_mask_40x30;

  for (int y = 0; y < 30; y++) {
    int src_row_off = (y * 2) * 80;
    for (int x = 0; x < 40; x++) {
      uint16_t p = pixels[src_row_off + (x * 2)];
      int r = ((p >> 11) & 0x1F) << 3;
      int g = ((p >> 5) & 0x3F) << 2;
      int b = (p & 0x1F) << 3;
      
      uint8_t y_lum = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
      *p_cur++ = y_lum;
      total_luminance_sum += y_lum;

      int cb = 128 + (((-43 * r - 85 * g + 128 * b)) >> 8);
      int cr = 128 + (((128 * r - 107 * g - 21 * b)) >> 8);

      bool is_skin = (cb >= min_cb && cb <= max_cb && cr >= min_cr && cr <= max_cr);
      *p_skin++ = is_skin;
      if (is_skin) skin_pixel_count++;
    }
  }

  float frame_mean_lum = (float)total_luminance_sum / 1200.0f;
  ema_global_luminance = 0.90f * ema_global_luminance + 0.10f * frame_mean_lum;

  static uint8_t smooth_40x30[1200];
  for (int y = 1; y < 29; y++) {
    int y_off = y * 40;
    for (int x = 1; x < 39; x++) {
      int idx = y_off + x;
      int sum = cur_40x30[idx - 41] + cur_40x30[idx - 40] + cur_40x30[idx - 39] +
                cur_40x30[idx - 1]  + cur_40x30[idx]      + cur_40x30[idx + 1]  +
                cur_40x30[idx + 39] + cur_40x30[idx + 40] + cur_40x30[idx + 41];
      uint8_t avg = (uint8_t)(sum / 9);
      smooth_40x30[idx] = avg;
      texture_40x30[idx] = (uint8_t)abs((int)cur_40x30[idx] - (int)avg);
    }
  }

  static bool first_frame = true;
  if (first_frame) {
    memcpy(prev_lum_buf, smooth_40x30, 1200);
    memset(mhi_buf, 0, 1200);
    first_frame = false;
    k_state.last_update_us = micros();
    return;
  }

  int total_delta_sum = 0;
  int sample_count = 0;
  for (int y = 1; y < 29; y++) {
    int y_off = y * 40;
    for (int x = 1; x < 39; x++) {
      int idx = y_off + x;
      total_delta_sum += abs((int)smooth_40x30[idx] - (int)prev_lum_buf[idx]);
      sample_count++;
    }
  }
  float global_delta_mean = sample_count > 0 ? ((float)total_delta_sum / (float)sample_count) : 0.0f;

  double M00 = 0.0, M10 = 0.0, M01 = 0.0, M20 = 0.0, M02 = 0.0, M11 = 0.0;

  for (int y = 1; y < 29; y++) {
    int y_off = y * 40;
    for (int x = 1; x < 39; x++) {
      int idx = y_off + x;

      uint8_t lum = smooth_40x30[idx];
      uint8_t prev_l = prev_lum_buf[idx];

      float local_delta = (float)abs((int)lum - (int)prev_l);
      float delta = fmaxf(0.0f, local_delta - global_delta_mean);

      if (delta > 6.0f) {
        mhi_buf[idx] = 255;
      } else {
        mhi_buf[idx] = (mhi_buf[idx] > 35) ? (mhi_buf[idx] - 35) : 0;
      }
      float mhi_weight = (float)mhi_buf[idx] / 255.0f;

      float gy = (float)abs((int)smooth_40x30[idx + 40] - (int)smooth_40x30[idx - 40]);
      float gx = (float)abs((int)smooth_40x30[idx + 1] - (int)smooth_40x30[idx - 1]);

      float energy = 2.5f * delta + 16.0f * mhi_weight + 1.5f * gy + 0.8f * gx;

      bool is_skin = skin_mask_40x30[idx];
      if (is_skin) {
        energy *= 1.25f;
      } else {
        float tex_val = (float)texture_40x30[idx];
        float texture_factor = 1.0f - (tex_val - 15.0f) / 40.0f;
        if (texture_factor < 0.35f) texture_factor = 0.35f;
        if (texture_factor > 1.0f)  texture_factor = 1.0f;
        energy *= (0.04f * texture_factor);
      }

      if (energy < 8.0f) continue;

      double weight = (double)energy;
      double dx = (double)x;
      double dy = (double)y;

      M00 += weight;
      M10 += dx * weight;
      M01 += dy * weight;
      M20 += dx * dx * weight;
      M02 += dy * dy * weight;
      M11 += dx * dy * weight;
    }
  }

  memcpy(prev_lum_buf, smooth_40x30, 1200);

  float dynamic_threshold = 95.0f - ((float)skin_pixel_count * 0.65f);
  if (dynamic_threshold < 45.0f) dynamic_threshold = 45.0f;

  uint32_t now_us = micros();
  uint32_t now_ms = millis();

  float dt_sec = (k_state.last_update_us > 0) ? ((float)(now_us - k_state.last_update_us) * 1e-6f) : 0.033f;
  float dt = fmaxf(0.01f, fminf(0.20f, dt_sec));
  k_state.last_update_us = now_us;

  float pred_x  = k_state.x + k_state.vx * dt + 0.5f * k_state.ax * dt * dt;
  float pred_y  = k_state.y + k_state.vy * dt + 0.5f * k_state.ay * dt * dt;
  float pred_vx = k_state.vx + k_state.ax * dt;
  float pred_vy = k_state.vy + k_state.ay * dt;
  float pred_ax = k_state.ax * 0.90f;
  float pred_ay = k_state.ay * 0.90f;

  pred_x = fmaxf(0.0f, fminf(640.0f, pred_x));
  pred_y = fmaxf(0.0f, fminf(480.0f, pred_y));

  float speed = sqrtf(fmaxf(0.0f, pred_vx * pred_vx + pred_vy * pred_vy));
  float search_radius = fminf(250.0f, 60.0f + 0.25f * speed);

  bool candidate_found = false;
  float cand_cx = pred_x, cand_cy = pred_y, cand_bw = k_state.w, cand_bh = k_state.h;

  if (M00 >= dynamic_threshold) {
    double inv_M00 = 1.0 / M00;
    double mean_x = M10 * inv_M00;
    double mean_y = M01 * inv_M00;

    double mu20 = (M20 * inv_M00) - (mean_x * mean_x);
    double mu02 = (M02 * inv_M00) - (mean_y * mean_y);

    float sigma_x = sqrtf(fmaxf(0.0f, (float)mu20));
    float sigma_y = sqrtf(fmaxf(0.0f, (float)mu02));

    float raw_cx = (float)mean_x * 16.0f;
    float raw_cy = (float)mean_y * 16.0f;

    float raw_bw = 2.4f * fmaxf(1.5f, sigma_x) * 16.0f;
    float raw_bh = 2.6f * fmaxf(2.0f, sigma_y) * 16.0f;

    float coupled_bh = fmaxf(raw_bh, 1.15f * raw_bw);
    coupled_bh = fminf(coupled_bh, 1.55f * raw_bw);

    cand_bw = fmaxf(60.0f, fminf(400.0f, raw_bw));
    cand_bh = fmaxf(80.0f, fminf(480.0f, coupled_bh));

    if (!k_state.active) {
      cand_cx = raw_cx;
      cand_cy = raw_cy;
      candidate_found = true;
    } else {
      float dist_sq = (raw_cx - pred_x) * (raw_cx - pred_x) + (raw_cy - pred_y) * (raw_cy - pred_y);
      if (dist_sq <= (search_radius * search_radius)) {
        cand_cx = raw_cx;
        cand_cy = raw_cy;
        candidate_found = true;
      }
    }
  }

  lock_confidence = lock_confidence * 0.85f + (candidate_found ? 1.0f : 0.0f) * 0.15f;

  if (candidate_found) {
    float speed_norm = fminf(1.0f, speed / 450.0f);
    float alpha = fminf(0.85f, 0.55f + 0.30f * speed_norm);
    float beta  = fminf(0.50f, 0.25f + 0.25f * speed_norm);
    float gamma = fminf(0.20f, 0.08f + 0.12f * speed_norm);

    float rx = cand_cx - pred_x;
    float ry = cand_cy - pred_y;

    k_state.x = pred_x + alpha * rx;
    k_state.y = pred_y + alpha * ry;

    k_state.vx = fmaxf(-1200.0f, fminf(1200.0f, pred_vx + (beta / dt) * rx));
    k_state.vy = fmaxf(-1200.0f, fminf(1200.0f, pred_vy + (beta / dt) * ry));

    k_state.ax = fmaxf(-3000.0f, fminf(3000.0f, pred_ax + (2.0f * gamma / (dt * dt)) * rx));
    k_state.ay = fmaxf(-3000.0f, fminf(3000.0f, pred_ay + (2.0f * gamma / (dt * dt)) * ry));

    k_state.w = k_state.w * 0.70f + cand_bw * 0.30f;
    k_state.h = k_state.h * 0.70f + cand_bh * 0.30f;
    k_state.w = fmaxf(60.0f, fminf(400.0f, k_state.w));
    k_state.h = fmaxf(80.0f, fminf(480.0f, k_state.h));

    k_state.active = true;
    last_valid_human_time = now_ms;
  } else {
    k_state.x = pred_x;
    k_state.y = pred_y;
    k_state.vx = pred_vx * 0.85f;
    k_state.vy = pred_vy * 0.85f;
    k_state.ax = pred_ax * 0.50f;
    k_state.ay = pred_ay * 0.50f;

    if (now_ms - last_valid_human_time > 450) {
      k_state.active = false;
      k_state.vx = 0.0f;
      k_state.vy = 0.0f;
      k_state.ax = 0.0f;
      k_state.ay = 0.0f;
    }
  }

  k_state.x = fmaxf(0.0f, fminf(640.0f, k_state.x));
  k_state.y = fmaxf(0.0f, fminf(480.0f, k_state.y));

  debug_m00 = (float)M00;
  debug_skin_px = skin_pixel_count;
  debug_lock_conf = lock_confidence;

  if (k_state.active && lock_confidence > 0.25f) {
    float center_x = frame_w / 2.0f;
    float center_y = frame_h / 2.0f;
    float err_x = ((k_state.x - center_x) / center_x) * 100.0f;
    float err_y = ((k_state.y - center_y) / center_y) * 100.0f;

    portENTER_CRITICAL(&target_mutex);
    current_target.detected = true;
    current_target.x = (int)(k_state.x - k_state.w / 2.0f);
    current_target.y = (int)(k_state.y - k_state.h / 2.0f);
    current_target.w = (int)k_state.w;
    current_target.h = (int)k_state.h;
    current_target.cx = (int)k_state.x;
    current_target.cy = (int)k_state.y;
    current_target.error_x = err_x;
    current_target.error_y = err_y;
    current_target.confidence = lock_confidence;
    current_target.total_energy = (float)M00;
    current_target.vx = k_state.vx;
    current_target.vy = k_state.vy;
    current_target.last_seen_ms = now_ms;
    portEXIT_CRITICAL(&target_mutex);
  } else {
    portENTER_CRITICAL(&target_mutex);
    current_target.detected = false;
    current_target.error_x = 0.0f;
    current_target.error_y = 0.0f;
    current_target.confidence = lock_confidence;
    current_target.total_energy = (float)M00;
    current_target.vx = 0.0f;
    current_target.vy = 0.0f;
    portEXIT_CRITICAL(&target_mutex);
  }
}

// --- Camera & AI Ingestion Task (Core 0) ---
void cameraTask(void *pvParameters) {
  uint32_t last_frame_time = millis();

  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      // Validate JPEG frame integrity before decoding to avoid esp_jpeg_decode error 6
      if (fb->len > 1024 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8) {
        processFrameAI(fb);

        uint32_t now = millis();
        if (now - last_frame_time > 0) {
          fps_ai = 1000.0f / (float)(now - last_frame_time);
        }
        last_frame_time = now;

        bool streaming_now = false;
        portENTER_CRITICAL(&g_stream_mutex);
        streaming_now = (g_stream_clients > 0);
        portEXIT_CRITICAL(&g_stream_mutex);

        if (streaming_now && g_latest_jpeg_buf) {
          if (fb->len <= 64 * 1024) {
            portENTER_CRITICAL(&g_stream_mutex);
            memcpy(g_latest_jpeg_buf, fb->buf, fb->len);
            g_latest_jpeg_len = fb->len;
            portEXIT_CRITICAL(&g_stream_mutex);

            if (g_frame_sem) {
              xSemaphoreGive(g_frame_sem);
            }
          }
        }
      }

      esp_camera_fb_return(fb);
    }
    // Yield execution to reset Task Watchdog Timer (TWDT) on Core 0
    vTaskDelay(pdMS_TO_TICKS(4));
    taskYIELD();
  }
}

// --- HTTP API Handlers ---
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, HTML_PAGE, strlen(HTML_PAGE));
}

static esp_err_t telemetry_handler(httpd_req_t *req) {
  char json[360];
  TrackTarget target;

  portENTER_CRITICAL(&target_mutex);
  target = current_target;
  portEXIT_CRITICAL(&target_mutex);

  snprintf(json, sizeof(json),
    "{\"detected\":%s,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"cx\":%d,\"cy\":%d,\"err_x\":%.1f,\"err_y\":%.1f,\"conf\":%.2f,\"fps_ai\":%.1f,\"fw\":%d,\"fh\":%d,\"m00\":%.1f,\"skin_px\":%d,\"lock_conf\":%.2f,\"vx\":%.1f,\"vy\":%.1f}",
    target.detected ? "true" : "false",
    target.x, target.y, target.w, target.h,
    target.cx, target.cy,
    target.error_x, target.error_y,
    target.confidence,
    fps_ai,
    frame_w, frame_h,
    debug_m00,
    debug_skin_px,
    debug_lock_conf,
    target.vx, target.vy
  );

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json, strlen(json));
}

static esp_err_t stream_handler(httpd_req_t *req) {
  esp_err_t res = ESP_OK;
  char part_buf[64];
  uint32_t last_stream_time = millis();

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  uint8_t* stream_buf = (uint8_t*)ps_malloc(64 * 1024);
  if (!stream_buf) stream_buf = (uint8_t*)malloc(64 * 1024);

  portENTER_CRITICAL(&g_stream_mutex);
  g_stream_clients++;
  portEXIT_CRITICAL(&g_stream_mutex);

  while (true) {
    if (g_frame_sem && xSemaphoreTake(g_frame_sem, pdMS_TO_TICKS(100)) == pdTRUE) {
      size_t len = 0;
      if (stream_buf && g_latest_jpeg_buf) {
        portENTER_CRITICAL(&g_stream_mutex);
        memcpy(stream_buf, g_latest_jpeg_buf, g_latest_jpeg_len);
        len = g_latest_jpeg_len;
        portEXIT_CRITICAL(&g_stream_mutex);
      }

      if (len > 0 && stream_buf) {
        size_t hlen = snprintf(part_buf, 64, STREAM_PART, len);
        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)stream_buf, len);

        if (res != ESP_OK) break;

        uint32_t now = millis();
        if (now - last_stream_time > 0) {
          fps_stream = 1000.0f / (float)(now - last_stream_time);
        }
        last_stream_time = now;
      }
    }
  }

  portENTER_CRITICAL(&g_stream_mutex);
  g_stream_clients--;
  if (g_stream_clients < 0) g_stream_clients = 0;
  portEXIT_CRITICAL(&g_stream_mutex);

  if (stream_buf) {
    free(stream_buf);
  }
  return res;
}

void startWebServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL };
  httpd_uri_t telemetry_uri = { .uri = "/telemetry", .method = HTTP_GET, .handler = telemetry_handler, .user_ctx = NULL };

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &telemetry_uri);
  }

  config.server_port = 81;
  config.ctrl_port   = 32769;
  httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };

  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

// --- Camera Initialization ---
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  
  config.xclk_freq_hz = 16000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_VGA;
  config.jpeg_quality = 8;
  config.fb_count     = 2;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) return false;

  sensor_t* s = esp_camera_sensor_get();
  if (s != NULL) {
    if (s->id.PID == OV3660_PID) {
      s->set_vflip(s, 1);
      s->set_hmirror(s, 1);
      s->set_brightness(s, 0);
      s->set_contrast(s, 0);
      s->set_saturation(s, 0);
      s->set_sharpness(s, 2);
      s->set_denoise(s, 0);
      s->set_whitebal(s, 1);
      s->set_awb_gain(s, 1);
      s->set_exposure_ctrl(s, 1);
      s->set_gain_ctrl(s, 1);
    } else {
      s->set_brightness(s, 0);
      s->set_contrast(s, 0);
      s->set_sharpness(s, 1);
      s->set_vflip(s, 1);
      s->set_hmirror(s, 1);
    }
  }

  for (int i = 0; i < 4; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(40);
  }

  return true;
}

// --- System Entrypoint ---
void setup() {
  Serial.begin(115200);
  delay(1000);

  setCpuFrequencyMhz(240);

  Serial.println("\n[KoRe] Biomechanical Face Tracker Starting...");

  if (!initCamera()) {
    Serial.println("FATAL: Camera initialization failed! Restarting...");
    delay(3000);
    ESP.restart();
  }
  Serial.println("Camera initialized.");

  if (psramFound()) {
    small_rgb_buf     = (uint8_t*)ps_malloc(80 * 60 * 2);
    prev_lum_buf      = (uint8_t*)ps_malloc(40 * 30);
    mhi_buf           = (uint8_t*)ps_malloc(40 * 30);
    g_latest_jpeg_buf = (uint8_t*)ps_malloc(64 * 1024);
    Serial.println("PSRAM buffers allocated.");
  } else {
    small_rgb_buf     = (uint8_t*)malloc(80 * 60 * 2);
    prev_lum_buf      = (uint8_t*)malloc(40 * 30);
    mhi_buf           = (uint8_t*)malloc(40 * 30);
    g_latest_jpeg_buf = (uint8_t*)malloc(64 * 1024);
    Serial.println("SRAM buffers allocated.");
  }

  g_frame_sem = xSemaphoreCreateBinary();

  if (prev_lum_buf) memset(prev_lum_buf, 0, 40 * 30);
  if (mhi_buf) memset(mhi_buf, 0, 40 * 30);

  WiFi.setTxPower(WIFI_POWER_17dBm);
  if (USE_AP_MODE) {
    WiFi.softAP(ap_ssid, ap_password);
    Serial.print("AP Mode: http://");
    Serial.println(WiFi.softAPIP());
  } else {
    WiFi.begin(sta_ssid, sta_password);
    Serial.printf("Connecting to WiFi '%s'", sta_ssid);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
      delay(400);
      Serial.print(".");
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.setSleep(false);
      Serial.println(" Connected!");
      Serial.print("Web UI: http://");
      Serial.println(WiFi.localIP());
    } else {
      WiFi.softAP(ap_ssid, ap_password);
      Serial.println("\nFallback AP Mode!");
      Serial.print("AP IP: http://");
      Serial.println(WiFi.softAPIP());
    }
  }

  startWebServer();
  Serial.println("Web server active.");

  // Launch Camera AI Task on Core 0
  xTaskCreatePinnedToCore(
    cameraTask,
    "Camera_AI_Task",
    8192,
    NULL,
    2,
    NULL,
    0
  );
  Serial.println("Camera AI task started on Core 0.");

  // Launch OLED Display Task on Core 1
  xTaskCreatePinnedToCore(
    oledTask,
    "OLED_Task",
    8192,
    NULL,
    1,
    NULL,
    1
  );
  Serial.println("OLED rendering task started on Core 1.");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(5000));
}
