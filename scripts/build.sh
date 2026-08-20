#!/usr/bin/env bash
# ==============================================================================
# KoRe Firmware Compilation Script
# Target: Seeed Studio XIAO ESP32-S3 Sense
# ==============================================================================

set -euo pipefail

SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FQBN="esp32:esp32:XIAO_ESP32S3:PSRAM=opi"

echo "[BUILD] Compiling KoRe firmware for ${FQBN}..."
arduino-cli compile -b "${FQBN}" "${SKETCH_DIR}/KoRe.ino"

echo "[BUILD] Compilation successful."
