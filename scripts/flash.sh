#!/usr/bin/env bash
# ==============================================================================
# KoRe Firmware Upload & Serial Monitor Script
# ==============================================================================

set -euo pipefail

SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${1:-/dev/ttyACM0}"
FQBN="esp32:esp32:XIAO_ESP32S3:PSRAM=opi"

echo "[FLASH] Uploading KoRe firmware to ${PORT}..."
arduino-cli upload -p "${PORT}" -b "${FQBN}" "${SKETCH_DIR}/KoRe.ino"

echo "[FLASH] Starting Serial Monitor @ 115200 baud (Ctrl+C to exit)..."
arduino-cli monitor -p "${PORT}" --config 115200
