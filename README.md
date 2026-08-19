# KoRe — Kinematic Optical Recognition & Biomechanical Face Engine

[![Board](https://img.shields.io/badge/Hardware-Seeed_XIAO_ESP32--S3_Sense-00979D.svg?style=for-the-badge&logo=arduino)](https://www.seeedstudio.com/XIAO-ESP32S3-Sense-p-5639.html)
[![Core Architecture](https://img.shields.io/badge/Architecture-Dual--Core_FreeRTOS_240MHz-blue.svg?style=for-the-badge)](#system-architecture)
[![Vision Latency](https://img.shields.io/badge/AI_Latency-%3C_0.5_ms-brightgreen.svg?style=for-the-badge)](#performance-benchmarks)
[![Ocular Dynamics](https://img.shields.io/badge/Physics-38.0_rad%2Fs_Mass--Spring--Damper-orange.svg?style=for-the-badge)](#key-engineering-features)
[![Display Driver](https://img.shields.io/badge/Display-LovyanGFX_SSD1306_1.0MHz_I2C-purple.svg?style=for-the-badge)](#hardware-interfacing--pinout)

**KoRe** (*Kinematic Optical Recognition Engine*) is a lightweight, standalone embedded computer vision and biomechanical ocular synthesis system built for the **Seeed Studio XIAO ESP32-S3 Sense** (Xtensa LX7 Dual-Core @ 240 MHz).

The firmware combines a real-time differential YCbCr computer vision pipeline, 4x3 spatial sector illumination analysis, multi-object candidate tracking, a 2D discrete Kalman tracking filter with dynamic measurement noise tuning, second-order mass-spring-damper gaze kinetics (`omega_n = 38.0 rad/s`, `zeta = 1.00`), 5th-order minimum-jerk saccades, a 1-bit LGFX sprite rendering engine (1.0 MHz Fast-Mode Plus I2C), a 2D Russell Circumplex affective emotion model, and dynamic power management (DFS clock scaling + SCCB sensor standby).

---

## System Architecture

KoRe runs on a dual-core FreeRTOS architecture, partitioning heavy image processing and networking to Core 0 while isolating 60 FPS ocular kinematics and sprite rendering on Core 1.

```
                   +---------------------------------------------------+
                   |         OV2640 / OV3660 CAMERA SENSOR (VGA)       |
                   +---------------------------------------------------+
                                             |
                                             v
    +---------------------------------------------------------------------------------+
    | CORE 0: cameraTask (Priority 2, Vision Pipeline & System Power Scaling)         |
    |  * Continuous Frame Acquisition (esp_camera_fb_get)                             |
    |  * 8x Hardware Downscaling (640x480 -> 80x60 RGB565 Array)                       |
    |  * 4x3 Sector Illumination Analysis & Dynamic Local YCbCr Chrominance Scaling   |
    |  * Motion History Image (MHI) Decay & Spatial Moment Accumulation (M00..M11)    |
    |  * Multi-Object Spatial Clustering & Priority Ranking (Up to 3 Candidates)      |
    |  * 2D Kalman Filter Prediction/Update ([x, y, vx, vy]^T with Dynamic R Tuning)   |
    |  * DFS Clock Scaling (240 MHz Active <-> 80 MHz Sleep Standby)                  |
    |  * SCCB Sensor Standby Register Control (OV2640 0x09 bit 4 / OV3660 0x3008)     |
    |  * Double-Buffered JPEG Frame Copier (g_stream_mutex)                           |
    +---------------------------------------------------------------------------------+
                                             |
                     [ Spinlock Critical Section (target_mutex) ]
                                             |
                                             v
    +---------------------------------------------------------------------------------+
    | CORE 1: oledTask (Priority 1, 60 FPS Display & Kinematics Loop)                 |
    |  * 2D Russell Circumplex Emotion Engine (Valence-Arousal Dynamics & Langevin)   |
    |  * Unified Rigid 2D Facial Rig (Eyes, Brows, and Mouth Locked to Single Offset) |
    |  * Anti-Jitter Coordinate Hysteresis (getFilteredOx, getFilteredOy)             |
    |  * Ocular Kinematics (38.0 rad/s Critically Damped Spring-Damper & Minimum-Jerk)|
    |  * Fixation Micro-Kinetics (Mean-Reverting Brownian Random Walk)                |
    |  * Non-Blocking Eyelid State Machine (Idle -> Closing -> Opening & Double-Blink)|
    |  * LovyanGFX High-Speed SSD1306 Sprite Renderer (1.0 MHz Fast-Mode+ I2C)       |
    +---------------------------------------------------------------------------------+
                                             |
                     [ Concurrent Async HTTP Endpoints ]
                                             |
                                             v
    +---------------------------------------------------------------------------------+
    | HTTP TELEMETRY & MJPEG SERVER (Port 80 Web UI/JSON, Port 81 MJPEG Stream)       |
    |  * /telemetry : Real-time JSON tracking state, candidate priority scores & FPS  |
    |  * /stream    : Non-blocking MJPEG live video stream                            |
    |  * /save_wifi & /switch_mode : NVS configuration management handlers            |
    +---------------------------------------------------------------------------------+
```

---

## Key Engineering Features

### 1. 2D Discrete Kalman Filter with Dynamic Noise Covariance (R) Tuning
- **State Vector (`x = [p, v]^T`):** Independent 1D position-velocity filters for X and Y Cartesian coordinates tracking spatial target motion in pixel space.
- **Adaptive Process Noise (`Q`):** Automatically increases process noise variance `Q` (450 -> 850) when skin pixel detection confirms human presence.
- **Dynamic Measurement Noise (`R`):**
  - **Stationary Target (`innovation < 6 px`):** Increases `R` up to 1.5x - 3.0x base to eliminate pixel discretization quantization jitter when a subject is resting still.
  - **Rapid Movement (`innovation > 20 px`):** Dynamically scales `R` down to enable zero-phase-lag tracking response during fast motion.
- **Joseph-Stabilized Covariance Update:** Uses the Joseph algebraic update formula to guarantee positive semi-definiteness of state covariance matrix `P`.

### 2. Local-Lighting Adaptive YCbCr Skin Classifier
- **Sector Illumination Analysis:** Divides the downscaled image into 12 spatial grid sectors (4 x 3), calculating mean local sector luminance.
- **Dynamic Chrominance Thresholding:** Adjusts Cb (`[75, 129]`) and Cr (`[127, 180]`) bounds per sector based on local lighting conditions.
- **Shadow Margin Relaxation & Glare Rejection:** Relaxes chrominance constraints in underexposed shadow regions (`luminance < 65`) while tightening bounds in overexposed glare regions (`luminance > 185`).

### 3. Geometric Face Centroid Lock (Feature-Motion Anti-Bias)
- **Problem Solved:** Motion-weighted centroid algorithms shift target tracking crosshairs towards moving mouths or blinking eyes.
- **Engineered Solution:** Pixels passing skin classification (`is_skin == true`) are assigned uniform spatial weighting `Phi_skin = 10.0 + 0.3(g_y + g_x)`. This forces spatial moments (`M10/M00` and `M01/M00`) to calculate the **true geometric center of mass** of the human face/head, keeping the tracking lock centered regardless of lip movements or eye blinks.

### 4. Multi-Candidate Target Tracking & Autonomous Inspection
- **Multi-Object Priority Engine:** Tracks up to 3 spatial candidate targets (`P1` Primary, `P2` Secondary, `P3` Tertiary) scored by skin area, motion energy, and foveal distance:

```
Priority_k = 15.0 * SkinPx_k + 1.8 * Motion_k + 0.10 * M00_k - 0.06 * |CenterDist_k|
```

- **Autonomous Inspection Scanning:** While tracking primary target `P1`, KoRe periodically executes autonomous saccadic glances every 2.4s to 3.8s to inspect candidate `P2` or `P3` before returning to `P1`.

### 5. Biomechanical Oculomotor Dynamics & Minimum-Jerk Splines
- **Smooth Pursuit (`omega_n = 38.0 rad/s`, `zeta = 1.00`):** Second-order mass-spring-damper differential kinetics configured for critical damping, providing smooth pursuit without overshooting or oscillation.
- **Minimum-Jerk Saccadic Trajectories:** Ballistic eye movements follow a 5th-order jerk minimization polynomial:

```
s(p) = 10*p^3 - 15*p^4 + 6*p^5,   where p in [0, 1]
```

- **Fixation Micro-Kinetics:** Mean-reverting Brownian random walk adds micro-drift (`~0.03 * sqrt(dt)`) during fixations, mimicking human ocular physiology.
- **Anti-Jitter Coordinate Hysteresis (`getFilteredOx`, `getFilteredOy`):** Filters out floating point sub-pixel jitter (`< 0.55 px`) to prevent 1px display quantization flickering on monochrome OLED pixel grids.

### 6. Intermittent Reconnaissance Duty Cycle FSM & SCCB Standby
- **State Machine (`STATE_ACTIVE` <-> `STATE_SLEEP_RECON`):** Cycles between active tracking (3-6s) and low-power reconnaissance standby (90-180s, escalating up to 8 minutes after consecutive misses).
- **Dynamic Frequency Scaling (DFS):** Scales CPU clock between 240 MHz (active CV/streaming) and 80 MHz (sleep standby, maintaining ESP32 Wi-Fi stack stability).
- **SCCB Software Standby:** Writes directly to camera registers (OV2640 `0x09` bit 4 / OV3660 `0x3008` bit 6) to power down sensor core, compensating for the un-wired `PWDN` pin on the XIAO ESP32-S3 Sense board.

### 7. Russell Circumplex 2D Emotion Engine
- **Valence-Arousal Model:** Tracks 2D emotional state (`V in [-1.0, +1.0]`, `A in [0.0, 1.0]`) using viscous homeostatic Langevin relaxation (`tau_v = 6.0s`, `tau_a = 4.5s`).
- **Refractory Lock:** Enforces a 5-8 second biological refractory period (`g_mood_lock_until`) between emotional state changes (`EXPR_IDLE`, `EXPR_ANGRY`, `EXPR_OVERLOAD`, `EXPR_JOY`, `EXPR_SEDIH`, `EXPR_SMIRK`, `EXPR_SHOCK`).

---

## Memory Architecture

To maximize computer vision execution speed on the ESP32-S3 without hitting PSRAM bus latency bottlenecks, memory allocation follows strict hardware partitioning rules:

| Buffer Name | Allocation Cap | Storage Location | Size | Rationale |
| :--- | :--- | :--- | :--- | :--- |
| `small_rgb_buf` | `MALLOC_CAP_INTERNAL` | Fast Internal SRAM | 80 x 60 x 2 bytes (9.6 KB) | Read/written thousands of times per frame during YCbCr downscaling. |
| `prev_lum_buf` | `MALLOC_CAP_INTERNAL` | Fast Internal SRAM | 40 x 30 bytes (1.2 KB) | Frame difference buffer accessed in high-frequency CV loops. |
| `mhi_buf` | `MALLOC_CAP_INTERNAL` | Fast Internal SRAM | 40 x 30 bytes (1.2 KB) | Motion History Image decay map accessed per pixel. |
| `g_latest_jpeg_buf` | `ps_malloc` | External PSRAM | 64 KB | Double-buffered JPEG frame container used only for HTTP MJPEG streaming. |

---

## Performance Benchmarks

| Metric | Measured Value | Remarks |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32-S3 Dual-Core Xtensa LX7 @ 240 MHz | Hardware FPU Enabled |
| **Camera Sensor** | OmniVision OV2640 / OV3660 | DVP Parallel Interface |
| **Frame Resolution** | VGA (640 x 480), Quality 8 JPEG | PSRAM Framebuffer |
| **AI Subsampling Grid** | 40 x 30 Matrix (1,200 pixels) | 8x Hardware IDCT Scaling |
| **AI Pipeline Latency** | **< 0.5 ms / frame** | Executed on Core 0 (`IRAM_ATTR`) |
| **Gaze Pursuit Frequency** | **omega_n = 38.0 rad/s** | Mass-Spring-Damper Model (`zeta = 1.00`) |
| **MJPEG Streaming FPS** | **30+ FPS** | Port 81 Dedicated Async Handler |
| **Display Refresh Rate** | **60 FPS** | LovyanGFX I2C @ 1.0 MHz |
| **Dynamic Memory Allocation** | < 12 KB Internal SRAM | Zero Redundant Buffering |

---

## Hardware Interfacing & Pinout

### Pin Mapping (Seeed Studio XIAO ESP32-S3 Sense)

| Peripheral Device | Signal | XIAO ESP32-S3 GPIO | Header Label |
| :--- | :--- | :--- | :--- |
| **OLED Display (SSD1306)** | I2C SCL | GPIO 5 | D4 |
| **OLED Display (SSD1306)** | I2C SDA | GPIO 6 | D5 |
| **Touch Sensor (Optional)** | Digital Input | GPIO 2 | D1 |
| **Camera DVP Bus** | XCLK, PCLK, VSYNC, HREF, Y2-Y9 | GPIO 10, 11, 12, 13, 14, 15, 16, 17, 18, 38, 39, 40, 47, 48 | Expansion Connector |

---

## API Endpoints & Web Telemetry HUD

KoRe hosts an asynchronous dual-port HTTP web server for live telemetry inspection, credential setup, and video streaming:

- **Web Dashboard (`GET http://<ESP32_IP>/`):** Serves the control panel interface rendering real-time target bounding boxes (`P1` Green, `P2` Cyan, `P3` Yellow), inspection badges, and telemetry overlays.
- **JSON Telemetry Endpoint (`GET http://<ESP32_IP>/telemetry`):** Returns real-time tracking metrics and multi-candidate arrays:
  ```json
  {
    "detected": true,
    "x": 210,
    "y": 140,
    "w": 180,
    "h": 220,
    "cx": 300,
    "cy": 250,
    "err_x": -6.25,
    "err_y": 4.16,
    "conf": 0.94,
    "fps_ai": 31.5,
    "fw": 640,
    "fh": 480,
    "num_cands": 3,
    "insp_idx": 0,
    "c0_cx": 300, "c0_cy": 250, "c0_p": 142.5,
    "c1_cx": 120, "c1_cy": 180, "c1_p": 88.2,
    "c2_cx": 480, "c2_cy": 310, "c2_p": 64.1
  }
  ```
- **MJPEG Video Stream (`GET http://<ESP32_IP>:81/stream`):** Dedicated Port 81 asynchronous stream handler.
- **Network Configuration (`GET /get_wifi`, `POST /save_wifi`, `POST /switch_mode`):** Manage Wi-Fi credentials stored in ESP32 NVS (`Preferences`).

---

## Building & Deployment

### Software Requirements
1. **Board Package:** `esp32` by Espressif Systems (v2.0.11 or later)
2. **Library Dependency:** [LovyanGFX](https://github.com/lovyan03/LovyanGFX) (Fast graphics driver)

### Arduino IDE Settings
- **Board:** `XIAO_ESP32S3`
- **PSRAM:** `OPI PSRAM`
- **Flash Mode:** `QIO 80MHz`
- **CPU Frequency:** `240MHz (WiFi/BT)`
- **Partition Scheme:** `Huge APP (3MB No OTA/1MB SPIFFS)`

---

## License

Designed and developed under an open-source embedded software engineering paradigm.
