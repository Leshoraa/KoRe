# KORE ENGINEERING SPECIFICATION AND ARCHITECTURAL STANDARDS
**Kinematic Optical Recognition and Biomechanical Face Engine**
*Target Platform: Seeed Studio XIAO ESP32-S3 Sense (Dual-Core Xtensa LX7 @ 240 MHz)*
*Document Revision: 2.5.0 : Production Technical Specification*

---

## TABLE OF CONTENTS
1. [System Architecture and Core Partitioning](#1-system-architecture-and-core-partitioning)
2. [Code Hygiene and Zero-Trashcode Enforcement](#2-code-hygiene-and-zero-trashcode-enforcement)
3. [Documentation and Annotation Standards](#3-documentation-and-annotation-standards)
4. [Deep Code Analysis and Bug Prevention Protocol](#4-deep-code-analysis-and-bug-prevention-protocol)
5. [Systemic Impact Assessment and Blast Radius Matrix](#5-systemic-impact-assessment-and-blast-radius-matrix)
6. [Strict UI, UX, and Visual Design Anti-Pattern Rules](#6-strict-ui-ux-and-visual-design-anti-pattern-rules)
7. [Memory Layout and Hot-Path Computational Discipline](#7-memory-layout-and-hot-path-computational-discipline)
8. [Mathematical, Physical, and Biomechanical Modeling Specifications](#8-mathematical-physical-and-biomechanical-modeling-specifications)
9. [Hardware Abstraction, Bus Overclocking, and Dynamic Power Management](#9-hardware-abstraction-bus-overclocking-and-dynamic-power-management)
10. [Repository File Hierarchy and Component Modularization](#10-repository-file-hierarchy-and-component-modularization)
11. [Production Release Verification Checklist](#11-production-release-verification-checklist)

---

## 1. SYSTEM ARCHITECTURE AND CORE PARTITIONING

KoRe runs on an asynchronous dual-core FreeRTOS topology, isolating image acquisition and signal processing from the real-time ocular kinematics rendering loop.

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
| - 2D Discrete Kalman Filter ([x, y, vx, vy]^T with Adaptive Measurement Noise)  |
| - Dynamic Frequency Scaling (DFS: 240 MHz Active <-> 80 MHz Sleep Standby)      |
| - Dynamic Wi-Fi Modem Power Save (WIFI_PS_MIN_MODEM during Standby)            |
| - SCCB Register Standby Control (OV2640 0x09 bit 4 / OV3660 0x3008)             |
| - PSRAM Double-Buffered JPEG Frame Replication (g_stream_mutex)                 |
+---------------------------------------------------------------------------------+
                                         |
                         [ Spinlock: target_mutex ]
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
| - OLED Anti-Burn-In Protection (+/-1 px Micro-Shift during Standby)             |
| - Graphical 1-Bit Weather Screen Rendering (Open-Meteo Periodic Popups)         |
| - LovyanGFX 1-Bit Sprite Renderer (1.0 MHz Fast-Mode Plus I2C Bus)              |
+---------------------------------------------------------------------------------+
                                         |
                                         v
+---------------------------------------------------------------------------------+
| CONCURRENT HTTP TELEMETRY, OTA & MJPEG SERVER                                   |
| - Port 80: Async JSON Telemetry (/telemetry) & Configuration Web UI (/)         |
| - Port 80: Camera Tuning (/camera_control), Web OTA Flash Updater (/update)     |
| - Port 80: Display Brightness (/set_brightness) & Weather Sync (/set_weather)   |
| - Port 80: Wi-Fi Scanner (/scan_wifi) & Diagnostics (/system_info)             |
| - Port 80: NVS Flash Persistence (/save_wifi, /switch_mode, /set_expression)    |
| - Port 81: Non-Blocking MJPEG Video Stream (/stream)                            |
+---------------------------------------------------------------------------------+
```

---

## 2. CODE HYGIENE AND ZERO-TRASHCODE ENFORCEMENT

Every commit and pull request must strictly eliminate legacy computational residue.

### 2.1 Prohibited Coding Practices
- **No Zombie Code:**
  Commented-out code blocks (`// old logic`) are strictly forbidden. Previous implementations are tracked through Git commit history. Unused code must be deleted immediately.
- **No Orphan Print Statements:**
  Raw calls to `Serial.println`, `printf`, or unformatted UART dumps are prohibited. Debugging must strictly utilize structured macros that compile to empty statements in production builds.
- **No Unused Identifiers:**
  Unused global buffers, unread loop variables, and redundant FreeRTOS handle declarations must be removed.
- **No Magic Numbers:**
  All physical coefficients, geometric dimensions, register addresses, and timeout limits must be defined as named constants with explicit units in their names (e.g., `GAZE_NATURAL_FREQUENCY_RAD_S`, `MHI_DECAY_THRESHOLD_US`).

### 2.2 Production Logging Architecture
```c
#if defined(ENABLE_SERIAL_DEBUG) && (ENABLE_SERIAL_DEBUG == 1)
    #define KORE_LOG_DBG(tag, fmt, ...) printf("[%s][DBG] %s:%d: " fmt "\n", tag, __func__, __LINE__, ##__VA_ARGS__)
    #define KORE_LOG_INF(tag, fmt, ...)  printf("[%s][INF] " fmt "\n", tag, ##__VA_ARGS__)
#else
    #define KORE_LOG_DBG(tag, fmt, ...) ((void)0)
    #define KORE_LOG_INF(tag, fmt, ...)  ((void)0)
#endif
#define KORE_LOG_ERR(tag, fmt, ...)     fprintf(stderr, "[%s][ERR] %s:%d: " fmt "\n", tag, __func__, __LINE__, ##__VA_ARGS__)
```

---

## 3. DOCUMENTATION AND ANNOTATION STANDARDS

Comments must not restate obvious language syntax. Documentation must explain engineering rationale, register constraints, memory alignment, and mathematical formulations.

### 3.1 Commentary Standards
- Do not state what the code does; state why the specific architectural choice was made.
- Complex pointer arithmetic, spinlocks, and register manipulation must include rationale blocks.
- Em dashes are prohibited in all documentation and code comments; use standard colons, hyphens, or parentheses instead.

```c
/* Correct Commentary Example */
/*
 * Allocate vision frame buffers in internal SRAM rather than external PSRAM.
 * The YCbCr downscaling and MHI accumulation access this buffer millions of times
 * per second; PSRAM bus latency is insufficient for sub-millisecond execution.
 */
static uint8_t* small_rgb_buf = (uint8_t*)heap_caps_malloc(80 * 60 * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
```

### 3.2 Function Interface Contract Format
Every core computational routine must provide a formal contract header:

```c
/**
 * @brief  Updates discrete 2D Kalman filter state vector with dynamic measurement covariance.
 * @details Predicts prior state using constant velocity kinematics, evaluates measurement
 *          residual covariance against sector illumination confidence, and updates posterior state.
 *
 * @param[in,out] kf            Pointer to discrete Kalman filter state structure.
 * @param[in]     measured_x    Measured horizontal centroid position in pixels.
 * @param[in]     measured_y    Measured vertical centroid position in pixels.
 * @param[in]     confidence    Target confidence metric in range [0.0, 1.0].
 * @param[in]     dt            Delta time since previous filter update step in seconds.
 *
 * @return True if filter converged without matrix singularity; false otherwise.
 *
 * @pre   kf != NULL && dt > 0.0f.
 * @post  kf->x and kf->y contain filtered state within frame boundaries [0, 80] and [0, 60].
 * @complexity Time: O(1), Space: O(1) (Zero dynamic allocation).
 * @thread_safety Requires external mutex protection when invoked across multiple FreeRTOS tasks.
 */
bool kf_update_2d(KalmanFilter2D *kf, float measured_x, float measured_y, float confidence, float dt);
```

---

## 4. DEEP CODE ANALYSIS AND BUG PREVENTION PROTOCOL

To ensure zero latent runtime defects, every code modification must be subjected to a rigorous multi-vector analysis protocol prior to implementation.

### 4.1 Static and Deterministic Bug Prevention Vectors
1. **Pointer and Boundary Verification:**
   - Every pointer parameter must be guarded against `NULL` prior to dereferencing.
   - All array indexing and buffer lookups must be bounded explicitly or masked using bitwise wrap-around (`idx & (CAPACITY - 1)`).
2. **Numerical Domain and Stability Guards:**
   - Divisions must be guarded against zero denominators with an explicit positive epsilon ($\epsilon = 10^{-6}$).
   - All floating-point operations within kinematic and filter routines must be validated against `isnan()` and `isinf()`.
   - Trigonometric arguments must be clamped strictly within domain limits $[-1.0, 1.0]$.
3. **Concurrency and Race Condition Prevention:**
   - Shared cross-core variables (`current_target`, `g_latest_jpeg_buf`, `g_stream_clients`) must never be accessed without locking their designated spinlock (`target_mutex`, `g_stream_mutex`).
   - Volatile qualifiers (`volatile`) must be placed on flags mutated across ISRs or tasks.
4. **FreeRTOS Stack Watermark Auditing:**
   - Every FreeRTOS task (`cameraTask`, `oledTask`, `httpd`) must be monitored using `uxTaskGetStackHighWaterMark()` to prevent silent stack overflows during nested interrupt service routines.

---

## 5. SYSTEMIC IMPACT ASSESSMENT AND BLAST RADIUS MATRIX

Before introducing any functional change, refactor, or optimization, the engineer must execute an impact assessment across all six operational dimensions:

| Impact Dimension | Potential Failure Mode | Prevention and Verification Criterion |
| :--- | :--- | :--- |
| **Inter-Core Concurrency** | Spinlock contention or lock starvation between Core 0 and Core 1. | Keep critical sections below $5 \ \mu\text{s}$. Never perform math, I/O, or delays inside `portENTER_CRITICAL`. |
| **Real-Time Scheduling** | Core 1 drops below $60\text{ FPS}$ ($> 16.66\text{ ms}$ period) causing jitter. | Profile `oledTask` execution budget; ensure rendering plus kinematics executes in $< 11.0\text{ ms}$. |
| **Memory Bus Latency** | PSRAM read/write stalls freezing the DMA camera pipeline. | Maintain vision pixel arrays in internal SRAM (`MALLOC_CAP_INTERNAL`); restrict PSRAM strictly to network JPEG chunks. |
| **Dynamic Power & DFS** | Background networking sockets prevent the CPU from entering $80\text{ MHz}$ sleep mode. | Verify that inactive states trigger `setCpuFrequencyMhz(80)` and SCCB sensor sleep registers after $5000\text{ ms}$. |
| **Finite State Invariants** | Saccade, Eyelid, or Expression state machines entering an unhandled or lockup state. | Ensure state machines have explicit timeout fallbacks and default reset transitions for all discrete states. |
| **NVS / Flash Integrity** | Corrupted Wi-Fi credentials or AP parameters causing continuous reboot loops. | Validate NVS read lengths and use default fallback strings if flash storage reads fail. |

---

## 6. STRICT UI, UX, AND VISUAL DESIGN ANTI-PATTERN RULES

The following design constraints are strictly enforced across all web dashboards, telemetry portals, graphical interfaces, documentation layouts, and visualization assets:

### 6.1 Prohibited Color Schemes and Palettes
- **No Rainbow Colors:** Multi-hued rainbow gradients and multi-colored spectrum schemes are forbidden.
- **No Harsh Gradients:** Abrupt linear color shifts and high-contrast color transitions are prohibited.
- **No Neon Colors:** Over-saturated fluorescent colors and glowing cyberpunk hues are banned.
- **No Basic Pastel Colors:** Washed-out pastel card palettes and chalky UI backgrounds are prohibited.
- **No Purple and Black Clichés:** Generic dark-mode purple-on-black or indigo-on-black palettes are forbidden.
- **No Pure White Backgrounds:** Pure `#FFFFFF` blinding white canvas backgrounds are forbidden; utilize calibrated neutral gray or dark matte surfaces.

### 6.2 Prohibited Geometry, Layouts, and Textures
- **No Over Corner Radius:** Pill-shaped cards, excessively curved containers, and exaggerated border radii ($> 8\text{ px}$) are prohibited.
- **No Soft Corner Radius Clichés:** Overly soft bubbly UI card corners are banned; use sharp or minimal geometric radii ($2\text{ px} - 4\text{ px}$).
- **No Heavy Drop Shadows:** Deep, blurry, or fuzzy drop shadows are prohibited; use clean 1-pixel borders for visual separation.
- **No Liquid Glass or Glassmorphism:** Translucent frosted-glass backdrops (`backdrop-filter: blur()`) with glossy reflections are forbidden.
- **No Dot Grids and Radial Orbs:** Glowing radial background balls, neon mesh gradients, and decorative dot grids are prohibited.
- **No Colored Left Stripes:** Decorative colored vertical border-left stripes on alerts, cards, or callouts are forbidden.
- **No 3 Feature Cards in a Row:** Clichéd 3-column symmetrical marketing card layouts are prohibited.

### 6.3 Prohibited Iconography, Typography, and Copywriting
- **No Generic Icon Sets (Lucide, Feather, Sparkle Icons):** Generic off-the-shelf icon kits and magical sparkle icons are prohibited; use functional technical diagrams and domain-specific glyphs.
- **No Animated Arrows:** Pulsing or drifting navigation arrows are forbidden.
- **No Emojis:** Graphical emoji characters are prohibited across all documents, source files, and user interfaces.
- **No Em Dashes:** The em dash character is strictly forbidden; use standard colons, hyphens, or parentheses.
- **No Overused Sans-Serif Fonts:** Inter, Geist, and Space Grotesk are prohibited; utilize clean system monospace, IBM Plex Mono, Roboto Mono, or neutral technical sans-serif fonts.
- **No Fake Testimonials and 3 Pricing Tiers:** Fabricated social proof and three-tier pricing cards are forbidden.
- **No Fake Terminal Window Mockups:** Decorative fake terminal windows with decorative macOS red-yellow-green buttons are forbidden.
- **No Cliché Copywriting ("It is not X, it is Y"):** Formulaic contrasts and buzzword-laden slogans are prohibited.
- **No Checkmark Bullets:** Green or blue checkmark icons used as bullet point replacements are forbidden.

### 6.4 Interaction and Functional Requirements
- **No Gratuitous Hover Animations:** Micro-animations and bouncy hover scaling on every element are forbidden. Motion is restricted strictly to functional state changes.
- **Mandatory Skeleton Loaders:** Asynchronous data streams and telemetry feeds must feature clean structural skeleton loaders to prevent layout shifts.
- **Mandatory Terms of Service and Privacy Policy:** Every network endpoint, web telemetry dashboard, and public interface must include explicit, legally compliant Terms of Service and Privacy Policy disclosures.

---

## 7. MEMORY LAYOUT AND HOT-PATH COMPUTATIONAL DISCIPLINE

### 7.1 Memory Partitioning Strategy (SRAM vs PSRAM)
The ESP32-S3 contains 512 KB internal SRAM and 8 MB octal SPI PSRAM. Memory allocation is strictly partitioned based on bandwidth demands:

| Buffer Identifier | Target Memory Space | Allocation Flag | Size | Rationale |
| :--- | :--- | :--- | :--- | :--- |
| `small_rgb_buf` | Internal SRAM | `MALLOC_CAP_INTERNAL` | $9.6 \text{ KB}$ | High-frequency pixel iteration (80x60 RGB565) |
| `prev_lum_buf` | Internal SRAM | `MALLOC_CAP_INTERNAL` | $1.2 \text{ KB}$ | Differential luminance calculation |
| `mhi_buf` | Internal SRAM | `MALLOC_CAP_INTERNAL` | $1.2 \text{ KB}$ | Temporal motion decay matrix |
| `g_latest_jpeg_buf` | External PSRAM | `MALLOC_CAP_SPIRAM` | $64.0 \text{ KB}$ | Asynchronous HTTP MJPEG streaming chunk buffer |

### 7.2 Hot-Path Execution Rules
1. **Zero Dynamic Allocation:** `malloc()`, `free()`, `new`, and `delete` are prohibited within `cameraTask` and `oledTask` execution loops. All working structures are allocated at startup.
2. **Deterministic Loop Timing:**
   - `oledTask`: Fixed $16.66 \text{ ms}$ interval ($60.0 \text{ FPS}$) in active mode, $33.33 \text{ ms}$ ($30.0 \text{ FPS}$) in standby mode.
   - `cameraTask`: Yields deterministically to lower-priority networking tasks when frame acquisition completes.
3. **Critical Section Minimization:**
   `portENTER_CRITICAL(&target_mutex)` and `portEXIT_CRITICAL(&target_mutex)` must strictly enclose copy operations only. No signal transformations, trigonometric routines, or I/O calls are permitted within critical sections.

---

## 8. MATHEMATICAL, PHYSICAL, AND BIOMECHANICAL MODELING SPECIFICATIONS

All kinematics, ocular dynamics, affective transitions, and tracking filters must maintain $\ge 95\%$ fidelity against empirical human biological data with rigorous numerical stability proofs.

### 8.1 Ocular Dynamics: Second-Order Critically Damped System
The biological human eyeball behaves mechanically as a mass-spring-damper system driven by extraocular muscle pairs:
\[
\ddot{\theta}(t) + 2\zeta\omega_n \dot{\theta}(t) + \omega_n^2 (\theta(t) - \theta_{\text{target}}) = 0
\]
- **Calibrated Natural Frequency:** $\omega_n = 38.0 \text{ rad/s}$ (matches human ocular motor bandwidth).
- **Calibrated Damping Ratio:** $\zeta = 1.00$ (Critical Damping : eliminates overshoot and visual instability).
- **Discrete Semi-Implicit Integration:**
  \[
  a_k = \omega_n^2 (\theta_{\text{target}} - \theta_k) - 2\zeta\omega_n v_k
  \]
  \[
  v_{k+1} = v_k + a_k \cdot \Delta t
  \]
  \[
  \theta_{k+1} = \theta_k + v_{k+1} \cdot \Delta t
  \]

### 8.2 Saccadic Trajectory: Fifth-Order Minimum-Jerk Formulation
To replicate biological saccades characterized by minimum cerebral motor control energy (Flash & Hogan formulation):
\[
\theta(\tau) = \theta_0 + (\theta_{\text{target}} - \theta_0)(10\tau^3 - 15\tau^4 + 6\tau^5), \quad \tau = \frac{t - t_0}{D} \in [0, 1]
\]
\[
\dot{\theta}(\tau) = \frac{\theta_{\text{target}} - \theta_0}{D}(30\tau^2 - 60\tau^3 + 30\tau^4)
\]
- **Main-Sequence Duration Rule:**
  The duration of human saccades scales linearly with angular displacement:
  \[
  D = D_0 + k \cdot |\Delta\theta| \quad (D_0 = 20 \text{ ms}, \ k = 2.5 \text{ ms/degree})
  \]

### 8.3 Affective Dynamics: 2D Russell Circumplex Stochastic Diffusion
The emotional state vector $\mathbf{e}_t = [V_t, A_t]^T$ (Valence, Arousal) evolves via a continuous-time Langevin stochastic differential equation:
\[
d\mathbf{e}_t = -\mathbf{\Gamma} (\mathbf{e}_t - \mathbf{e}_{\text{baseline}}) dt + \mathbf{\Sigma} d\mathbf{W}_t + \mathbf{K}_{\text{stimulus}} \mathbf{u}_t
\]
- $\mathbf{\Gamma} = \text{diag}(\gamma_V, \gamma_A)$: Emotional decay rate returning to baseline homeostasis.
- $\mathbf{\Sigma} d\mathbf{W}_t$: Microscopic stochastic Langevin drift representing spontaneous biological mood fluctuations.
- $\mathbf{K}_{\text{stimulus}} \mathbf{u}_t$: Transient response vector driven by visual tracking confidence and target proximity.

### 8.4 Discrete 2D Kalman State Estimator with Adaptive Covariance
- **State Vector:** $\mathbf{x}_k = [x, y, v_x, v_y]^T$
- **State Transition and Measurement Matrices:**
  \[
  \mathbf{F} = \begin{bmatrix} 1 & 0 & \Delta t & 0 \\ 0 & 1 & 0 & \Delta t \\ 0 & 0 & 1 & 0 \\ 0 & 0 & 0 & 1 \end{bmatrix}, \quad
  \mathbf{H} = \begin{bmatrix} 1 & 0 & 0 & 0 \\ 0 & 1 & 0 & 0 \end{bmatrix}
  \]
- **Dynamic Measurement Noise Tuning:**
  \[
  \mathbf{R}_k = \mathbf{R}_0 \cdot \left(1.0 + \frac{\alpha}{\text{confidence}_k + \epsilon}\right)
  \]
- **Symmetric Covariance Enforcement:**
  \[
  \mathbf{P}_k = \frac{1}{2} (\mathbf{P}_k + \mathbf{P}_k^T)
  \]

### 8.5 Quantitative Modeling Benchmarks
- **Normalized Root Mean Square Error:** $\text{NRMSE} \le 0.05$ relative to empirical physiological benchmarks ($\ge 95\%$ fidelity).
- **Coefficient of Determination:** $R^2 \ge 0.95$.
- **Lyapunov Stability:** All system transition matrix continuous eigenvalues satisfy $\text{Re}(\lambda_i) < 0$.

---

## 9. HARDWARE ABSTRACTION, BUS OVERCLOCKING, AND DYNAMIC POWER MANAGEMENT

### 9.1 Display Driver Overclocking (Fast-Mode Plus I2C)
The 0.96" SSD1306 OLED display ($128 \times 64$ pixels) is driven via the LovyanGFX library with hardware I2C overclocked to $1.0 \text{ MHz}$ (Fast-Mode Plus):
- Standard $400 \text{ kHz}$ I2C transfer latency per full frame: $\approx 26.5 \text{ ms}$ (limits display to $< 38 \text{ FPS}$).
- Overclocked $1.0 \text{ MHz}$ I2C transfer latency per full frame: $\approx 10.6 \text{ ms}$ (enables solid $60.0 \text{ FPS}$ rendering).
- Pinout: `pin_scl = 5`, `pin_sda = 6`, I2C Port 0.

### 9.2 Dynamic Frequency Scaling (DFS) and Camera Standby
To preserve battery runtime on portable 3.7V LiPo configurations:
- **Active State (Target Detected):** CPU frequency set to $240 \text{ MHz}$, camera sensor active, rendering at $60 \text{ FPS}$.
- **Standby State (No Target for $> 5000 \text{ ms}$):**
  - CPU frequency scaled down to $80 \text{ MHz}$ via `setCpuFrequencyMhz(80)` (retains Wi-Fi connectivity without socket termination).
  - Camera sensor placed into hardware standby via SCCB register command (`0x09` bit 4 on OV2640, `0x3008` on OV3660).
  - Display rendering period increased to $33.33 \text{ ms}$ ($30 \text{ FPS}$), reducing I2C bus traffic by $50\%$.

### 9.3 OLED Panel Longevity, Anti-Burn-In, and Live Brightness Control
To mitigate pixel degradation on monochrome OLED matrices:
- **Periodic Micro-Pixel Shifting:** In standby reconnaissance mode, origin rendering offsets are dynamically jittered by $\pm 1\text{ px}$ every 60 seconds.
- **Persistent User Brightness Control:** Panel brightness is configured via `POST /set_brightness` ($0 - 255$) and persisted to NVS without forced auto-dimming.

### 9.4 Wi-Fi Modem Sleep & Power Save Management
- Dynamic modem power saving is engaged (`WIFI_PS_MIN_MODEM`) when the system enters reconnaissance standby, reducing radio current draw during prolonged background monitoring.

### 9.5 Web Over-The-Air (OTA) Streaming Engine and Flash Partitioning
- **Chunked Binary Ingestion:** Receives firmware bytes via `POST /update` using Arduino `Update` library with `U_FLASH` target.
- **Flash Protection:** Validates stream boundary and triggers non-blocking timer before executing `ESP.restart()`.
- **Partition Requirement:** Configured under `Huge APP (3MB No OTA/1MB SPIFFS)` or dual OTA scheme.

### 9.6 Open-Meteo Weather Synchronization Engine
- **Non-Blocking Background Fetch:** Background FreeRTOS task on Core 0 queries current meteorological observations from Open-Meteo every 30 minutes without stalling camera or display rendering.
- **1-Bit Vector Icon Synthesis:** 128x64 OLED layout with dynamic Sun, Cloud, Rain, Lightning, and Fog glyphs.
- **Configurable Periodic Popup:** Displays 6-second weather overview during idle standby or via Web Dashboard trigger.

---

## 10. REPOSITORY FILE HIERARCHY AND COMPONENT MODULARIZATION

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
│   ├── kore_types.h                # Data structs: TrackTarget, ObjectCandidate, Expression, WeatherInfo
│   ├── kore_kalman.h               # Discrete Kalman filter declarations
│   ├── kore_kinematics.h           # Mass-spring-damper and minimum-jerk contracts
│   └── kore_affective.h            # 2D Russell Circumplex Langevin state model
├── src/
│   ├── KoRe.ino                    # Master entry point and FreeRTOS task initializers
│   ├── core/
│   │   ├── camera_pipeline.h       # Camera initialization and vision pipeline header
│   │   ├── camera_pipeline.cpp     # YCbCr downscaling, MHI decay, and spatial clustering
│   │   ├── display_engine.h        # LovyanGFX SSD1306 display engine header
│   │   └── display_engine.cpp      # LovyanGFX SSD1306 sprite composition, facial rig, and weather renderer
│   ├── math/
│   │   ├── kore_kalman.cpp         # 2D discrete Kalman filter implementation
│   │   ├── kore_kinematics.cpp     # Mass-spring-damper integration & minimum-jerk polynomial
│   │   └── kore_affective.cpp      # 2D Russell Circumplex Langevin affective model
│   └── net/
│       ├── http_server.h           # Async HTTP REST API and MJPEG server header
│       ├── http_server.cpp         # Telemetry, expression, camera control, and OTA endpoints
│       ├── weather_client.h        # Open-Meteo background weather client header
│       ├── weather_client.cpp      # Open-Meteo HTTP JSON fetcher and WMO code mapper
│       ├── wifi_manager.h          # NVS Preferences and Wi-Fi manager header
│       ├── wifi_manager.cpp        # NVS Preferences Wi-Fi configuration and captive portal
│       └── web_ui.h                # Web control panel HTML/CSS/JS (embedded PROGMEM string)
├── tests/
│   ├── unit/
│   │   ├── test_affective_langevin.cpp       # Langevin stochastic diffusion convergence tests
│   │   ├── test_kalman_convergence.cpp       # 2D Kalman state estimator convergence tests
│   │   ├── test_kinematics_feedforward.cpp   # Feedforward gaze kinematics validation
│   │   └── test_minimum_jerk.cpp             # 5th-order polynomial trajectory verification
│   └── fixtures/
│       └── sample_motion_frames.h  # Synthetic downscaled frame test vectors
├── scripts/
│   ├── build.sh                    # Command-line compilation via Arduino CLI / ESP-IDF
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

## 11. PRODUCTION RELEASE VERIFICATION CHECKLIST

Prior to merging changes or tagging firmware releases, the following verification gates must pass:

- [ ] **Code Hygiene Audit:** Zero zombie code blocks, legacy prototypes, or deprecated variables.
- [ ] **Debug Log Elimination:** `ENABLE_SERIAL_DEBUG` set to `false`, eliminating all unformatted UART overhead.
- [ ] **Deep Code Analysis:** Full validation of pointer nullability, bounded array access, division-by-zero guards ($\epsilon = 10^{-6}$), and floating-point validity (`!isnan() && !isinf()`).
- [ ] **Systemic Impact Verification:** Confirmation that changes do not increase critical section hold times beyond $5 \ \mu\text{s}$, do not breach the $16.66 \text{ ms}$ frame budget on Core 1, and do not introduce PSRAM bus contention.
- [ ] **Design Rules Compliance:** Zero em dashes, zero emojis, zero banned color schemes (rainbow, neon, pastel, purple and black), zero glassmorphism, zero drop shadows, and full skeleton loader compliance.
- [ ] **Dual-Core Memory Isolation:** Verification that all image arrays (`small_rgb_buf`, `prev_lum_buf`, `mhi_buf`) reside in internal SRAM (`MALLOC_CAP_INTERNAL`), with external PSRAM limited strictly to streaming buffers (`g_latest_jpeg_buf`).
- [ ] **Deterministic Framerate:** Core 1 display cycle measures $16.66 \pm 0.2 \text{ ms}$ ($60 \text{ FPS}$) without screen tearing on the $1.0 \text{ MHz}$ I2C bus.
- [ ] **Mathematical Model Convergence:** Kinematics and Kalman filter routines verified free of $NaN$ and infinite outputs under all input vectors across all 4 unit test suites.
- [ ] **OTA & Live Control Reliability:** Web OTA update (`POST /update`) and dynamic camera register tuning (`POST /camera_control`) operate without freezing FreeRTOS task schedules.
- [ ] **DFS Power State Verification:** Current consumption drops measurably when transitioning from $240 \text{ MHz}$ active mode to $80 \text{ MHz}$ standby mode with camera sensor sleep and modem power save active.
- [ ] **Compiler Cleanliness:** Firmware compiles with zero warnings under standard ESP32-S3 compiler profiles.
