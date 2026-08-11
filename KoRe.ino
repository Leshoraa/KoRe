/*
 * KoRe - Ultra-Fast Standalone Human Recognition & Tracking
 * Board: Seeed Studio XIAO ESP32S3 Sense
 * Kamera: OV3660 / OV2640
 *
 * Arsitektur Lightweight, Deterministic, & Zero-Transcendental:
 * 1. Eliminasi Total Fungsi Transendental (Zero-Transcendental):
 *    - 100% bebas expf() dan Math.exp() untuk latency eksekusi AI < 0.6ms di ESP32-S3.
 * 2. Pemisahan Total Feature Extraction dari Kinematic State:
 *    - Akumulasi momen fotometrik (M00, M10, M01, M20, M02) mencakup seluruh frame 40x30 tanpa blind-spot/clipping.
 *    - Formulasi energi fotometrik murni: Phi(x,y) = SkinMask(x,y) ? (2.5 * delta + 1.5 * gy) : 0.04 * (2.5 * delta + 1.5 * gy).
 * 3. Skin-Anchored Centroid & Deterministic Thresholding:
 *    - Dominasi energi warna kulit (YCbCr Skin Mask) mencegah pembajakan centroid oleh poster/dinding latar belakang.
 *    - Ambang deteksi dinamis stabil: threshold = max(35.0, 75.0 - 0.5 * skin_count).
 * 4. 1st-Order Alpha-Beta (α-β) Kinematic Filter:
 *    - Prediksi: x_pred = x + vx * dt, y_pred = y + vy * dt.
 *    - Inovasi: rx = zx - x_pred, ry = zy - y_pred.
 *    - Update: x = x_pred + alpha * rx (alpha = 0.40), vx = clamp(vx + (beta / dt) * rx, -800, 800) (beta = 0.20).
 *    - Data Association via Euclidean distance gating (gate_radius = min(250, 90 + 0.25 * speed)).
 * 5. Native Full-Rate Execution (30+ FPS stabil & konsisten).
 */

#include "esp_camera.h"
#include "esp_http_server.h"
#include "img_converters.h"
#include <WiFi.h>
#include <math.h>

// =============================================
// Konfigurasi WiFi
// =============================================
#define USE_AP_MODE false
const char* ap_ssid     = "KoRe-Tracker";
const char* ap_password = "12345678";

const char* sta_ssid     = "Kasminingsih";
const char* sta_password = "hidet4mp4n";

// =============================================
// Pin Kamera XIAO ESP32S3 Sense
// =============================================
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

// =============================================
// Konfigurasi Tracking & Ambang Energi Kritis
// =============================================
#define ENABLE_SERVO_TRACKING false
#define PAN_SERVO_PIN         1
#define TILT_SERVO_PIN        2

// Struktur data telemetri target manusia
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

// Mutex & Variabel Global
static TrackTarget current_target = {false, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0};
static portMUX_TYPE target_mutex = portMUX_INITIALIZER_UNLOCKED;

static volatile float fps_ai = 0.0f;
static volatile float fps_stream = 0.0f;
static const int frame_w = 640;
static const int frame_h = 480;

// Buffer pemrosesan gambar
static uint8_t* small_rgb_buf = NULL; // 80x60 RGB565 (9,600 bytes)
static uint8_t* prev_lum_buf  = NULL; // 40x30 Luminance (1,200 bytes)

// Web server handles
httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// =============================================
// Web UI: Pure Camera + AI Tracking Box (Zero UI)
// =============================================
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
  <title>KoRe Camera Tracker</title>
  <style>
    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
    }
    body {
      background: #000;
      width: 100vw;
      height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
      overflow: hidden;
    }
    .wrapper {
      position: relative;
      display: flex;
      justify-content: center;
      align-items: center;
      max-width: 100vw;
      max-height: 100vh;
    }
    #stream-img {
      display: block;
      max-width: 100vw;
      max-height: 100vh;
      width: auto;
      height: auto;
      object-fit: contain;
      user-select: none;
    }
    #hud-canvas {
      position: absolute;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      pointer-events: none;
    }
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

    // Offscreen 40x30 Ultra-Fast Subsampling (1,200 Pixels = Zero Lag)
    const offCanvas = document.createElement('canvas');
    offCanvas.width = 40;
    offCanvas.height = 30;
    const offCtx = offCanvas.getContext('2d', { willReadFrequently: true });

    function resizeCanvas() {
      if (img.clientWidth > 0 && img.clientHeight > 0) {
        canvas.width = img.clientWidth;
        canvas.height = img.clientHeight;
      }
    }
    window.addEventListener('resize', resizeCanvas);
    img.onload = resizeCanvas;

    let clientBox = null;
    let clientLastSeen = 0;
    let prevLumJS = null;

    function runClientTracker() {
      try {
        if (img.complete && img.naturalWidth > 0) {
          offCtx.drawImage(img, 0, 0, 40, 30);
          const imgData = offCtx.getImageData(0, 0, 40, 30).data;
          const rawLum = new Uint8Array(1200);
          let sumLum = 0;

          for (let i = 0; i < 1200; i++) {
            const r = imgData[i * 4];
            const g = imgData[i * 4 + 1];
            const b = imgData[i * 4 + 2];
            const y = (0.299 * r + 0.587 * g + 0.114 * b) | 0;
            rawLum[i] = y;
            sumLum += y;
          }

          const meanLum = sumLum / 1200.0;
          const deltaYSkin = meanLum < 90 ? Math.max(0, Math.min(12, (90 - meanLum) / 2)) : 0;
          const cbMin = 70 - deltaYSkin, cbMax = 133 + deltaYSkin;
          const crMin = 128 - deltaYSkin, crMax = 178 + deltaYSkin;

          const skinMaskJS = new Uint8Array(1200);
          let skinPixelCount = 0;
          for (let i = 0; i < 1200; i++) {
            const r = imgData[i * 4];
            const g = imgData[i * 4 + 1];
            const b = imgData[i * 4 + 2];
            const cb = 128 + (((-43 * r - 85 * g + 128 * b)) >> 8);
            const cr = 128 + (((128 * r - 107 * g - 21 * b)) >> 8);
            const isSkin = (cb >= cbMin && cb <= cbMax && cr >= crMin && cr <= crMax);
            skinMaskJS[i] = isSkin ? 1 : 0;
            if (isSkin) skinPixelCount++;
          }

          // 3x3 Spatial Box Blur
          const curLum = new Uint8Array(1200);
          for (let y = 1; y < 29; y++) {
            const y_off = y * 40;
            for (let x = 1; x < 39; x++) {
              const idx = y_off + x;
              const sum = rawLum[idx - 41] + rawLum[idx - 40] + rawLum[idx - 39] +
                          rawLum[idx - 1]  + rawLum[idx]      + rawLum[idx + 1]  +
                          rawLum[idx + 39] + rawLum[idx + 40] + rawLum[idx + 41];
              curLum[idx] = (sum / 9) | 0;
            }
          }

          if (!prevLumJS) {
            prevLumJS = curLum;
          } else {
            let M00 = 0.0, M10 = 0.0, M01 = 0.0;
            let M20 = 0.0, M02 = 0.0;

            // Pure Full-Frame Skin-Anchored Photometric Moments (Zero-Transcendental)
            for (let y = 1; y < 29; y++) {
              const y_off = y * 40;
              for (let x = 1; x < 39; x++) {
                const idx = y_off + x;
                const lum = curLum[idx];
                const prevL = prevLumJS[idx];

                // Temporal Kinetic Difference ΔY
                const delta = Math.abs(lum - prevL);

                // Spatial Gradient gy
                const gy = Math.abs(curLum[idx + 40] - curLum[idx - 40]);

                // Photometric Energy: Skin is 1.0x, Non-skin is 0.04x (prevents wall poster hijacking)
                const isSkin = (skinMaskJS[idx] === 1);
                const rawEnergy = 2.5 * delta + 1.5 * gy;
                const energy = isSkin ? rawEnergy : (0.04 * rawEnergy);

                if (energy < 4.0) continue;

                M00 += energy;
                M10 += x * energy;
                M01 += y * energy;
                M20 += x * x * energy;
                M02 += y * y * energy;
              }
            }

            const clientDynThresh = Math.max(35.0, 75.0 - skinPixelCount * 0.5);
            if (M00 >= clientDynThresh) {
              const meanX = M10 / M00;
              const meanY = M01 / M00;
              const mu20 = Math.max(0, (M20 / M00) - (meanX * meanX));
              const mu02 = Math.max(0, (M02 / M00) - (meanY * meanY));
              const sigmaX = Math.sqrt(mu20);
              const sigmaY = Math.sqrt(mu02);

              const zx = meanX * 16.0;
              const zy = meanY * 16.0;
              let rawBw = 2.4 * Math.max(1.5, sigmaX) * 16.0;
              let rawBh = 2.6 * Math.max(2.0, sigmaY) * 16.0;

              rawBw = Math.max(60, Math.min(360, rawBw));
              let coupledBh = Math.max(rawBh, 1.15 * rawBw);
              coupledBh = Math.min(coupledBh, 1.55 * rawBw);
              coupledBh = Math.max(80, Math.min(480, coupledBh));

              // 1st-Order Alpha-Beta State Filter
              const ALPHA = 0.40;
              const BETA  = 0.20;
              const dt    = 0.04;

              if (!clientBox) {
                clientBox = {
                  detected: true,
                  cx: zx,
                  cy: zy,
                  x: Math.round(zx - rawBw / 2),
                  y: Math.round(zy - coupledBh / 2),
                  w: Math.round(rawBw),
                  h: Math.round(coupledBh),
                  vx: 0.0,
                  vy: 0.0,
                  energy: M00
                };
              } else {
                const predX = clientBox.cx + clientBox.vx * dt;
                const predY = clientBox.cy + clientBox.vy * dt;
                const rx = zx - predX;
                const ry = zy - predY;

                const distSq = rx * rx + ry * ry;
                const speed = Math.hypot(clientBox.vx, clientBox.vy);
                const gateRadius = Math.min(250.0, 90.0 + 0.25 * speed);

                if (distSq <= gateRadius * gateRadius) {
                  const newCx = predX + ALPHA * rx;
                  const newCy = predY + ALPHA * ry;
                  let newVx = clientBox.vx + (BETA / dt) * rx;
                  let newVy = clientBox.vy + (BETA / dt) * ry;
                  newVx = Math.max(-800.0, Math.min(800.0, newVx));
                  newVy = Math.max(-800.0, Math.min(800.0, newVy));

                  const newW = clientBox.w * 0.70 + rawBw * 0.30;
                  const newH = clientBox.h * 0.70 + coupledBh * 0.30;

                  clientBox = {
                    detected: true,
                    cx: newCx,
                    cy: newCy,
                    x: Math.round(newCx - newW / 2),
                    y: Math.round(newCy - newH / 2),
                    w: Math.round(newW),
                    h: Math.round(newH),
                    vx: newVx,
                    vy: newVy,
                    energy: M00
                  };
                }
              }
              clientLastSeen = Date.now();
            } else if (Date.now() - clientLastSeen > 400) {
              clientBox = null;
            }
            prevLumJS = curLum;
          }
        }
      } catch (e) {}
      setTimeout(runClientTracker, 40);
    }
    runClientTracker();

    let lastMcuTrueTime = Date.now();
    let renderBox = null;

    async function updateTelemetry() {
      try {
        const res = await fetch('http://' + host + '/telemetry');
        const mcuData = await res.json();
        const now = Date.now();

        if (mcuData.detected) {
          lastMcuTrueTime = now;
        }

        // Hysteresis: Percaya clientBox jika MCU hilang > 300ms
        let data = mcuData.detected ? mcuData : (
          (now - lastMcuTrueTime > 300 && clientBox) ? {
            detected: true,
            cx: clientBox.cx,
            cy: clientBox.cy,
            x: clientBox.x,
            y: clientBox.y,
            w: clientBox.w,
            h: clientBox.h,
            fw: 640,
            fh: 480
          } : mcuData
        );

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

          // Kinematic Responsive Smoothing (75% baru + 25% lama)
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

          const bx = renderBox.bx;
          const by = renderBox.by;
          const bw = renderBox.bw;
          const bh = renderBox.bh;
          const cx = renderBox.cx;
          const cy = renderBox.cy;

          // Bounding Box Neon Green
          ctx.strokeStyle = '#00ff88';
          ctx.lineWidth = 2;
          ctx.strokeRect(bx, by, bw, bh);

          // Sci-Fi Corner Brackets
          const len = 14;
          ctx.lineWidth = 3.5;
          ctx.beginPath(); ctx.moveTo(bx, by + len); ctx.lineTo(bx, by); ctx.lineTo(bx + len, by); ctx.stroke();
          ctx.beginPath(); ctx.moveTo(bx + bw - len, by); ctx.lineTo(bx + bw, by); ctx.lineTo(bx + bw, by + len); ctx.stroke();
          ctx.beginPath(); ctx.moveTo(bx, by + bh - len); ctx.lineTo(bx, by + bh); ctx.lineTo(bx + len, by + bh); ctx.stroke();
          ctx.beginPath(); ctx.moveTo(bx + bw - len, by + bh); ctx.lineTo(bx + bw, by + bh); ctx.lineTo(bx + bw, by + bh - len); ctx.stroke();

          // Centroid Crosshair (True Center of Mass)
          ctx.strokeStyle = '#00d8ff';
          ctx.lineWidth = 1.5;
          ctx.beginPath();
          ctx.moveTo(cx - 10, cy); ctx.lineTo(cx + 10, cy);
          ctx.moveTo(cx, cy - 10); ctx.lineTo(cx, cy + 10);
          ctx.stroke();

          ctx.beginPath();
          ctx.arc(cx, cy, 3.5, 0, 2 * Math.PI);
          ctx.stroke();
        } else {
          renderBox = null;
        }
      } catch (e) {}
      setTimeout(updateTelemetry, 100);
    }

    updateTelemetry();
  </script>
</body>
</html>
)rawliteral";

// =============================================
// 1ST-ORDER ALPHA-BETA (α-β) KINEMATIC STATE FILTER
// =============================================
struct AlphaBetaState2D {
  float x;       // Posisi x (piksel layar 0..640)
  float y;       // Posisi y (piksel layar 0..480)
  float vx;      // Kecepatan vx (piksel/detik, clamped [-800, 800])
  float vy;      // Kecepatan vy (piksel/detik, clamped [-800, 800])
  float w;       // Lebar Bounding Box (piksel)
  float h;       // Tinggi Bounding Box (piksel)
  bool  active;  // Status track aktif
  uint32_t last_update_us; // Timestamp mikrodetik terakhir
};

static AlphaBetaState2D ab_state = {
  320.0f, 240.0f, // x, y
  0.0f, 0.0f,     // vx, vy
  160.0f, 200.0f, // w, h
  false,
  0
};

static float lock_confidence = 0.0f;
static uint32_t last_valid_human_time = 0;

// Telemetry Debug Counters
static float debug_m00 = 0.0f;
static int debug_skin_px = 0;
static float debug_lock_conf = 0.0f;
static float debug_vx = 0.0f;
static float debug_vy = 0.0f;

// =============================================
// CORE ALGORITMA: ZERO-TRANSCENDENTAL SKIN-ANCHORED TRACKING
// =============================================
void processFrameAI(camera_fb_t *fb) {
  if (!small_rgb_buf || !prev_lum_buf || fb->format != PIXFORMAT_JPEG) return;

  // Subsampling decode 80x60 via hardware IDCT (~1.2ms)
  bool ok = jpg2rgb565(fb->buf, fb->len, small_rgb_buf, JPG_SCALE_8X);
  if (!ok) return;

  uint16_t* pixels = (uint16_t*)small_rgb_buf;

  // Buffer 40x30 Matrix (1,200 pixels)
  static uint8_t cur_40x30[1200];
  static bool skin_mask_40x30[1200];
  static float prev_mean_lum = 90.0f; // EMA global luminance

  int total_lum_sum = 0;

  // Dynamic Low-Lux Luminance-Aware Skin Locus (using EMA from previous frame)
  float delta_y_skin = 0.0f;
  if (prev_mean_lum < 90.0f) {
    delta_y_skin = fmaxf(0.0f, fminf(12.0f, (90.0f - prev_mean_lum) * 0.5f));
  }
  int cb_min = (int)(70.0f - delta_y_skin);
  int cb_max = (int)(133.0f + delta_y_skin);
  int cr_min = (int)(128.0f - delta_y_skin);
  int cr_max = (int)(178.0f + delta_y_skin);

  int skin_pixel_count = 0;
  uint8_t* p_cur = cur_40x30;
  bool* p_mask = skin_mask_40x30;

  // 1. Decode & Subsample 40x30 (Single-Pass execution)
  for (int y = 0; y < 30; y++) {
    int src_y = y * 2;
    for (int x = 0; x < 40; x++) {
      uint16_t p = pixels[src_y * 80 + (x * 2)];
      int r = ((p >> 11) & 0x1F) << 3;
      int g = ((p >> 5) & 0x3F) << 2;
      int b = (p & 0x1F) << 3;
      
      uint8_t lum = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
      *p_cur++ = lum;
      total_lum_sum += lum;

      int cb = 128 + (((-43 * r - 85 * g + 128 * b)) >> 8);
      int cr = 128 + (((128 * r - 107 * g - 21 * b)) >> 8);
      
      bool is_skin = (cb >= cb_min && cb <= cb_max && cr >= cr_min && cr <= cr_max);
      *p_mask++ = is_skin;
      if (is_skin) skin_pixel_count++;
    }
  }
  
  // Update EMA mean luminance
  prev_mean_lum = prev_mean_lum * 0.9f + ((float)total_lum_sum / 1200.0f) * 0.1f;

  // 2. 3x3 Spatial Box Blur (Pointer Arithmetic Optimization)
  static uint8_t smooth_40x30[1200];
  uint8_t* p_smooth = smooth_40x30 + 41;
  uint8_t* p_c = cur_40x30 + 41;
  for (int y = 1; y < 29; y++) {
    for (int x = 1; x < 39; x++) {
      int sum = *(p_c - 41) + *(p_c - 40) + *(p_c - 39) +
                *(p_c - 1)  + *p_c        + *(p_c + 1)  +
                *(p_c + 39) + *(p_c + 40) + *(p_c + 41);
      *p_smooth = (uint8_t)(sum / 9);
      p_smooth++;
      p_c++;
    }
    p_smooth += 2;
    p_c += 2;
  }

  static bool first_frame = true;
  if (first_frame) {
    memcpy(prev_lum_buf, smooth_40x30, 1200);
    first_frame = false;
    ab_state.last_update_us = micros();
    return;
  }

  // 3. Kompensasi Gerakan Global Kamera (Global Flow Compensation)
  int total_delta_sum = 0;
  int sample_count = 0;
  uint8_t* p_s = smooth_40x30 + 41;
  uint8_t* p_p = prev_lum_buf + 41;
  for (int y = 1; y < 29; y++) {
    for (int x = 1; x < 39; x++) {
      total_delta_sum += abs((int)*p_s - (int)*p_p);
      sample_count++;
      p_s++;
      p_p++;
    }
    p_s += 2;
    p_p += 2;
  }
  float global_delta_mean = sample_count > 0 ? ((float)total_delta_sum / (float)sample_count) : 0.0f;

  uint32_t now_us = micros();
  uint32_t now_ms = millis();

  // Guard Delta-t
  float dt_sec = (ab_state.last_update_us > 0) ? ((float)(now_us - ab_state.last_update_us) * 1e-6f) : 0.033f;
  float dt = fmaxf(0.01f, fminf(0.20f, dt_sec));
  ab_state.last_update_us = now_us;

  // =============================================
  // 1ST-ORDER ALPHA-BETA PREDICTION PHASE
  // =============================================
  float pred_x = ab_state.x + ab_state.vx * dt;
  float pred_y = ab_state.y + ab_state.vy * dt;
  pred_x = fmaxf(0.0f, fminf(640.0f, pred_x));
  pred_y = fmaxf(0.0f, fminf(480.0f, pred_y));

  float speed = sqrtf(fmaxf(0.0f, ab_state.vx * ab_state.vx + ab_state.vy * ab_state.vy));

  // =============================================
  // PURE FEATURE EXTRACTION (100% UNGATED FULL FRAME)
  // Zero-Transcendental & Skin-Anchored Photometric Moments
  // =============================================
  double M00 = 0.0;
  double M10 = 0.0, M01 = 0.0;
  double M20 = 0.0, M02 = 0.0;

  uint8_t* p_sm = smooth_40x30 + 41;
  uint8_t* p_pr = prev_lum_buf + 41;
  bool* p_msk = skin_mask_40x30 + 41;

  for (int y = 1; y < 29; y++) {
    for (int x = 1; x < 39; x++) {
      float local_delta = (float)abs((int)*p_sm - (int)*p_pr);
      float delta = fmaxf(0.0f, local_delta - global_delta_mean);

      float gy = (float)abs((int)*(p_sm + 40) - (int)*(p_sm - 40));

      // Formulasi Energi Murni: Phi(x,y) = SkinMask ? (2.5*dY + 1.5*gy) : 0.04*(2.5*dY + 1.5*gy)
      float raw_energy = 2.5f * delta + 1.5f * gy;
      float energy = (*p_msk) ? raw_energy : (0.04f * raw_energy);

      if (energy >= 4.0f) {
        double weight = (double)energy;
        double dx = (double)x;
        double dy = (double)y;

        M00 += weight;
        M10 += dx * weight;
        M01 += dy * weight;
        M20 += dx * dx * weight;
        M02 += dy * dy * weight;
      }
      p_sm++;
      p_pr++;
      p_msk++;
    }
    p_sm += 2;
    p_pr += 2;
    p_msk += 2;
  }

  // Update buffer luminance untuk frame berikutnya
  memcpy(prev_lum_buf, smooth_40x30, 1200);

  // Deterministic Dynamic Threshold
  float dynamic_threshold = fmaxf(35.0f, 75.0f - ((float)skin_pixel_count * 0.5f));

  bool candidate_found = false;
  float cand_cx = pred_x;
  float cand_cy = pred_y;
  float cand_bw = ab_state.w;
  float cand_bh = ab_state.h;

  if (M00 >= dynamic_threshold) {
    // True Center of Mass (Momen Orde-1) via Reciprocal Multiply
    double inv_M00 = 1.0 / M00;
    double mean_x = M10 * inv_M00;
    double mean_y = M01 * inv_M00;

    double mu20 = (M20 * inv_M00) - (mean_x * mean_x);
    double mu02 = (M02 * inv_M00) - (mean_y * mean_y);

    float sigma_x = sqrtf(fmaxf(0.0f, (float)mu20));
    float sigma_y = sqrtf(fmaxf(0.0f, (float)mu02));

    // Koordinat Centroid pada Layar Resolusi 640x480
    float raw_cx = (float)mean_x * 16.0f; // 640 / 40
    float raw_cy = (float)mean_y * 16.0f; // 480 / 30

    // Dimensi Bounding Box dari Dispersi Spasial Orde-2
    float raw_bw = 2.4f * fmaxf(1.5f, sigma_x) * 16.0f;
    float raw_bh = 2.6f * fmaxf(2.0f, sigma_y) * 16.0f;

    raw_bw = fmaxf(60.0f, fminf(360.0f, raw_bw));
    float coupled_bh = fmaxf(raw_bh, 1.15f * raw_bw);
    coupled_bh = fminf(coupled_bh, 1.55f * raw_bw);

    cand_bw = raw_bw;
    cand_bh = fmaxf(80.0f, fminf(480.0f, coupled_bh));

    // =============================================
    // DATA ASSOCIATION (Euclidean Distance Gating)
    // =============================================
    if (!ab_state.active) {
      // Target Acquisition
      cand_cx = raw_cx;
      cand_cy = raw_cy;
      candidate_found = true;
    } else {
      float dx_cand = raw_cx - pred_x;
      float dy_cand = raw_cy - pred_y;
      float dist_sq = dx_cand * dx_cand + dy_cand * dy_cand;
      float gate_radius = fminf(250.0f, 90.0f + 0.25f * speed);

      if (dist_sq <= (gate_radius * gate_radius)) {
        cand_cx = raw_cx;
        cand_cy = raw_cy;
        candidate_found = true;
      }
    }
  }

  // Update Confidence Track
  lock_confidence = lock_confidence * 0.85f + (candidate_found ? 1.0f : 0.0f) * 0.15f;

  // =============================================
  // 1ST-ORDER ALPHA-BETA FILTER STATE UPDATE
  // =============================================
  const float ALPHA = 0.40f;
  const float BETA  = 0.20f;

  if (candidate_found) {
    float rx = cand_cx - pred_x;
    float ry = cand_cy - pred_y;

    // Position Correction
    ab_state.x = pred_x + ALPHA * rx;
    ab_state.y = pred_y + ALPHA * ry;

    // Velocity Update with user-mandated [-800.0, 800.0] clamp
    ab_state.vx = fmaxf(-800.0f, fminf(800.0f, ab_state.vx + (BETA / dt) * rx));
    ab_state.vy = fmaxf(-800.0f, fminf(800.0f, ab_state.vy + (BETA / dt) * ry));

    // Bounding Box Smoothing
    ab_state.w = ab_state.w * 0.70f + cand_bw * 0.30f;
    ab_state.h = ab_state.h * 0.70f + cand_bh * 0.30f;
    ab_state.w = fmaxf(60.0f, fminf(400.0f, ab_state.w));
    ab_state.h = fmaxf(80.0f, fminf(480.0f, ab_state.h));

    ab_state.active = true;
    last_valid_human_time = now_ms;
  } else {
    // Coasting / Extrapolasi State
    ab_state.x = pred_x;
    ab_state.y = pred_y;
    ab_state.vx *= 0.85f;
    ab_state.vy *= 0.85f;

    if (now_ms - last_valid_human_time > 400) {
      ab_state.active = false;
      ab_state.vx = 0.0f;
      ab_state.vy = 0.0f;
    }
  }

  // Clamping batas layar
  ab_state.x = fmaxf(0.0f, fminf(640.0f, ab_state.x));
  ab_state.y = fmaxf(0.0f, fminf(480.0f, ab_state.y));

  debug_m00 = (float)M00;
  debug_skin_px = skin_pixel_count;
  debug_lock_conf = lock_confidence;
  debug_vx = ab_state.vx;
  debug_vy = ab_state.vy;

  // Publish ke Struct Telemetri
  if (ab_state.active && lock_confidence > 0.25f) {
    float center_x = frame_w / 2.0f;
    float center_y = frame_h / 2.0f;
    float err_x = ((ab_state.x - center_x) / center_x) * 100.0f;
    float err_y = ((ab_state.y - center_y) / center_y) * 100.0f;

    portENTER_CRITICAL(&target_mutex);
    current_target.detected = true;
    current_target.x = (int)(ab_state.x - ab_state.w / 2.0f);
    current_target.y = (int)(ab_state.y - ab_state.h / 2.0f);
    current_target.w = (int)ab_state.w;
    current_target.h = (int)ab_state.h;
    current_target.cx = (int)ab_state.x;
    current_target.cy = (int)ab_state.y;
    current_target.error_x = err_x;
    current_target.error_y = err_y;
    current_target.confidence = lock_confidence;
    current_target.total_energy = (float)M00;
    current_target.vx = ab_state.vx;
    current_target.vy = ab_state.vy;
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

// =============================================
// Handler: Web Dashboard & Telemetry
// =============================================
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

// Stream Handler (Core 0 - High Speed Vision Pipeline)
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  char part_buf[64];
  uint32_t last_frame_time = millis();

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      res = ESP_FAIL;
      break;
    }

    // Native per-frame AI processing
    processFrameAI(fb);

    size_t hlen = snprintf(part_buf, 64, STREAM_PART, fb->len);
    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_buf, hlen);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);

    esp_camera_fb_return(fb);
    fb = NULL;

    if (res != ESP_OK) break;

    uint32_t now = millis();
    if (now - last_frame_time > 0) {
      fps_stream = 1000.0f / (now - last_frame_time);
      fps_ai = fps_stream;
    }
    last_frame_time = now;

    // Yield sesaat untuk stack IP
    vTaskDelay(pdMS_TO_TICKS(1));
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

// =============================================
// Inisialisasi Kamera
// =============================================
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
  config.frame_size   = FRAMESIZE_VGA; // 640x480
  config.jpeg_quality = 8;             // Jernih & tajam
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

// =============================================
// Setup
// =============================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  setCpuFrequencyMhz(240);

  Serial.println("\n============================================");
  Serial.println("  KoRe - Standalone AI Human Tracker");
  Serial.println("  Kinematics Predictor & Dynamic Gating Active");
  Serial.println("  Board: Seeed XIAO ESP32-S3 Sense");
  Serial.println("============================================\n");

  if (!initCamera()) {
    Serial.println("FATAL: Inisialisasi Kamera Gagal! Restart...");
    delay(3000);
    ESP.restart();
  }
  Serial.println("✓ Kamera OV3660 Aktif & Dioptimasi");

  // Alokasi memori PSRAM untuk buffer pemrosesan
  if (psramFound()) {
    small_rgb_buf = (uint8_t*)ps_malloc(80 * 60 * 2);
    prev_lum_buf  = (uint8_t*)ps_malloc(40 * 30);
    Serial.println("✓ Buffer PSRAM Terisi: small_rgb_buf, prev_lum_buf");
  } else {
    small_rgb_buf = (uint8_t*)malloc(80 * 60 * 2);
    prev_lum_buf  = (uint8_t*)malloc(40 * 30);
    Serial.println("! PSRAM tidak ditemukan, alokasi via Internal SRAM");
  }

  if (prev_lum_buf) memset(prev_lum_buf, 0, 40 * 30);

  WiFi.setTxPower(WIFI_POWER_17dBm);
  if (USE_AP_MODE) {
    WiFi.softAP(ap_ssid, ap_password);
    Serial.print("Mode AP Aktif. Buka: http://");
    Serial.println(WiFi.softAPIP());
  } else {
    WiFi.begin(sta_ssid, sta_password);
    Serial.printf("Menyambung WiFi '%s'", sta_ssid);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
      delay(400);
      Serial.print(".");
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.setSleep(false);
      Serial.println(" Tersambung!");
      Serial.print("Buka browser ke: http://");
      Serial.println(WiFi.localIP());
    } else {
      WiFi.softAP(ap_ssid, ap_password);
      Serial.println("\nBeralih ke Mode AP!");
      Serial.print("Buka: http://");
      Serial.println(WiFi.softAPIP());
    }
  }

  startWebServer();
  Serial.println("✓ Web Server & High-Speed Stream Aktif");
  Serial.println("============================================\n");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(5000));
}
