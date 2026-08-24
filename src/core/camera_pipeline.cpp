/**
 * @file camera_pipeline.cpp
 * @brief High-speed YCbCr vision pipeline, spatial clustering, and FreeRTOS Core 0 camera task implementation.
 */

#include "camera_pipeline.h"
#include "include/kore_config.h"
#include "include/kore_types.h"
#include "include/kore_kalman.h"
#include "esp_heap_caps.h"
#include "img_converters.h"
#include <WiFi.h>
#include <math.h>
#include <Arduino.h>
#include <esp_random.h>

/* Global Synchronization State */
portMUX_TYPE g_target_mutex = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE g_stream_mutex = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE g_weather_mutex = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t g_frame_sem = NULL;

TrackTarget g_current_target = {false, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0};
ObjectCandidate g_object_candidates[MAX_OBJECT_CANDIDATES] = {0};
int g_num_candidates = 0;
int g_inspected_candidate_idx = 0;
volatile ReconState g_recon_state = STATE_ACTIVE;
volatile float g_fps_ai = 0.0f;
volatile float g_fps_stream = 0.0f;
volatile int g_stream_clients = 0;
volatile uint32_t g_last_web_activity_ms = 0;
uint8_t* g_latest_jpeg_buf = NULL;
size_t g_latest_jpeg_len = 0;
volatile bool g_camera_init_ok = false;
volatile uint8_t g_oled_brightness = OLED_DEFAULT_BRIGHTNESS;
WeatherInfo g_weather_info = {0.0f, 0, 0, "", "", false, 0};

/* Internal Vision Frame Buffers in SRAM */
static uint8_t* small_rgb_buf = NULL;
static uint8_t* prev_lum_buf  = NULL;
static uint8_t* mhi_buf       = NULL;

static KalmanTracker2D s_k_tracker;
static float s_lock_confidence = 0.0f;
static uint32_t s_last_valid_human_time = 0;
static float s_ema_global_luminance = 100.0f;
static int s_warmup_frames = 3;

static uint32_t s_last_inspection_time_ms = 0;
static uint32_t s_inspection_hold_time_ms = 2800;
static uint8_t s_sleep_miss_count = 0;

static const int FRAME_W = 640;
static const int FRAME_H = 480;

static inline bool isWebOrStreamActive(uint32_t now) {
    bool has_clients = false;
    portENTER_CRITICAL(&g_stream_mutex);
    has_clients = (g_stream_clients > 0);
    portEXIT_CRITICAL(&g_stream_mutex);
    if (has_clients) return true;
    if (g_last_web_activity_ms > 0 && (now - g_last_web_activity_ms < 6000)) return true;
    return false;
}

void setCameraSleep(bool enable) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;
    if (s->id.PID == OV2640_PID) {
        s->set_reg(s, 0xFF, 0xFF, 0x01);
        s->set_reg(s, 0x09, 0xFF, enable ? 0x10 : 0x00);
    } else if (s->id.PID == OV3660_PID) {
        s->set_reg(s, 0x3008, 0x40, enable ? 0x40 : 0x00);
    }
}

bool allocateVisionBuffers(void) {
    small_rgb_buf = (uint8_t*)heap_caps_malloc(80 * 60 * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    prev_lum_buf  = (uint8_t*)heap_caps_malloc(40 * 30, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    mhi_buf       = (uint8_t*)heap_caps_malloc(40 * 30, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    if (psramFound()) {
        g_latest_jpeg_buf = (uint8_t*)ps_malloc(STREAM_BUFFER_SIZE_BYTES);
    } else {
        g_latest_jpeg_buf = (uint8_t*)malloc(STREAM_BUFFER_SIZE_BYTES);
    }

    if (!small_rgb_buf) small_rgb_buf = (uint8_t*)malloc(80 * 60 * 2);
    if (!prev_lum_buf)  prev_lum_buf  = (uint8_t*)malloc(40 * 30);
    if (!mhi_buf)       mhi_buf       = (uint8_t*)malloc(40 * 30);

    if (prev_lum_buf) memset(prev_lum_buf, 0, 40 * 30);
    if (mhi_buf) memset(mhi_buf, 0, 40 * 30);

    g_frame_sem = xSemaphoreCreateBinary();
    return (small_rgb_buf && prev_lum_buf && mhi_buf && g_latest_jpeg_buf);
}

void processFrameAI(camera_fb_t *fb) {
    if (!fb || !fb->buf || fb->len < 1024 || !small_rgb_buf || !prev_lum_buf || !mhi_buf || fb->format != PIXFORMAT_JPEG) return;

    bool ok = jpg2rgb565(fb->buf, fb->len, small_rgb_buf, JPG_SCALE_8X);
    if (!ok) return;

    uint16_t* pixels = (uint16_t*)small_rgb_buf;

    static uint8_t cur_40x30[1200] __attribute__((aligned(16)));
    static bool skin_mask_40x30[1200] __attribute__((aligned(16)));
    static uint8_t texture_40x30[1200] __attribute__((aligned(16)));
    int skin_pixel_count = 0;
    uint32_t total_luminance_sum = 0;

    uint32_t sec_lum_sum[3][4] = {0};
    int sec_pixel_cnt[3][4] = {0};

    uint8_t* p_cur = cur_40x30;

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
    s_ema_global_luminance = 0.90f * s_ema_global_luminance + 0.10f * frame_mean_lum;

    float sec_mean_lum[3][4];
    for (int sy = 0; sy < 3; sy++) {
        for (int sx = 0; sx < 4; sx++) {
            sec_mean_lum[sy][sx] = sec_pixel_cnt[sy][sx] > 0 ? ((float)sec_lum_sum[sy][sx] / (float)sec_pixel_cnt[sy][sx]) : frame_mean_lum;
        }
    }

    /* Pre-compute integer-scaled sector boundaries to eliminate per-pixel float-to-int casts */
    int sec_min_cb[3][4], sec_max_cb[3][4], sec_min_cr[3][4], sec_max_cr[3][4];
    int sec_shadow_margin[3][4], sec_min_r_thresh[3][4], sec_min_cr_cb_diff[3][4];
    for (int sy = 0; sy < 3; sy++) {
        for (int sx = 0; sx < 4; sx++) {
            float local_lum = sec_mean_lum[sy][sx];
            float delta_local = fmaxf(0.0f, fminf(16.0f, (95.0f - local_lum) * 0.45f));
            sec_min_cb[sy][sx] = (int)(75.0f - delta_local);
            sec_max_cb[sy][sx] = (int)(129.0f + delta_local);
            sec_min_cr[sy][sx] = (int)(127.0f - delta_local);
            sec_max_cr[sy][sx] = (int)(180.0f + delta_local);
            sec_shadow_margin[sy][sx]   = (local_lum < 65.0f) ? 4 : 0;
            sec_min_r_thresh[sy][sx]    = (local_lum < 65.0f) ? 8 : 10;
            sec_min_cr_cb_diff[sy][sx]  = (local_lum < 65.0f) ? 4 : 6;
            if (local_lum > 185.0f) {
                sec_min_cb[sy][sx] += 3;
                sec_max_cb[sy][sx] -= 3;
                sec_min_cr[sy][sx] += 3;
                sec_max_cr[sy][sx] -= 3;
            }
        }
    }

    bool* p_skin = skin_mask_40x30;
    for (int y = 0; y < 30; y++) {
        int src_row_off = (y * 2) * 80;
        int sec_y = y / 10;
        for (int x = 0; x < 40; x++) {
            int sec_x = x / 10;

            uint16_t p = pixels[src_row_off + (x * 2)];
            int r = ((p >> 11) & 0x1F) << 3;
            int g = ((p >> 5) & 0x3F) << 2;
            int b = (p & 0x1F) << 3;

            int cb = 128 + (((-43 * r - 85 * g + 128 * b)) >> 8);
            int cr = 128 + (((128 * r - 107 * g - 21 * b)) >> 8);

            int min_cb = sec_min_cb[sec_y][sec_x];
            int max_cb = sec_max_cb[sec_y][sec_x];
            int min_cr = sec_min_cr[sec_y][sec_x];
            int max_cr = sec_max_cr[sec_y][sec_x];
            int shadow_margin = sec_shadow_margin[sec_y][sec_x];
            int min_r_thresh = sec_min_r_thresh[sec_y][sec_x];
            int min_cr_cb_diff = sec_min_cr_cb_diff[sec_y][sec_x];

            bool is_skin = (r + shadow_margin >= b) && 
                           ((cr - cb) >= min_cr_cb_diff) && 
                           (r >= min_r_thresh) && 
                           (cb >= min_cb && cb <= max_cb && cr >= min_cr && cr <= max_cr);

            *p_skin++ = is_skin;
            if (is_skin) skin_pixel_count++;
        }
    }

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

    if (s_warmup_frames > 0) {
        s_warmup_frames--;
        memcpy(prev_lum_buf, smooth_40x30, 1200);
        memset(mhi_buf, 0, 1200);
        kf2d_tracker_init(&s_k_tracker);
        s_k_tracker.last_update_us = micros();
        s_lock_confidence = 0.0f;
        portENTER_CRITICAL(&g_target_mutex);
        g_current_target.detected = false;
        g_current_target.confidence = 0.0f;
        portEXIT_CRITICAL(&g_target_mutex);
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

    float M00 = 0.0f, M10 = 0.0f, M01 = 0.0f, M20 = 0.0f, M02 = 0.0f, M11 = 0.0f;
    float prev_grid_x = s_k_tracker.kf_x.p * (1.0f / 16.0f);
    float prev_grid_y = s_k_tracker.kf_y.p * (1.0f / 16.0f);

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

            if (!is_skin && (raw_energy < 22.0f || energy < 1.0f)) continue;

            float weight = energy;
            float dx = (float)x;
            float dy = (float)y;

            if (s_k_tracker.active) {
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

            float center_dist = fabsf(cx - 320.0f);
            float priority = 15.0f * sec_skin[s] + 1.8f * sec_motion[s] + 0.10f * sec_M00[s] - 0.06f * center_dist;

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

    for (int i = 0; i < active_cnt - 1; i++) {
        for (int j = i + 1; j < active_cnt; j++) {
            if (temp_cand[j].priority_score > temp_cand[i].priority_score) {
                ObjectCandidate tmp = temp_cand[i];
                temp_cand[i] = temp_cand[j];
                temp_cand[j] = tmp;
            }
        }
    }

    portENTER_CRITICAL(&g_target_mutex);
    g_num_candidates = active_cnt;
    for (int i = 0; i < MAX_OBJECT_CANDIDATES; i++) {
        if (i < active_cnt) {
            g_object_candidates[i] = temp_cand[i];
        } else {
            g_object_candidates[i].active = false;
            g_object_candidates[i].priority_score = 0.0f;
        }
    }
    portEXIT_CRITICAL(&g_target_mutex);

    memcpy(prev_lum_buf, smooth_40x30, 1200);

    float dynamic_threshold = 95.0f - ((float)skin_pixel_count * 0.65f);
    if (dynamic_threshold < 45.0f) dynamic_threshold = 45.0f;

    uint32_t now_us = micros();
    uint32_t now_ms = millis();

    if (g_num_candidates > 1) {
        if (now_ms - s_last_inspection_time_ms > s_inspection_hold_time_ms) {
            g_inspected_candidate_idx = (g_inspected_candidate_idx + 1) % g_num_candidates;
            s_last_inspection_time_ms = now_ms;
            s_inspection_hold_time_ms = (esp_random() % 1400) + 2400;
        }
    } else {
        g_inspected_candidate_idx = 0;
    }

    float dt_sec = (s_k_tracker.last_update_us > 0) ? ((float)(now_us - s_k_tracker.last_update_us) * 1e-6f) : 0.033f;
    float dt = fmaxf(0.01f, fminf(0.20f, dt_sec));
    s_k_tracker.last_update_us = now_us;

    float q_process = (skin_pixel_count >= 4) ? 850.0f : 450.0f;
    kf1d_predict(&s_k_tracker.kf_x, dt, q_process);
    kf1d_predict(&s_k_tracker.kf_y, dt, q_process);

    float pred_x = s_k_tracker.kf_x.p;
    float pred_y = s_k_tracker.kf_y.p;
    float speed = sqrtf(s_k_tracker.kf_x.v * s_k_tracker.kf_x.v + s_k_tracker.kf_y.v * s_k_tracker.kf_y.v);
    float search_radius = fminf(320.0f, 120.0f + 0.35f * speed);

    bool candidate_found = false;
    float cand_cx = pred_x, cand_cy = pred_y, cand_bw = s_k_tracker.w, cand_bh = s_k_tracker.h;

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

        float raw_bw = 2.4f * fmaxf(2.8f, sigma_x) * 16.0f;
        float raw_bh = 2.8f * fmaxf(3.2f, sigma_y) * 16.0f;

        float coupled_bh = fmaxf(raw_bh, 1.25f * raw_bw);
        coupled_bh = fminf(coupled_bh, 1.50f * raw_bw);

        cand_bw = fmaxf(140.0f, fminf(240.0f, raw_bw));
        cand_bh = fmaxf(180.0f, fminf(310.0f, coupled_bh));

        if (!s_k_tracker.active) {
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
    } else if (s_k_tracker.active && skin_pixel_count >= 8) {
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

            cand_bw = fmaxf(s_k_tracker.w * 0.85f, fmaxf(140.0f, fminf(240.0f, raw_bw_skin)));
            cand_bh = fmaxf(s_k_tracker.h * 0.85f, fmaxf(180.0f, fminf(310.0f, coupled_bh_skin)));

            float effective_radius_skin = (skin_pixel_count >= 4) ? 450.0f : search_radius;
            float dist_sq_skin = (raw_cx_skin - pred_x) * (raw_cx_skin - pred_x) + (raw_cy_skin - pred_y) * (raw_cy_skin - pred_y);
            if (dist_sq_skin <= (effective_radius_skin * effective_radius_skin)) {
                cand_cx = raw_cx_skin;
                cand_cy = raw_cy_skin;
                candidate_found = true;
            }
        }
    }

    s_lock_confidence = s_lock_confidence * 0.85f + (candidate_found ? 1.0f : 0.0f) * 0.15f;

    if (candidate_found) {
        float dist_innov = sqrtf((cand_cx - pred_x) * (cand_cx - pred_x) + (cand_cy - pred_y) * (cand_cy - pred_y));
        float R_dynamic = kf2d_compute_dynamic_r(dist_innov, skin_pixel_count, s_lock_confidence);

        kf1d_update(&s_k_tracker.kf_x, cand_cx, R_dynamic);
        kf1d_update(&s_k_tracker.kf_y, cand_cy, R_dynamic);

        s_k_tracker.w = s_k_tracker.w * 0.70f + cand_bw * 0.30f;
        s_k_tracker.h = s_k_tracker.h * 0.70f + cand_bh * 0.30f;
        s_k_tracker.w = fmaxf(60.0f, fminf(400.0f, s_k_tracker.w));
        s_k_tracker.h = fmaxf(80.0f, fminf(480.0f, s_k_tracker.h));

        s_k_tracker.active = true;
        s_last_valid_human_time = now_ms;
    } else {
        s_k_tracker.kf_x.v *= 0.85f;
        s_k_tracker.kf_y.v *= 0.85f;

        if (now_ms - s_last_valid_human_time > 450) {
            s_k_tracker.active = false;
            s_k_tracker.kf_x.v = 0.0f;
            s_k_tracker.kf_y.v = 0.0f;
        }
    }

    s_k_tracker.kf_x.p = fmaxf(0.0f, fminf((float)FRAME_W, s_k_tracker.kf_x.p));
    s_k_tracker.kf_y.p = fmaxf(0.0f, fminf((float)FRAME_H, s_k_tracker.kf_y.p));

    float current_area = sqrtf(s_k_tracker.w * s_k_tracker.h);
    float proximity_z = constrain((current_area - 140.0f) / 130.0f, 0.0f, 1.0f);

    if (s_k_tracker.active && s_lock_confidence > 0.25f) {
        float center_x = FRAME_W / 2.0f;
        float center_y = FRAME_H / 2.0f;
        float err_x = ((s_k_tracker.kf_x.p - center_x) / center_x) * 100.0f;
        float err_y = ((s_k_tracker.kf_y.p - center_y) / center_y) * 100.0f;

        portENTER_CRITICAL(&g_target_mutex);
        g_current_target.detected = true;
        g_current_target.x = (int)(s_k_tracker.kf_x.p - s_k_tracker.w / 2.0f);
        g_current_target.y = (int)(s_k_tracker.kf_y.p - s_k_tracker.h / 2.0f);
        g_current_target.w = (int)s_k_tracker.w;
        g_current_target.h = (int)s_k_tracker.h;
        g_current_target.cx = (int)s_k_tracker.kf_x.p;
        g_current_target.cy = (int)s_k_tracker.kf_y.p;
        g_current_target.error_x = err_x;
        g_current_target.error_y = err_y;
        g_current_target.confidence = s_lock_confidence;
        g_current_target.total_energy = (float)M00;
        g_current_target.vx = s_k_tracker.kf_x.v;
        g_current_target.vy = s_k_tracker.kf_y.v;
        g_current_target.proximity = proximity_z;
        g_current_target.last_seen_ms = now_ms;
        portEXIT_CRITICAL(&g_target_mutex);
    } else {
        portENTER_CRITICAL(&g_target_mutex);
        g_current_target.detected = false;
        g_current_target.error_x = 0.0f;
        g_current_target.error_y = 0.0f;
        g_current_target.confidence = s_lock_confidence;
        g_current_target.total_energy = (float)M00;
        g_current_target.vx = 0.0f;
        g_current_target.vy = 0.0f;
        g_current_target.proximity = 0.0f;
        portEXIT_CRITICAL(&g_target_mutex);
    }
}

bool initCamera(void) {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = CAM_PIN_Y2;
    config.pin_d1       = CAM_PIN_Y3;
    config.pin_d2       = CAM_PIN_Y4;
    config.pin_d3       = CAM_PIN_Y5;
    config.pin_d4       = CAM_PIN_Y6;
    config.pin_d5       = CAM_PIN_Y7;
    config.pin_d6       = CAM_PIN_Y8;
    config.pin_d7       = CAM_PIN_Y9;
    config.pin_xclk     = CAM_PIN_XCLK;
    config.pin_pclk     = CAM_PIN_PCLK;
    config.pin_vsync    = CAM_PIN_VSYNC;
    config.pin_href     = CAM_PIN_HREF;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_pwdn     = CAM_PIN_PWDN;
    config.pin_reset    = CAM_PIN_RESET;
    
    config.xclk_freq_hz = CAM_XCLK_FREQ_HZ;
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

    for (int i = 0; i < 4; i++) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
        delay(40);
    }

    return true;
}

void cameraTask(void *pvParameters) {
    (void)pvParameters;
    uint32_t last_frame_time = millis();
    uint32_t state_timer = millis();

    uint32_t active_duration_ms = ACTIVE_STATE_TIMEOUT_MS;
    uint32_t sleep_duration_ms  = (esp_random() % 90000) + 90000;
    uint32_t last_camera_retry_ms = 0;

    while (true) {
        uint32_t now = millis();
        bool web_active = isWebOrStreamActive(now);

        if (!g_camera_init_ok) {
            if (now - last_camera_retry_ms > 5000) {
                last_camera_retry_ms = now;
                if (initCamera()) {
                    g_camera_init_ok = true;
                    KORE_LOG_INF("CAM", "Camera hardware initialized successfully");
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (g_recon_state == STATE_ACTIVE) {
            camera_fb_t *fb = esp_camera_fb_get();
            if (fb) {
                if (fb->len > 1024 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8) {
                    processFrameAI(fb);

                    if (now - last_frame_time > 0) {
                        g_fps_ai = 1000.0f / (float)(now - last_frame_time);
                    }
                    last_frame_time = now;

                    if (web_active && g_latest_jpeg_buf) {
                        if (fb->len <= STREAM_BUFFER_SIZE_BYTES) {
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

            bool target_engaged = false;
            portENTER_CRITICAL(&g_target_mutex);
            target_engaged = (g_current_target.detected && (now - g_current_target.last_seen_ms < 500));
            portEXIT_CRITICAL(&g_target_mutex);

            if (web_active || target_engaged) {
                state_timer = now;
                if (target_engaged && s_lock_confidence > 0.4f) {
                    s_sleep_miss_count = 0;
                }
            } else if (now - state_timer > active_duration_ms) {
                setCameraSleep(true);
                g_recon_state = STATE_SLEEP_RECON;
                state_timer = now;

                s_sleep_miss_count++;
                if (s_sleep_miss_count >= 6) {
                    sleep_duration_ms = (esp_random() % 180000) + 300000;
                } else if (s_sleep_miss_count >= 3) {
                    sleep_duration_ms = (esp_random() % 120000) + 180000;
                } else {
                    sleep_duration_ms = (esp_random() % 90000) + 90000;
                }
            }
        } else if (g_recon_state == STATE_SLEEP_RECON) {
            if (web_active || (now - state_timer > sleep_duration_ms)) {
                setCameraSleep(false);
                vTaskDelay(pdMS_TO_TICKS(30));

                kf2d_tracker_init(&s_k_tracker);
                portENTER_CRITICAL(&g_target_mutex);
                g_current_target.detected = false;
                g_current_target.confidence = 0.0f;
                g_current_target.error_x = 0.0f;
                g_current_target.error_y = 0.0f;
                g_current_target.vx = 0.0f;
                g_current_target.vy = 0.0f;
                g_current_target.proximity = 0.0f;
                portEXIT_CRITICAL(&g_target_mutex);

                s_warmup_frames = 3;
                g_recon_state = STATE_ACTIVE;
                state_timer = now;
                active_duration_ms = ACTIVE_STATE_TIMEOUT_MS;
            } else {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }

        vTaskDelay(1);
    }
}
