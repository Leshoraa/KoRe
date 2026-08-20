# Changelog

All notable changes to the KoRe project are documented in this file.

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
