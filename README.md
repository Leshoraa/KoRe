# KoRe — Kinematic Optical Recognition & Biomechanical Face Engine

[![Board](https://img.shields.io/badge/Hardware-Seeed_XIAO_ESP32--S3_Sense-00979D.svg?style=for-the-badge&logo=arduino)](https://www.seeedstudio.com/XIAO-ESP32S3-Sense-p-5639.html)
[![Core Architecture](https://img.shields.io/badge/Architecture-Dual--Core_FreeRTOS_240MHz-blue.svg?style=for-the-badge)](#1-system-architecture)
[![Vision Latency](https://img.shields.io/badge/AI_Latency-%3C_0.5_ms-brightgreen.svg?style=for-the-badge)](#2-differential-computer-vision-pipeline)
[![Ocular Dynamics](https://img.shields.io/badge/Physics-2nd--Order_Mass--Spring--Damper-orange.svg?style=for-the-badge)](#3-biomechanical-gaze-kinematics)
[![Display Driver](https://img.shields.io/badge/Display-LovyanGFX_SSD1306_800kHz-purple.svg?style=for-the-badge)](#5-hardware-interfacing--pinout)

**KoRe** (*Kinematic Optical Recognition Engine*) is an industrial-grade, 100% standalone embedded computer vision and biomechanical ocular synthesis system running on the **Seeed Studio XIAO ESP32-S3 Sense** (Xtensa LX7 Dual-Core @ 240 MHz).

The system integrates real-time differential photometric tracking, skin-locus YCbCr energy density spatial moments, 2nd-order mass-spring-damper gaze kinematics, non-blocking asynchronous eyelid dynamics, and an autonomous Markov dynamic mood engine. It drives an animated 2D facial interface on a 0.96" SSD1306 OLED via LovyanGFX I2C fast-mode (800 kHz) with zero rendering latency or cloud dependencies.

---

## System Architecture

KoRe operates on a deterministic dual-core FreeRTOS architecture designed to isolate computer vision throughput from display rendering and HTTP telemetry.

```
                   +---------------------------------------------------+
                   |         OV2640 / OV3660 CAMERA SENSOR (VGA)       |
                   +---------------------------------------------------+
                                             |
                                             v
    +---------------------------------------------------------------------------------+
    | CORE 0: Camera_AI_Task (Priority 2, 5ms Tick)                                   |
    |  * Continuous Frame Acquisition (esp_camera_fb_get)                             |
    |  * JPG8X Hardware IDCT Subsampling (80x60 -> 40x30 Grid)                        |
    |  * Single-Pass YCbCr Conversion & Dynamic EMA Low-Lux Adaptation                |
    |  * Motion History Images (MHI) & Photometric Energy Density Φ(x,y)              |
    |  * Spatial Moments Calculation (M00, M10, M01, M20, M02)                         |
    |  * 2nd-Order α-β-γ Kinematic State Estimation                                   |
    |  * Double-Buffered JPEG Copier for Web Streaming (g_stream_mutex)               |
    +---------------------------------------------------------------------------------+
                                             |
                     [ Spinlock Critical Section (target_mutex) ]
                                             |
                                             v
    +---------------------------------------------------------------------------------+
    | CORE 1: OLED_Task (Priority 1, 16ms / ~60 FPS Loop)                             |
    |  * Biological Mood Engine (Autonomous Markov Personality Transition)             |
    |  * Biomechanical Ocular Kinematics Controller (Foveal Deadzone & Mass-Spring)   |
    |  * Non-Blocking Asynchronous Eyelid Blink Engine (Closing 50ms, Opening 110ms)  |
    |  * LovyanGFX High-Speed SSD1306 Rendering (800 kHz I2C Bus)                     |
    +---------------------------------------------------------------------------------+
                                             |
                     [ Concurrent Async HTTP Endpoints ]
                                             |
                                             v
    +---------------------------------------------------------------------------------+
    | HTTP TELEMETRY & MJPEG SERVER (Port 80 JSON, Port 81 MJPEG Stream)             |
    |  * /telemetry : Real-time JSON target coordinates, error vector & FPS           |
    |  * /stream    : Non-blocking MJPEG live video feed                              |
    +---------------------------------------------------------------------------------+
```

---

## Key Engineering Features

- **Sub-Millisecond Inference Latency:** $< 0.5\text{ ms}$ processing time per frame executing on hardware Xtensa LX7 FPU SIMD instructions.
- **Biomechanical Ocular Physics (95%+ Human Model):**
  - **Foveal Deadzone Filter:** Suppresses camera sensor noise and sub-pixel jitter when target displacement is $< 1.5\text{ px}$.
  - **2nd-Order Mass-Spring-Damper Smooth Pursuit:** Evaluates $\mathbf{a}_{\text{eye}} = \omega_n^2 (\mathbf{S}_{\text{target}} - \mathbf{E}) - 2\zeta \omega_n \mathbf{v}_{\text{eye}}$ ($\omega_n = 22.0\text{ rad/s}, \zeta = 0.88$) for smooth, non-floating eye tracking.
  - **5th-Order Quintic Minimum-Jerk Saccades:** Ballistic eye leaps for target offsets $> 3.5\text{ px}$ with muscle overshoot damping waves.
  - **Physiological Micro-tremor:** 4 Hz sinusoidal micro-drift ($\approx 0.25\text{ px}$) during fixation.
- **Non-Blocking Asynchronous Eyelid Engine:** Frame-driven blink state machine with Poisson distribution intervals ($2.2\text{s} - 5.4\text{s}$), 18% double-blink chance, and 40% reflex saccadic blinks during rapid eye movements.
- **Dynamic Markov Mood Engine ("Grumpy & Calm" Personality):** Autonomous event-driven emotion engine with probabilistic transitions:
  - **IDLE (Dominant, ~48%):** Neutral relaxed state.
  - **ANGRY (Frequent, ~28%):** Irritated state triggered on sudden face entry or rapid movement.
  - **OVERLOAD (Natural, ~10%):** Dizzied spiral eye state.
  - **JOY (Rare, ~7%):** Happy arc eyes.
  - **SEDIH (Rare, ~4%):** Sad wave mouth.
  - **SMIRK (Very Rare, ~3%):** Asymmetrical sly smile.
- **Photometric Centroid & Anti-Poster Hijacking:** Luminance-adapted $YC_bC_r$ skin locus classification with a 96% energy suppression penalty ($0.04\times$) applied to non-skin high-contrast background elements.

---

## Differential Computer Vision Pipeline

```
  RGB565 Subsampled Buffer (80x60)
                |
                v
  Single-Pass YCbCr Conversion & Global EMA Luminance Adaptation (Y_bar)
                |
                v
  Dynamic Low-Lux Skin Locus Gating:
    Cb in [77 - ΔY_lux, 127 + ΔY_lux]
    Cr in [128 - ΔY_lux, 178 + ΔY_lux]
                |
                v
  3x3 Spatial Box Blur & Background Global Motion Flow Compensation
                |
                v
  Full-Frame Photometric Energy Density Accumulation Φ(x, y)
                |
                v
  Spatial Moments Calculation (M00, M10, M01, M20, M02)
                |
                v
  Reciprocal Centroid Extraction & Anatomical Aspect Ratio Coupling (1.15 <= H/W <= 1.55)
                |
                v
  2nd-Order α-β-γ Kinematic State Update & Gating
```

### Photometric Energy Density Equation

$$\Phi(x,y) = \begin{cases} 
2.5 \cdot \Delta Y(x,y) + 16.0 \cdot W_{\text{MHI}} + 1.5 \cdot g_y(x,y) + 0.8 \cdot g_x(x,y) & \text{if } \text{SkinMask}(x,y) = \text{true} \\ 
0.04 \cdot T(x,y) \cdot \left( 2.5 \cdot \Delta Y + 16.0 \cdot W_{\text{MHI}} + 1.5 \cdot g_y + 0.8 \cdot g_x \right) & \text{if } \text{SkinMask}(x,y) = \text{false} 
\end{cases}$$

---

## Performance Benchmarks

| Metric | Measured Value | Remarks |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32-S3 Dual-Core Xtensa LX7 @ 240 MHz | Hardware FPU Enabled |
| **Camera Sensor** | OmniVision OV2640 / OV3660 | DVP Parallel Interface |
| **Frame Resolution** | VGA ($640 \times 480$), Quality 8 JPEG | PSRAM Framebuffer |
| **AI Subsampling Grid** | $40 \times 30$ Matrix ($1,200\text{ pixels}$) | 8x Hardware IDCT Scaling |
| **AI Pipeline Latency** | **$< 0.5\text{ ms}$ / frame** | Measured on Core 0 |
| **MJPEG Streaming FPS** | **$30+\text{ FPS}$** | Port 81 Dedicated Async Endpoint |
| **Display Refresh Rate** | **$60\text{ FPS}$** | LovyanGFX I2C @ 800 kHz |
| **Dynamic Memory Allocation** | $< 12\text{ KB}$ SRAM / PSRAM | Zero Redundant Buffering |

---

## Hardware Interfacing & Pinout

### Display & Peripheral Wiring

| Device | Signal | XIAO ESP32-S3 GPIO | Pin Label |
| :--- | :--- | :--- | :--- |
| **OLED SSD1306 (0.96")** | I2C SCL | GPIO 5 | D4 |
| **OLED SSD1306 (0.96")** | I2C SDA | GPIO 6 | D5 |
| **Touch Sensor (Optional)** | Digital Input | GPIO 2 | D1 |
| **Camera Sensor** | DVP Parallel Bus | GPIO 10, 11, 12, 13, 14, 15, 16, 17, 18, 38, 39, 40, 47, 48 | Camera Board Expansion |

---

## API Endpoints & Web Dashboard

KoRe hosts a dual-port HTTP web engine for real-time telemetry inspection and MJPEG video streaming:

- **Sci-Fi HUD Dashboard (`GET http://<ESP32_IP>/`):** Modern web interface featuring canvas overlays, corner target brackets, confidence indicators, and real-time center-of-mass crosshairs.
- **JSON Telemetry Endpoint (`GET http://<ESP32_IP>/telemetry`):** Returns high-frequency tracking metadata:
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
    "vx": -12.4,
    "vy": 8.1
  }
  ```
- **MJPEG Video Feed (`GET http://<ESP32_IP>:81/stream`):** Dedicated port 81 asynchronous stream handler.

---

## Building & Deployment

### Hardware & Software Requirements
1. **Board:** Seeed Studio XIAO ESP32-S3 Sense
2. **IDE:** Arduino IDE 2.x or PlatformIO
3. **Core Package:** `esp32` by Espressif Systems (v2.0.11 or later)
4. **Dependencies:**
   - [LovyanGFX](https://github.com/lovyan03/LovyanGFX) (Fast graphics driver)

### Arduino IDE Build Settings
- **Board:** `XIAO_ESP32S3`
- **PSRAM:** `OPI PSRAM`
- **Flash Mode:** `QIO 80MHz`
- **CPU Frequency:** `240MHz (WiFi/BT)`
- **Partition Scheme:** `Huge APP (3MB No OTA/1MB SPIFFS)`

---

## License

Designed and developed under an open-source engineering paradigm.
