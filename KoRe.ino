/**
 * @file KoRe.ino
 * @brief Dual-Core Biomechanical Vision & Kinematics Firmware
 * @details Hardware Platform: Seeed Studio XIAO ESP32-S3 Sense
 *          Display Interface: 0.96" SSD1306 OLED via LovyanGFX I2C Bus (1.0MHz Fast-mode Plus)
 *          Architecture: FreeRTOS Dual-Core Asynchronous Task Split
 *                        Core 0: Computer Vision Pipeline, Spatial Clustering, HTTP Web Server, and DFS Clock Scaling
 *                        Core 1: 60 FPS Biomechanical Gaze Kinematics & 1-Bit LGFX Sprite Engine
 */

#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "img_converters.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <math.h>
#include <esp_random.h>
#include <LovyanGFX.hpp>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* --- Hardware & Debug Configuration --- */
#define ENABLE_TOUCH_PIN false 
#define TOUCH_PIN 2            
#define ENABLE_SERIAL_DEBUG false   // Keep disabled in production; UART TX burns ~2-3mA unnecessary power

/* --- Battery Power Management (3.7V 500mAh LiPo Setup) --- */
#define WIFI_TX_POWER WIFI_POWER_19_5dBm // Max TX power for reliable Wi-Fi transmission through physical obstacles
#define CPU_FREQ_ACTIVE 240               // MHz: Full clock frequency for real-time vision processing and MJPEG streaming
#define CPU_FREQ_SLEEP  80                // MHz: Clock floor required to keep the ESP32 Wi-Fi stack stable without dropping sockets
#define OLED_BRIGHTNESS        128        // Default OLED panel brightness (0-255)
#define CAMERA_XCLK_FREQ 10000000         // 10 MHz XCLK (dropping from 16MHz cuts thermal dissipation while maintaining VGA JPEG bandwidth)
#define FPS_ACTIVE_US 16666               // 60 FPS frame budget (16.66ms period)
#define FPS_SLEEP_US  33333               // 30 FPS frame budget in sleep state (cuts I2C bus transactions by 50%)
#define GAZE_GAIN_X   1.75f               // Horizontal gaze gain multiplier (compensates camera lens FOV vs display aspect ratio)
#define GAZE_GAIN_Y   1.45f               // Vertical gaze gain multiplier

/**
 * @enum Expression
 * @brief Discrete 2D animated facial states rendered on the OLED display.
 */
enum Expression {
  EXPR_IDLE,
  EXPR_JOY,
  EXPR_ANGRY,
  EXPR_SMIRK,
  EXPR_SHOCK,
  EXPR_OVERLOAD,
  EXPR_SEDIH
};

/**
 * @class LGFX
 * @brief Low-level driver configuration for the SSD1306 OLED display using LovyanGFX.
 * @note Overclocks I2C write frequency to 1.0MHz (Fast-mode Plus) to achieve 60 FPS sprite pushes without tearing.
 */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_SSD1306 _panel_instance;
  lgfx::Bus_I2C _bus_instance;

public:
  LGFX() {
    {
      auto cfg = _bus_instance.config();
      cfg.i2c_port = 0;
      cfg.freq_write = 1000000; 
      cfg.freq_read = 400000;
      cfg.pin_scl = 5; 
      cfg.pin_sda = 6; 
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
LGFX_Sprite canvas(&lcd);

/* --- NVS Network Configuration & Fallback Defaults --- */
const char* ap_ssid_default     = "KoRe-Tracker";
const char* ap_password_default = "12345678";

const char* sta_ssid_default     = "Kasminingsih";
const char* sta_password_default = "hidet4mp4n";

static char sta_ssid[64] = {0};
static char sta_password[64] = {0};
static char ap_ssid[64] = {0};
static char ap_password[64] = {0};

static bool g_is_ap_mode = false;
static DNSServer dnsServer;

/* --- Camera Sensor Pin Mapping (XIAO ESP32-S3 Sense Pinout) --- */
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

/**
 * @struct TrackTarget
 * @brief Shared target state object passed across FreeRTOS cores.
 * @note Access must be protected with `target_mutex` because Core 0 (CV) writes while Core 1 (UI) reads at 60Hz.
 */
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
  float proximity;     // Normalized target proximity Z in range [0.0 (far), 1.0 (near)]
  uint32_t last_seen_ms;
};

/**
 * @struct ObjectCandidate
 * @brief Candidate descriptor for multi-object spatial sector tracking.
 */
struct ObjectCandidate {
  bool active;
  int cx;
  int cy;
  int w;
  int h;
  float priority_score;
  float error_x;
  float error_y;
  int skin_px;
  float motion_energy;
  float proximity;
};

#define MAX_OBJECT_CANDIDATES 3
static ObjectCandidate g_object_candidates[MAX_OBJECT_CANDIDATES] = {0};
static int g_num_candidates = 0;
static int g_inspected_candidate_idx = 0;
static uint32_t g_last_inspection_time_ms = 0;
static uint32_t g_inspection_hold_time_ms = 2800;

static TrackTarget current_target = {false, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0};
static portMUX_TYPE target_mutex = portMUX_INITIALIZER_UNLOCKED; // Protects target state across Core 0 (CV) and Core 1 (UI)

static volatile float fps_ai = 0.0f;
static volatile float fps_stream = 0.0f;
static const int frame_w = 640;
static const int frame_h = 480;

/* --- Internal Memory Allocation Pointers ---
 * Note: Must be allocated in internal SRAM (MALLOC_CAP_INTERNAL) rather than PSRAM.
 * The vision pipeline reads/writes these pixel arrays thousands of times per frame;
 * PSRAM bus latency is far too high for real-time YCbCr image scaling and MHI calculation.
 */
static uint8_t* small_rgb_buf = NULL;
static uint8_t* prev_lum_buf  = NULL;
static uint8_t* mhi_buf       = NULL;

/* --- Inter-Core Asynchronous Synchronization & Streaming --- */
static volatile int g_stream_clients = 0;
static volatile uint32_t g_last_web_activity_ms = 0;
static uint8_t* g_latest_jpeg_buf = NULL; // Allocated in PSRAM (64KB buffer for HTTP stream chunks)
static size_t g_latest_jpeg_len = 0;
static portMUX_TYPE g_stream_mutex = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t g_frame_sem = NULL;
static bool g_camera_init_ok = false;

static inline bool isWebOrStreamActive(uint32_t now) {
  bool has_clients = false;
  portENTER_CRITICAL(&g_stream_mutex);
  has_clients = (g_stream_clients > 0);
  portEXIT_CRITICAL(&g_stream_mutex);
  if (has_clients) return true;
  if (g_last_web_activity_ms > 0 && (now - g_last_web_activity_ms < 6000)) return true;
  return false;
}

/* --- Embedded HTTP Service Handles --- */
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

/* --- Gaze Kinematics & Animation State --- */
Expression currentExpr = EXPR_IDLE;
static bool g_is_transitioning = false;

unsigned long lastBlink = 0;
float animFrame = 0.0f;
unsigned long lastAnimUpdate = 0;

bool lastTouchState = false;
unsigned long lastTouchCheck = 0;

float currentOffsetX = 0.0f;
float currentOffsetY = 0.0f;
float currentVergence = 0.0f;
float currentEyeScale = 1.0f;
float startOffsetX = 0.0f;
float startOffsetY = 0.0f;
float targetOffsetX = 0.0f;
float targetOffsetY = 0.0f;
unsigned long gazeStartTime = 0;
unsigned long gazeDuration = 120;
unsigned long nextGazeTime = 0;
bool inSaccade = false;

/* --- Biomechanical Dynamic Model State Variables --- */
static float eye_vx = 0.0f;           
static float eye_vy = 0.0f;           
static float smoothedTargetX = 0.0f;  
static float smoothedTargetY = 0.0f;  
static bool trackInSaccade = false;   
static uint32_t trackSaccadeStart = 0;
static uint32_t trackSaccadeDuration = 60;
static float trackSaccadeStartX = 0.0f;
static float trackSaccadeStartY = 0.0f;

/**
 * @enum BlinkState
 * @brief Non-blocking state machine for eyelid blinking animations.
 */
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

/* --- Intermittent Reconnaissance Duty Cycle FSM --- */
enum ReconState {
  STATE_ACTIVE,       // Full 30 FPS vision tracking @ 240 MHz CPU
  STATE_SLEEP_RECON,  // Camera paused in software standby, CPU scaled down to 80 MHz, random saccades
  STATE_SAMPLING      // High-speed verification pulse @ 240 MHz CPU
};

static volatile ReconState g_recon_state = STATE_ACTIVE;
static uint32_t g_state_timer = 0;
static float g_rand_target_x = 0.0f;
static float g_rand_target_y = 0.0f;
static uint32_t g_last_saccade_shift = 0;
static uint32_t g_nextGazeTime = 4500;

/* --- Progressive Sleep Escalation --- */
static uint8_t g_sleep_miss_count = 0;       // Count of consecutive wake-up windows without human detection

/**
 * @brief Toggles camera sensor software standby mode via SCCB register writes.
 * @param enable True to power down sensor core; false to wake up.
 * @note The XIAO ESP32-S3 Sense board leaves PWDN unconnected (-1), making SCCB register writes
 *       the only way to turn off the sensor array during sleep states.
 */
void setCameraSleep(bool enable) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  if (s->id.PID == OV2640_PID) {
    s->set_reg(s, 0xFF, 0xFF, 0x01);                  // Switch to register bank 1
    s->set_reg(s, 0x09, 0xFF, enable ? 0x10 : 0x00);  // COM2: Toggle bit 4 software standby
  } else if (s->id.PID == OV3660_PID) {
    s->set_reg(s, 0x3008, 0x40, enable ? 0x40 : 0x00); // System control bit 6 standby mode
  }
}

/* --- Biomechanical Oculomotor Model (Sleep/Idle Saccades) ---
 *
 * Literature References:
 *   - Bahill et al. (1975): Main Sequence law relating amplitude to duration.
 *   - Flash & Hogan (1985): Minimum-Jerk velocity profile optimization.
 *   - Carpenter (1988): Oculomotor kinetics and neural pulse-step integration.
 *   - Robinson (1964): Post-saccadic glissadic overshoot dynamics.
 *
 * Display Spatial Calibration:
 *   OLED physical coordinate range (±17.5px) maps to ±30° human oculomotor range.
 *   Conversion factor: 1.0px ≈ 1.714° (30.0 / 17.5).
 *
 * Main Sequence Law (degree space):
 *   A_deg = A_px × 1.714
 *   Duration(ms) = 110.0 + 4.2 × A_deg + 12.0 × sqrt(A_deg) [clamped to 110-220ms]
 *
 * Trajectory Profile: 5th-order Minimum-Jerk polynomial spline:
 *   s(p) = 10p^3 - 15p^4 + 6p^5,  p ∈ [0, 1]
 *
 * Amplitude Threshold: Enforces minimum displacement A_px ≥ 4.0px (~6.8°) to prevent twitching.
 * Fixation Micro-Kinetics: Mean-reverting Brownian random walk + sub-pixel foveal tremor.
 */

// Pixel-to-degree conversion: ±17.5px OLED range ≈ ±30° human gaze
static const float PX_TO_DEG = 30.0f / 17.5f;  // ≈ 1.714 °/px

// Previous state tracker for ACTIVE ↔ SLEEP transitions
static ReconState g_prev_recon_state = STATE_ACTIVE;

// --- Telemetry Dashboard & WiFi Configuration Web UI ---
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>KoRe Setup & Telemetry</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { background-color: #f4f4f5; color: #18181b; font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; min-height: 100vh; display: flex; justify-content: center; align-items: center; padding: 16px; }
    .card { background: #ffffff; border: 1px solid #e4e4e7; border-radius: 4px; max-width: 460px; width: 100%; padding: 24px; }
    h1 { font-size: 18px; font-weight: 600; margin-bottom: 4px; color: #18181b; }
    p.subtitle { font-size: 13px; color: #71717a; margin-bottom: 16px; }
    .mode-badge { display: inline-block; width: 100%; text-align: center; padding: 6px 12px; font-size: 12px; font-weight: 600; border-radius: 4px; background: #e0f2fe; color: #0369a1; margin-bottom: 16px; border: 1px solid #bae6fd; }
    .mode-badge.ap { background: #fef3c7; color: #b45309; border-color: #fde68a; }
    .stream-box { position: relative; width: 100%; background: #000000; border-radius: 4px; overflow: hidden; margin-bottom: 16px; border: 1px solid #d4d4d8; text-align: center; display: flex; justify-content: center; align-items: center; }
    .stream-box img { width: 100%; height: auto; display: block; object-fit: contain; }
    #hud-canvas { position: absolute; top: 0; left: 0; width: 100%; height: 100%; pointer-events: none; }
    .section-title { font-size: 14px; font-weight: 600; color: #27272a; margin-top: 16px; margin-bottom: 12px; border-bottom: 1px solid #f4f4f5; padding-bottom: 4px; }
    .form-group { margin-bottom: 12px; }
    label { display: block; font-size: 12px; font-weight: 500; color: #52525b; margin-bottom: 4px; }
    input[type="text"], input[type="password"] { width: 100%; padding: 8px 10px; font-size: 14px; border: 1px solid #d4d4d8; border-radius: 4px; background: #ffffff; color: #18181b; outline: none; }
    input[type="text"]:focus, input[type="password"]:focus { border-color: #18181b; }
    button { width: 100%; padding: 10px; margin-top: 16px; background-color: #18181b; color: #ffffff; font-size: 14px; font-weight: 500; border: none; border-radius: 4px; cursor: pointer; }
    button:hover { background-color: #3f3f46; }
    button:disabled { background-color: #a1a1aa; cursor: not-allowed; }
    .btn-switch-mode { background-color: #2563eb; color: #ffffff; margin-top: 0; margin-bottom: 16px; }
    .btn-switch-mode:hover { background-color: #1d4ed8; }
    #status { display: none; margin-top: 14px; padding: 10px; font-size: 13px; border-radius: 4px; border: 1px solid #d4d4d8; background: #fafafa; color: #18181b; text-align: center; }
  </style>
</head>
<body>
  <div class="card">
    <h1>Pengaturan WiFi & Telemetri KoRe</h1>
    <p class="subtitle">Konfigurasi Access Point (AP), WiFi Client (STA) & Multi-Target Tracking</p>
    
    <div id="mode-badge" class="mode-badge">Memuat status mode...</div>

    <div class="stream-box">
      <img id="stream-img" src="" alt="Camera Stream" crossorigin="anonymous">
      <canvas id="hud-canvas"></canvas>
    </div>

    <button type="button" id="btn-switch-mode" class="btn-switch-mode" style="display:none;"></button>

    <form id="wifi-form">
      <div class="section-title">WiFi Client (STA)</div>
      <div class="form-group">
        <label for="sta_ssid">SSID WiFi</label>
        <input type="text" id="sta_ssid" name="sta_ssid" placeholder="Nama WiFi" required>
      </div>
      <div class="form-group">
        <label for="sta_pass">Password WiFi</label>
        <input type="password" id="sta_pass" name="sta_pass" placeholder="Password WiFi">
      </div>

      <div class="section-title">Access Point (AP)</div>
      <div class="form-group">
        <label for="ap_ssid">SSID Access Point</label>
        <input type="text" id="ap_ssid" name="ap_ssid" placeholder="SSID ESP AP" required>
      </div>
      <div class="form-group">
        <label for="ap_pass">Password Access Point</label>
        <input type="password" id="ap_pass" name="ap_pass" placeholder="Password ESP AP (kosongkan atau min 8 karakter)">
      </div>

      <button type="submit" id="btn-save">Simpan & Hubungkan</button>
    </form>
    <div id="status"></div>
  </div>

  <script>
    const host = window.location.hostname;
    const img = document.getElementById('stream-img');
    const canvas = document.getElementById('hud-canvas');
    const ctx = canvas.getContext('2d');
    img.src = 'http://' + host + ':81/stream';
    img.onerror = function() {
      setTimeout(function() {
        img.src = 'http://' + host + ':81/stream?t=' + Date.now();
      }, 1000);
    };

    function resizeCanvas() {
      if (img.clientWidth > 0 && img.clientHeight > 0) {
        canvas.width = img.clientWidth;
        canvas.height = img.clientHeight;
      }
    }
    window.addEventListener('resize', resizeCanvas);
    img.onload = resizeCanvas;

    let renderBoxes = [];
    let telemetryTimer = null;

    async function updateTelemetry() {
      if (document.visibilityState === 'hidden') {
        telemetryTimer = setTimeout(updateTelemetry, 1000);
        return;
      }

      try {
        const res = await fetch('http://' + host + '/telemetry');
        const data = await res.json();
        ctx.clearRect(0, 0, canvas.width, canvas.height);

        if (data.detected && data.fw > 0 && data.fh > 0) {
          resizeCanvas();
          const scaleX = canvas.width / data.fw;
          const scaleY = canvas.height / data.fh;

          const numCands = data.num_cands || 1;
          const colors = ['#00ff9d', '#00e5ff', '#ffea00'];
          const labels = ['P1 PRIMARY TRACK', 'P2 SCAN CANDIDATE', 'P3 SCAN CANDIDATE'];

          const cands = [
            { cx: data.c0_cx || data.cx, cy: data.c0_cy || data.cy, p: data.c0_p || 100 },
            { cx: data.c1_cx || 0, cy: data.c1_cy || 0, p: data.c1_p || 0 },
            { cx: data.c2_cx || 0, cy: data.c2_cy || 0, p: data.c2_p || 0 }
          ];

          for (let i = 0; i < numCands; i++) {
            const cand = cands[i];
            if (cand.cx <= 0) continue;

            const bw = data.w || 160;
            const bh = data.h || 200;

            const targetBx = (cand.cx - bw / 2) * scaleX;
            const targetBy = (cand.cy - bh / 2) * scaleY;
            const targetBw = bw * scaleX;
            const targetBh = bh * scaleY;
            const targetCx = cand.cx * scaleX;
            const targetCy = cand.cy * scaleY;

            if (!renderBoxes[i]) {
              renderBoxes[i] = { bx: targetBx, by: targetBy, bw: targetBw, bh: targetBh, cx: targetCx, cy: targetCy };
            } else {
              renderBoxes[i].bx = renderBoxes[i].bx * 0.25 + targetBx * 0.75;
              renderBoxes[i].by = renderBoxes[i].by * 0.25 + targetBy * 0.75;
              renderBoxes[i].bw = renderBoxes[i].bw * 0.25 + targetBw * 0.75;
              renderBoxes[i].bh = renderBoxes[i].bh * 0.25 + targetBh * 0.75;
              renderBoxes[i].cx = renderBoxes[i].cx * 0.25 + targetCx * 0.75;
              renderBoxes[i].cy = renderBoxes[i].cy * 0.25 + targetCy * 0.75;
            }

            const rBox = renderBoxes[i];
            const bx = rBox.bx, by = rBox.by, bWidth = rBox.bw, bHeight = rBox.bh, cX = rBox.cx, cY = rBox.cy;
            const color = colors[i % colors.length];
            const isInspected = (i === data.insp_idx);

            // Bounding Box Line
            ctx.strokeStyle = color;
            ctx.lineWidth = isInspected ? 2.5 : 1.5;
            if (!isInspected) ctx.setLineDash([6, 4]); else ctx.setLineDash([]);
            ctx.strokeRect(bx, by, bWidth, bHeight);
            ctx.setLineDash([]);

            // Corner Brackets
            const len = 14;
            ctx.lineWidth = 3.5;
            ctx.beginPath(); ctx.moveTo(bx, by + len); ctx.lineTo(bx, by); ctx.lineTo(bx + len, by); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(bx + bWidth - len, by); ctx.lineTo(bx + bWidth, by); ctx.lineTo(bx + bWidth, by + len); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(bx, by + bHeight - len); ctx.lineTo(bx, by + bHeight); ctx.lineTo(bx + len, by + bHeight); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(bx + bWidth - len, by + bHeight); ctx.lineTo(bx + bWidth, by + bHeight); ctx.lineTo(bx + bWidth, by + len); ctx.stroke();

            // Label & Priority Badge
            ctx.fillStyle = color;
            ctx.font = 'bold 12px monospace';
            const statusText = isInspected ? ' [SCANNING...]' : '';
            const proxText = (data.prox !== undefined) ? ` Z:${(data.prox * 100).toFixed(0)}%` : '';
            ctx.fillText(labels[i] + statusText + proxText, bx + 4, by - 6);

            // Target Crosshair
            if (isInspected) {
              ctx.strokeStyle = '#00d8ff';
              ctx.lineWidth = 1.5;
              ctx.beginPath();
              ctx.moveTo(cX - 10, cY); ctx.lineTo(cX + 10, cY);
              ctx.moveTo(cX, cY - 10); ctx.lineTo(cX, cY + 10);
              ctx.stroke();
              ctx.beginPath(); ctx.arc(cX, cY, 4, 0, 2 * Math.PI); ctx.stroke();
            }
          }
        } else {
          renderBoxes = [];
        }
      } catch (e) {}
      telemetryTimer = setTimeout(updateTelemetry, 100);
    }

    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'visible') {
        if (telemetryTimer) clearTimeout(telemetryTimer);
        updateTelemetry();
      }
    });

    updateTelemetry();

    // WiFi Setup & Mode Switcher Logic
    fetch('/get_wifi')
      .then(res => res.json())
      .then(data => {
        if (data.sta_ssid) document.getElementById('sta_ssid').value = data.sta_ssid;
        if (data.sta_pass) document.getElementById('sta_pass').value = data.sta_pass;
        if (data.ap_ssid) document.getElementById('ap_ssid').value = data.ap_ssid;
        if (data.ap_pass) document.getElementById('ap_pass').value = data.ap_pass;

        const badge = document.getElementById('mode-badge');
        const btnSwitch = document.getElementById('btn-switch-mode');
        
        if (data.is_ap) {
          badge.innerText = 'Mode Saat Ini: Access Point (AP)';
          badge.classList.add('ap');
          btnSwitch.innerText = 'Masuk ke STA Mode';
          btnSwitch.dataset.targetMode = 'STA';
        } else {
          badge.innerText = 'Mode Saat Ini: WiFi Client (STA)';
          badge.classList.remove('ap');
          btnSwitch.innerText = 'Masuk ke AP Mode';
          btnSwitch.dataset.targetMode = 'AP';
        }
        btnSwitch.style.display = 'block';
      })
      .catch(() => {});

    document.getElementById('btn-switch-mode').addEventListener('click', function() {
      const targetMode = this.dataset.targetMode;
      const btn = this;
      const status = document.getElementById('status');
      
      btn.disabled = true;
      document.getElementById('btn-save').disabled = true;
      status.style.display = 'block';

      const apUrl = 'http://192.168.4.1';

      if (targetMode === 'AP') {
        btn.innerText = 'Beralih ke AP Mode...';
        status.innerHTML = '<b>Beralih ke AP Mode...</b><br>ESP sedang reboot.<br>Hubungkan Wi-Fi laptop/HP Anda ke AP <b>KoRe-Tracker</b> lalu buka: <a href="' + apUrl + '" style="color:#2563eb;font-weight:bold;text-decoration:underline;">' + apUrl + '</a><br><small>Mengalihkan otomatis dalam 4 detik...</small>';

        fetch('/switch_mode', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ mode: targetMode })
        }).finally(() => {
          setTimeout(() => { window.location.href = apUrl; }, 4000);
        });
      } else {
        const staSsid = document.getElementById('sta_ssid').value || 'WiFi Router';
        const staUrl = 'http://kore.local';
        btn.innerText = 'Beralih ke STA Mode...';
        status.innerHTML = '<b>Beralih ke STA Mode...</b><br>ESP32 sedang reboot dan terhubung ke Router <b>' + staSsid + '</b>.<br>Buka URL STA: <a href="' + staUrl + '" style="color:#2563eb;font-weight:bold;text-decoration:underline;">' + staUrl + '</a><br><small>Mengalihkan otomatis ke ' + staUrl + ' dalam 6 detik...</small>';

        fetch('/switch_mode', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ mode: targetMode })
        }).finally(() => {
          setTimeout(() => { window.location.href = staUrl; }, 6000);
        });
      }
    });

    document.getElementById('wifi-form').addEventListener('submit', function(e) {
      e.preventDefault();
      const btn = document.getElementById('btn-save');
      const status = document.getElementById('status');

      const apPass = document.getElementById('ap_pass').value;
      if (apPass.length > 0 && apPass.length < 8) {
        status.style.display = 'block';
        status.style.color = '#dc2626';
        status.innerText = 'Error: Password AP harus dikosongkan (Open AP) atau minimal 8 karakter!';
        return;
      }

      status.style.color = '#18181b';
      btn.disabled = true;
      document.getElementById('btn-switch-mode').disabled = true;
      btn.innerText = 'Menyimpan...';
      status.style.display = 'block';

      const staSsid = document.getElementById('sta_ssid').value || 'WiFi Router';
      const staUrl = 'http://kore.local';
      status.innerHTML = '<b>Pengaturan Disimpan!</b><br>ESP32 sedang reboot dan terhubung ke Wi-Fi Router <b>' + staSsid + '</b>.<br>Buka URL STA: <a href="' + staUrl + '" style="color:#2563eb;font-weight:bold;text-decoration:underline;">' + staUrl + '</a><br><small>Mengalihkan otomatis ke ' + staUrl + ' dalam 6 detik...</small>';

      const body = {
        sta_ssid: document.getElementById('sta_ssid').value,
        sta_pass: document.getElementById('sta_pass').value,
        ap_ssid: document.getElementById('ap_ssid').value,
        ap_pass: document.getElementById('ap_pass').value
      };

      fetch('/save_wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
      }).finally(() => {
        setTimeout(() => { window.location.href = staUrl; }, 6000);
      });
    });
  </script>
</body>
</html>
)rawliteral";

/* --- Kinematic Interpolation & Mathematical Utilities --- */

/**
 * @brief Evaluates a cubic ease-in-out curve for smooth interpolation.
 * @param t Normalized progress parameter in range [0.0, 1.0].
 * @return Interpolated progress scalar.
 */
float easeInOutCubic(float t) {
  return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

/**
 * @brief Quadratic ease-in curve for the eyelid closing phase.
 */
float blinkCloseEase(float t) {
  return 1.0f - (t * t);
}

/**
 * @brief Sinusoidal ease-out curve for the eyelid opening phase.
 */
float blinkOpenEase(float t) {
  return sinf(t * 1.5707963f);
}

/**
 * @brief Standard scalar linear interpolation (lerp).
 */
float customLerp(float a, float b, float t) {
  return a + t * (b - a);
}

void drawSensorOverlay() {
  /* Sensor telemetry overlay hook */
}

/**
 * @brief Master 60 FPS biomechanical 2D gaze pursuit and fixation controller.
 * @details Handles target tracking via critically damped mass-spring-damper kinetics (zeta=1.0, omega_n=38 rad/s),
 *          spontaneous foveal fixation saccades via 5th-order minimum-jerk splines, and Brownian fixation micro-drift.
 */
static float trackSaccadeTargetX = 0.0f;
static float trackSaccadeTargetY = 0.0f;
static bool s_prevTargetDetected = false;
static float s_deadbandTargetX = 0.0f;
static float s_deadbandTargetY = 0.0f;
static bool s_hasTargetLock = false;

void updateGazeSystem() {
  TrackTarget target;
  portENTER_CRITICAL(&target_mutex);
  target = current_target;
  portEXIT_CRITICAL(&target_mutex);

  unsigned long now = millis();
  static uint32_t lastGazeTimeUs = 0;
  uint32_t nowUs = micros();
  float dt = (lastGazeTimeUs > 0) ? (float)(nowUs - lastGazeTimeUs) * 0.000001f : 0.016666f;
  dt = constrain(dt, 0.005f, 0.040f); // Clamp delta time to avoid integrator explosions during FreeRTOS preemption
  lastGazeTimeUs = nowUs;

  /* === GAZE FREEZE DURING MORPH TRANSITIONS ===
   * Freeze eye movement during expression morphs to prevent the spring-damper
   * system from jerking eyes sideways while facial geometry is changing.
   * Target low-pass filters remain active so pursuit resumes smoothly post-morph.
   */
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
        const float DEADBAND_RADIUS = 1.35f;
        if (deltaDist > DEADBAND_RADIUS) {
          float excess = (deltaDist - DEADBAND_RADIUS) / deltaDist;
          s_deadbandTargetX += deltaX * excess * 0.40f;
          s_deadbandTargetY += deltaY * excess * 0.40f;
        }
      }

      // Keep target low-pass filter warm
      float alpha = 1.0f - expf(-20.0f * dt);
      smoothedTargetX += (s_deadbandTargetX - smoothedTargetX) * alpha;
      smoothedTargetY += (s_deadbandTargetY - smoothedTargetY) * alpha;
    }
    // Zero velocity vectors to prevent snapping when morph completes
    eye_vx = 0.0f;
    eye_vy = 0.0f;
    trackInSaccade = false;
    inSaccade = false;
    return;
  }

  /* === ACTIVE TARGET PURSUIT === */
  bool targetActive = (g_recon_state == STATE_ACTIVE) && (target.detected || ((now - target.last_seen_ms) < 300 && target.last_seen_ms > 0));
  if (targetActive) {
    float normX = constrain((target.error_x / 100.0f) * GAZE_GAIN_X, -1.0f, 1.0f);
    float normY = constrain((target.error_y / 100.0f) * GAZE_GAIN_Y, -1.0f, 1.0f);

    float rawTargetX = normX * 22.0f;
    float rawTargetY = normY * 14.0f;

    // Foveal deadband & noise gate: suppresses camera sensor noise and sub-pixel pixel jitter (< 1.35 px)
    if (!s_hasTargetLock || !s_prevTargetDetected) {
      s_deadbandTargetX = rawTargetX;
      s_deadbandTargetY = rawTargetY;
      s_hasTargetLock = true;
    } else {
      float deltaX = rawTargetX - s_deadbandTargetX;
      float deltaY = rawTargetY - s_deadbandTargetY;
      float deltaDist = sqrtf(deltaX * deltaX + deltaY * deltaY);
      
      const float DEADBAND_RADIUS = 1.35f; // Ignore micro-jitter within 1.35px threshold
      if (deltaDist > DEADBAND_RADIUS) {
        float excess = (deltaDist - DEADBAND_RADIUS) / deltaDist;
        s_deadbandTargetX += deltaX * excess * 0.40f;
        s_deadbandTargetY += deltaY * excess * 0.40f;
      }
    }

    float effectiveTargetX = s_deadbandTargetX;
    float effectiveTargetY = s_deadbandTargetY;

    // Handle target initial acquisition or large position step jumps
    if (!s_prevTargetDetected) {
      s_prevTargetDetected = true;
      float dist_init = sqrtf((effectiveTargetX - currentOffsetX) * (effectiveTargetX - currentOffsetX) + 
                              (effectiveTargetY - currentOffsetY) * (effectiveTargetY - currentOffsetY));
      if (dist_init > 10.0f) {
        trackInSaccade = true;
        trackSaccadeStart = now;
        trackSaccadeStartX = currentOffsetX;
        trackSaccadeStartY = currentOffsetY;
        trackSaccadeTargetX = effectiveTargetX;
        trackSaccadeTargetY = effectiveTargetY;
        trackSaccadeDuration = (uint32_t)constrain(120.0f + dist_init * 3.5f, 130.0f, 260.0f);
      } else {
        trackInSaccade = false;
      }
      smoothedTargetX = currentOffsetX;
      smoothedTargetY = currentOffsetY;
      eye_vx = 0.0f;
      eye_vy = 0.0f;
      inSaccade = false;
    }

    // Continuous exponential low-pass target filter
    float alpha = 1.0f - expf(-20.0f * dt);
    smoothedTargetX += (effectiveTargetX - smoothedTargetX) * alpha;
    smoothedTargetY += (effectiveTargetY - smoothedTargetY) * alpha;

    currentVergence = 0.0f;
    currentEyeScale = 1.0f;

    // Trigger saccadic glance if target error is large (> 15.0 px)
    float dx_eye = effectiveTargetX - currentOffsetX;
    float dy_eye = effectiveTargetY - currentOffsetY;
    float dist_eye = sqrtf(dx_eye * dx_eye + dy_eye * dy_eye);

    if (dist_eye > 15.0f && !trackInSaccade) {
      trackInSaccade = true;
      trackSaccadeStart = now;
      trackSaccadeDuration = (uint32_t)constrain(100.0f + dist_eye * 3.0f, 120.0f, 220.0f);
      trackSaccadeStartX = currentOffsetX;
      trackSaccadeStartY = currentOffsetY;
      trackSaccadeTargetX = effectiveTargetX;
      trackSaccadeTargetY = effectiveTargetY;
      eye_vx = 0.0f;
      eye_vy = 0.0f;
    }

    if (trackInSaccade) {
      float elapsed = (float)(now - trackSaccadeStart);
      float progress = elapsed / (float)trackSaccadeDuration;

      if (progress >= 1.0f) {
        currentOffsetX = trackSaccadeTargetX;
        currentOffsetY = trackSaccadeTargetY;
        smoothedTargetX = trackSaccadeTargetX;
        smoothedTargetY = trackSaccadeTargetY;
        trackInSaccade = false;
      } else {
        float p = progress;
        float s = p * p * p * (10.0f + p * (-15.0f + 6.0f * p)); // 5th-order minimum-jerk spline
        float distX = trackSaccadeTargetX - trackSaccadeStartX;
        float distY = trackSaccadeTargetY - trackSaccadeStartY;
        currentOffsetX = trackSaccadeStartX + (distX * s);
        currentOffsetY = trackSaccadeStartY + (distY * s);
      }
    } else {
      // Second-order mass-spring-damper differential kinetics (Critical damping: omega_n=38, zeta=1.0)
      float omega_n = 38.0f;
      float zeta = 1.00f;

      float ax = (omega_n * omega_n) * (smoothedTargetX - currentOffsetX) - (2.0f * zeta * omega_n) * eye_vx;
      float ay = (omega_n * omega_n) * (smoothedTargetY - currentOffsetY) - (2.0f * zeta * omega_n) * eye_vy;

      eye_vx += ax * dt;
      eye_vy += ay * dt;

      // Micro-velocity noise damping near equilibrium
      if (fabsf(smoothedTargetX - currentOffsetX) < 0.30f && fabsf(eye_vx) < 1.2f) {
        eye_vx *= 0.60f;
      }
      if (fabsf(smoothedTargetY - currentOffsetY) < 0.30f && fabsf(eye_vy) < 1.2f) {
        eye_vy *= 0.60f;
      }

      currentOffsetX += eye_vx * dt;
      currentOffsetY += eye_vy * dt;
    }

    currentOffsetX = constrain(currentOffsetX, -17.5f, 17.5f);
    currentOffsetY = constrain(currentOffsetY, -12.0f, 11.0f);

    inSaccade = false;
    nextGazeTime = now + 800;
    return;
  }

  /* === AMBIENT SPONTANEOUS FIXATIONS (IDLE & SLEEP WANDERING) === */
  s_prevTargetDetected = false;
  s_hasTargetLock = false;
  trackInSaccade = false;

  // Exponential decay for ocular vergence and scale
  float alpha_decay = 1.0f - expf(-8.0f * dt);
  currentVergence += (0.0f - currentVergence) * alpha_decay;
  currentEyeScale += (1.0f - currentEyeScale) * alpha_decay;

  bool isSleep = (g_recon_state == STATE_SLEEP_RECON);

  if (!inSaccade && !g_is_transitioning && now >= nextGazeTime) {
    startOffsetX = currentOffsetX;
    startOffsetY = currentOffsetY;

    if (isSleep) {
      // Sleep state: Slow, relaxed wandering saccades
      uint32_t pick = esp_random() % 100;
      float distFromCenter = sqrtf(startOffsetX * startOffsetX + startOffsetY * startOffsetY);

      if (pick < 35 && distFromCenter >= 3.0f) {
        targetOffsetX = ((float)(esp_random() % 20) - 10.0f) * 0.1f;
        targetOffsetY = ((float)(esp_random() % 16) - 8.0f) * 0.1f;
      } else if (pick < 75) {
        float signX = (startOffsetX > 1.0f) ? -1.0f : ((startOffsetX < -1.0f) ? 1.0f : ((esp_random() % 2 == 0) ? -1.0f : 1.0f));
        targetOffsetX = signX * (4.5f + (float)(esp_random() % 800) * 0.01f);
        targetOffsetY = ((float)(esp_random() % 600) - 300.0f) * 0.01f;
      } else {
        float signX = (esp_random() % 2 == 0) ? -1.0f : 1.0f;
        float signY = (esp_random() % 2 == 0) ? -1.0f : 1.0f;
        targetOffsetX = signX * (4.0f + (float)(esp_random() % 600) * 0.01f);
        targetOffsetY = signY * (2.5f + (float)(esp_random() % 400) * 0.01f);
      }

      targetOffsetX = constrain(targetOffsetX, -15.0f, 15.0f);
      targetOffsetY = constrain(targetOffsetY, -9.5f, 8.5f);

      float ds = sqrtf((targetOffsetX - startOffsetX) * (targetOffsetX - startOffsetX) + 
                       (targetOffsetY - startOffsetY) * (targetOffsetY - startOffsetY));
      gazeDuration = (uint32_t)constrain(140.0f + ds * 4.0f, 150.0f, 280.0f);
      nextGazeTime = now + gazeDuration + (esp_random() % 2000 + 3000);
      gazeStartTime = now;
      inSaccade = true;
    } else {
      // Active idle state: Spontaneous human-like gaze fixations
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

      if (currentExpr == EXPR_SEDIH) {
        targetOffsetY += 4.0f;
      }
      
      targetOffsetX = constrain(targetOffsetX, -16.5f, 16.5f);
      targetOffsetY = constrain(targetOffsetY, -10.0f, 9.0f);

      float ds = sqrtf((targetOffsetX - startOffsetX) * (targetOffsetX - startOffsetX) + 
                       (targetOffsetY - startOffsetY) * (targetOffsetY - startOffsetY));
      gazeDuration = (uint32_t)constrain(120.0f + ds * 3.5f, 130.0f, 240.0f);
      nextGazeTime = now + gazeDuration + (esp_random() % 1800 + 2200);
      gazeStartTime = now;
      inSaccade = true;
    }
  }

  /* --- Saccadic Trajectory Evaluation --- */
  if (inSaccade) {
    float elapsed = (float)(now - gazeStartTime);
    float progress = elapsed / (float)gazeDuration;

    if (progress >= 1.0f) {
      currentOffsetX = targetOffsetX;
      currentOffsetY = targetOffsetY;
      inSaccade = false;
    } else {
      float p = progress;
      float s = p * p * p * (10.0f + p * (-15.0f + 6.0f * p)); // Minimum-jerk interpolation
      float distX = targetOffsetX - startOffsetX;
      float distY = targetOffsetY - startOffsetY;
      currentOffsetX = startOffsetX + (distX * s);
      currentOffsetY = startOffsetY + (distY * s);
    }
  } else {
    // Fixation micro-kinetics: Mean-reverting Brownian random walk
    float u1 = ((float)(esp_random() % 1000) - 500.0f) * 0.001f;
    float u2 = ((float)(esp_random() % 1000) - 500.0f) * 0.001f;
    float drift_sigma = 0.03f * sqrtf(dt);
    eye_vx += u1 * drift_sigma;
    eye_vy += u2 * drift_sigma;
    eye_vx *= 0.88f;
    eye_vy *= 0.88f;

    currentOffsetX += eye_vx;
    currentOffsetY += eye_vy;
  }

  currentOffsetX = constrain(currentOffsetX, -17.5f, 17.5f);
  currentOffsetY = constrain(currentOffsetY, -12.0f, 11.0f);
}

/* --- 1-Bit LGFX Sprite Display Primitives --- */

// Anti-jitter coordinate hysteresis: Prevents 1px display quantization flickering when resting stationary
static inline int getFilteredOx(float rawOffsetX) {
  static float s_stable_ox = 0.0f;
  if (fabsf(rawOffsetX - s_stable_ox) >= 0.55f) {
    s_stable_ox = roundf(rawOffsetX);
  }
  return (int)s_stable_ox;
}

static inline int getFilteredOy(float rawOffsetY) {
  static float s_stable_oy = 0.0f;
  if (fabsf(rawOffsetY - s_stable_oy) >= 0.55f) {
    s_stable_oy = roundf(rawOffsetY);
  }
  return (int)s_stable_oy;
}

void drawEyes(float eyeHeightFactor, int ox, int oy, uint16_t color, float vergence = 0.0f, float scale = 1.0f) {
  int lx = 32 + ox;
  int rx = 96 + ox;
  int ly = 28 + oy;
  int ry = 28 + oy;

  int maxEyeWidth = 28;
  int maxEyeHeight = 38;
  int eyeHeight = (int)roundf((float)maxEyeHeight * eyeHeightFactor);

  if (eyeHeight <= 3) {
    canvas.fillRoundRect(lx - maxEyeWidth / 2, ly - 1, maxEyeWidth, 3, 1, color);
    canvas.fillRoundRect(rx - maxEyeWidth / 2, ry - 1, maxEyeWidth, 3, 1, color);
  } else {
    int radius = (eyeHeight < 24) ? eyeHeight / 2 : 12;
    canvas.fillRoundRect(lx - maxEyeWidth / 2, ly - eyeHeight / 2, maxEyeWidth, eyeHeight, radius, color);
    canvas.fillRoundRect(rx - maxEyeWidth / 2, ry - eyeHeight / 2, maxEyeWidth, eyeHeight, radius, color);
  }
}

void drawJoyEyes(int ox, int oy, float joyScale, uint16_t color, float vergence = 0.0f, float scale = 1.0f) {
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

void drawAngryBrows(float eyeHeightFactor, int ox, int oy, float browAlpha, uint16_t color, float vergence = 0.0f, float scale = 1.0f) {
  if (eyeHeightFactor <= 0.15f || browAlpha <= 0.01f) return;

  int lx = 32 + ox;
  int rx = 96 + ox;
  int ly = 28 + oy;
  int ry = 28 + oy;

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

void drawShockEyes(int ox, int oy, uint16_t color, float vergence = 0.0f, float scale = 1.0f) {
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

void drawSpiralEye(int cx, int cy, float rotAngle, uint16_t color, float scale = 1.0f) {
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

void drawSpiralEyes(int ox, int oy, float rotAngle, uint16_t color, float vergence = 0.0f, float scale = 1.0f) {
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

void drawSedihEyes(int ox, int oy, uint16_t color, float vergence = 0.0f, float scale = 1.0f) {
  int lx = 32 + ox;
  int rx = 96 + ox;
  int ly = 28 + oy;
  int ry = 28 + oy;

  float halfW = 11.0f;
  if (scale <= 0.05f) {
    canvas.fillRoundRect(lx - 14, ly - 1, 28, 3, 1, color);
    canvas.fillRoundRect(rx - 14, ry - 1, 28, 3, 1, color);
    return;
  }
  for (float x = -halfW; x <= halfW; x += 0.4f) {
    float normX = x / halfW;
    float y = (float)ly + (4.0f * scale * (1.0f - normX * normX));
    canvas.fillCircle(lx + (int)roundf(x), (int)roundf(y), 1, color);
    canvas.fillCircle(rx + (int)roundf(x), (int)roundf(y), 1, color);
  }
}

// Synchronized mouth path rendering (Rigid group locked)
void drawMouthCustom(int ox, int oy, float curve, float baseY, float width, float asym, uint16_t color, float scale = 1.0f) {
  int mx = 64 + ox;
  int my = (int)roundf(baseY + (float)oy);
  float scaledWidth = width * scale;
  for (float x = -scaledWidth; x <= scaledWidth; x += 0.4f) {
    float normX = (scale > 0.01f) ? (x / scale) : x;
    float y = (float)my + (curve * normX * normX + asym * normX) * scale;
    canvas.fillCircle(mx + (int)roundf(x), (int)roundf(y), 1, color);
  }
}

void drawJoyMouth(int ox, int oy, float joyScale, uint16_t color, float scale = 1.0f) {
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

void drawShockMouth(int ox, int oy, uint16_t color, float scale = 1.0f) {
  int mx = 64 + ox;
  int my = (int)roundf(39.0f + (float)oy);
  int w = (int)roundf(16.0f * scale);
  int h = (int)roundf(14.0f * scale);
  canvas.fillRoundRect(mx - w / 2, my, w, h, (int)roundf(5.0f * scale), color);
}

void drawOverloadMouth(int ox, int oy, uint16_t color, float scale = 1.0f) {
  int mx = 64 + ox;
  int my = (int)roundf(45.0f + (float)oy);
  canvas.fillEllipse(mx, my, (int)roundf(7.0f * scale), (int)roundf(5.0f * scale), color);
}

void drawSedihMouth(int ox, int oy, float phase, uint16_t color, float scale = 1.0f) {
  int mx = 64 + ox;
  int baseY = (int)roundf(44.0f + (float)oy);
  float halfW = 8.0f * scale;
  for (float x = -halfW; x <= halfW; x += 0.4f) {
    float y = (float)baseY + (1.2f * sinf(0.8f * (x / scale) + phase)) * scale;
    canvas.fillCircle(mx + (int)roundf(x), (int)roundf(y), 1, color);
  }
}

void renderFaceState(float eyeHeightFactor, int ox, int oy, float mouthCurve, float mouthY, float mouthWidth, float browAlpha, bool inverted, float vergence = 0.0f, float scale = 1.0f) {
  uint16_t bgColor = inverted ? TFT_WHITE : TFT_BLACK;
  uint16_t fgColor = inverted ? TFT_BLACK : TFT_WHITE;

  canvas.fillScreen(bgColor);
  drawEyes(eyeHeightFactor, ox, oy, fgColor, vergence, scale);
  drawAngryBrows(eyeHeightFactor, ox, oy, browAlpha, bgColor, vergence, scale);
  drawMouthCustom(ox, oy, mouthCurve, mouthY, mouthWidth, 0.0f, fgColor, scale);
  drawSensorOverlay();
  canvas.pushSprite(0, 0);
}

void drawFaceIdle(float eyeHeightFactor, int ox, int oy, float vergence = 0.0f, float scale = 1.0f) {
  renderFaceState(eyeHeightFactor, ox, oy, -0.030f, 44.0f, 7.5f, 0.0f, false, vergence, scale);
}

void drawFaceJoy(int ox, int oy, float vergence = 0.0f, float scale = 1.0f) {
  canvas.fillScreen(TFT_BLACK);
  drawJoyEyes(ox, oy, 1.0f, TFT_WHITE, vergence, scale);
  drawJoyMouth(ox, oy, 1.0f, TFT_WHITE, scale);
  drawSensorOverlay();
  canvas.pushSprite(0, 0);
}

void drawFaceAngry(float eyeHeightFactor, int ox, int oy, float vergence = 0.0f, float scale = 1.0f) {
  renderFaceState(eyeHeightFactor, ox, oy, 0.042f, 43.0f, 7.0f, 1.0f, true, vergence, scale);
}

void drawFaceSmirk(float eyeHeightFactor, int ox, int oy, float vergence = 0.0f, float scale = 1.0f) {
  canvas.fillScreen(TFT_BLACK);
  drawEyes(0.35f * eyeHeightFactor, ox, oy, TFT_WHITE, vergence, scale);
  drawMouthCustom(ox, oy, -0.035f, 43.0f, 9.0f, 0.14f, TFT_WHITE, scale);
  drawSensorOverlay();
  canvas.pushSprite(0, 0);
}

void drawFaceShock(float eyeHeightFactor, int ox, int oy, float vergence = 0.0f, float scale = 1.0f) {
  canvas.fillScreen(TFT_BLACK);
  if (eyeHeightFactor > 0.3f) {
    drawShockEyes(ox, oy, TFT_WHITE, vergence, scale);
  } else {
    drawEyes(eyeHeightFactor, ox, oy, TFT_WHITE, vergence, scale);
  }
  drawShockMouth(ox, oy, TFT_WHITE, scale);
  drawSensorOverlay();
  canvas.pushSprite(0, 0);
}

void drawFaceOverload(float eyeHeightFactor, int ox, int oy, float frame = 0.0f, float vergence = 0.0f, float scale = 1.0f) {
  canvas.fillScreen(TFT_BLACK);
  drawSpiralEyes(ox, oy, frame, TFT_WHITE, vergence, scale);
  drawOverloadMouth(ox, oy, TFT_WHITE, scale);
  drawSensorOverlay();
  canvas.pushSprite(0, 0);
}

void drawFaceSedih(float eyeHeightFactor, int ox, int oy, float frame = 0.0f, float vergence = 0.0f, float scale = 1.0f) {
  canvas.fillScreen(TFT_BLACK);
  drawSedihEyes(ox, oy, TFT_WHITE, vergence, scale);
  drawSedihMouth(ox, oy, frame, TFT_WHITE, scale);
  drawSensorOverlay();
  canvas.pushSprite(0, 0);
}

void drawFace(Expression expr, float eyeHeightFactor, float offsetX, float offsetY, float frame = 0.0f, float vergence = 0.0f, float scale = 1.0f) {
  int ox = getFilteredOx(offsetX);
  int oy = getFilteredOy(offsetY);
  switch (expr) {
    case EXPR_IDLE: drawFaceIdle(eyeHeightFactor, ox, oy, vergence, scale); break;
    case EXPR_JOY: drawFaceJoy(ox, oy, vergence, scale); break;
    case EXPR_ANGRY: drawFaceAngry(eyeHeightFactor, ox, oy, vergence, scale); break;
    case EXPR_SMIRK: drawFaceSmirk(eyeHeightFactor, ox, oy, vergence, scale); break;
    case EXPR_SHOCK: drawFaceShock(eyeHeightFactor, ox, oy, vergence, scale); break;
    case EXPR_OVERLOAD: drawFaceOverload(eyeHeightFactor, ox, oy, frame, vergence, scale); break;
    case EXPR_SEDIH: drawFaceSedih(eyeHeightFactor, ox, oy, frame, vergence, scale); break;
  }
}

/* --- Expression Transition Engine (170ms Ballistic Morph Window) --- */
void transitionExpression(Expression fromExpr, Expression toExpr, float durationMs = 170.0f) {
  if (fromExpr == toExpr) return;

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

  int steps = 10;
  float stepDelay = durationMs / steps;

  for (int i = 0; i <= steps; i++) {
    updateGazeSystem(); // Maintain continuous gaze kinematics updates during morph loop

    float t = (float)i / (float)steps;

    // Ballistic blink profile: eyelid closes in 60ms, holds closed slit for 40ms, re-opens in 70ms
    float curEyeH;
    if (t <= 0.40f) {
      float p = t / 0.40f;
      curEyeH = startEyeH * fmaxf(0.04f, 1.0f - p * p);
    } else if (t <= 0.60f) {
      curEyeH = 0.04f; // Hold closed slit state during geometry swap
    } else {
      float p = (t - 0.60f) / 0.40f;
      curEyeH = endEyeH * fmaxf(0.04f, sinf(p * 1.5707963f));
    }

    float easedT = easeInOutCubic(t);
    float curCurve = customLerp(startCurve, endCurve, easedT);
    float curY     = customLerp(startY, endY, easedT);
    float curW     = customLerp(startW, endW, easedT);
    float curAsym  = customLerp(startAsym, endAsym, easedT);
    float curBrow  = customLerp(startBrow, endBrow, easedT);

    float joyScale = (t < 0.5f) ? fmaxf(0.0f, 1.0f - t * 2.0f) : fmaxf(0.0f, (t - 0.5f) * 2.0f);

    // Invert monochrome screen colors for EXPR_ANGRY at mid-blink (t=0.5) to avoid visual flicker
    bool inverted = (t < 0.5f) ? (fromExpr == EXPR_ANGRY) : (toExpr == EXPR_ANGRY);
    uint16_t bgColor = inverted ? TFT_WHITE : TFT_BLACK;
    uint16_t fgColor = inverted ? TFT_BLACK : TFT_WHITE;

    int ox = getFilteredOx(currentOffsetX);
    int oy = getFilteredOy(currentOffsetY);

    canvas.fillScreen(bgColor);

    // Render underlying eye shape cleanly when eyelids are closed
    Expression activeExpr = (t < 0.5f) ? fromExpr : toExpr;
    if (activeExpr == EXPR_IDLE || activeExpr == EXPR_ANGRY || activeExpr == EXPR_SMIRK) {
      drawEyes(curEyeH, ox, oy, fgColor, currentVergence, currentEyeScale);
    } else if (activeExpr == EXPR_JOY) {
      drawJoyEyes(ox, oy, joyScale, fgColor, currentVergence, currentEyeScale);
    } else if (activeExpr == EXPR_SHOCK) {
      drawShockEyes(ox, oy, fgColor, currentVergence, currentEyeScale * curEyeH);
    } else if (activeExpr == EXPR_OVERLOAD) {
      drawSpiralEyes(ox, oy, t * 2.0f, fgColor, currentVergence, currentEyeScale * curEyeH);
    } else if (activeExpr == EXPR_SEDIH) {
      drawSedihEyes(ox, oy, fgColor, currentVergence, currentEyeScale * curEyeH);
    }

    if (curBrow > 0.01f) {
      drawAngryBrows(curEyeH, ox, oy, curBrow, bgColor, currentVergence, currentEyeScale);
    }

    // Rigid synchronized mouth interpolation
    bool fromCurveMouth = (fromExpr == EXPR_IDLE || fromExpr == EXPR_ANGRY || fromExpr == EXPR_SMIRK);
    bool toCurveMouth   = (toExpr == EXPR_IDLE || toExpr == EXPR_ANGRY || toExpr == EXPR_SMIRK);

    if (fromCurveMouth && toCurveMouth) {
      drawMouthCustom(ox, oy, curCurve, curY, curW, curAsym, fgColor, currentEyeScale);
    } else {
      Expression mouthExpr = (t < 0.5f) ? fromExpr : toExpr;
      if (mouthExpr == EXPR_IDLE || mouthExpr == EXPR_ANGRY || mouthExpr == EXPR_SMIRK) {
        drawMouthCustom(ox, oy, curCurve, curY, curW, curAsym, fgColor, currentEyeScale);
      } else if (mouthExpr == EXPR_JOY) {
        drawJoyMouth(ox, oy, joyScale, fgColor, currentEyeScale);
      } else if (mouthExpr == EXPR_SHOCK) {
        drawShockMouth(ox, oy, fgColor, currentEyeScale);
      } else if (mouthExpr == EXPR_OVERLOAD) {
        drawOverloadMouth(ox, oy, fgColor, currentEyeScale);
      } else if (mouthExpr == EXPR_SEDIH) {
        drawSedihMouth(ox, oy, t * 4.0f, fgColor, currentEyeScale);
      }
    }

    drawSensorOverlay();
    canvas.pushSprite(0, 0);
    vTaskDelay(pdMS_TO_TICKS((int)stepDelay));
  }
  currentExpr = toExpr;
}

void blink(Expression expr, float currentOffsetX = 0.0f, float currentOffsetY = 0.0f) {
  if (expr == EXPR_ANGRY || expr == EXPR_OVERLOAD || expr == EXPR_SEDIH || expr == EXPR_JOY) return;

  int closeSteps = 3;
  for (int i = 0; i <= closeSteps; i++) {
    float t = (float)i / closeSteps;
    float h = blinkCloseEase(t);
    drawFace(expr, h, currentOffsetX, currentOffsetY, 0.0f, currentVergence, currentEyeScale);
    vTaskDelay(pdMS_TO_TICKS(11));
  }

  int openSteps = 5;
  for (int i = 0; i <= openSteps; i++) {
    float t = (float)i / openSteps;
    float h = blinkOpenEase(t);
    drawFace(expr, h, currentOffsetX, currentOffsetY, 0.0f, currentVergence, currentEyeScale);
    vTaskDelay(pdMS_TO_TICKS(14));
  }
}

void setNextExpression(Expression newExpr) {
  if (currentExpr != newExpr && !g_is_transitioning) {
    g_is_transitioning = true;
    transitionExpression(currentExpr, newExpr, 170.0f);
    animFrame = 0.0f;
    g_blinkState = BLINK_IDLE_STATE;
    g_blinkEyeHeight = 1.0f;
    g_nextBlinkTime = millis() + ((newExpr == EXPR_ANGRY) ? (esp_random() % 4000 + 4000) : (esp_random() % 3500 + 3500));
    g_is_transitioning = false;
  }
}

/* --- Russell Circumplex 2D Emotion Engine --- */
static float g_emotion_valence = 0.05f;   // V ∈ [-1.0, +1.0] (Displeasure to Pleasure)
static float g_emotion_arousal = 0.15f;   // A ∈ [0.0, 1.0] (Quiescence to High Activation)
static uint32_t g_last_mood_update = 0;
static uint32_t g_mood_lock_until = 0;    // Biological refractory period lock (milliseconds)
static uint32_t g_nextMoodShiftTime = 0;
static bool g_lastTargetDetectedState = false;
static float g_target_presence_ema = 0.0f;

/**
 * @brief Updates 2D Valence-Arousal emotion dynamics and triggers state machine mood shifts.
 * @note Uses Langevin homeostatic relaxation (tau_v=6.0s, tau_a=4.5s) and enforces a 5-8 second refractory lock on transitions.
 */
void updateBiologicalMoodEngine() {
  unsigned long now = millis();
  if (g_is_transitioning || now < g_mood_lock_until) return; // Enforce atomic transition locking and refractory lock

  TrackTarget target;
  portENTER_CRITICAL(&target_mutex);
  target = current_target;
  portEXIT_CRITICAL(&target_mutex);

  // Leaky target presence exponential moving average (EMA)
  float raw_pres = (target.detected && target.confidence > 0.28f) ? 1.0f : 0.0f;
  g_target_presence_ema = g_target_presence_ema * 0.88f + raw_pres * 0.12f;
  bool is_detected = (g_target_presence_ema > 0.58f);

  float dt = (g_last_mood_update > 0) ? (float)(now - g_last_mood_update) * 0.001f : 0.050f;
  dt = constrain(dt, 0.01f, 0.20f);
  g_last_mood_update = now;

  // Emotional equilibrium targets based on visual detection stimulus
  float target_v = 0.05f;
  float target_a = 0.15f;

  if (is_detected) {
    target_a = 0.50f + 0.30f * target.proximity;
    target_v = 0.40f;
  } else {
    target_v = 0.05f;
    target_a = 0.15f;
  }

  // Viscous homeostatic Langevin relaxation (tau_v = 6.0s, tau_a = 4.5s)
  float tau_v = 6.0f;
  float tau_a = 4.5f;
  g_emotion_valence += ((target_v - g_emotion_valence) / tau_v) * dt;
  g_emotion_arousal += ((target_a - g_emotion_arousal) / tau_a) * dt;

  // Event 1: Initial target acquisition reaction (debounced and refractory locked)
  if (is_detected && !g_lastTargetDetectedState) {
    g_lastTargetDetectedState = true;
    uint32_t roll = esp_random() % 100;
    Expression reactExpr = EXPR_IDLE;
    if (roll < 45) {
      reactExpr = EXPR_ANGRY;
      g_emotion_valence = -0.6f;
      g_emotion_arousal = 0.75f;
    } else if (roll < 65) {
      reactExpr = EXPR_SHOCK;
      g_emotion_valence = -0.2f;
      g_emotion_arousal = 0.85f;
    } else if (roll < 85) {
      reactExpr = EXPR_SMIRK;
      g_emotion_valence = 0.45f;
      g_emotion_arousal = 0.35f;
    } else {
      reactExpr = EXPR_IDLE;
      g_emotion_valence = 0.10f;
      g_emotion_arousal = 0.25f;
    }
    setNextExpression(reactExpr);
    g_mood_lock_until = now + (esp_random() % 3000 + 5000); // 5-8s refractory lock
    g_nextMoodShiftTime = g_mood_lock_until + (esp_random() % 4000 + 4000);
    return;
  }

  // Event 2: Target loss reaction (debounced and refractory locked)
  if (!is_detected && g_lastTargetDetectedState && g_target_presence_ema < 0.15f) {
    g_lastTargetDetectedState = false;
    uint32_t roll = esp_random() % 100;
    Expression reactExpr = (roll < 75) ? EXPR_IDLE : ((roll < 90) ? EXPR_SEDIH : EXPR_ANGRY);
    setNextExpression(reactExpr);
    g_mood_lock_until = now + (esp_random() % 2500 + 4500); // 4.5-7s refractory lock
    g_nextMoodShiftTime = g_mood_lock_until + (esp_random() % 4000 + 4000);
    return;
  }

  // Spontaneous Markov mood transitions when in equilibrium
  if (g_nextMoodShiftTime == 0) {
    g_nextMoodShiftTime = now + (esp_random() % 6000 + 8000);
  }

  if (now >= g_nextMoodShiftTime) {
    uint32_t roll = esp_random() % 100;
    Expression nextMood = EXPR_IDLE;

    switch (currentExpr) {
      case EXPR_IDLE:
        if (roll < 65) nextMood = EXPR_IDLE;          // 65% Dominant Idle
        else if (roll < 82) nextMood = EXPR_ANGRY;    // 17% ANGRY
        else if (roll < 90) nextMood = EXPR_SMIRK;    // 8% SMIRK
        else if (roll < 95) nextMood = EXPR_JOY;      // 5% JOY
        else if (roll < 98) nextMood = EXPR_SEDIH;    // 3% SEDIH
        else nextMood = EXPR_OVERLOAD;                // 2% OVERLOAD
        break;

      case EXPR_ANGRY:
        if (roll < 70) nextMood = EXPR_IDLE;          // 70% Calm down to IDLE
        else if (roll < 90) nextMood = EXPR_SMIRK;    // 20% Transition to Smirk
        else nextMood = EXPR_SEDIH;                   // 10% SEDIH
        break;

      case EXPR_JOY:
        if (roll < 75) nextMood = EXPR_IDLE;          // 75% IDLE
        else if (roll < 95) nextMood = EXPR_SMIRK;    // 20% SMIRK
        else nextMood = EXPR_ANGRY;                   // 5% ANGRY
        break;

      case EXPR_SMIRK:
        if (roll < 70) nextMood = EXPR_IDLE;          // 70% IDLE
        else if (roll < 90) nextMood = EXPR_JOY;      // 20% JOY
        else nextMood = EXPR_ANGRY;                   // 10% ANGRY
        break;

      case EXPR_SHOCK:
        if (roll < 70) nextMood = EXPR_IDLE;          // 70% IDLE
        else if (roll < 90) nextMood = EXPR_SMIRK;    // 20% SMIRK
        else nextMood = EXPR_ANGRY;                   // 10% ANGRY
        break;

      case EXPR_OVERLOAD:
        if (roll < 75) nextMood = EXPR_IDLE;
        else nextMood = EXPR_SEDIH;
        break;

      case EXPR_SEDIH:
        if (roll < 70) nextMood = EXPR_IDLE;
        else if (roll < 90) nextMood = EXPR_SMIRK;
        else nextMood = EXPR_JOY;
        break;
    }

    setNextExpression(nextMood);
    g_mood_lock_until = now + (esp_random() % 3000 + 5000); // 5-8s refractory lock
    if (nextMood == EXPR_IDLE) {
      g_nextMoodShiftTime = g_mood_lock_until + (esp_random() % 8000 + 8000); // 8-16s dwell in IDLE
    } else {
      g_nextMoodShiftTime = g_mood_lock_until + (esp_random() % 4000 + 5000); // 5-9s dwell in active moods
    }
  }
}

/**
 * @brief FreeRTOS OLED UI task pinned to Core 1 (60 FPS rendering & gaze dynamics).
 * @param pvParameters FreeRTOS task parameter payload pointer.
 */
void oledTask(void *pvParameters) {
  pinMode(TOUCH_PIN, INPUT_PULLDOWN);

  lcd.init();
  lcd.setRotation(2);
  lcd.setBrightness(OLED_BRIGHTNESS);

  // Allocate 1-bit monochrome off-screen canvas sprite (128x64 pixels = 1024 bytes)
  canvas.setColorDepth(1);
  canvas.createSprite(128, 64);

  currentExpr = EXPR_IDLE;
  drawFace(currentExpr, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);

  while (true) {
    uint32_t frame_start_us = micros();
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

    Expression prevExpr = currentExpr;
    updateBiologicalMoodEngine();

    /* --- Reconnaissance State Handoff ---
     * Preserves physical eye offset across ACTIVE ↔ SLEEP state transitions */
    if (g_recon_state != g_prev_recon_state) {
      inSaccade            = false;
      trackInSaccade       = false;
      s_prevTargetDetected = false;
      nextGazeTime         = now + 600;
      g_prev_recon_state   = g_recon_state;
    }

    if (currentExpr != prevExpr) {
      // Expression transition already rendered step 10 frame; reset timing baseline
      frame_start_us = micros();
    } else {
      updateGazeSystem();

      /* --- Eyelid Blinking State Machine --- */
      bool canBlink = (currentExpr != EXPR_OVERLOAD && currentExpr != EXPR_SEDIH && currentExpr != EXPR_JOY);

      if (!canBlink) {
        g_blinkState = BLINK_IDLE_STATE;
        g_blinkEyeHeight = 1.0f;
      } else {
        if (g_blinkState == BLINK_IDLE_STATE) {
          if (g_nextBlinkTime == 0) {
            uint32_t initInterval = (currentExpr == EXPR_ANGRY) ? (esp_random() % 4000 + 4000) : (esp_random() % 3500 + 3500);
            g_nextBlinkTime = now + initInterval;
          }
          if (now >= g_nextBlinkTime) {
            g_blinkState = BLINK_CLOSING_STATE;
            g_blinkStartTime = now;
            if (g_isDoubleBlinkPending) {
              g_isDoubleBlinkPending = false;
            } else if ((esp_random() % 100) < 14) { 
              g_isDoubleBlinkPending = true;
            }
          }
        }

        if (g_blinkState == BLINK_CLOSING_STATE) {
          float elapsed = (float)(now - g_blinkStartTime);
          float duration = (currentExpr == EXPR_ANGRY) ? 35.0f : 50.0f;
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
          float duration = (currentExpr == EXPR_ANGRY) ? 80.0f : 110.0f;
          if (elapsed >= duration) {
            g_blinkEyeHeight = 1.0f;
            g_blinkState = BLINK_IDLE_STATE;
            if (g_isDoubleBlinkPending) {
              g_nextBlinkTime = now + 120;
            } else {
              uint32_t interval = (currentExpr == EXPR_ANGRY) ? (esp_random() % 4000 + 4000) : (esp_random() % 3500 + 3500);
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

      /* --- Render Dispatch to OLED Sprite --- */
      if (currentExpr == EXPR_OVERLOAD || currentExpr == EXPR_SEDIH) {
        animFrame += 0.025f;
      }

      drawFace(currentExpr, g_blinkEyeHeight, currentOffsetX, currentOffsetY, animFrame, currentVergence, currentEyeScale);
    }

    /* --- Adaptive Frame Pacing: 60 FPS (Active) / 30 FPS (Sleep) --- */
    uint32_t frame_budget_us = (g_recon_state == STATE_SLEEP_RECON) ? FPS_SLEEP_US : FPS_ACTIVE_US;
    uint32_t frame_elapsed_us = micros() - frame_start_us;
    if (frame_elapsed_us < frame_budget_us) {
      uint32_t wait_us = frame_budget_us - frame_elapsed_us;
      if (wait_us > 3000) {
        vTaskDelay(pdMS_TO_TICKS(wait_us / 1000 - 1));
      }
      while ((micros() - frame_start_us) < frame_budget_us) {
        taskYIELD();
      }
    }
  }
}

/* --- 2D Discrete Kalman Filter Architecture --- */

/**
 * @struct KalmanFilter1D
 * @brief Discrete 1D linear Kalman filter tracking state vector [p, v]^T.
 */
struct KalmanFilter1D {
  float p;       // Estimated target position (pixels)
  float v;       // Estimated target velocity (pixels/sec)
  float P00;     // State covariance P[0,0]
  float P01;     // State covariance P[0,1]
  float P11;     // State covariance P[1,1]

  void init(float init_p) {
    p = init_p;
    v = 0.0f;
    P00 = 100.0f;
    P01 = 0.0f;
    P11 = 100.0f;
  }

  void predict(float dt, float q_accel) {
    // State transition F = [1, dt; 0, 1]
    p = p + v * dt;

    // Process noise covariance Q for constant velocity model with acceleration variance q
    float dt2 = dt * dt;
    float dt3 = dt2 * dt;
    float dt4 = dt3 * dt;
    float Q00 = 0.25f * dt4 * q_accel;
    float Q01 = 0.50f * dt3 * q_accel;
    float Q11 = dt2 * q_accel;

    float P00_new = P00 + dt * (P01 + P01) + dt2 * P11 + Q00;
    float P01_new = P01 + dt * P11 + Q01;
    float P11_new = P11 + Q11;

    P00 = P00_new;
    P01 = P01_new;
    P11 = P11_new;
  }

  void update(float z, float R) {
    float y = z - p;               // Innovation residual
    float S = P00 + R;             // Innovation covariance
    if (S < 1e-4f) S = 1e-4f;
    float invS = 1.0f / S;

    float K0 = P00 * invS;         // Kalman gain for position state
    float K1 = P01 * invS;         // Kalman gain for velocity state

    p = p + K0 * y;
    v = v + K1 * y;

    // Joseph-stabilized covariance update to guarantee positive semi-definiteness
    float P00_temp = (1.0f - K0) * P00;
    float P01_temp = (1.0f - K0) * P01;
    float P11_temp = P11 - K1 * P01;

    P00 = fmaxf(1e-3f, P00_temp);
    P01 = P01_temp;
    P11 = fmaxf(1e-3f, P11_temp);
  }
};

/**
 * @struct KalmanTracker2D
 * @brief Container tracking 2D Cartesian target kinematic state [x, y, vx, vy]^T.
 */
struct KalmanTracker2D {
  KalmanFilter1D kf_x;
  KalmanFilter1D kf_y;
  float w;
  float h;
  bool active;
  uint32_t last_update_us;

  void init() {
    kf_x.init(320.0f);
    kf_y.init(240.0f);
    w = 160.0f;
    h = 200.0f;
    active = false;
    last_update_us = 0;
  }
};

static KalmanTracker2D k_tracker;
static float lock_confidence = 0.0f;
static uint32_t last_valid_human_time = 0;
static float ema_global_luminance = 100.0f;

static float debug_m00 = 0.0f;
static int debug_skin_px = 0;
static float debug_lock_conf = 0.0f;

/**
 * @brief Differential computer vision engine executing YCbCr skin classification and Kalman tracking.
 * @param fb Pointer to camera frame buffer containing raw JPEG bytes.
 * @note Marked with `IRAM_ATTR` to run out of fast instruction RAM. Performs 8x downscaling to 80x60,
 *       analyzes 4x3 spatial lighting sectors, computes motion history image (MHI), and tracks 2D targets.
 */
void IRAM_ATTR processFrameAI(camera_fb_t *fb) {
  if (!fb || !fb->buf || fb->len < 1024 || !small_rgb_buf || !prev_lum_buf || !mhi_buf || fb->format != PIXFORMAT_JPEG) return;

  // 8x fast hardware downscaling from VGA (640x480) to 80x60 RGB565 array
  bool ok = jpg2rgb565(fb->buf, fb->len, small_rgb_buf, JPG_SCALE_8X);
  if (!ok) return;

  uint16_t* pixels = (uint16_t*)small_rgb_buf;

  static uint8_t cur_40x30[1200] __attribute__((aligned(16)));
  static bool skin_mask_40x30[1200] __attribute__((aligned(16)));
  static uint8_t texture_40x30[1200] __attribute__((aligned(16)));
  int skin_pixel_count = 0;
  uint32_t total_luminance_sum = 0;

  // Local grid illumination metrics (4 horizontal sectors x 3 vertical sectors)
  uint32_t sec_lum_sum[3][4] = {0};
  int sec_pixel_cnt[3][4] = {0};

  uint8_t* p_cur = cur_40x30;

  // Pass 1: Subsampled Y-luminance conversion and 4x3 sector illumination analysis
  for (int y = 0; y < 30; y++) {
    int src_row_off = (y * 2) * 80;
    int sec_y = y / 10;
    for (int x = 0; x < 40; x++) {
      uint16_t p = pixels[src_row_off + (x * 2)];
      int r = ((p >> 11) & 0x1F) << 3;
      int g = ((p >> 5) & 0x3F) << 2;
      int b = (p & 0x1F) << 3;
      
      uint8_t y_lum = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
      *p_cur++ = y_lum;
      total_luminance_sum += y_lum;

      int sec_x = x / 10;
      sec_lum_sum[sec_y][sec_x] += y_lum;
      sec_pixel_cnt[sec_y][sec_x]++;
    }
  }

  float frame_mean_lum = (float)total_luminance_sum / 1200.0f;
  ema_global_luminance = 0.90f * ema_global_luminance + 0.10f * frame_mean_lum;

  // Precompute mean luminance for each of the 12 local sectors
  float sec_mean_lum[3][4];
  for (int sy = 0; sy < 3; sy++) {
    for (int sx = 0; sx < 4; sx++) {
      sec_mean_lum[sy][sx] = sec_pixel_cnt[sy][sx] > 0 ? ((float)sec_lum_sum[sy][sx] / (float)sec_pixel_cnt[sy][sx]) : frame_mean_lum;
    }
  }

  // Pass 2: Dynamic Local-Lighting Adaptive YCbCr Skin Classifier
  bool* p_skin = skin_mask_40x30;
  for (int y = 0; y < 30; y++) {
    int src_row_off = (y * 2) * 80;
    int sec_y = y / 10;
    for (int x = 0; x < 40; x++) {
      int sec_x = x / 10;
      float local_lum = sec_mean_lum[sec_y][sec_x];

      uint16_t p = pixels[src_row_off + (x * 2)];
      int r = ((p >> 11) & 0x1F) << 3;
      int g = ((p >> 5) & 0x3F) << 2;
      int b = (p & 0x1F) << 3;

      int cb = 128 + (((-43 * r - 85 * g + 128 * b)) >> 8);
      int cr = 128 + (((128 * r - 107 * g - 21 * b)) >> 8);

      // Local lighting compensation: adjust Cb/Cr thresholds dynamically per sector
      float delta_local = fmaxf(0.0f, fminf(16.0f, (95.0f - local_lum) * 0.45f));
      int min_cb = (int)(75.0f - delta_local);
      int max_cb = (int)(129.0f + delta_local);
      int min_cr = (int)(127.0f - delta_local);
      int max_cr = (int)(180.0f + delta_local);

      // Relax shadow margins under severe backlighting or underexposed shadows
      int shadow_margin = (local_lum < 65.0f) ? 4 : 0;
      int min_r_thresh   = (local_lum < 65.0f) ? 8 : 10;
      int min_cr_cb_diff = (local_lum < 65.0f) ? 4 : 6;

      // Reject specular highlight glare
      if (local_lum > 185.0f) {
        min_cb += 3;
        max_cb -= 3;
        min_cr += 3;
        max_cr -= 3;
      }

      bool is_skin = (r + shadow_margin >= b) && 
                     ((cr - cb) >= min_cr_cb_diff) && 
                     (r >= min_r_thresh) && 
                     (cb >= min_cb && cb <= max_cb && cr >= min_cr && cr <= max_cr);

      *p_skin++ = is_skin;
      if (is_skin) skin_pixel_count++;
    }
  }

  // Pass 3: 3x3 Spatial box filter for noise reduction and spatial gradient extraction
  static uint8_t smooth_40x30[1200] __attribute__((aligned(16)));
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
    k_tracker.init();
    k_tracker.last_update_us = micros();
    return;
  }

  // Compute global frame difference mean
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

  float M00 = 0.0f, M10 = 0.0f, M01 = 0.0f, M20 = 0.0f, M02 = 0.0f, M11 = 0.0f;

  float prev_grid_x = k_tracker.kf_x.p * (1.0f / 16.0f);
  float prev_grid_y = k_tracker.kf_y.p * (1.0f / 16.0f);

  // Pass 4: 3-Sector Multi-Object Spatial Accumulators
  float sec_M00[3] = {0}, sec_M10[3] = {0}, sec_M01[3] = {0};
  float sec_M20[3] = {0}, sec_M02[3] = {0};
  int sec_skin[3] = {0};
  float sec_motion[3] = {0};

  for (int y = 1; y < 29; y++) {
    int y_off = y * 40;
    for (int x = 1; x < 39; x++) {
      int idx = y_off + x;

      uint8_t lum = smooth_40x30[idx];
      uint8_t prev_l = prev_lum_buf[idx];

      float local_delta = (float)abs((int)lum - (int)prev_l);
      float delta = fmaxf(0.0f, local_delta - global_delta_mean);

      // Decay Motion History Image (MHI) buffer
      if (delta > 6.0f) {
        mhi_buf[idx] = 255;
      } else {
        mhi_buf[idx] = (mhi_buf[idx] > 35) ? (mhi_buf[idx] - 35) : 0;
      }
      float mhi_weight = (float)mhi_buf[idx] / 255.0f;

      float gy = (float)abs((int)smooth_40x30[idx + 40] - (int)smooth_40x30[idx - 40]);
      float gx = (float)abs((int)smooth_40x30[idx + 1] - (int)smooth_40x30[idx - 1]);

      bool is_skin = skin_mask_40x30[idx];
      uint8_t raw_lum = cur_40x30[idx];

      // Reject non-skin high-luminance background light sources (lamps, windows)
      if (!is_skin && raw_lum > 215) continue;

      float raw_energy = 2.5f * delta + 16.0f * mhi_weight + 1.5f * gy + 0.8f * gx;
      float energy = raw_energy;

      if (is_skin) {
        energy = 10.0f + 0.3f * (gy + gx);
      } else {
        float tex_val = (float)texture_40x30[idx];
        float texture_factor = 1.0f - (tex_val - 15.0f) / 40.0f;
        if (texture_factor < 0.20f) texture_factor = 0.20f;
        if (texture_factor > 1.0f)  texture_factor = 1.0f;
        energy = raw_energy * (0.04f * texture_factor);
      }

      if (!is_skin && (raw_energy < 22.0f || energy < 1.0f)) continue; // Require strong motion/gradient for non-skin pixels

      float weight = energy;
      float dx = (float)x;
      float dy = (float)y;

      // Spatial gating: Attenuate pixels distant from previous Kalman tracking estimate
      if (k_tracker.active) {
        float dist_x = dx - prev_grid_x;
        float dist_y = dy - prev_grid_y;
        float norm_dist_sq = (dist_x * dist_x) * (1.0f / 64.0f) + (dist_y * dist_y) * (1.0f / 49.0f);
        if (norm_dist_sq > 2.25f && !is_skin) {
          continue;
        }
        if (norm_dist_sq > 1.0f && !is_skin) {
          weight *= 0.25f;
        }
      }

      M00 += weight;
      M10 += dx * weight;
      M01 += dy * weight;
      M20 += dx * dx * weight;
      M02 += dy * dy * weight;
      M11 += dx * dy * weight;

      // Accumulate into 3 horizontal sectors for multi-object candidate scoring
      int s_idx = (x < 14) ? 0 : ((x < 27) ? 1 : 2);
      sec_M00[s_idx] += weight;
      sec_M10[s_idx] += dx * weight;
      sec_M01[s_idx] += dy * weight;
      sec_M20[s_idx] += dx * dx * weight;
      sec_M02[s_idx] += dy * dy * weight;
      if (is_skin) sec_skin[s_idx]++;
      sec_motion[s_idx] += energy;
    }
  }

  // Pass 5: Segment and rank up to 3 spatial object candidates
  ObjectCandidate temp_cand[3];
  int active_cnt = 0;

  for (int s = 0; s < 3; s++) {
    if (sec_M00[s] >= 8.0f || sec_skin[s] >= 2) {
      float inv_M = 1.0f / fmaxf(1.0f, sec_M00[s]);
      float mx = sec_M10[s] * inv_M;
      float my = sec_M01[s] * inv_M;

      float sig_x = sqrtf(fmaxf(0.0f, (sec_M20[s] * inv_M) - (mx * mx)));
      float sig_y = sqrtf(fmaxf(0.0f, (sec_M02[s] * inv_M) - (my * my)));

      float cx = mx * 16.0f;
      float cy = my * 16.0f;
      float bw = fmaxf(130.0f, fminf(240.0f, 2.4f * fmaxf(2.8f, sig_x) * 16.0f));
      float bh = fmaxf(160.0f, fminf(310.0f, 2.8f * fmaxf(3.2f, sig_y) * 16.0f));

      // Candidate priority score combining skin ratio, motion energy, area, and center proximity penalty
      float center_dist = fabsf(cx - 320.0f);
      float priority = 15.0f * sec_skin[s] + 1.8f * sec_motion[s] + 0.10f * sec_M00[s] - 0.06f * center_dist;

      // Estimate target proximity Z from bounding box surface area
      float area_norm = sqrtf(bw * bh);
      float prox = constrain((area_norm - 140.0f) / 130.0f, 0.0f, 1.0f);

      temp_cand[active_cnt].active = true;
      temp_cand[active_cnt].cx = (int)cx;
      temp_cand[active_cnt].cy = (int)cy;
      temp_cand[active_cnt].w = (int)bw;
      temp_cand[active_cnt].h = (int)bh;
      temp_cand[active_cnt].priority_score = priority;
      temp_cand[active_cnt].skin_px = sec_skin[s];
      temp_cand[active_cnt].motion_energy = sec_motion[s];
      temp_cand[active_cnt].proximity = prox;
      temp_cand[active_cnt].error_x = ((cx - 320.0f) / 320.0f) * 100.0f;
      temp_cand[active_cnt].error_y = ((cy - 240.0f) / 240.0f) * 100.0f;
      active_cnt++;
    }
  }

  // Sort candidates by Priority Score descending
  for (int i = 0; i < active_cnt - 1; i++) {
    for (int j = i + 1; j < active_cnt; j++) {
      if (temp_cand[j].priority_score > temp_cand[i].priority_score) {
        ObjectCandidate tmp = temp_cand[i];
        temp_cand[i] = temp_cand[j];
        temp_cand[j] = tmp;
      }
    }
  }

  // Update global candidate array under mutex lock
  portENTER_CRITICAL(&target_mutex);
  g_num_candidates = active_cnt;
  for (int i = 0; i < MAX_OBJECT_CANDIDATES; i++) {
    if (i < active_cnt) {
      g_object_candidates[i] = temp_cand[i];
    } else {
      g_object_candidates[i].active = false;
      g_object_candidates[i].priority_score = 0.0f;
    }
  }
  portEXIT_CRITICAL(&target_mutex);

  memcpy(prev_lum_buf, smooth_40x30, 1200);

  float dynamic_threshold = 95.0f - ((float)skin_pixel_count * 0.65f);
  if (dynamic_threshold < 45.0f) dynamic_threshold = 45.0f;

  uint32_t now_us = micros();
  uint32_t now_ms = millis();

  // Autonomous object inspection switcher state machine
  if (g_num_candidates > 1) {
    if (now_ms - g_last_inspection_time_ms > g_inspection_hold_time_ms) {
      g_inspected_candidate_idx = (g_inspected_candidate_idx + 1) % g_num_candidates;
      g_last_inspection_time_ms = now_ms;
      g_inspection_hold_time_ms = (esp_random() % 1400) + 2400; // Hold inspection for 2.4s to 3.8s
    }
  } else {
    g_inspected_candidate_idx = 0;
  }

  float dt_sec = (k_tracker.last_update_us > 0) ? ((float)(now_us - k_tracker.last_update_us) * 1e-6f) : 0.033f;
  float dt = fmaxf(0.01f, fminf(0.20f, dt_sec));
  k_tracker.last_update_us = now_us;

  // Pass 6: 2D Kalman Filter Prediction Step
  float q_process = (skin_pixel_count >= 4) ? 850.0f : 450.0f; // Increase process noise Q when skin pixels confirm human presence
  k_tracker.kf_x.predict(dt, q_process);
  k_tracker.kf_y.predict(dt, q_process);

  float pred_x = k_tracker.kf_x.p;
  float pred_y = k_tracker.kf_y.p;
  float speed = sqrtf(k_tracker.kf_x.v * k_tracker.kf_x.v + k_tracker.kf_y.v * k_tracker.kf_y.v);
  float search_radius = fminf(320.0f, 120.0f + 0.35f * speed);

  bool candidate_found = false;
  float cand_cx = pred_x, cand_cy = pred_y, cand_bw = k_tracker.w, cand_bh = k_tracker.h;

  if (M00 >= dynamic_threshold) {
    float inv_M00 = 1.0f / M00;
    float mean_x = M10 * inv_M00;
    float mean_y = M01 * inv_M00;

    float mu20 = (M20 * inv_M00) - (mean_x * mean_x);
    float mu02 = (M02 * inv_M00) - (mean_y * mean_y);

    float sigma_x = sqrtf(fmaxf(0.0f, mu20));
    float sigma_y = sqrtf(fmaxf(0.0f, mu02));

    float raw_cx = mean_x * 16.0f;
    float raw_cy = mean_y * 16.0f;

    // Head/shoulder aspect ratio coupling
    float raw_bw = 2.4f * fmaxf(2.8f, sigma_x) * 16.0f;
    float raw_bh = 2.8f * fmaxf(3.2f, sigma_y) * 16.0f;

    float coupled_bh = fmaxf(raw_bh, 1.25f * raw_bw);
    coupled_bh = fminf(coupled_bh, 1.50f * raw_bw);

    cand_bw = fmaxf(140.0f, fminf(240.0f, raw_bw));
    cand_bh = fmaxf(180.0f, fminf(310.0f, coupled_bh));

    if (!k_tracker.active) {
      if (skin_pixel_count >= 4) {
        cand_cx = raw_cx;
        cand_cy = raw_cy;
        candidate_found = true;
      }
    } else {
      float effective_radius = (skin_pixel_count >= 4) ? 450.0f : search_radius;
      float dist_sq = (raw_cx - pred_x) * (raw_cx - pred_x) + (raw_cy - pred_y) * (raw_cy - pred_y);
      if (dist_sq <= (effective_radius * effective_radius)) {
        cand_cx = raw_cx;
        cand_cy = raw_cy;
        candidate_found = true;
      }
    }
  } else if (k_tracker.active && skin_pixel_count >= 8) {
    // Static target persistence fallback using skin locus centroid
    float M00_skin = 0.0f, M10_skin = 0.0f, M01_skin = 0.0f;
    float M20_skin = 0.0f, M02_skin = 0.0f;
    for (int y = 1; y < 29; y++) {
      int y_off = y * 40;
      for (int x = 1; x < 39; x++) {
        int idx = y_off + x;
        if (skin_mask_40x30[idx]) {
          float fx = (float)x;
          float fy = (float)y;
          M00_skin += 1.0f;
          M10_skin += fx;
          M01_skin += fy;
          M20_skin += fx * fx;
          M02_skin += fy * fy;
        }
      }
    }
    if (M00_skin >= 15.0f) {
      float inv_M00_skin = 1.0f / M00_skin;
      float mean_x_skin = M10_skin * inv_M00_skin;
      float mean_y_skin = M01_skin * inv_M00_skin;

      float mu20_skin = (M20_skin * inv_M00_skin) - (mean_x_skin * mean_x_skin);
      float mu02_skin = (M02_skin * inv_M00_skin) - (mean_y_skin * mean_y_skin);

      float sigma_x_skin = sqrtf(fmaxf(0.0f, mu20_skin));
      float sigma_y_skin = sqrtf(fmaxf(0.0f, mu02_skin));

      float raw_cx_skin = mean_x_skin * 16.0f;
      float raw_cy_skin = mean_y_skin * 16.0f;

      float raw_bw_skin = 2.4f * fmaxf(2.8f, sigma_x_skin) * 16.0f;
      float raw_bh_skin = 2.8f * fmaxf(3.2f, sigma_y_skin) * 16.0f;
      float coupled_bh_skin = fmaxf(raw_bh_skin, 1.25f * raw_bw_skin);
      coupled_bh_skin = fminf(coupled_bh_skin, 1.50f * raw_bw_skin);

      cand_bw = fmaxf(k_tracker.w * 0.85f, fmaxf(140.0f, fminf(240.0f, raw_bw_skin)));
      cand_bh = fmaxf(k_tracker.h * 0.85f, fmaxf(180.0f, fminf(310.0f, coupled_bh_skin)));

      float effective_radius_skin = (skin_pixel_count >= 4) ? 450.0f : search_radius;
      float dist_sq_skin = (raw_cx_skin - pred_x) * (raw_cx_skin - pred_x) + (raw_cy_skin - pred_y) * (raw_cy_skin - pred_y);
      if (dist_sq_skin <= (effective_radius_skin * effective_radius_skin)) {
        cand_cx = raw_cx_skin;
        cand_cy = raw_cy_skin;
        candidate_found = true;
      }
    }
  }

  lock_confidence = lock_confidence * 0.85f + (candidate_found ? 1.0f : 0.0f) * 0.15f;

  if (candidate_found) {
    // Dynamic measurement noise R covariance adjustment:
    // Stationary target (< 6px innovation): increase R to smooth quantization noise.
    // Rapid movement (> 20px innovation): decrease R to eliminate phase lag during pursuit.
    float dist_innov = sqrtf((cand_cx - pred_x) * (cand_cx - pred_x) + (cand_cy - pred_y) * (cand_cy - pred_y));
    
    float R_base = 25.0f;
    float R_dynamic;
    if (skin_pixel_count >= 4) {
      if (dist_innov < 6.0f) {
        R_dynamic = R_base * (1.5f + 1.5f * lock_confidence);
      } else {
        R_dynamic = R_base / (1.0f + 0.12f * (dist_innov - 6.0f));
      }
    } else {
      R_dynamic = R_base * 2.0f;
    }
    R_dynamic = constrain(R_dynamic, 3.0f, 150.0f);

    k_tracker.kf_x.update(cand_cx, R_dynamic);
    k_tracker.kf_y.update(cand_cy, R_dynamic);

    k_tracker.w = k_tracker.w * 0.70f + cand_bw * 0.30f;
    k_tracker.h = k_tracker.h * 0.70f + cand_bh * 0.30f;
    k_tracker.w = fmaxf(60.0f, fminf(400.0f, k_tracker.w));
    k_tracker.h = fmaxf(80.0f, fminf(480.0f, k_tracker.h));

    k_tracker.active = true;
    last_valid_human_time = now_ms;
  } else {
    // Predict-only velocity decay when measurement is temporarily lost
    k_tracker.kf_x.v *= 0.85f;
    k_tracker.kf_y.v *= 0.85f;

    if (now_ms - last_valid_human_time > 450) {
      k_tracker.active = false;
      k_tracker.kf_x.v = 0.0f;
      k_tracker.kf_y.v = 0.0f;
    }
  }

  // Constrain estimated position within image bounds
  k_tracker.kf_x.p = fmaxf(0.0f, fminf(640.0f, k_tracker.kf_x.p));
  k_tracker.kf_y.p = fmaxf(0.0f, fminf(480.0f, k_tracker.kf_y.p));

  // Compute normalized target proximity Z in range [0.0, 1.0]
  float current_area = sqrtf(k_tracker.w * k_tracker.h);
  float proximity_z = constrain((current_area - 140.0f) / 130.0f, 0.0f, 1.0f);

  debug_m00 = (float)M00;
  debug_skin_px = skin_pixel_count;
  debug_lock_conf = lock_confidence;

  if (k_tracker.active && lock_confidence > 0.25f) {
    float center_x = frame_w / 2.0f;
    float center_y = frame_h / 2.0f;
    float err_x = ((k_tracker.kf_x.p - center_x) / center_x) * 100.0f;
    float err_y = ((k_tracker.kf_y.p - center_y) / center_y) * 100.0f;

    portENTER_CRITICAL(&target_mutex);
    current_target.detected = true;
    current_target.x = (int)(k_tracker.kf_x.p - k_tracker.w / 2.0f);
    current_target.y = (int)(k_tracker.kf_y.p - k_tracker.h / 2.0f);
    current_target.w = (int)k_tracker.w;
    current_target.h = (int)k_tracker.h;
    current_target.cx = (int)k_tracker.kf_x.p;
    current_target.cy = (int)k_tracker.kf_y.p;
    current_target.error_x = err_x;
    current_target.error_y = err_y;
    current_target.confidence = lock_confidence;
    current_target.total_energy = (float)M00;
    current_target.vx = k_tracker.kf_x.v;
    current_target.vy = k_tracker.kf_y.v;
    current_target.proximity = proximity_z;
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
    current_target.proximity = 0.0f;
    portEXIT_CRITICAL(&target_mutex);
  }
}

/**
 * @brief Autonomous camera capture, vision processing, and power scaling task pinned to Core 0.
 * @param pvParameters FreeRTOS task parameter payload pointer.
 * @note Core 0 manages the camera interrupt handler, YCbCr vision pipeline, HTTP web server requests,
 *       and dynamic CPU frequency scaling (240MHz active vs 80MHz sleep).
 */
void cameraTask(void *pvParameters) {
  uint32_t last_frame_time = millis();
  g_state_timer = millis();

  // Initial power optimization windows: 3-6s active reconnaissance vs 90-180s sleep standby
  uint32_t active_duration_ms = (esp_random() % 3000) + 3000; 
  uint32_t sleep_duration_ms  = (esp_random() % 90000) + 90000; 
  bool last_wifi_ps_sleep = false;

  uint32_t last_camera_retry_ms = 0;

  while (true) {
    uint32_t now = millis();
    bool web_active = isWebOrStreamActive(now);

    // Keep Wi-Fi power save mode disabled when STA mode is connected so Web UI remains responsive
    if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
      if (last_wifi_ps_sleep) {
        WiFi.setSleep(WIFI_PS_NONE);
        last_wifi_ps_sleep = false;
      }
    }

    if (!g_camera_init_ok) {
      if (now - last_camera_retry_ms > 5000) {
        last_camera_retry_ms = now;
        if (initCamera()) {
          g_camera_init_ok = true;
          #if ENABLE_SERIAL_DEBUG
          Serial.println("[Camera_Task] Camera hardware initialized successfully!");
          #endif
        }
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (g_recon_state == STATE_ACTIVE) {
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb) {
        if (fb->len > 1024 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8) { // Verify valid JPEG SOI header
          processFrameAI(fb);

          if (now - last_frame_time > 0) {
            fps_ai = 1000.0f / (float)(now - last_frame_time);
          }
          last_frame_time = now;

          // Copy frame buffer into PSRAM stream buffer when web client is connected
          if (web_active && g_latest_jpeg_buf) {
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

      // Check if target is actively engaged by vision AI
      bool target_engaged = false;
      portENTER_CRITICAL(&target_mutex);
      target_engaged = (current_target.detected && (now - current_target.last_seen_ms < 500));
      portEXIT_CRITICAL(&target_mutex);

      // Prevent entering sleep state while web clients are streaming or target is engaged
      if (web_active || target_engaged) {
        g_state_timer = now;
        // Only reset sleep escalation on confirmed human presence (skin pixels >= 4)
        if (target_engaged && debug_skin_px >= 4) {
          g_sleep_miss_count = 0;
        }
      } else if (now - g_state_timer > active_duration_ms) {
        // Transition ACTIVE -> SLEEP_RECON:
        // 1. Put camera sensor hardware into software standby via SCCB register writes
        setCameraSleep(true);
        // 2. Drop CPU clock to 80 MHz floor (keeps Wi-Fi stack stable while reducing power draw)
        setCpuFrequencyMhz(CPU_FREQ_SLEEP);
        g_recon_state = STATE_SLEEP_RECON;
        g_state_timer = now;

        // Progressive sleep escalation:
        // Level 0 (0-2 misses):  90-180s   (1.5-3 min)
        // Level 1 (3-5 misses): 180-300s   (3-5 min)
        // Level 2 (6+ misses):  300-480s   (5-8 min)
        g_sleep_miss_count++;
        if (g_sleep_miss_count >= 6) {
          sleep_duration_ms = (esp_random() % 180000) + 300000;
        } else if (g_sleep_miss_count >= 3) {
          sleep_duration_ms = (esp_random() % 120000) + 180000;
        } else {
          sleep_duration_ms = (esp_random() % 90000) + 90000;
        }
      }
    } 
    else if (g_recon_state == STATE_SLEEP_RECON) {
      // Wake up immediately on web client activity or when sleep timer expires
      if (web_active || (now - g_state_timer > sleep_duration_ms)) {
        // Transition SLEEP_RECON -> ACTIVE:
        // 1. Scale CPU clock back up to 240 MHz for real-time computer vision
        setCpuFrequencyMhz(CPU_FREQ_ACTIVE);
        // 2. Wake camera sensor from software standby
        setCameraSleep(false);
        vTaskDelay(pdMS_TO_TICKS(30)); // Allow sensor AGC/AEC loop to stabilize

        k_tracker.init();
        portENTER_CRITICAL(&target_mutex);
        current_target.detected = false;
        current_target.confidence = 0.0f;
        current_target.error_x = 0.0f;
        current_target.error_y = 0.0f;
        current_target.vx = 0.0f;
        current_target.vy = 0.0f;
        current_target.proximity = 0.0f;
        portEXIT_CRITICAL(&target_mutex);

        g_recon_state = STATE_ACTIVE;
        g_state_timer = now;
        active_duration_ms = (esp_random() % 3000) + 3000; // 3-6s active reconnaissance window
      } else {
        vTaskDelay(pdMS_TO_TICKS(50));
      }
    }

    vTaskDelay(1);
  }
}

/* --- Embedded HTTP Web Server Handlers --- */

/**
 * @brief Serves the main HTML Web UI control dashboard page.
 */
static esp_err_t index_handler(httpd_req_t *req) {
  g_last_web_activity_ms = millis();
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, HTML_PAGE, strlen(HTML_PAGE));
}

/**
 * @brief Serves real-time telemetry, target state, and multi-object candidate data in JSON format.
 */
static esp_err_t telemetry_handler(httpd_req_t *req) {
  g_last_web_activity_ms = millis();
  char json[512];
  TrackTarget target;
  int num_cands = 0;
  int insp_idx = 0;
  ObjectCandidate cands[3];

  portENTER_CRITICAL(&target_mutex);
  target = current_target;
  num_cands = g_num_candidates;
  insp_idx = g_inspected_candidate_idx;
  for (int i = 0; i < 3; i++) cands[i] = g_object_candidates[i];
  portEXIT_CRITICAL(&target_mutex);

  snprintf(json, sizeof(json),
    "{\"detected\":%s,\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"cx\":%d,\"cy\":%d,\"err_x\":%.1f,\"err_y\":%.1f,\"conf\":%.2f,\"fps_ai\":%.1f,\"fw\":%d,\"fh\":%d,\"m00\":%.1f,\"skin_px\":%d,\"lock_conf\":%.2f,\"vx\":%.1f,\"vy\":%.1f,\"prox\":%.2f,\"num_cands\":%d,\"insp_idx\":%d,\"c0_cx\":%d,\"c0_cy\":%d,\"c0_p\":%.1f,\"c1_cx\":%d,\"c1_cy\":%d,\"c1_p\":%.1f,\"c2_cx\":%d,\"c2_cy\":%d,\"c2_p\":%.1f}",
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
    target.vx, target.vy,
    target.proximity,
    num_cands, insp_idx,
    cands[0].cx, cands[0].cy, cands[0].priority_score,
    cands[1].cx, cands[1].cy, cands[1].priority_score,
    cands[2].cx, cands[2].cy, cands[2].priority_score
  );

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json, strlen(json));
}

/**
 * @brief Streams real-time MJPEG video frames over HTTP chunked multipart response.
 */
static esp_err_t stream_handler(httpd_req_t *req) {
  esp_err_t res = ESP_OK;
  char part_buf[64];
  uint32_t last_stream_time = millis();

  if (!g_camera_init_ok) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Camera Offline");
    return ESP_OK;
  }

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  uint8_t* stream_buf = (uint8_t*)ps_malloc(64 * 1024);
  if (!stream_buf) stream_buf = (uint8_t*)malloc(64 * 1024);
  if (!stream_buf) return ESP_FAIL;

  portENTER_CRITICAL(&g_stream_mutex);
  g_stream_clients++;
  portEXIT_CRITICAL(&g_stream_mutex);
  g_last_web_activity_ms = millis();

  while (true) {
    if (!g_camera_init_ok) {
      break;
    }
    if (g_frame_sem && xSemaphoreTake(g_frame_sem, pdMS_TO_TICKS(200)) == pdTRUE) {
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
        g_last_web_activity_ms = now;
        if (now - last_stream_time > 0) {
          fps_stream = 1000.0f / (float)(now - last_stream_time);
        }
        last_stream_time = now;
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
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

static bool extract_json_value(const char* json, const char* key, char* out, size_t max_len) {
  if (!json || !key || !out || max_len == 0) return false;
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
  const char* start = strstr(json, pattern);
  if (!start) {
    snprintf(pattern, sizeof(pattern), "%s=", key);
    start = strstr(json, pattern);
    if (!start) return false;
    start += strlen(pattern);
    size_t i = 0;
    while (*start && *start != '&' && *start != ' ' && *start != '\r' && *start != '\n' && i < max_len - 1) {
      out[i++] = *start++;
    }
    out[i] = '\0';
    return true;
  }
  start += strlen(pattern);
  const char* end = strchr(start, '"');
  if (!end) return false;
  size_t len = end - start;
  if (len >= max_len) len = max_len - 1;
  strncpy(out, start, len);
  out[len] = '\0';
  return true;
}

static void restart_esp_task(void *pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(1500));
  esp_restart();
}

/**
 * @brief Serves saved Wi-Fi configuration credentials and current mode status.
 */
static esp_err_t get_wifi_handler(httpd_req_t *req) {
  g_last_web_activity_ms = millis();
  char json[320];
  snprintf(json, sizeof(json),
    "{\"sta_ssid\":\"%s\",\"sta_pass\":\"%s\",\"ap_ssid\":\"%s\",\"ap_pass\":\"%s\",\"is_ap\":%s}",
    sta_ssid, sta_password, ap_ssid, ap_password, g_is_ap_mode ? "true" : "false"
  );
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json, strlen(json));
}

/**
 * @brief Updates operational mode (AP vs STA) in NVS and triggers system reboot.
 */
static esp_err_t switch_mode_handler(httpd_req_t *req) {
  g_last_web_activity_ms = millis();
  char buf[256];
  int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (ret <= 0) return ESP_FAIL;
  buf[ret] = '\0';

  char target_mode[16] = {0};
  extract_json_value(buf, "mode", target_mode, sizeof(target_mode));

  Preferences prefs;
  prefs.begin("kore_cfg", false);
  if (strcasecmp(target_mode, "AP") == 0) {
    prefs.putString("wifi_mode", "AP");
  } else if (strcasecmp(target_mode, "STA") == 0) {
    prefs.putString("wifi_mode", "STA");
  }
  prefs.end();

  const char* resp = "{\"status\":\"ok\",\"message\":\"Mode beralih. ESP reboot...\"}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, resp, strlen(resp));

  xTaskCreate(restart_esp_task, "restart_task", 2048, NULL, 1, NULL);
  return ESP_OK;
}

/**
 * @brief Saves updated Wi-Fi credentials into NVS storage and reboots ESP32-S3.
 */
static esp_err_t save_wifi_handler(httpd_req_t *req) {
  g_last_web_activity_ms = millis();
  char buf[512];
  int ret, remaining = req->content_len;
  if (remaining >= sizeof(buf)) remaining = sizeof(buf) - 1;

  ret = httpd_req_recv(req, buf, remaining);
  if (ret <= 0) {
    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
      httpd_resp_send_408(req);
    }
    return ESP_FAIL;
  }
  buf[ret] = '\0';

  char new_sta_ssid[64] = {0};
  char new_sta_pass[64] = {0};
  char new_ap_ssid[64]  = {0};
  char new_ap_pass[64]  = {0};

  bool has_sta_s = extract_json_value(buf, "sta_ssid", new_sta_ssid, sizeof(new_sta_ssid));
  bool has_sta_p = extract_json_value(buf, "sta_pass", new_sta_pass, sizeof(new_sta_pass));
  bool has_ap_s  = extract_json_value(buf, "ap_ssid", new_ap_ssid, sizeof(new_ap_ssid));
  bool has_ap_p  = extract_json_value(buf, "ap_pass", new_ap_pass, sizeof(new_ap_pass));

  Preferences prefs;
  prefs.begin("kore_cfg", false);
  if (has_sta_s && strlen(new_sta_ssid) > 0) {
    prefs.putString("sta_ssid", new_sta_ssid);
    prefs.putString("ssid", new_sta_ssid);
    prefs.putString("wifi_mode", "STA");
    strncpy(sta_ssid, new_sta_ssid, sizeof(sta_ssid) - 1);
  }
  if (has_sta_p) {
    prefs.putString("sta_pass", new_sta_pass);
    prefs.putString("pass", new_sta_pass);
    strncpy(sta_password, new_sta_pass, sizeof(sta_password) - 1);
  }
  if (has_ap_s && strlen(new_ap_ssid) > 0) {
    prefs.putString("ap_ssid", new_ap_ssid);
    strncpy(ap_ssid, new_ap_ssid, sizeof(ap_ssid) - 1);
  }
  if (has_ap_p) {
    prefs.putString("ap_pass", new_ap_pass);
    strncpy(ap_password, new_ap_pass, sizeof(ap_password) - 1);
  }
  prefs.end();

  const char* resp = "{\"status\":\"ok\",\"message\":\"Pengaturan disimpan. ESP reboot...\"}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, resp, strlen(resp));

  xTaskCreate(restart_esp_task, "restart_task", 2048, NULL, 1, NULL);
  return ESP_OK;
}

/**
 * @brief Registers HTTP URI routes for the web dashboard (port 80) and MJPEG video stream (port 81).
 */
void startWebServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  httpd_uri_t index_uri       = { .uri = "/",                   .method = HTTP_GET,  .handler = index_handler,       .user_ctx = NULL };
  httpd_uri_t telemetry_uri   = { .uri = "/telemetry",           .method = HTTP_GET,  .handler = telemetry_handler,   .user_ctx = NULL };
  httpd_uri_t get_wifi_uri    = { .uri = "/get_wifi",            .method = HTTP_GET,  .handler = get_wifi_handler,    .user_ctx = NULL };
  httpd_uri_t save_wifi_uri   = { .uri = "/save_wifi",           .method = HTTP_POST, .handler = save_wifi_handler,   .user_ctx = NULL };
  httpd_uri_t switch_mode_uri = { .uri = "/switch_mode",         .method = HTTP_POST, .handler = switch_mode_handler, .user_ctx = NULL };
  httpd_uri_t captive_1       = { .uri = "/generate_204",        .method = HTTP_GET,  .handler = index_handler,       .user_ctx = NULL };
  httpd_uri_t captive_2       = { .uri = "/gen_204",             .method = HTTP_GET,  .handler = index_handler,       .user_ctx = NULL };
  httpd_uri_t captive_3       = { .uri = "/hotspot-detect.html", .method = HTTP_GET,  .handler = index_handler,       .user_ctx = NULL };
  httpd_uri_t captive_4       = { .uri = "/connecttest.txt",     .method = HTTP_GET,  .handler = index_handler,       .user_ctx = NULL };

  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &telemetry_uri);
    httpd_register_uri_handler(camera_httpd, &get_wifi_uri);
    httpd_register_uri_handler(camera_httpd, &save_wifi_uri);
    httpd_register_uri_handler(camera_httpd, &switch_mode_uri);
    httpd_register_uri_handler(camera_httpd, &captive_1);
    httpd_register_uri_handler(camera_httpd, &captive_2);
    httpd_register_uri_handler(camera_httpd, &captive_3);
    httpd_register_uri_handler(camera_httpd, &captive_4);
  }

  config.server_port = 81;
  config.ctrl_port   = 32769;
  httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = NULL };

  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

/**
 * @brief Configures and initializes OV2640/OV3660 camera hardware driver settings.
 * @return True if initialized successfully, false otherwise.
 */
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
  
  config.xclk_freq_hz = CAMERA_XCLK_FREQ;
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
      s->set_brightness(s, 2);       
      s->set_contrast(s, 2);         
      s->set_sharpness(s, 1);
      s->set_vflip(s, 1);
      s->set_hmirror(s, 1);
      s->set_whitebal(s, 1);         
      s->set_awb_gain(s, 1);         
      s->set_exposure_ctrl(s, 1);    
      s->set_aec2(s, 1);             
      s->set_ae_level(s, 1);         
      s->set_gain_ctrl(s, 1);        
      s->set_agc_gain(s, 15);        
      s->set_gainceiling(s, GAINCEILING_16X); 
      s->set_bpc(s, 1);             
      s->set_wpc(s, 1);             
    }
  }

  // Flush 4 initial garbage frames to allow AEC/AGC hardware pipelines to warm up
  for (int i = 0; i < 4; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(40);
  }

  return true;
}

/**
 * @brief Displays status messages on the OLED panel during system boot sequence.
 */
void showBootStatus(const char* line1, const char* line2 = nullptr) {
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(1);
  lcd.setCursor(4, 20);
  lcd.print(line1);
  if (line2) {
    lcd.setCursor(4, 36);
    lcd.print(line2);
  }
}

/**
 * @brief System initialization entrypoint. Configures clocks, NVS, memory, network, and FreeRTOS tasks.
 */
void setup() {
  #if ENABLE_SERIAL_DEBUG
  Serial.begin(115200);
  delay(1000);
  #else
  delay(500);
  #endif

  // Boot hardware at full 240 MHz clock speed
  setCpuFrequencyMhz(CPU_FREQ_ACTIVE);

  // Initialize OLED display early to present boot progress messages
  lcd.init();
  lcd.setRotation(2);
  lcd.setBrightness(OLED_BRIGHTNESS);
  showBootStatus("KoRe Starting...");

  #if ENABLE_SERIAL_DEBUG
  Serial.println("\n[KoRe] Biomechanical Face Tracker Starting...");
  #endif

  // Read saved Wi-Fi configuration from Non-Volatile Storage (NVS)
  Preferences prefs;
  prefs.begin("kore_cfg", false);
  String stored_wifi_mode = prefs.getString("wifi_mode", "AUTO");
  String stored_sta_ssid  = prefs.getString("sta_ssid", prefs.getString("ssid", ""));
  String stored_sta_pass  = prefs.getString("sta_pass", prefs.getString("pass", ""));
  String stored_ap_ssid   = prefs.getString("ap_ssid", "");
  String stored_ap_pass   = prefs.getString("ap_pass", "");

  if (stored_sta_ssid.length() > 0) {
    strncpy(sta_ssid, stored_sta_ssid.c_str(), sizeof(sta_ssid) - 1);
    strncpy(sta_password, stored_sta_pass.c_str(), sizeof(sta_password) - 1);
    #if ENABLE_SERIAL_DEBUG
    Serial.printf("[NVS] Loaded stored STA Wi-Fi credentials for SSID '%s'\n", sta_ssid);
    #endif
  } else {
    strncpy(sta_ssid, sta_ssid_default, sizeof(sta_ssid) - 1);
    strncpy(sta_password, sta_password_default, sizeof(sta_password) - 1);
    #if ENABLE_SERIAL_DEBUG
    Serial.println("[NVS] Using default STA Wi-Fi credentials.");
    #endif
  }

  if (stored_ap_ssid.length() > 0) {
    strncpy(ap_ssid, stored_ap_ssid.c_str(), sizeof(ap_ssid) - 1);
    strncpy(ap_password, stored_ap_pass.c_str(), sizeof(ap_password) - 1);
    #if ENABLE_SERIAL_DEBUG
    Serial.printf("[NVS] Loaded stored AP Wi-Fi credentials for SSID '%s'\n", ap_ssid);
    #endif
  } else {
    strncpy(ap_ssid, ap_ssid_default, sizeof(ap_ssid) - 1);
    strncpy(ap_password, ap_password_default, sizeof(ap_password) - 1);
    #if ENABLE_SERIAL_DEBUG
    Serial.println("[NVS] Using default AP Wi-Fi credentials.");
    #endif
  }
  prefs.end();

  showBootStatus("Init Camera...");
  if (!initCamera()) {
    g_camera_init_ok = false;
    showBootStatus("Camera Offline", "Web Mode Active");
    #if ENABLE_SERIAL_DEBUG
    Serial.println("WARNING: Camera initialization failed! Starting Web UI in Web-Only Mode...");
    #endif
    delay(1500);
  } else {
    g_camera_init_ok = true;
    #if ENABLE_SERIAL_DEBUG
    Serial.println("Camera initialized successfully.");
    #endif
  }

  // Allocate CV working buffers in internal DRAM for fast, zero-wait-state memory access
  small_rgb_buf     = (uint8_t*)heap_caps_malloc(80 * 60 * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  prev_lum_buf      = (uint8_t*)heap_caps_malloc(40 * 30, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  mhi_buf           = (uint8_t*)heap_caps_malloc(40 * 30, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  // Allocate JPEG HTTP streaming buffer in external PSRAM
  if (psramFound()) {
    g_latest_jpeg_buf = (uint8_t*)ps_malloc(64 * 1024);
    #if ENABLE_SERIAL_DEBUG
    Serial.println("AI buffers allocated in Internal SRAM, Stream buffer in PSRAM.");
    #endif
  } else {
    g_latest_jpeg_buf = (uint8_t*)malloc(64 * 1024);
    #if ENABLE_SERIAL_DEBUG
    Serial.println("All buffers allocated in SRAM.");
    #endif
  }

  if (!small_rgb_buf) small_rgb_buf = (uint8_t*)malloc(80 * 60 * 2);
  if (!prev_lum_buf)  prev_lum_buf  = (uint8_t*)malloc(40 * 30);
  if (!mhi_buf)       mhi_buf       = (uint8_t*)malloc(40 * 30);

  g_frame_sem = xSemaphoreCreateBinary();

  if (prev_lum_buf) memset(prev_lum_buf, 0, 40 * 30);
  if (mhi_buf) memset(mhi_buf, 0, 40 * 30);

  bool force_ap = (stored_wifi_mode == "AP");
  bool connected = false;

  if (!force_ap && strlen(sta_ssid) > 0) {
    WiFi.mode(WIFI_STA);
    delay(100);
    WiFi.disconnect(true);
    delay(150);
    WiFi.setSleep(WIFI_PS_NONE);          // Disable Wi-Fi sleep to keep HTTP web server responsive
    WiFi.setAutoReconnect(true);
    WiFi.setTxPower(WIFI_TX_POWER);       // Max radio TX power for range stability

    char wifi_msg[40];
    snprintf(wifi_msg, sizeof(wifi_msg), "WiFi: %s", sta_ssid);
    showBootStatus(wifi_msg, "Connecting...");

    #if ENABLE_SERIAL_DEBUG
    Serial.printf("\nConnecting to WiFi '%s'...", sta_ssid);
    #endif

    if (strlen(sta_password) > 0) {
      WiFi.begin(sta_ssid, sta_password);
    } else {
      WiFi.begin(sta_ssid);
    }

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 50) {
      delay(300);
      #if ENABLE_SERIAL_DEBUG
      Serial.print(".");
      #endif
      if (attempts % 4 == 0) {
        char dots[16] = "Connecting";
        int d = (attempts / 4) % 3;
        for (int i = 0; i <= d; i++) strcat(dots, ".");
        showBootStatus(wifi_msg, dots);
      }
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
    }
  }

  if (connected) {
    g_is_ap_mode = false;
    if (MDNS.begin("kore")) {
      MDNS.addService("http", "tcp", 80);
    }
    char sta_ip_buf[40];
    snprintf(sta_ip_buf, sizeof(sta_ip_buf), "IP: %s", WiFi.localIP().toString().c_str());
    showBootStatus("WiFi Connected!", sta_ip_buf);
    #if ENABLE_SERIAL_DEBUG
    Serial.println(" Connected to WiFi!");
    Serial.print("Web UI: http://");
    Serial.println(WiFi.localIP());
    #endif
    delay(1500);
  } else {
    // Fall back to Access Point (AP) mode if STA connection fails or AP mode is forced
    #if ENABLE_SERIAL_DEBUG
    if (force_ap) {
      Serial.println("\n[WiFi] Forced AP Mode active.");
    } else {
      Serial.printf("\n[WiFi] Could not connect to '%s'. Entering AP Fallback Mode...\n", sta_ssid);
    }
    #endif
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.setTxPower(WIFI_TX_POWER);

    if (strlen(ap_password) > 0 && strlen(ap_password) < 8) {
      WiFi.softAP(ap_ssid, NULL); // Fall back to open AP if password length < 8 chars
    } else if (strlen(ap_password) == 0) {
      WiFi.softAP(ap_ssid, NULL);
    } else {
      WiFi.softAP(ap_ssid, ap_password);
    }

    dnsServer.start(53, "*", WiFi.softAPIP());
    g_is_ap_mode = true;

    xTaskCreate([](void* arg) {
      while (true) {
        if (g_is_ap_mode) {
          dnsServer.processNextRequest();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }, "AP_DNS_Task", 2048, NULL, 1, NULL);

    char ap_ip_buf[40];
    if (!force_ap && strlen(sta_ssid) > 0) {
      showBootStatus("STA Fail -> AP Mode", WiFi.softAPIP().toString().c_str());
    } else {
      snprintf(ap_ip_buf, sizeof(ap_ip_buf), "AP IP: %s", WiFi.softAPIP().toString().c_str());
      showBootStatus("AP Mode Active", ap_ip_buf);
    }
    #if ENABLE_SERIAL_DEBUG
    Serial.println("AP Mode active!");
    Serial.print("Web UI: http://");
    Serial.println(WiFi.softAPIP());
    #endif
    delay(1500);
  }

  startWebServer();
  #if ENABLE_SERIAL_DEBUG
  Serial.println("Web server active.");
  #endif

  // Pin vision processing task to Core 0 (priority 2)
  xTaskCreatePinnedToCore(
    cameraTask,
    "Camera_AI_Task",
    8192,
    NULL,
    2,
    NULL,
    0
  );

  // Pin OLED animation task to Core 1 (priority 1)
  xTaskCreatePinnedToCore(
    oledTask,
    "OLED_Task",
    8192,
    NULL,
    1,
    NULL,
    1
  );
  #if ENABLE_SERIAL_DEBUG
  Serial.println("All tasks started.");
  #endif
}

/**
 * @brief Idle main loop. Yields execution to background FreeRTOS scheduler tasks.
 */
void loop() {
  vTaskDelay(pdMS_TO_TICKS(5000));
}
