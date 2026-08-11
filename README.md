# KoRe - Ultra-Fast Standalone Embedded Human Tracker

[![Board](https://img.shields.io/badge/Hardware-Seeed_XIAO_ESP32--S3_Sense-00979D.svg?style=flat-square&logo=arduino)](https://www.seeedstudio.com/XIAO-ESP32S3-Sense-p-5639.html)
[![AI Latency](https://img.shields.io/badge/AI_Latency-%3C_0.5_ms-brightgreen.svg?style=flat-square)](#performa--spesifikasi)
[![Framerate](https://img.shields.io/badge/Stream_Framerate-30%2B_FPS_(VGA)-blue.svg?style=flat-square)](#performa--spesifikasi)
[![Math Engine](https://img.shields.io/badge/Math_Engine-Zero--Transcendental-orange.svg?style=flat-square)](#3-arsitektur-matematika-fisika--differential-computer-vision)
[![Memory](https://img.shields.io/badge/Footprint-Single--Pass_SRAM-purple.svg?style=flat-square)](#optimasi-hardware--komputasi-tensilica-xtensa)

**KoRe** (*Kinematic Optical Recognition Engine*) adalah sistem pengenalan dan penjejakan manusia (*Human Recognition & Motion Tracking*) deterministik berkecepatan ultra-tinggi yang berjalan secara *standalone* murni pada mikrokontroler **Seeed Studio XIAO ESP32-S3 Sense** (240 MHz Dual-Core Xtensa LX7). 

Sistem ini menggabungkan *Differential Computer Vision*, *Low-Lux Luminance-Aware Skin Locus*, serta *1st-Order Alpha-Beta Kinematic Filter* dengan optimasi komputasi *Zero-Transcendental* untuk menghasilkan latency inferensi **$< 0.5\text{ ms}$ per frame** pada resolusi VGA (640x480) tanpa ketergantungan server atau cloud eksternal.

---

## Daftar Isi
1. [Fitur Utama (Key Features)](#1-fitur-utama-key-features)
2. [Spesifikasi & Benchmark Performa](#2-spesifikasi--benchmark-performa)
3. [Arsitektur Matematika, Fisika, & Differential Computer Vision](#3-arsitektur-matematika-fisika--differential-computer-vision)
4. [Optimasi Hardware & Komputasi Tensilica Xtensa](#4-optimasi-hardware--komputasi-tensilica-xtensa)
5. [Spesifikasi Hardware & Pemetaan Pinout](#5-spesifikasi-hardware--pemetaan-pinout)
6. [Arsitektur Telemetri & Web API Endpoint](#6-arsitektur-telemetri--web-api-endpoint)
7. [Panduan Kompilasi & Deployment](#7-panduan-kompilasi--deployment)
8. [Struktur Berkas Repositori](#8-struktur-berkas-repositori)

---

## 1. Fitur Utama (Key Features)

- **Zero-Transcendental Math Architecture:** Eliminasi total fungsi transendental `expf()` dan `exp()` pada C++ dan JavaScript. Seluruh komputasi berjalan pada aritmatika integer dan operasi floating-point dasar FPU.
- **Skin-Anchored Photometric Centroid:** Penjejakan berbasis pembobotan warna kulit $YC_bC_r$ ($1.0\times$) dengan supresi noise latar belakang non-kulit ($0.04\times$). Mencegah jebakan tracking pada poster dinding, perabot, dan bayangan statis bergradien tinggi (*anti-poster hijacking*).
- **1st-Order Alpha-Beta ($\alpha-\beta$) Kinematic Filter:** Estimasi posisi dan kecepatan dinamis secara proaktif dengan pengaman divergensi kecepatan (*velocity clamping* $[-800.0, 800.0]\text{ px/s}$).
- **Adaptive Euclidean Distance Gating:** Radius asosiasi data adaptif berbasis kecepatan target ($R_{\text{gate}} = \min(250.0, 90.0 + 0.25 \cdot \|\mathbf{v}\|)$) untuk mengisolasi target dari *false positive* di tepi layar.
- **Single-Pass YCbCr Decode & EMA Low-Lux Adaptation:** Dekode RGB565 ke matriks intensitas $Y$ dan klasifikasi warna kulit dalam satu kali iterasi sekuensial dengan parameter ambang adaptif berbasis *Exponential Moving Average* (EMA).
- **Anatomical Aspect Ratio Coupling:** Pengikatan rasio dispersi spasial manusia ($1.15 \le H/W \le 1.55$) untuk memastikan *Bounding Box* tetap stabil dan tidak terdistorsi saat gerakan menyapu cepat (*panning*).
- **Embedded Web Dashboard (Zero UI Canvas):** Antarmuka web modern dengan *Sci-Fi HUD Canvas Overlay* (Neon Green Bounding Box, Corner Brackets, True Center of Mass Crosshair, dan indikator telemetri real-time).
- **Dual-Core Concurrency:** Core 0 menangani *Vision Pipeline* dan *MJPEG Streamer*, Core 1 mengeksekusi *HTTP Server* dan *Telemetry Dispatcher* dengan proteksi *Critical Section Mutex*.

---

## 2. Spesifikasi & Benchmark Performa

| Parameter | Spesifikasi | Keterangan |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32-S3 Dual-Core Xtensa LX7 @ 240 MHz | FPU Hardware Enabled |
| **Sensor Kamera** | OmniVision OV3660 / OV2640 | Lensa Wide DVP |
| **Capture Resolution** | VGA ($640 \times 480$), JPEG Quality 8 | Jernih & Minim Artefak |
| **AI Subsampling Grid** | $40 \times 30$ Matrix ($1,200\text{ pixels}$) | Hardware IDCT $8\times$ Decode |
| **AI Execution Latency** | **$< 0.5\text{ ms}$ / frame** | Benchmark murni di ESP32-S3 |
| **Video Streaming Rate** | **$30+\text{ FPS}$** | Port 81 MJPEG Stream |
| **Telemetry Refresh** | $10\text{ Hz}$ ($100\text{ ms}$ Polling) | Port 80 JSON Endpoint |
| **Dynamic Memory Usage** | $< 12\text{ KB}$ Total Buffer | Single-Pass Processing (Tanpa Buffer Redundan) |

---

## 3. Arsitektur Matematika, Fisika, & Differential Computer Vision

```
 +-------------------------------------------------------------------------+
 |                          CAMERA FRAME (VGA 640x480)                     |
 +-------------------------------------------------------------------------+
                                      │
                                      ▼ [Hardware IDCT 8x Subsampling]
 +-------------------------------------------------------------------------+
 |                RGB565 SMALL BUFFER (80x60) -> 40x30 MATRIX              |
 +-------------------------------------------------------------------------+
                                      │
           ┌──────────────────────────┴──────────────────────────┐
           ▼                                                     ▼
 +──────────────────────────────────+  +───────────────────────────────────+
 |   SINGLE-PASS YCbCr CONVERSION   |  |   DYNAMIC LOW-LUX SKIN LOCUS      |
 |   Y  = (77R + 150G + 29B) >> 8   |  |   ΔY = clamp((90 - Y_bar)/2, 0,12)|
 |   Cb = 128 + (-43R-85G+128B)>>8  |  |   Cb in [70-ΔY, 133+ΔY]           |
 |   Cr = 128 + (128R-107G-21B)>>8  |  |   Cr in [128-ΔY, 178+ΔY]          |
 +──────────────────────────────────+  +───────────────────────────────────+
           │                                                     │
           └──────────────────────────┬──────────────────────────┘
                                      ▼
 +-------------------------------------------------------------------------+
 |             3x3 BOX BLUR + GLOBAL CAMERA FLOW COMPENSATION              |
 |                ΔY_local = |Y_cur - Y_prev| - ΔY_global                  |
 +-------------------------------------------------------------------------+
                                      │
                                      ▼
 +-------------------------------------------------------------------------+
 |                 PHOTOMETRIC ENERGY ACCUMULATION Φ(x, y)                 |
 |       Φ(x,y) = SkinMask ? (2.5 ΔY + 1.5 gy) : 0.04 * (2.5 ΔY + 1.5 gy)  |
 +-------------------------------------------------------------------------+
                                      │
                                      ▼
 +-------------------------------------------------------------------------+
 |            SPATIAL MOMENTS & DISPERSION (Reciprocal Multiply)           |
 |       M_00, M_10, M_01, M_20, M_02  --> Centroid (x_bar, y_bar)        |
 |       Spatial Variance: μ_20, μ_02  --> Bounding Box (w_raw, h_raw)     |
 +-------------------------------------------------------------------------+
                                      │
                                      ▼
 +-------------------------------------------------------------------------+
 |             DATA ASSOCIATION (Euclidean Distance Gating)                |
 |             dist^2 <= R_gate^2,  R_gate = min(250, 90 + 0.25 * ||v||)   |
 +-------------------------------------------------------------------------+
                                      │
                                      ▼
 +-------------------------------------------------------------------------+
 |          1st-ORDER ALPHA-BETA (α-β) KINEMATIC STATE CORRECTION          |
 |          x_k  = x_pred + α * r_x,    y_k  = y_pred + α * r_y            |
 |          vx_k = clamp(vx + (β/dt)*r_x, -800, 800)                       |
 |          vy_k = clamp(vy + (β/dt)*r_y, -800, 800)                       |
 +-------------------------------------------------------------------------+
```

### 3.1. Dynamic Low-Lux Skin Locus (YCbCr Space)
Kondisi pencahayaan ruangan rendah (*low-lux*) menggeser distribusi krominansi wajah ke arah saturasi rendah. Ambang batas krominansi $C_b$ dan $C_r$ dimodulasi secara adaptif menggunakan *Exponential Moving Average* (EMA) luminansi global $\bar{Y}$:

$$\bar{Y}_{k} = 0.90 \cdot \bar{Y}_{k-1} + 0.10 \cdot \left( \frac{1}{N} \sum_{i=1}^{N} Y_i \right)$$

$$\Delta Y = \text{clamp}\left( \frac{90.0 - \bar{Y}_k}{2.0}, \, 0.0, \, 12.0 \right)$$

Piksel diklasifikasikan sebagai warna kulit $\text{SkinMask}(x,y)$ jika dan hanya jika:

$$\begin{cases}
C_b(x,y) \in [70.0 - \Delta Y, \; 133.0 + \Delta Y] \\
C_r(x,y) \in [128.0 - \Delta Y, \; 178.0 + \Delta Y]
\end{cases}$$

### 3.2. Full-Frame Photometric Energy Density $\Phi(x,y)$
Untuk mencegah pembajakan pelacakan (*tracking lock*) oleh poster dinding dengan kontras tinggi saat subjek bergerak cepat, energi fotometrik piksel non-kulit diredam secara drastis sebesar $96\%$ ($0.04\times$):

$$\Delta Y(x,y) = \max\left( 0.0, \; |Y(x,y) - Y_{\text{prev}}(x,y)| - \bar{\Delta}_{\text{global}} \right)$$

$$g_y(x,y) = |Y(x, y+1) - Y(x, y-1)|$$

$$\Phi(x,y) = \begin{cases} 
2.5 \cdot \Delta Y(x,y) + 1.5 \cdot g_y(x,y) & \text{jika } \text{SkinMask}(x,y) = \text{true} \\ 
0.04 \cdot \left( 2.5 \cdot \Delta Y(x,y) + 1.5 \cdot g_y(x,y) \right) & \text{jika } \text{SkinMask}(x,y) = \text{false} 
\end{cases}$$

Piksel dengan $\Phi(x,y) < 4.0$ diabaikan (*zero-energy cutoff*) untuk mengeliminasi noise sensorik.

### 3.3. Spatial Moments & Center of Mass (Momen Spasial Orde 0, 1, 2)
Akumulasi momen spasial $M_{pq}$ dihitung di seluruh ruang matriks $40 \times 30$ ($N = 1,200$):

$$M_{pq} = \sum_{y=1}^{28} \sum_{x=1}^{38} x^p y^q \Phi(x,y)$$

Titik pusat massa (*Centroid*) dan varians spasial sentral dihitung menggunakan perkalian resiprokal tunggal $\text{inv\_}M_{00} = \frac{1.0}{M_{00}}$ untuk memotong siklus instruksi pembagian FPU:

$$\bar{x} = M_{10} \cdot \text{inv\_}M_{00}, \quad \bar{y} = M_{01} \cdot \text{inv\_}M_{00}$$

$$\mu_{20} = (M_{20} \cdot \text{inv\_}M_{00}) - \bar{x}^2, \quad \mu_{02} = (M_{02} \cdot \text{inv\_}M_{00}) - \bar{y}^2$$

$$\sigma_x = \sqrt{\max(0.0, \mu_{20})}, \quad \sigma_y = \sqrt{\max(0.0, \mu_{02})}$$

### 3.4. Skalasi Bounding Box & Anatomical Aspect Ratio Coupling
Centroid dan dispersi spasial diproyeksikan ke resolusi layar asli $640 \times 480$ ($S_x = 16.0, S_y = 16.0$):

$$z_x = \bar{x} \cdot 16.0, \quad z_y = \bar{y} \cdot 16.0$$

$$w_{\text{raw}} = \text{clamp}\left(2.4 \cdot \max(1.5, \sigma_x) \cdot 16.0, \; 60.0, \; 360.0\right)$$

$$h_{\text{raw}} = 2.6 \cdot \max(2.0, \sigma_y) \cdot 16.0$$

Untuk mempertahankan morfologi torso dan wajah manusia, tinggi *Bounding Box* dikunci dalam rasio anatomis:

$$h_{\text{coupled}} = \text{clamp}\left( \min(\max(h_{\text{raw}}, 1.15 \cdot w_{\text{raw}}), 1.55 \cdot w_{\text{raw}}), \; 80.0, \; 480.0 \right)$$

### 3.5. 1st-Order Alpha-Beta ($\alpha-\beta$) Kinematic Filter
Filter kinematika memprediksi dan memperbarui vektor state target $\mathbf{x}_k = [x, y, v_x, v_y]^T$:

#### Fase Prediksi (State Prior):
$$\hat{x}_k = x_{k-1} + v_{x,k-1} \cdot \Delta t$$

$$\hat{y}_k = y_{k-1} + v_{y,k-1} \cdot \Delta t$$

#### Data Association (Euclidean Gating):
Kandidat pengukuran $\mathbf{z}_k = [z_x, z_y]^T$ diterima jika berada dalam radius gerbang dinamis:

$$(z_x - \hat{x}_k)^2 + (z_y - \hat{y}_k)^2 \le R_{\text{gate}}^2, \quad R_{\text{gate}} = \min(250.0, \; 90.0 + 0.25 \cdot \|\mathbf{v}_{k-1}\|)$$

#### Fase Koreksi (State Update):
Residual inovasi: $r_x = z_x - \hat{x}_k, \quad r_y = z_y - \hat{y}_k$.

$$x_k = \hat{x}_k + \alpha \cdot r_x \quad (\alpha = 0.40)$$

$$y_k = \hat{y}_k + \alpha \cdot r_y \quad (\alpha = 0.40)$$

$$v_{x,k} = \text{clamp}\left( v_{x,k-1} + \frac{\beta}{\Delta t} \cdot r_x, \; -800.0, \; 800.0 \right) \quad (\beta = 0.20)$$

$$v_{y,k} = \text{clamp}\left( v_{y,k-1} + \frac{\beta}{\Delta t} \cdot r_y, \; -800.0, \; 800.0 \right) \quad (\beta = 0.20)$$

Saat target tidak terdeteksi (*missed detection*), sistem memasuki mode *coasting* dengan atenuasi kecepatan: $\mathbf{v}_k \leftarrow 0.85 \cdot \mathbf{v}_{k-1}$. Jika target hilang lebih dari $400\text{ ms}$, status track di-reset ke inaktif.

---

## 4. Optimasi Hardware & Komputasi Tensilica Xtensa

1. **Pointer Increment Memory Traversal:** Seluruh loop intensif ($40 \times 30$) menghindari kalkulasi indeks perkalian alamat 2D (`y * 40 + x`) dan menggunakan *pointer increment* bertingkat (`*p_cur++`, `*p_smooth++`, `*p_mask++`).
2. **Reciprocal FPU Division:** Mengganti instruksi `fdiv.s` (membutuhkan ~14-16 clock cycle) dengan satu kali pembagian diikuti instruksi `fmul.s` (~1-4 clock cycle).
3. **Single-Pass Decode Pipeline:** Menghilangkan array perantara $C_b$ dan $C_r$ berukuran $2.4\text{ KB}$, menurunkan konsumsi SRAM dan meningkatkan *cache locality*.
4. **Hardware IDCT Scaling:** Memanfaatkan fitur subsampling terintegrasi $8\times$ pada pustaka `esp32-camera` (`JPG_SCALE_8X`) untuk mendekode frame VGA langsung menjadi buffer $80 \times 60$ dalam waktu $\sim 1.2\text{ ms}$.

---

## 5. Spesifikasi Hardware & Pemetaan Pinout

Modul yang digunakan adalah **Seeed Studio XIAO ESP32-S3 Sense** yang dilengkapi board ekspansi kamera OV3660/OV2640 dan slot microSD.

```
       ┌───────────────────────────────┐
       │   XIAO ESP32-S3 Sense Top     │
       │                               │
       │    ┌─────────────────────┐    │
       │    │     OV3660 /        │    │
       │    │      OV2640         │    │
       │    │   Camera Sensor     │    │
       │    └─────────────────────┘    │
       │                               │
       │  [D0/A0/IO1]     [5V]         │
       │  [D1/A1/IO2]     [GND]        │
       │  [D2/A2/IO3]     [3V3]        │
       │  [D3/A3/IO4]     [D10/IO10]   │
       │  [D4/A4/IO5]     [D9/IO9]     │
       │  [D5/A5/IO6]     [D8/IO8]     │
       │  [D6/TX/IO43]    [D7/RX/IO44] │
       └───────────────────────────────┘
```

### Tabel Pinout Kamera Internal (DVP Interface)

| Fungsi Sinyal Kamera | Pin GPIO ESP32-S3 | Keterangan |
| :--- | :--- | :--- |
| **XCLK** | `GPIO 10` | Sinyal Clock Eksternal Sensor ($16\text{ MHz}$) |
| **PCLK** | `GPIO 13` | Pixel Clock |
| **VSYNC** | `GPIO 38` | Vertical Synchronization |
| **HREF** | `GPIO 47` | Horizontal Reference |
| **SIOD (SDA)** | `GPIO 40` | I2C SCCB Data Line |
| **SIOC (SCL)** | `GPIO 39` | I2C SCCB Clock Line |
| **Y9 (D7)** | `GPIO 48` | Parallel Video Data Bit 7 |
| **Y8 (D6)** | `GPIO 11` | Parallel Video Data Bit 6 |
| **Y7 (D5)** | `GPIO 12` | Parallel Video Data Bit 5 |
| **Y6 (D4)** | `GPIO 14` | Parallel Video Data Bit 4 |
| **Y5 (D3)** | `GPIO 16` | Parallel Video Data Bit 3 |
| **Y4 (D2)** | `GPIO 18` | Parallel Video Data Bit 2 |
| **Y3 (D1)** | `GPIO 17` | Parallel Video Data Bit 1 |
| **Y2 (D0)** | `GPIO 15` | Parallel Video Data Bit 0 |
| **PWDN** | `-1` | Not Connected (Internal Pull-down) |
| **RESET** | `-1` | Not Connected (Software Reset via SCCB) |

---

## 6. Arsitektur Telemetri & Web API Endpoint

KoRe mengaktifkan web server multi-port secara simultan:

- **Port 80 (HTTP Control & Telemetry):**
  - `GET /` : Single-page Web UI Dashboard (HTML5 + Responsive Canvas HUD).
  - `GET /telemetry` : Real-time JSON Endpoint untuk status pelacakan.
- **Port 81 (High-Speed MJPEG Stream):**
  - `GET /stream` : Multipart JPEG stream berkecepatan 30+ FPS.

### Skema Telemetri JSON (`GET /telemetry`)

```json
{
  "detected": true,
  "x": 210,
  "y": 120,
  "w": 180,
  "h": 240,
  "cx": 300,
  "cy": 240,
  "err_x": -6.2,
  "err_y": 0.0,
  "conf": 0.95,
  "fps_ai": 31.4,
  "fw": 640,
  "fh": 480,
  "m00": 482.5,
  "skin_px": 84,
  "lock_conf": 0.95,
  "vx": 12.4,
  "vy": -4.1
}
```

### Kamus Data Telemetri

| Kunci (*Field*) | Tipe Data | Satuan / Rentang | Deskripsi |
| :--- | :--- | :--- | :--- |
| `detected` | `boolean` | `true` / `false` | Status deteksi dan penguncian target |
| `x`, `y` | `integer` | $0 \dots 640$, $0 \dots 480$ | Koordinat sudut kiri-atas Bounding Box |
| `w`, `h` | `integer` | $\text{px}$ | Lebar dan tinggi Bounding Box |
| `cx`, `cy` | `integer` | $0 \dots 640$, $0 \dots 480$ | Titik tengah (*Centroid*) target |
| `err_x`, `err_y` | `float` | $-100.0 \dots 100.0 \;\%$ | Deviasi posisi target dari titik tengah frame |
| `conf` / `lock_conf`| `float` | $0.00 \dots 1.00$ | Tingkat keyakinan pelacakan (*Confidence Metric*) |
| `fps_ai` | `float` | $\text{Hz} / \text{FPS}$ | Frekuensi inferensi komputer visi per detik |
| `fw`, `fh` | `integer` | $\text{px}$ | Resolusi frame kamera asli ($640 \times 480$) |
| `m00` | `float` | $\ge 0.0$ | Total akumulasi energi fotometrik ($M_{00}$) |
| `skin_px` | `integer` | $0 \dots 1,200$ | Jumlah piksel warna kulit yang terdeteksi |
| `vx`, `vy` | `float` | $\text{px/s}$ | Estimasi vektor kecepatan kinematika target |

---

## 7. Panduan Kompilasi & Deployment

### Persyaratan Software
- [Arduino IDE](https://www.arduino.cc/en/software) (v2.0+) atau [PlatformIO](https://platformio.org/).
- ESP32 Board Package: **`esp32 by Espressif Systems`** (v2.0.11 atau lebih baru).

### Konfigurasi Menu Tools (Arduino IDE)

> [!IMPORTANT]
> Konfigurasi **PSRAM** wajib diatur ke **OPI PSRAM** dan **CPU Frequency** ke **240MHz**. Kegagalan mengatur opsi ini akan menyebabkan *Frame Buffer Allocation Panic* saat inisialisasi kamera.

| Pengaturan (*Setting*) | Nilai (*Value*) |
| :--- | :--- |
| **Board** | `XIAO_ESP32S3` |
| **CPU Frequency** | `240MHz (WiFi/BT)` |
| **Flash Mode** | `QIO 80MHz` |
| **Flash Size** | `8MB (64Mb)` |
| **Partition Scheme** | `16M Flash (3MB APP/9.9MB FATFS)` atau `Default 4MB with spiffs` |
| **PSRAM** | **`OPI PSRAM`** |
| **Core Debug Level** | `None` / `Info` |
| **Upload Speed** | `921600` |
| **USB CDC On Boot** | `Enabled` |

### Langkah Flashing & Konfigurasi WiFi

1. Buka berkas [KoRe.ino](file:///home/fioren/Arduino/Project/KoRe/KoRe.ino) pada Arduino IDE.
2. Atur kredensial jaringan pada baris 32-37:
   ```cpp
   #define USE_AP_MODE false
   const char* sta_ssid     = "NAMA_WIFI_ANDA";
   const char* sta_password = "PASSWORD_WIFI_ANDA";
   ```
   *(Atur `USE_AP_MODE true` jika ingin mengaktifkan mode Standalone Access Point `KoRe-Tracker`)*.
3. Hubungkan board Seeed XIAO ESP32-S3 Sense via kabel data USB-C.
4. Pilih Port COM yang sesuai dan klik tombol **Upload**.
5. Buka **Serial Monitor** pada baudrate `115200` untuk melihat alamat IP yang diperoleh.
6. Buka browser pada perangkat Anda dan akses:
   ```text
   http://<ALAMAT_IP_ESP32>/
   ```

> [!NOTE]
> Jika menggunakan Access Point Mode (`USE_AP_MODE true`), hubungkan WiFi laptop/smartphone ke SSID `KoRe-Tracker` (Password: `12345678`), lalu buka alamat `http://192.168.4.1/`.

---

## 8. Struktur Berkas Repositori

```
KoRe/
├── KoRe.ino             # Sumber kode utama firmware (C++, Vision Pipeline, Web Server, HUD)
├── camera/              # Direktori konfigurasi & driver sensor kamera
└── README.md            # Dokumentasi teknis sistem & spesifikasi arsitektur
```

---

## Lisensi & Kontribusi
Proyek ini dikembangkan di bawah lisensi terbuka untuk keperluan riset *Embedded Computer Vision*, Robotika Otonom, dan *Edge AI Processing*. Kontribusi, pengujian performa, dan *pull request* sangat dipersilakan.
