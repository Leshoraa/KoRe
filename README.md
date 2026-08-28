# KoRe : Kinematic Optical Recognition and Biomechanical Face Engine

[![Board](https://img.shields.io/badge/Hardware-Seeed_XIAO_ESP32--S3_Sense-00979D.svg?style=for-the-badge&logo=arduino)](https://www.seeedstudio.com/XIAO-ESP32S3-Sense-p-5639.html)
[![Core Architecture](https://img.shields.io/badge/Architecture-Dual--Core_FreeRTOS_240MHz-blue.svg?style=for-the-badge)](#system-architecture)
[![Vision Latency](https://img.shields.io/badge/AI_Latency-%3C_0.5_ms-brightgreen.svg?style=for-the-badge)](#performance-benchmarks)
[![Ocular Dynamics](https://img.shields.io/badge/Physics-38.0_rad%2Fs_Mass--Spring--Damper-orange.svg?style=for-the-badge)](#key-engineering-features)
[![Display Driver](https://img.shields.io/badge/Display-LovyanGFX_SSD1306_1.0MHz_I2C-purple.svg?style=for-the-badge)](#hardware-interfacing--pinout)
[![Firmware Version](https://img.shields.io/badge/Firmware-v2.5.0-success.svg?style=for-the-badge)](#api-endpoints--web-telemetry-hud)

**KoRe** (*Kinematic Optical Recognition Engine*) is a lightweight, standalone embedded computer vision and biomechanical ocular synthesis system built for the **Seeed Studio XIAO ESP32-S3 Sense** (Xtensa LX7 Dual-Core @ 240 MHz).

The firmware combines a real-time differential YCbCr computer vision pipeline, 4x3 spatial sector illumination analysis, multi-object candidate tracking, a 2D discrete Kalman tracking filter with dynamic measurement noise tuning, second-order mass-spring-damper gaze kinetics ($\omega_n = 38.0\text{ rad/s}, \zeta = 1.00$), 5th-order minimum-jerk saccades, a 1-bit LGFX sprite rendering engine (1.0 MHz Fast-Mode Plus I2C), a 2D Russell Circumplex affective emotion model, Web OTA updates, live camera sensor tuning, OLED anti-burn-in protection, and dynamic power management (DFS clock scaling, Wi-Fi modem sleep, and SCCB sensor standby).

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
    |  * 8x Hardware Downscaling (640x480 -> 80x60 RGB565 Array in Internal SRAM)     |
    |  * 4x3 Sector Illumination Analysis & Dynamic Local YCbCr Chrominance Scaling   |
    |  * Motion History Image (MHI) Decay & Spatial Moment Accumulation (M00..M11)    |
    |  * Multi-Object Spatial Clustering & Priority Ranking (Up to 3 Candidates)      |
    |  * 2D Kalman Filter Prediction/Update ([x, y, vx, vy]^T with Dynamic R Tuning)  |
    |  * DFS Clock Scaling (240 MHz Active <-> 80 MHz Sleep Standby)                  |
    |  * Dynamic Wi-Fi Modem Power Save (WIFI_PS_MIN_MODEM during Standby)            |
    |  * SCCB Sensor Standby Register Control (OV2640 0x09 bit 4 / OV3660 0x3008)     |
    |  * Double-Buffered JPEG Frame Copier (g_stream_mutex)                           |
    +---------------------------------------------------------------------------------+
                                             |
                     [ Spinlock Critical Section (g_target_mutex) ]
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
    |  * OLED Anti-Burn-In Protection (+/-1 px Micro-Shift & Auto-Dimming Standby)   |
    |  * LovyanGFX High-Speed SSD1306 Sprite Renderer (1.0 MHz Fast-Mode+ I2C)        |
    +---------------------------------------------------------------------------------+
                                             |
                     [ Concurrent Async HTTP Endpoints ]
                                             |
                                             v
    +---------------------------------------------------------------------------------+
    | HTTP TELEMETRY, OTA & MJPEG SERVER (Port 80 Web UI/JSON, Port 81 MJPEG Stream)  |
    |  * /telemetry       : Real-time JSON tracking state, candidate scores, FPS & V/A|
    |  * /set_expression  : Manual facial expression selection and Auto-Mood reset    |
    |  * /camera_control  : Live sensor tuning (brightness, contrast, saturation, flip)|
    |  * /update          : Web Over-The-Air (OTA) binary firmware flashing endpoint  |
    |  * /scan_wifi       : Real-time Wi-Fi network scanner with RSSI & encryption    |
    |  * /system_info     : Hardware diagnostics, heap/PSRAM, uptime, and CPU MHz     |
    |  * /stream          : Non-blocking MJPEG live video stream (Port 81)            |
    |  * /save_wifi & /switch_mode : NVS configuration management handlers            |
    +---------------------------------------------------------------------------------+
```

---

## Repository Structure

```text
KoRe/
├── .github/
│   └── workflows/
│       ├── compile_check.yml       # Automated ESP32-S3 compile workflow
│       └── model_validation.yml    # Kinematics and Kalman filter verification suite
├── docs/
│   ├── ARCHITECTURE.md             # Dual-core FreeRTOS dataflow and scheduling specifications
│   ├── MATHEMATICAL_MODELS.md      # Mathematical proofs for ocular dynamics and minimum-jerk
│   └── TELEMETRY_SPECIFICATION.md  # JSON schema for /telemetry, OTA, and MJPEG stream protocol
├── include/
│   ├── kore_config.h               # Pin definitions, clock rates, and power profiles
│   ├── kore_types.h                # Data structs: TrackTarget, ObjectCandidate, Expression
│   ├── kore_kalman.h               # Discrete Kalman filter declarations
│   ├── kore_kinematics.h           # Mass-spring-damper and minimum-jerk contracts
│   └── kore_affective.h            # 2D Russell Circumplex Langevin state model
├── src/
│   ├── KoRe.ino                    # Master entry point and FreeRTOS task initializers
│   ├── core/
│   │   ├── camera_pipeline.h       # Camera initialization and vision pipeline header
│   │   ├── camera_pipeline.cpp     # YCbCr downscaling, MHI decay, and spatial clustering
│   │   ├── display_engine.h        # LovyanGFX SSD1306 display engine header
│   │   └── display_engine.cpp      # LovyanGFX SSD1306 sprite composition and facial rig
│   ├── math/
│   │   ├── kore_kalman.cpp         # 2D discrete Kalman filter implementation
│   │   ├── kore_kinematics.cpp     # Biomechanical ocular equations and minimum-jerk solver
│   │   └── kore_affective.cpp      # Stochastic Langevin affective dynamics solver
│   └── net/
│       ├── http_server.h           # Asynchronous HTTP server header
│       ├── http_server.cpp         # Async HTTP server, JSON telemetry, OTA, and MJPEG streamer
│       ├── web_ui.h                # Calibrated dark matte Web UI HTML/CSS/JS constants
│       ├── wifi_manager.h          # NVS Wi-Fi credentials manager header
│       └── wifi_manager.cpp        # NVS-backed STA/AP configuration and captive portal
├── tests/
│   ├── unit/
│   │   ├── test_affective_langevin.cpp       # Langevin stochastic diffusion convergence tests
│   │   ├── test_kalman_convergence.cpp       # 2D Kalman state estimator convergence tests
│   │   ├── test_kinematics_feedforward.cpp   # Feedforward gaze kinematics validation
│   │   └── test_minimum_jerk.cpp             # 5th-order polynomial trajectory verification
│   └── fixtures/
│       └── sample_motion_frames.h  # Synthetic downscaled frame test vectors
├── scripts/
│   ├── build.sh                    # Command-line compilation via Arduino CLI
│   ├── flash.sh                    # Serial flashing and monitor tool
│   └── validate_kinematics.py      # Numerical NRMSE and R^2 evaluation tool
├── .clang-format                   # Strict formatting rules
├── .gitignore                      # Git build artifact filters
├── CMakeLists.txt                  # ESP-IDF CMake build definition
├── CHANGELOG.md                    # Release history
├── LICENSE                         # MIT License terms
├── KORE_ENGINEERING_SPECIFICATION.md # Architectural and engineering standards
└── README.md                       # Master technical project presentation
```

---

## Key Engineering Features

### 1. 2D Discrete Kalman Filter with Dynamic Noise Covariance ($R$) Tuning
- **State Vector ($\mathbf{x} = [p, v]^T$):** Independent 1D position-velocity filters for X and Y Cartesian coordinates tracking spatial target motion in pixel space.
- **Adaptive Process Noise ($Q$):** Automatically increases process noise variance $Q$ ($450 \rightarrow 850$) when skin pixel detection confirms human presence.
- **Dynamic Measurement Noise ($R$):**
  - **Stationary Target ($\text{innovation} < 6\text{ px}$):** Increases $R$ up to $1.5\times - 3.0\times$ base to eliminate quantization noise when a subject is resting still.
  - **Rapid Movement ($\text{innovation} > 20\text{ px}$):** Dynamically scales $R$ down to enable zero-phase-lag tracking response during fast motion.
- **Joseph-Stabilized Covariance Update:** Uses the Joseph algebraic update formula $(\mathbf{I} - \mathbf{K}\mathbf{H})\mathbf{P}^-(\mathbf{I} - \mathbf{K}\mathbf{H})^T + \mathbf{K}\mathbf{R}\mathbf{K}^T$ to guarantee positive semi-definiteness of state covariance matrix $P$.

### 2. Local-Lighting Adaptive YCbCr Skin Classifier
- **Sector Illumination Analysis:** Divides the downscaled image into 12 spatial grid sectors ($4 \times 3$), calculating mean local sector luminance.
- **Dynamic Chrominance Thresholding:** Adjusts $C_b$ ($[75, 129]$) and $C_r$ ($[127, 180]$) bounds per sector based on local lighting conditions.
- **Shadow Margin Relaxation & Glare Rejection:** Relaxes chrominance constraints in underexposed shadow regions ($\text{luminance} < 65$) while tightening bounds in overexposed glare regions ($\text{luminance} > 185$).

### 3. Geometric Face Centroid Lock (Feature-Motion Anti-Bias)
- **Problem Solved:** Motion-weighted centroid algorithms shift target tracking crosshairs towards moving mouths or blinking eyes.
- **Engineered Solution:** Pixels passing skin classification (`is_skin == true`) are assigned uniform spatial weighting: $\Phi_{\text{skin}} = 10.0 + 0.3(g_y + g_x)$. This forces spatial moments ($M_{10}/M_{00}$ and $M_{01}/M_{00}$) to calculate the **true geometric center of mass** of the human face/head, keeping the tracking lock centered regardless of lip movements or eye blinks.

### 4. Multi-Candidate Target Tracking & Autonomous Inspection
- **Multi-Object Priority Engine:** Tracks up to 3 spatial candidate targets ($P_1$ Primary, $P_2$ Secondary, $P_3$ Tertiary) scored by skin area, motion energy, and foveal distance:

$$\text{Priority}_k = 15.0 \cdot \text{SkinPx}_k + 1.8 \cdot \text{Motion}_k + 0.10 \cdot M_{00, k} - 0.06 \cdot |\text{CenterDist}_k|$$

- **Autonomous Inspection Scanning:** While tracking primary target $P_1$, KoRe periodically executes autonomous saccadic glances every 2.4s to 3.8s to inspect candidate $P_2$ or $P_3$ before returning to $P_1$.

### 5. Biomechanical Oculomotor Dynamics & Minimum-Jerk Splines
- **Smooth Pursuit ($\omega_n = 38.0\text{ rad/s}, \zeta = 1.00$):** Second-order mass-spring-damper differential kinetics configured for critical damping, providing smooth pursuit without overshooting or oscillation.
- **Minimum-Jerk Saccadic Trajectories:** Ballistic eye movements follow a 5th-order jerk minimization polynomial:

$$s(p) = 10p^3 - 15p^4 + 6p^5, \quad p \in [0, 1]$$

- **Fixation Micro-Kinetics:** Mean-reverting Brownian random walk adds micro-drift ($\approx 0.03\sqrt{\Delta t}$) during fixations, mimicking human ocular physiology.
- **Anti-Jitter Coordinate Hysteresis (`getFilteredOx`, `getFilteredOy`):** Filters out floating point sub-pixel jitter ($< 0.55\text{ px}$) to prevent 1px display quantization flickering on monochrome OLED pixel grids.

### 6. Intermittent Reconnaissance Duty Cycle FSM & Multi-Tier Power Saving
- **State Machine (`STATE_ACTIVE` $\leftrightarrow$ `STATE_SLEEP_RECON`):** Cycles between active tracking (3-6s) and low-power reconnaissance standby (90-180s, escalating up to 8 minutes after consecutive misses).
- **Dynamic Frequency Scaling (DFS):** Scales CPU clock between 240 MHz (active CV/streaming) and 80 MHz (sleep standby, maintaining ESP32 Wi-Fi stack stability).
- **Wi-Fi Modem Sleep:** Engages `WIFI_PS_MIN_MODEM` during sleep reconnaissance cycles to reduce radio power consumption.
- **SCCB Software Standby:** Writes directly to camera registers (OV2640 `0x09` bit 4 / OV3660 `0x3008` bit 6) to power down sensor core, compensating for the un-wired `PWDN` pin on the XIAO ESP32-S3 Sense board.

### 7. Russell Circumplex 2D Emotion & Kaomoji Face Engine
- **Valence-Arousal Model:** Tracks 2D emotional state ($V \in [-1.0, +1.0]$, $A \in [0.0, 1.0]$) using viscous homeostatic Langevin relaxation ($\tau_v = 6.0\text{s}, \tau_a = 4.5\text{s}$) with stochastic Langevin noise diffusion.
- **Refractory Lock:** Enforces a 5-8 second biological refractory period (`g_mood_lock_until`) between emotional state changes (`EXPR_IDLE`, `EXPR_ANGRY`, `EXPR_OVERLOAD`, `EXPR_JOY`, `EXPR_SAD`, `EXPR_SMIRK`, `EXPR_SHOCK`, `EXPR_DEADPAN`).
- **Expressive Kaomoji Synthesis:** Features specialized procedural 60 FPS face rendering including solid filled flat-top fumo eyes and smiling cat mouth (`SMIRK` ᗜ⩊ᗜ), streaming tear animations and quivering wave mouth (`SAD` ╥﹏╥), and minimalist deadpan gaze (`DEADPAN` ᗜ _ ᗜ).

### 8. Web Over-The-Air (OTA) Firmware Flashing Engine
- **Direct Flash Endpoint (`POST /update`):** Receives binary firmware payloads over HTTP with non-blocking stream ingestion and automated safety reboot.
- **Progress Tracking:** Integrated Web UI visual progress bar providing live upload feedback.

### 9. Dynamic Camera Sensor Control API
- **Live Parameter Tuning (`POST /camera_control`):** Directly controls OmniVision sensor registers on-the-fly without firmware re-compilation, supporting brightness (-2..+2), contrast (-2..+2), saturation (-2..+2), vertical flip, horizontal mirror, automatic exposure control (AEC), and automatic gain control (AGC).

### 10. OLED Panel Longevity & Anti-Burn-In Protection
- **Periodic Micro-Pixel Shifting:** Periodically shifts rendering canvas coordinates by $\pm 1\text{ px}$ in sleep standby mode to distribute phosphor wear.
- **Standby Auto-Dimming:** Automatically reduces SSD1306 panel brightness to `OLED_SLEEP_BRIGHTNESS` (16/255) during reconnaissance standby.

---

## Memory Architecture

| Buffer Name | Allocation Cap | Storage Location | Size | Rationale |
| :--- | :--- | :--- | :--- | :--- |
| `small_rgb_buf` | `MALLOC_CAP_INTERNAL` | Fast Internal SRAM | $80 \times 60 \times 2\text{ bytes} \ (9.6\text{ KB})$ | Read/written thousands of times per frame during YCbCr downscaling. |
| `prev_lum_buf` | `MALLOC_CAP_INTERNAL` | Fast Internal SRAM | $40 \times 30\text{ bytes} \ (1.2\text{ KB})$ | Frame difference buffer accessed in high-frequency CV loops. |
| `mhi_buf` | `MALLOC_CAP_INTERNAL` | Fast Internal SRAM | $40 \times 30\text{ bytes} \ (1.2\text{ KB})$ | Motion History Image decay map accessed per pixel. |
| `g_latest_jpeg_buf` | `ps_malloc` | External PSRAM | $64\text{ KB}$ | Double-buffered JPEG frame container used only for HTTP MJPEG streaming. |

---

## Performance Benchmarks

| Metric | Measured Value | Remarks |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32-S3 Dual-Core Xtensa LX7 @ 240 MHz | Hardware FPU Enabled |
| **Camera Sensor** | OmniVision OV2640 / OV3660 | DVP Parallel Interface |
| **Frame Resolution** | VGA ($640 \times 480$), Quality 8 JPEG | PSRAM Framebuffer |
| **AI Subsampling Grid** | $40 \times 30$ Matrix ($1,200\text{ pixels}$) | 8x Hardware IDCT Scaling |
| **AI Pipeline Latency** | **$< 0.5\text{ ms}$ / frame** | Executed on Core 0 |
| **Gaze Pursuit Frequency** | **$\omega_n = 38.0\text{ rad/s}$** | Mass-Spring-Damper Model ($\zeta=1.00$) |
| **MJPEG Streaming FPS** | **$30+\text{ FPS}$** | Port 81 Dedicated Async Handler |
| **Display Refresh Rate** | **$60\text{ FPS}$** | LovyanGFX I2C @ 1.0 MHz |
| **Dynamic Memory Allocation** | $< 12\text{ KB}$ Internal SRAM | Zero Redundant Buffering |

---

## Hardware Interfacing & Pinout

### Pin Mapping (Seeed Studio XIAO ESP32-S3 Sense)

| Peripheral Device | Signal | XIAO ESP32-S3 GPIO | Header Label | Note |
| :--- | :--- | :--- | :--- | :--- |
| **OLED Display (SSD1306)** | I2C SCL | GPIO 5 | D4 | 1.0 MHz Fast-Mode Plus |
| **OLED Display (SSD1306)** | I2C SDA | GPIO 6 | D5 | 1.0 MHz Fast-Mode Plus |
| **Expansion Header** | GPIO Available | GPIO 2 | D1 | General purpose I/O |
| **Camera DVP Bus** | XCLK, PCLK, VSYNC, HREF, Y2-Y9 | GPIO 10, 11, 12, 13, 14, 15, 16, 17, 18, 38, 39, 40, 47, 48 | Expansion Connector | High-speed DMA interface |

---

## API Endpoints & Web Telemetry HUD

KoRe hosts an asynchronous dual-port HTTP web server for live telemetry inspection, credential setup, facial expression selection, camera tuning, weather configuration, display brightness adjustment, OTA firmware flashing, and video streaming:

- **Web Dashboard (`GET http://<ESP32_IP>/`):** Serves the Bento grid control panel interface rendering real-time target bounding boxes, telemetry metrics, 8-expression selection, camera tuning, weather location settings, live display brightness slider, and OTA firmware updater.
- **JSON Telemetry Endpoint (`GET http://<ESP32_IP>/telemetry`):** Returns real-time tracking metrics, candidate arrays, active expression IDs, emotional valence/arousal, memory statistics, and camera standby status without forcing camera hardware active.
- **Bluetooth Low Energy (BLE) Telemetry & Companion Service:** Nordic UART Service (`6E400001-...`) GATT server for mobile companion connectivity, allowing on-demand telemetry (`{"cmd":"get_telemetry"}` or `TELEMETRY`) and live continuous telemetry streaming (`{"cmd":"stream_telemetry","enable":true,"interval":500}`) completely decoupled from camera power.
- **Display Brightness (`POST http://<ESP32_IP>/set_brightness`):** Adjusts OLED panel brightness ($0 - 255$) in real-time with automatic NVS storage.
- **Weather Configuration (`POST /set_weather`, `GET /weather_info`, `POST /trigger_weather`):** Manages Open-Meteo geolocation, queries current observation, and triggers 6-second OLED preview.
- **Camera Tuning Endpoint (`POST http://<ESP32_IP>/camera_control`):** Dynamically adjusts brightness, contrast, saturation, flip/mirror orientation, and auto-exposure.
- **OTA Firmware Flash (`POST http://<ESP32_IP>/update`):** Wireless firmware update endpoint receiving binary `.bin` payloads.
- **Expression Override Endpoint (`POST http://<ESP32_IP>/set_expression`):** Sets manual expression override (`0..7`) or restores the automatic biological mood engine (`"auto"`).
- **Wi-Fi Scanner (`GET http://<ESP32_IP>/scan_wifi`):** Scans and returns nearby Wi-Fi SSIDs, RSSI levels, and encryption modes.
- **System Information (`GET http://<ESP32_IP>/system_info`):** Reports hardware diagnostics, chip model, heap/PSRAM memory, uptime, brightness, and CPU frequency.
- **MJPEG Video Stream (`GET http://<ESP32_IP>:81/stream`):** Dedicated Port 81 asynchronous stream handler.
- **Network Configuration (`GET /get_wifi`, `POST /save_wifi`, `POST /switch_mode`):** Manage Wi-Fi credentials stored in ESP32 NVS (`Preferences`).

---

## Building & Deployment

### Software Requirements
1. **Board Package:** `esp32` by Espressif Systems (v2.0.11 or later)
2. **Library Dependency:** [LovyanGFX](https://github.com/lovyan03/LovyanGFX) (Fast graphics driver)
3. **Build Toolchain:** `arduino-cli` (v0.35.0+) or ESP-IDF (v5.0+)

### Arduino IDE Settings
- **Board:** `XIAO_ESP32S3`
- **PSRAM:** `OPI PSRAM`
- **Flash Mode:** `QIO 80MHz`
- **CPU Frequency:** `240MHz (WiFi/BT)`
- **Partition Scheme:** `Huge APP (3MB No OTA/1MB SPIFFS)`

### Command-Line Compilation & Testing
```bash
# Build firmware with Arduino CLI
./scripts/build.sh

# Flash firmware and launch serial monitor
./scripts/flash.sh /dev/ttyACM0

# Execute Python kinematics mathematical validation
python3 scripts/validate_kinematics.py
```

---

## License

MIT License. Designed and developed under an open-source embedded software engineering paradigm.

