/**
 * @file web_ui.h
 * @brief Production Web UI dashboard and telemetry control interface for KoRe.
 * @details Neumorphic (Soft UI) Design System:
 *          - Pure solid surfaces with soft extruded and inset dual-shadows.
 *          - Zero glassmorphism (no blur / semi-transparency).
 *          - Zero rainbow gradients; solid cohesive tech palette (Soft Slate Base + Sky Accent).
 *          - Fully responsive tactile cards, rounded pill buttons, and inset inputs.
 *          - Real-time video stream viewport, HUD canvas, and telemetry indicators.
 */

#ifndef WEB_UI_H
#define WEB_UI_H

#include <pgmspace.h>

static const char HTML_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>KoRe Vision & Telemetry</title>
  <style>
    :root {
      --bg-base: #e8eef5;
      --bg-card: #e8eef5;
      --bg-subtle: #e2e9f2;
      --shadow-light: #ffffff;
      --shadow-dark: #c4d0de;
      --shadow-dark-deep: #b2c0d2;
      --accent-primary: #0284c7;
      --accent-hover: #0369a1;
      --accent-active: #075985;
      --accent-pill: #e0f2fe;
      --accent-pill-text: #0369a1;
      --text-main: #1e293b;
      --text-muted: #64748b;
      --text-subtle: #94a3b8;
      --status-live: #ef4444;
      --status-warning: #d97706;
      --border-subtle: #dbe4ef;
    }

    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
      -webkit-tap-highlight-color: transparent;
    }

    body {
      background-color: var(--bg-base);
      color: var(--text-main);
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 24px 16px;
      -webkit-font-smoothing: antialiased;
      -moz-osx-font-smoothing: grayscale;
    }

    .card {
      background-color: var(--bg-card);
      border-radius: 28px;
      box-shadow: 18px 18px 36px var(--shadow-dark), -18px -18px 36px var(--shadow-light);
      max-width: 480px;
      width: 100%;
      padding: 28px 24px;
      border: 1px solid var(--border-subtle);
    }

    /* Header Section */
    .header {
      display: flex;
      align-items: center;
      gap: 14px;
      margin-bottom: 18px;
    }

    .avatar-icon {
      width: 48px;
      height: 48px;
      border-radius: 16px;
      background-color: var(--bg-card);
      box-shadow: 5px 5px 10px var(--shadow-dark), -5px -5px 10px var(--shadow-light);
      display: flex;
      align-items: center;
      justify-content: center;
      color: var(--accent-primary);
      flex-shrink: 0;
    }

    .avatar-icon svg {
      width: 24px;
      height: 24px;
      fill: none;
      stroke: currentColor;
      stroke-width: 2;
      stroke-linecap: round;
      stroke-linejoin: round;
    }

    .header-text h1 {
      font-size: 17px;
      font-weight: 700;
      color: var(--text-main);
      letter-spacing: -0.01em;
      line-height: 1.2;
    }

    .header-text p.subtitle {
      font-size: 11px;
      color: var(--text-muted);
      margin-top: 3px;
      line-height: 1.35;
    }

    /* Mode Badge */
    .mode-badge {
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 8px;
      width: 100%;
      text-align: center;
      padding: 10px 14px;
      font-size: 11px;
      font-weight: 700;
      border-radius: 16px;
      background-color: var(--bg-card);
      color: var(--accent-primary);
      box-shadow: inset 3px 3px 6px var(--shadow-dark), inset -3px -3px 6px var(--shadow-light);
      margin-bottom: 20px;
      letter-spacing: 0.04em;
      text-transform: uppercase;
    }

    .mode-badge::before {
      content: '';
      display: inline-block;
      width: 8px;
      height: 8px;
      border-radius: 50%;
      background-color: var(--accent-primary);
    }

    .mode-badge.ap {
      color: var(--status-warning);
    }

    .mode-badge.ap::before {
      background-color: var(--status-warning);
    }

    /* Stream Viewport Section */
    .stream-wrapper {
      background-color: var(--bg-card);
      border-radius: 24px;
      padding: 10px;
      box-shadow: 7px 7px 14px var(--shadow-dark), -7px -7px 14px var(--shadow-light);
      margin-bottom: 18px;
    }

    .stream-container {
      position: relative;
      width: 100%;
      background-color: #090d16;
      border-radius: 16px;
      overflow: hidden;
      min-height: 240px;
      display: flex;
      justify-content: center;
      align-items: center;
      box-shadow: inset 2px 2px 6px #03060a;
    }

    .live-badge {
      position: absolute;
      top: 12px;
      right: 12px;
      z-index: 10;
      background-color: var(--status-live);
      color: #ffffff;
      font-size: 10px;
      font-weight: 700;
      letter-spacing: 0.06em;
      padding: 4px 8px;
      border-radius: 8px;
      display: flex;
      align-items: center;
      gap: 5px;
      box-shadow: 0 2px 6px rgba(239, 68, 68, 0.4);
    }

    .live-badge-dot {
      width: 6px;
      height: 6px;
      border-radius: 50%;
      background-color: #ffffff;
      animation: live-pulse 1.4s infinite ease-in-out;
    }

    @keyframes live-pulse {
      0%, 100% { opacity: 1; transform: scale(1); }
      50% { opacity: 0.4; transform: scale(0.8); }
    }

    .skeleton-loader {
      position: absolute;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: #0d121f;
      display: flex;
      flex-direction: column;
      justify-content: center;
      align-items: center;
      color: #64748b;
      font-size: 11px;
      font-weight: 600;
      letter-spacing: 0.05em;
      gap: 10px;
    }

    .skeleton-loader .skeleton-grid {
      width: 52px;
      height: 40px;
      border: 1.5px dashed #334155;
      border-radius: 6px;
    }

    .stream-container img {
      width: 100%;
      height: auto;
      display: block;
      object-fit: contain;
      z-index: 2;
    }

    #hud-canvas {
      position: absolute;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      pointer-events: none;
      z-index: 3;
    }

    /* Telemetry Metrics Grid */
    .telemetry-row {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 12px;
      margin-bottom: 22px;
    }

    .telemetry-cell {
      background-color: var(--bg-card);
      border-radius: 18px;
      padding: 12px 10px;
      text-align: center;
      box-shadow: 5px 5px 10px var(--shadow-dark), -5px -5px 10px var(--shadow-light);
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
    }

    .telemetry-label {
      color: var(--text-muted);
      display: block;
      font-size: 9.5px;
      font-weight: 700;
      margin-bottom: 4px;
      text-transform: uppercase;
      letter-spacing: 0.05em;
    }

    .telemetry-val {
      color: var(--text-main);
      font-size: 15px;
      font-weight: 800;
      font-family: ui-monospace, "IBM Plex Mono", "Roboto Mono", monospace;
    }

    /* Section Titles */
    .section-title {
      font-size: 12px;
      font-weight: 700;
      color: var(--text-main);
      margin-top: 20px;
      margin-bottom: 12px;
      text-transform: uppercase;
      letter-spacing: 0.06em;
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .section-title::after {
      content: '';
      flex: 1;
      height: 1px;
      background-color: var(--border-subtle);
    }

    /* Expression Control Matrix */
    .expr-grid {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 10px;
      margin-bottom: 12px;
    }

    .btn-expr {
      width: 100%;
      padding: 11px 4px;
      font-size: 11px;
      font-weight: 700;
      background-color: var(--bg-card);
      border: none;
      color: var(--text-muted);
      border-radius: 14px;
      cursor: pointer;
      text-transform: uppercase;
      letter-spacing: 0.03em;
      box-shadow: 4px 4px 8px var(--shadow-dark), -4px -4px 8px var(--shadow-light);
      transition: all 0.15s ease;
      display: flex;
      align-items: center;
      justify-content: center;
    }

    .btn-expr:hover {
      color: var(--text-main);
    }

    .btn-expr:active {
      transform: translateY(1px);
    }

    .btn-expr.active {
      background-color: var(--bg-subtle);
      color: var(--accent-primary);
      box-shadow: inset 3px 3px 6px var(--shadow-dark), inset -3px -3px 6px var(--shadow-light);
    }

    .btn-expr-auto {
      width: 100%;
      padding: 12px 14px;
      font-size: 12px;
      font-weight: 700;
      background-color: var(--bg-card);
      border: none;
      color: var(--text-main);
      border-radius: 16px;
      cursor: pointer;
      letter-spacing: 0.02em;
      box-shadow: 5px 5px 10px var(--shadow-dark), -5px -5px 10px var(--shadow-light);
      margin-bottom: 18px;
      transition: all 0.15s ease;
    }

    .btn-expr-auto:hover {
      color: var(--accent-primary);
    }

    .btn-expr-auto.active {
      background-color: var(--bg-subtle);
      color: var(--accent-primary);
      box-shadow: inset 3px 3px 6px var(--shadow-dark), inset -3px -3px 6px var(--shadow-light);
    }

    /* Switch Mode Button */
    .btn-switch-mode {
      width: 100%;
      padding: 13px 14px;
      font-size: 12px;
      font-weight: 700;
      background-color: var(--bg-card);
      border: none;
      color: var(--accent-primary);
      border-radius: 16px;
      cursor: pointer;
      letter-spacing: 0.02em;
      box-shadow: 5px 5px 10px var(--shadow-dark), -5px -5px 10px var(--shadow-light);
      margin-bottom: 18px;
      transition: all 0.15s ease;
    }

    .btn-switch-mode:hover {
      color: var(--accent-hover);
    }

    .btn-switch-mode:active {
      box-shadow: inset 3px 3px 6px var(--shadow-dark), inset -3px -3px 6px var(--shadow-light);
    }

    /* Form & Inset Inputs */
    .form-group {
      margin-bottom: 14px;
    }

    label {
      display: block;
      font-size: 11px;
      font-weight: 700;
      color: var(--text-muted);
      margin-bottom: 6px;
      text-transform: uppercase;
      letter-spacing: 0.04em;
    }

    input[type="text"], input[type="password"] {
      width: 100%;
      padding: 12px 16px;
      font-size: 13px;
      font-family: inherit;
      border: none;
      border-radius: 14px;
      background-color: var(--bg-card);
      color: var(--text-main);
      outline: none;
      box-shadow: inset 3px 3px 6px var(--shadow-dark), inset -3px -3px 6px var(--shadow-light);
      transition: box-shadow 0.2s ease;
    }

    input[type="text"]:focus, input[type="password"]:focus {
      box-shadow: inset 4px 4px 8px var(--shadow-dark-deep), inset -4px -4px 8px var(--shadow-light);
    }

    input::placeholder {
      color: var(--text-subtle);
    }

    /* Save Button */
    .btn-submit {
      width: 100%;
      padding: 14px 18px;
      margin-top: 14px;
      background-color: var(--accent-primary);
      color: #ffffff;
      font-family: inherit;
      font-size: 13px;
      font-weight: 700;
      border: none;
      border-radius: 16px;
      cursor: pointer;
      letter-spacing: 0.03em;
      box-shadow: 6px 6px 14px var(--shadow-dark), -6px -6px 14px var(--shadow-light);
      transition: all 0.15s ease;
    }

    .btn-submit:hover {
      background-color: var(--accent-hover);
    }

    .btn-submit:active {
      background-color: var(--accent-active);
      box-shadow: inset 2px 2px 6px rgba(0, 0, 0, 0.3);
      transform: translateY(1px);
    }

    .btn-submit:disabled, .btn-switch-mode:disabled {
      opacity: 0.6;
      cursor: not-allowed;
    }

    /* Status Box */
    #status {
      display: none;
      margin-top: 16px;
      padding: 14px 16px;
      font-size: 12px;
      font-weight: 500;
      border-radius: 16px;
      background-color: var(--bg-card);
      color: var(--text-main);
      line-height: 1.5;
      box-shadow: inset 3px 3px 6px var(--shadow-dark), inset -3px -3px 6px var(--shadow-light);
    }

    /* Legal Disclosures */
    .legal-footer {
      margin-top: 24px;
      padding-top: 14px;
      border-top: 1px solid var(--border-subtle);
      font-size: 10.5px;
      color: var(--text-muted);
      line-height: 1.6;
      text-align: center;
    }

    .legal-footer a {
      color: var(--accent-primary);
      text-decoration: none;
      font-weight: 600;
      cursor: pointer;
    }

    .legal-footer a:hover {
      text-decoration: underline;
    }

    .legal-modal {
      display: none;
      margin-top: 12px;
      padding: 14px;
      background-color: var(--bg-card);
      border-radius: 14px;
      font-size: 10px;
      color: var(--text-muted);
      line-height: 1.6;
      text-align: left;
      box-shadow: inset 3px 3px 6px var(--shadow-dark), inset -3px -3px 6px var(--shadow-light);
    }

    @media (max-width: 420px) {
      .card {
        padding: 20px 16px;
      }
      .expr-grid {
        grid-template-columns: repeat(4, 1fr);
        gap: 6px;
      }
      .btn-expr {
        font-size: 9.5px;
        padding: 10px 2px;
      }
    }
  </style>
</head>
<body>
  <div class="card">
    <div class="header">
      <div class="avatar-icon">
        <svg viewBox="0 0 24 24">
          <path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z"></path>
          <circle cx="12" cy="13" r="4"></circle>
        </svg>
      </div>
      <div class="header-text">
        <h1>KoRe Vision & Telemetry</h1>
        <p class="subtitle">Platform: Seeed XIAO ESP32-S3 Sense | FreeRTOS Dual-Core</p>
      </div>
    </div>
    
    <div id="mode-badge" class="mode-badge">Memuat status jaringan...</div>

    <div class="stream-wrapper">
      <div class="stream-container">
        <div class="live-badge">
          <div class="live-badge-dot"></div>
          LIVE
        </div>
        <div id="stream-skeleton" class="skeleton-loader">
          <div class="skeleton-grid"></div>
          <span>INITIALIZING SENSOR FEED...</span>
        </div>
        <img id="stream-img" src="" alt="Camera Stream" crossorigin="anonymous">
        <canvas id="hud-canvas"></canvas>
      </div>
    </div>

    <div class="telemetry-row">
      <div class="telemetry-cell">
        <span class="telemetry-label">AI FPS</span>
        <span id="tel-fps" class="telemetry-val">0.0</span>
      </div>
      <div class="telemetry-cell">
        <span class="telemetry-label">CONFIDENCE</span>
        <span id="tel-conf" class="telemetry-val">0.00</span>
      </div>
      <div class="telemetry-cell">
        <span class="telemetry-label">TARGET PROX</span>
        <span id="tel-prox" class="telemetry-val">0%</span>
      </div>
    </div>

    <div class="section-title">Kontrol Ekspresi Wajah</div>
    <div class="expr-grid">
      <button type="button" class="btn-expr" data-expr="0">IDLE</button>
      <button type="button" class="btn-expr" data-expr="1">JOY</button>
      <button type="button" class="btn-expr" data-expr="2">ANGRY</button>
      <button type="button" class="btn-expr" data-expr="3">SMIRK</button>
      <button type="button" class="btn-expr" data-expr="4">SHOCK</button>
      <button type="button" class="btn-expr" data-expr="5">OVERLOAD</button>
      <button type="button" class="btn-expr" data-expr="6">SEDIH</button>
      <button type="button" class="btn-expr" data-expr="7">DEADPAN</button>
    </div>
    <button type="button" id="btn-expr-auto" class="btn-expr-auto active">Kembalikan ke Default (Auto Mood)</button>

    <button type="button" id="btn-switch-mode" class="btn-switch-mode" style="display:none;"></button>

    <form id="wifi-form">
      <div class="section-title">WiFi Client (STA Mode)</div>
      <div class="form-group">
        <label for="sta_ssid">SSID WiFi</label>
        <input type="text" id="sta_ssid" name="sta_ssid" placeholder="Nama WiFi Router" required>
      </div>
      <div class="form-group">
        <label for="sta_pass">Password WiFi</label>
        <input type="password" id="sta_pass" name="sta_pass" placeholder="Password WiFi">
      </div>

      <div class="section-title">Access Point (AP Mode)</div>
      <div class="form-group">
        <label for="ap_ssid">SSID Access Point</label>
        <input type="text" id="ap_ssid" name="ap_ssid" placeholder="SSID KoRe Access Point" required>
      </div>
      <div class="form-group">
        <label for="ap_pass">Password Access Point</label>
        <input type="password" id="ap_pass" name="ap_pass" placeholder="Minimal 8 karakter atau kosong">
      </div>

      <button type="submit" id="btn-save" class="btn-submit">Simpan Konfigurasi & Hubungkan</button>
    </form>

    <div id="status"></div>

    <div class="legal-footer">
      <p>KoRe Biomechanical Engine. On-Device Local Signal Processing.</p>
      <p><a id="toggle-privacy">Kebijakan Privasi</a> | <a id="toggle-terms">Ketentuan Layanan</a></p>
      <div id="legal-content" class="legal-modal">
        <strong>Ketentuan Layanan & Kebijakan Privasi:</strong><br>
        1. Pemrosesan Data: Seluruh inferensi visi komputer, klasifikasi YCbCr, dan dinamika okulomotor dieksekusi secara lokal pada memori internal ESP32-S3 (SRAM).<br>
        2. Privasi: Tidak ada data gambar, video feed, atau kredensial Wi-Fi yang dikirim ke server pihak ketiga atau cloud eksternal.<br>
        3. Kredensial: Kredensial Access Point dan STA disimpan secara lokal di NVS Flash perangkat.
      </div>
    </div>
  </div>

  <script>
    const host = window.location.hostname;
    const img = document.getElementById('stream-img');
    const skeleton = document.getElementById('stream-skeleton');
    const canvas = document.getElementById('hud-canvas');
    const ctx = canvas.getContext('2d');

    img.src = 'http://' + host + ':81/stream';
    
    img.onload = function() {
      if (skeleton) skeleton.style.display = 'none';
      resizeCanvas();
    };

    img.onerror = function() {
      if (skeleton) skeleton.style.display = 'flex';
      setTimeout(function() {
        img.src = 'http://' + host + ':81/stream?t=' + Date.now();
      }, 1500);
    };

    function resizeCanvas() {
      if (img.clientWidth > 0 && img.clientHeight > 0) {
        canvas.width = img.clientWidth;
        canvas.height = img.clientHeight;
      }
    }
    window.addEventListener('resize', resizeCanvas);

    let renderBoxes = [];
    let telemetryTimer = null;

    async function updateTelemetry() {
      if (document.visibilityState === 'hidden') {
        telemetryTimer = setTimeout(updateTelemetry, 1000);
        return;
      }

      try {
        const res = await fetch('http://' + host + '/telemetry');
        const data = await res.json();
        ctx.clearRect(0, 0, canvas.width, canvas.height);

        document.getElementById('tel-fps').innerText = (data.fps_ai || 0).toFixed(1);
        document.getElementById('tel-conf').innerText = (data.conf || 0).toFixed(2);
        document.getElementById('tel-prox').innerText = ((data.prox || 0) * 100).toFixed(0) + '%';

        if (data.detected && data.fw > 0 && data.fh > 0) {
          resizeCanvas();
          const scaleX = canvas.width / data.fw;
          const scaleY = canvas.height / data.fh;

          const numCands = data.num_cands || 1;
          const colors = ['#0284c7', '#64748b', '#94a3b8'];
          const labels = ['P1 PRIMARY TRACK', 'P2 SCAN CANDIDATE', 'P3 SCAN CANDIDATE'];

          const cands = [
            { cx: data.c0_cx || data.cx, cy: data.c0_cy || data.cy, p: data.c0_p || 100 },
            { cx: data.c1_cx || 0, cy: data.c1_cy || 0, p: data.c1_p || 0 },
            { cx: data.c2_cx || 0, cy: data.c2_cy || 0, p: data.c2_p || 0 }
          ];

          for (let i = 0; i < numCands; i++) {
            const cand = cands[i];
            if (cand.cx <= 0) continue;

            const bw = data.w || 160;
            const bh = data.h || 200;

            const targetBx = (cand.cx - bw / 2) * scaleX;
            const targetBy = (cand.cy - bh / 2) * scaleY;
            const targetBw = bw * scaleX;
            const targetBh = bh * scaleY;
            const targetCx = cand.cx * scaleX;
            const targetCy = cand.cy * scaleY;

            if (!renderBoxes[i]) {
              renderBoxes[i] = { bx: targetBx, by: targetBy, bw: targetBw, bh: targetBh, cx: targetCx, cy: targetCy };
            } else {
              renderBoxes[i].bx = renderBoxes[i].bx * 0.25 + targetBx * 0.75;
              renderBoxes[i].by = renderBoxes[i].by * 0.25 + targetBy * 0.75;
              renderBoxes[i].bw = renderBoxes[i].bw * 0.25 + targetBw * 0.75;
              renderBoxes[i].bh = renderBoxes[i].bh * 0.25 + targetBh * 0.75;
              renderBoxes[i].cx = renderBoxes[i].cx * 0.25 + targetCx * 0.75;
              renderBoxes[i].cy = renderBoxes[i].cy * 0.25 + targetCy * 0.75;
            }

            const rBox = renderBoxes[i];
            const bx = rBox.bx, by = rBox.by, bWidth = rBox.bw, bHeight = rBox.bh, cX = rBox.cx, cY = rBox.cy;
            const color = colors[i % colors.length];
            const isInspected = (i === data.insp_idx);

            /* Bounding Box Line */
            ctx.strokeStyle = color;
            ctx.lineWidth = isInspected ? 2.5 : 1.5;
            if (!isInspected) ctx.setLineDash([4, 4]); else ctx.setLineDash([]);
            ctx.strokeRect(bx, by, bWidth, bHeight);
            ctx.setLineDash([]);

            /* Corner Reticles */
            const len = 12;
            ctx.lineWidth = 2.5;
            ctx.beginPath(); ctx.moveTo(bx, by + len); ctx.lineTo(bx, by); ctx.lineTo(bx + len, by); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(bx + bWidth - len, by); ctx.lineTo(bx + bWidth, by); ctx.lineTo(bx + bWidth, by + len); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(bx, by + bHeight - len); ctx.lineTo(bx, by + bHeight); ctx.lineTo(bx + len, by + bHeight); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(bx + bWidth - len, by + bHeight); ctx.lineTo(bx + bWidth, by + bHeight); ctx.lineTo(bx + bWidth, by + len); ctx.stroke();

            /* Label & Metrics */
            ctx.fillStyle = color;
            ctx.font = '700 10px monospace';
            const statusText = isInspected ? ' [SCANNING]' : '';
            const proxText = (data.prox !== undefined) ? ` Z:${(data.prox * 100).toFixed(0)}%` : '';
            ctx.fillText(labels[i] + statusText + proxText, bx + 3, by - 6);

            /* Technical Crosshair */
            if (isInspected) {
              ctx.strokeStyle = '#ffffff';
              ctx.lineWidth = 1.5;
              ctx.beginPath();
              ctx.moveTo(cX - 8, cY); ctx.lineTo(cX + 8, cY);
              ctx.moveTo(cX, cY - 8); ctx.lineTo(cX, cY + 8);
              ctx.stroke();
              ctx.beginPath(); ctx.arc(cX, cY, 3, 0, 2 * Math.PI); ctx.stroke();
            }
          }
        } else {
          renderBoxes = [];
        }

        /* Update active expression indicator */
        const isManual = (data.is_manual === true || data.is_manual === 'true');
        const currentExpr = (data.expr !== undefined) ? parseInt(data.expr, 10) : -1;
        const btnAuto = document.getElementById('btn-expr-auto');

        if (!isManual) {
          if (btnAuto) btnAuto.classList.add('active');
          document.querySelectorAll('.btn-expr').forEach(btn => btn.classList.remove('active'));
        } else {
          if (btnAuto) btnAuto.classList.remove('active');
          document.querySelectorAll('.btn-expr').forEach(btn => {
            if (parseInt(btn.dataset.expr, 10) === currentExpr) {
              btn.classList.add('active');
            } else {
              btn.classList.remove('active');
            }
          });
        }
      } catch (e) {}
      telemetryTimer = setTimeout(updateTelemetry, 100);
    }

    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'visible') {
        if (telemetryTimer) clearTimeout(telemetryTimer);
        updateTelemetry();
      }
    });

    updateTelemetry();

    /* Expression Control Logic */
    document.querySelectorAll('.btn-expr').forEach(btn => {
      btn.addEventListener('click', function() {
        const exprId = this.dataset.expr;
        document.querySelectorAll('.btn-expr').forEach(b => b.classList.remove('active'));
        const btnAuto = document.getElementById('btn-expr-auto');
        if (btnAuto) btnAuto.classList.remove('active');
        this.classList.add('active');

        fetch('/set_expression', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ expr: parseInt(exprId, 10) })
        }).catch(() => {});
      });
    });

    const btnExprAuto = document.getElementById('btn-expr-auto');
    if (btnExprAuto) {
      btnExprAuto.addEventListener('click', function() {
        document.querySelectorAll('.btn-expr').forEach(b => b.classList.remove('active'));
        this.classList.add('active');

        fetch('/set_expression', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ expr: 'auto' })
        }).catch(() => {});
      });
    }

    /* Wi-Fi Configuration Logic */
    fetch('/get_wifi')
      .then(res => res.json())
      .then(data => {
        if (data.sta_ssid) document.getElementById('sta_ssid').value = data.sta_ssid;
        if (data.sta_pass) document.getElementById('sta_pass').value = data.sta_pass;
        if (data.ap_ssid) document.getElementById('ap_ssid').value = data.ap_ssid;
        if (data.ap_pass) document.getElementById('ap_pass').value = data.ap_pass;

        const badge = document.getElementById('mode-badge');
        const btnSwitch = document.getElementById('btn-switch-mode');
        
        if (data.is_ap) {
          badge.innerText = 'STATUS OPERASIONAL: ACCESS POINT (AP)';
          badge.classList.add('ap');
          btnSwitch.innerText = 'Beralih ke WiFi Client (STA Mode)';
          btnSwitch.dataset.targetMode = 'STA';
        } else {
          badge.innerText = 'STATUS OPERASIONAL: WIFI CLIENT (STA)';
          badge.classList.remove('ap');
          btnSwitch.innerText = 'Beralih ke Access Point (AP Mode)';
          btnSwitch.dataset.targetMode = 'AP';
        }
        btnSwitch.style.display = 'block';
      })
      .catch(() => {});

    document.getElementById('btn-switch-mode').addEventListener('click', function() {
      const targetMode = this.dataset.targetMode;
      const btn = this;
      const status = document.getElementById('status');
      
      btn.disabled = true;
      document.getElementById('btn-save').disabled = true;
      status.style.display = 'block';

      const apUrl = 'http://192.168.4.1';

      if (targetMode === 'AP') {
        btn.innerText = 'Beralih ke AP Mode...';
        status.innerHTML = 'ESP32 sedang reboot ke AP Mode.<br>Hubungkan Wi-Fi ke <b>KoRe</b> lalu buka: <a href="' + apUrl + '" style="color:#0284c7;font-weight:700;">' + apUrl + '</a><br><small>Pengalihan otomatis dalam 4 detik...</small>';

        fetch('/switch_mode', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ mode: targetMode })
        }).finally(() => {
          setTimeout(() => { window.location.href = apUrl; }, 4000);
        });
      } else {
        const staSsid = document.getElementById('sta_ssid').value || 'WiFi Router';
        const staUrl = 'http://kore.local';
        btn.innerText = 'Beralih ke STA Mode...';
        status.innerHTML = 'ESP32 sedang reboot dan menghubungkan ke <b>' + staSsid + '</b>.<br>Buka URL STA: <a href="' + staUrl + '" style="color:#0284c7;font-weight:700;">' + staUrl + '</a><br><small>Pengalihan otomatis ke ' + staUrl + ' dalam 6 detik...</small>';

        fetch('/switch_mode', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ mode: targetMode })
        }).finally(() => {
          setTimeout(() => { window.location.href = staUrl; }, 6000);
        });
      }
    });

    document.getElementById('wifi-form').addEventListener('submit', function(e) {
      e.preventDefault();
      const btn = document.getElementById('btn-save');
      const status = document.getElementById('status');

      const apPass = document.getElementById('ap_pass').value;
      if (apPass.length > 0 && apPass.length < 8) {
        status.style.display = 'block';
        status.style.color = '#ef4444';
        status.innerText = 'Error: Password AP harus dikosongkan (Open AP) atau minimal 8 karakter.';
        return;
      }

      status.style.color = 'var(--text-main)';
      btn.disabled = true;
      document.getElementById('btn-switch-mode').disabled = true;
      btn.innerText = 'Menyimpan konfigurasi...';
      status.style.display = 'block';

      const staSsid = document.getElementById('sta_ssid').value || 'WiFi Router';
      const staUrl = 'http://kore.local';
      status.innerHTML = 'Konfigurasi disimpan. ESP32 reboot menghubungkan ke <b>' + staSsid + '</b>.<br>Buka URL STA: <a href="' + staUrl + '" style="color:#0284c7;font-weight:700;">' + staUrl + '</a><br><small>Pengalihan otomatis dalam 6 detik...</small>';

      const body = {
        sta_ssid: document.getElementById('sta_ssid').value,
        sta_pass: document.getElementById('sta_pass').value,
        ap_ssid: document.getElementById('ap_ssid').value,
        ap_pass: document.getElementById('ap_pass').value
      };

      fetch('/save_wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body)
      }).finally(() => {
        setTimeout(() => { window.location.href = staUrl; }, 6000);
      });
    });

    /* Legal Disclosures Toggle */
    const toggleLegal = () => {
      const modal = document.getElementById('legal-content');
      modal.style.display = (modal.style.display === 'block') ? 'none' : 'block';
    };
    document.getElementById('toggle-privacy').addEventListener('click', toggleLegal);
    document.getElementById('toggle-terms').addEventListener('click', toggleLegal);
  </script>
</body>
</html>
)rawliteral";

#endif /* WEB_UI_H */
