# KoRe System Architecture and Core Partitioning

## 1. Executive Summary

KoRe (*Kinematic Optical Recognition Engine*) is an asynchronous, dual-core embedded vision and biomechanical ocular synthesis system designed for the Seeed Studio XIAO ESP32-S3 Sense (Dual-Core Xtensa LX7 @ 240 MHz).

The firmware architecture decouples image acquisition, optical clustering, and networking on Core 0 from the 60 FPS real-time ocular kinematics and sprite rendering pipeline on Core 1.

---

## 2. Dual-Core Task Allocation & Priority Topology

```
+---------------------------------------------------------------------------------+
|                       OV2640 / OV3660 CAMERA SENSOR (VGA)                       |
+---------------------------------------------------------------------------------+
                                         |
                                         v
+---------------------------------------------------------------------------------+
| CORE 0: cameraTask (Priority 2, Vision Pipeline & Power Scaling)                |
| - Frame Acquisition via DMA (esp_camera_fb_get)                                 |
| - 8x Spatial Downscaling (640x480 -> 80x60 Internal SRAM Array)                 |
| - 4x3 Sector Illumination Profiling & Dynamic YCbCr Chrominance Adaptation      |
| - Motion History Image (MHI) Temporal Decay & Spatial Moments (M00..M11)        |
| - Spatial Multi-Candidate Clustering & Priority Ranking (Max 3 Targets)         |
| - 2D Discrete Kalman Filter ([x, y, vx, vy]^T with Dynamic Noise Covariance)    |
| - Dynamic Frequency Scaling (DFS: 240 MHz Active <-> 80 MHz Sleep Standby)      |
| - SCCB Register Standby Control (OV2640 0x09 bit 4 / OV3660 0x3008)             |
| - PSRAM Double-Buffered JPEG Frame Replication (g_stream_mutex)                 |
+---------------------------------------------------------------------------------+
                                         |
                         [ Spinlock: g_target_mutex ]
                                         |
                                         v
+---------------------------------------------------------------------------------+
| CORE 1: oledTask (Priority 1, 60 FPS Kinematics & Rendering Loop)               |
| - 2D Russell Circumplex Affective Engine (Valence-Arousal Langevin Diffusion)   |
| - Unified Rigid 2D Facial Rig (Eyes, Eyebrows, Mouth Bound to Global Offset)    |
| - Coordinate Hysteresis Filtering (getFilteredOx, getFilteredOy)                |
| - Ocular Dynamics (38.0 rad/s Critically Damped Mass-Spring-Damper)             |
| - 5th-Order Minimum-Jerk Saccades (Flash & Hogan Formulation)                   |
| - Fixation Micro-Kinetics (Mean-Reverting Brownian Random Walk)                 |
| - Non-Blocking Eyelid State Machine (Idle -> Closing -> Opening -> Blink-Chain) |
| - LovyanGFX 1-Bit Sprite Renderer (1.0 MHz Fast-Mode Plus I2C Bus)              |
+---------------------------------------------------------------------------------+
                                         |
                                         v
+---------------------------------------------------------------------------------+
| CONCURRENT HTTP TELEMETRY & MJPEG STREAMER                                      |
| - Port 80: Async JSON Telemetry Endpoint (/telemetry) & Configuration Web UI    |
| - Port 81: Non-Blocking MJPEG Video Stream (/stream)                            |
| - NVS Flash Persistence (/save_wifi, /switch_mode)                              |
+---------------------------------------------------------------------------------+
```

---

## 3. Inter-Core Concurrency & Spinlock Discipline

1. **Spinlock Hold Duration:**
   `portENTER_CRITICAL(&g_target_mutex)` and `portENTER_CRITICAL(&g_stream_mutex)` must strictly enclose pointer swaps and memory copy operations only. Critical sections must execute in $< 5 \ \mu\text{s}$.
2. **Lock Independence:**
   Core 0 and Core 1 never block each other during mathematical computation, sensor communication, or I2C display updates.

---

## 4. Memory Layout and Hardware Partitioning

| Identifier | Location | Allocation Flag | Size | Architectural Role |
| :--- | :--- | :--- | :--- | :--- |
| `small_rgb_buf` | Internal SRAM | `MALLOC_CAP_INTERNAL` | 9.6 KB | High-frequency 80x60 pixel downscaling array |
| `prev_lum_buf` | Internal SRAM | `MALLOC_CAP_INTERNAL` | 1.2 KB | Differential luminance history map |
| `mhi_buf` | Internal SRAM | `MALLOC_CAP_INTERNAL` | 1.2 KB | Temporal motion decay matrix |
| `g_latest_jpeg_buf` | External PSRAM | `ps_malloc` | 64.0 KB | Double-buffered MJPEG HTTP streaming container |

---

## 5. Dynamic Frequency Scaling (DFS) and Power States

- **Active Mode (Target Detected or Web Client Active):**
  CPU frequency: 240 MHz. Camera active. OLED refresh: 60 FPS (16.66 ms).
- **Standby Mode (No Target for $> 5000\text{ ms}$):**
  CPU frequency: 80 MHz via `setCpuFrequencyMhz(80)`. Camera sensor placed in software standby via SCCB register writes (`0x09` on OV2640 / `0x3008` on OV3660). OLED refresh: 30 FPS (33.33 ms).
