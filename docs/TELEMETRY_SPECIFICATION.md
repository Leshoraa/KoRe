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
  "c0_w": 120,
  "c0_h": 140,
  "c0_p": 142.5,
  "c1_cx": 120,
  "c1_cy": 180,
  "c1_w": 90,
  "c1_h": 110,
  "c1_p": 88.2,
  "c2_cx": 480,
  "c2_cy": 310,
  "c2_w": 80,
  "c2_h": 95,
  "c2_p": 64.1,
  "expr": 3,
  "expr_name": "SMIRK",
  "is_manual": false,
  "valence": 0.35,
  "arousal": 0.60,
  "heap_free": 125432,
  "psram_free": 7340032,
  "uptime_s": 3600,
  "cpu_mhz": 240
}
```

### 2.3 Expression Control
- **`POST /set_expression`:** Sets the manual face expression override or restores default auto mood engine.
  - **Body:** `{"expr": 3}` (0: IDLE, 1: JOY, 2: ANGRY, 3: SMIRK, 4: SHOCK, 5: OVERLOAD, 6: SAD, 7: DEADPAN) or `{"expr": "auto"}` / `{"expr": -1}` to restore default.
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

### 2.10 Display Brightness Control
- **`POST /set_brightness`:** Adjusts OLED display brightness in real-time ($0 - 255$) with optional NVS persistence.
  - **Body:** `{"brightness": 180, "save": true}`
  - **Response:** `{"status": "ok"}`

### 2.11 Weather Location & Configuration
- **`POST /set_weather`:** Configures city name, geographic coordinates, and automatic standby popup flag.
  - **Body:** `{"city": "Jakarta", "lat": -6.2088, "lon": 106.8456, "enabled": true}`
  - **Response:** `{"status": "ok"}`
- **`GET /weather_info`:** Retrieves latest parsed Open-Meteo observation payload.
  - **Response:** `application/json`
  - **Schema:**
```json
{
  "city": "Jakarta",
  "lat": -6.2088,
  "lon": 106.8456,
  "enabled": true,
  "valid": true,
  "temp": 29.5,
  "humidity": 78,
  "code": 3,
  "condition": "OVERCAST",
  "last_sync_s": 120
}
```
- **`POST /trigger_weather`:** Triggers instantaneous 6-second weather screen display on the OLED panel.
  - **Response:** `{"status": "ok"}`

### 2.12 Extended Telemetry Fields
The `/telemetry` and BLE telemetry payloads include dynamic system, affective, and camera state metrics:
- `valence` (float): Current emotional valence [-1.0, 1.0]
- `arousal` (float): Current emotional arousal [0.0, 1.0]
- `curiosity`, `social`, `boredom`, `fatigue`, `mischief` (float): Homeostatic drive states [0.0, 1.0]
- `thought` (string): Real-time cognitive inner thought summary
- `bonding` (float): Bonding level with human companion [0.0, 1.0]
- `cam_sleep` (bool): True if camera hardware is in low-power standby mode (telemetry continues to function without forcing camera active)
- `cam_online` (bool): True if camera sensor hardware initialized successfully
- `heap_free` (uint): Free internal heap in bytes
- `psram_free` (uint): Free PSRAM in bytes
- `uptime_s` (uint): System uptime in seconds
- `cpu_mhz` (int): Current CPU frequency in MHz

---

## 3. Bluetooth Low Energy (BLE) NUS Telemetry Protocol

KoRe exposes a high-throughput Nordic UART Service (NUS) over BLE GATT for mobile applications:
- **Service UUID:** `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- **RX Characteristic (Write / Write Without Response):** `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- **TX Characteristic (Notify):** `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

### 3.1 On-Demand Telemetry Snapshot
- **Command:** `{"cmd": "get_telemetry"}` or `{"cmd": "telemetry"}` or raw string `TELEMETRY`
- **Behavior:** Returns the full JSON telemetry payload via TX notification chunked into safe MTU packets without waking up or forcing the camera sensor ON.

### 3.2 Continuous Telemetry Streaming
- **Start Streaming:** `{"cmd": "stream_telemetry", "enable": true, "interval": 500}` or `STREAM_TELEMETRY:500`
- **Stop Streaming:** `{"cmd": "stream_telemetry", "enable": false}` or `STREAM_TELEMETRY:0`
- **Behavior:** KoRe's background FreeRTOS BLE telemetry task automatically transmits real-time telemetry updates at the specified period (e.g. 500ms) over GATT notifications.

### 3.3 Camera Decoupling Architecture
Querying or streaming telemetry via BLE or HTTP `/telemetry` operates strictly passively and **never forces the camera hardware to stay ON or wake from standby sleep**. The camera enters standby sleep (`STATE_SLEEP_RECON`) automatically when not actively tracking or streaming video, preserving battery and reducing heat.


