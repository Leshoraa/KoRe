# KoRe Telemetry and Streaming API Specification

## 1. Overview

KoRe hosts a lightweight, asynchronous dual-port HTTP server on the ESP32-S3:
- **Port 80:** Web Control Dashboard, Wi-Fi Configuration API, and JSON Telemetry.
- **Port 81:** Non-blocking multipart MJPEG video stream.

---

## 2. API Endpoints

### 2.1 Web Dashboard
- **URL:** `GET http://<ESP32_IP>/`
- **Response:** `text/html` (Production dashboard with dark matte technical surface and skeleton loader).

### 2.2 Real-Time JSON Telemetry
- **URL:** `GET http://<ESP32_IP>/telemetry`
- **Response:** `application/json`
- **Schema:**
```json
{
  "detected": true,
  "x": 210,
  "y": 140,
  "w": 180,
  "h": 220,
  "cx": 300,
  "cy": 250,
  "err_x": -6.2,
  "err_y": 4.1,
  "conf": 0.94,
  "fps_ai": 31.5,
  "fw": 640,
  "fh": 480,
  "vx": -1.2,
  "vy": 0.4,
  "prox": 0.85,
  "num_cands": 3,
  "insp_idx": 0,
  "c0_cx": 300,
  "c0_cy": 250,
  "c0_p": 142.5,
  "c1_cx": 120,
  "c1_cy": 180,
  "c1_p": 88.2,
  "c2_cx": 480,
  "c2_cy": 310,
  "c2_p": 64.1,
  "expr": 3,
  "expr_name": "SMIRK",
  "is_manual": false
}
```

### 2.3 Expression Control
- **`POST /set_expression`:** Sets the manual face expression override or restores default auto mood engine.
  - **Body:** `{"expr": 3}` (0: IDLE, 1: JOY, 2: ANGRY, 3: SMIRK, 4: SHOCK, 5: OVERLOAD, 6: SEDIH, 7: DEADPAN) or `{"expr": "auto"}` / `{"expr": -1}` to restore default.
  - **Response:** `{"status": "ok"}`

### 2.4 MJPEG Video Stream
- **URL:** `GET http://<ESP32_IP>:81/stream`
- **Response:** `multipart/x-mixed-replace;boundary=123456789000000000000987654321`

### 2.5 Wi-Fi Configuration
- **`GET /get_wifi`:** Returns current saved credentials and mode flag.
- **`POST /save_wifi`:** Updates SSID and password in NVS and reboots device.
- **`POST /switch_mode`:** Toggles between AP and STA mode in NVS and reboots device.

### 2.6 WiFi Network Scanner
- **URL:** `GET http://<ESP32_IP>/scan_wifi`
- **Response:** `application/json`
- **Schema:**
```json
{
  "networks": [
    {"ssid": "MyNetwork", "rssi": -45, "enc": "WPA2"},
    {"ssid": "OpenNet", "rssi": -72, "enc": "OPEN"}
  ],
  "count": 2
}
```

### 2.7 System Information
- **URL:** `GET http://<ESP32_IP>/system_info`
- **Response:** `application/json`
- **Schema:**
```json
{
  "firmware": "2.5.0",
  "compiled": "Aug 23 2026 12:00:00",
  "chip": "ESP32-S3",
  "cores": 2,
  "cpu_mhz": 240,
  "heap_free": 125432,
  "heap_min": 98000,
  "psram_free": 7340032,
  "psram_total": 8388608,
  "uptime_s": 3600,
  "wifi_rssi": -52,
  "wifi_mode": "STA",
  "ip": "192.168.1.100",
  "camera_ok": true,
  "stream_clients": 1
}
```

### 2.8 Camera Sensor Control
- **`POST /camera_control`:** Adjusts camera sensor parameters dynamically without re-flashing.
  - **Body:** `{"param": "brightness", "val": 1}`
  - **Supported Params:**
    - `brightness` (-2 to +2)
    - `contrast` (-2 to +2)
    - `saturation` (-2 to +2)
    - `vflip` (0 or 1)
    - `hmirror` (0 or 1)
    - `aec` (0: manual, 1: auto exposure)
    - `agc` (0: manual, 1: auto gain)
  - **Response:** `{"status": "ok"}`

### 2.9 Web Over-The-Air (OTA) Update
- **`POST /update`:** Uploads binary firmware (`.bin`) directly to ESP32-S3 flash partition.
  - **Payload:** Raw binary firmware bytes (`application/octet-stream`)
  - **Response:** `{"status": "ok", "message": "Firmware flashed! Rebooting..."}`

### 2.10 Extended Telemetry Fields
The `/telemetry` endpoint includes dynamic system and affective metrics:
- `valence` (float): Current emotional valence [-1.0, 1.0]
- `arousal` (float): Current emotional arousal [0.0, 1.0]
- `heap_free` (uint): Free internal heap in bytes
- `psram_free` (uint): Free PSRAM in bytes
- `uptime_s` (uint): System uptime in seconds
- `cpu_mhz` (int): Current CPU frequency in MHz

