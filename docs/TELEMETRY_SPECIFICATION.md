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
