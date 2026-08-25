# Changelog

All notable changes to the KoRe project are documented in this file.

## [2.6.0] - 2026-08-25

### Added
- Bluetooth Low Energy (BLE) Nordic UART Service (NUS) GATT server (`src/net/ble_manager.cpp`, `src/net/ble_manager.h`) enabling direct, offline companion notifications from Android/MacroDroid.
- Intelligent multi-format payload parser for BLE stream (JSON, bracket app tags, pipe/colon separators, and raw strings).
- Seamless coexistence with phone Bluetooth audio (TWS) and Wi-Fi stack.
- Automatic BLE advertising restart upon device disconnection.

## [2.5.0] - 2026-08-23

### Added
- Open-Meteo weather integration with 1-bit graphical weather screen on OLED (random 6-second popups during idle standby and manual dashboard trigger).
- Real-time and persistent OLED brightness slider in Web Dashboard (`POST /set_brightness`) with NVS storage.
- Weather location configuration endpoints (`POST /set_weather`, `GET /weather_info`, `POST /trigger_weather`).
- Web Over-The-Air (OTA) firmware update endpoint (`POST /update`) and Web UI upload progress bar.
- Live camera sensor tuning endpoint (`POST /camera_control`) for brightness, contrast, saturation, flip, and AEC/AGC gain.
- OLED anti-burn-in protection featuring periodic micro-pixel shifting ($\pm 1\text{ px}$) during standby reconnaissance.
- Dynamic Wi-Fi modem sleep (`WIFI_PS_MIN_MODEM`) during reconnaissance standby to extend portable battery runtime.
- New "Device & OTA" control tab on the Bento grid Web Dashboard.

### Changed
- Removed automatic brightness reduction (*auto-dimming*) during sleep reconnaissance to keep OLED panel brightness strictly consistent with user preference.

### Fixed
- **CRITICAL**: Fixed CI / host unit test suite compilation on Linux by isolating ESP32-IDF/FreeRTOS headers in `kore_types.h` and `kore_config.h`.
- **CRITICAL**: Fixed camera task dispatch in `KoRe.ino` allowing continuous background sensor auto-recovery when camera is offline during initial boot.
- Fixed buffer size discrepancies in `KORE_ENGINEERING_SPECIFICATION.md` Table 7.1 ($1.2\text{ KB}$ for differential luminance and MHI maps).

## [2.4.0] - 2026-08-23

### Added
- WiFi network scanner endpoint (`GET /scan_wifi`) with SSID, RSSI, and encryption info.
- System diagnostics endpoint (`GET /system_info`) reporting heap, PSRAM, uptime, and chip info.
- Extended telemetry with emotional valence/arousal, heap metrics, and CPU frequency.
- Stochastic Langevin noise diffusion term in affective mood engine (synced with documentation).
- Full CI test coverage: all 4 unit test suites now compiled and executed.
- Realistic synthetic test frame fixtures for 4 scenarios (rest, skin, dark, bright).
- `MAX_STREAM_CLIENTS` configuration limit.
- `KORE_FIRMWARE_VERSION` compile-time version string.

### Changed
- Kalman covariance update now uses true Joseph-stabilized formulation for improved numerical stability.
- Saccade duration calculations consolidated to use `compute_saccade_duration_ms()` function.
- WiFi boot timeout reduced from 15s to 7.5s for faster startup.
- OLED timing loop uses `delayMicroseconds()` instead of busy-spinning `taskYIELD()` loop.
- Camera pipeline pixel loop pre-computes integer-scaled sector boundaries (eliminated per-pixel float casts).

### Fixed
- **CRITICAL**: Added fatal guard preventing Kernel Panic when vision buffer allocation fails.
- **CRITICAL**: Masked plain-text WiFi passwords in `GET /get_wifi` response (was leaking credentials with CORS `*`).
- **CRITICAL**: Made `g_camera_init_ok` volatile for cross-core safety.
- Removed ~460-line `processFrameAI` from IRAM_ATTR (was consuming 6-10 KB scarce instruction RAM).
- Removed dead `last_wifi_ps_sleep` variable and unreachable code block.
- Removed dead touch sensor initialization (`PIN_TOUCH_INPUT` was configured but never read).
- Camera task active duration now uses `ACTIVE_STATE_TIMEOUT_MS` config macro instead of bypassing it.
- Removed dead touch sensor config macros from `kore_config.h`.

## [2.3.0] - 2026-08-20

### Added
- Modular architecture complying with KORE_ENGINEERING_SPECIFICATION.md Section 10.
- Discrete 2D Kalman filter with adaptive measurement noise covariance ($R$).
- 5th-order minimum-jerk saccadic trajectory solver (Flash & Hogan formulation).
- 2D Russell Circumplex Langevin stochastic affective dynamics engine.
- Production dark matte Web UI dashboard with structural skeleton loader and on-device privacy disclosures.
- Automated CI workflows and mathematical validation suite.

### Changed
- Refactored monolithic codebase into `include/`, `src/core/`, `src/math/`, and `src/net/`.
- Overclocked LovyanGFX I2C write bus to 1.0 MHz Fast-Mode Plus.
- Replaced unformatted UART prints with structured zero-cost logging macros.

### Fixed
- Eliminated coordinate quantization flickering on monochrome OLED using anti-jitter hysteresis.
- Removed legacy zombie code and unformatted print statements.
