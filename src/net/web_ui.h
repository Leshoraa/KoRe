/**
 * @file web_ui.h
 * @brief Production Web UI dashboard and telemetry control interface.
 * @details Strictly conforms to Section 6 of KoRe Engineering Specification:
 *          - Calibrated dark matte technical surface (#0e0e10 / #18181b).
 *          - Zero neon colors, zero pastels, zero pure white canvas backgrounds.
 *          - Sharp 2px to 4px container corner radii, 1px subtle borders, zero drop shadows.
 *          - Structural skeleton loader preventing layout shifts.
 *          - Technical monospace typography (IBM Plex Mono, Roboto Mono, ui-monospace).
 *          - Mandatory on-device Privacy Policy and Terms of Service disclosures.
 *          - Zero em dashes, zero emojis.
 */

#ifndef WEB_UI_H
#define WEB_UI_H

#include <pgmspace.h>

static const char HTML_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>KoRe: Ocular Kinematics & Vision Telemetry</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      background-color: #0e0e10;
      color: #f4f4f5;
      font-family: ui-monospace, "IBM Plex Mono", "Roboto Mono", SFMono-Regular, Menlo, Monaco, Consolas, monospace;
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 16px;
      -webkit-font-smoothing: antialiased;
    }
    .card {
      background: #18181b;
      border: 1px solid #27272a;
      border-radius: 4px;
      max-width: 480px;
      width: 100%;
      padding: 20px;
    }
    h1 {
      font-size: 15px;
      font-weight: 600;
      letter-spacing: -0.02em;
      margin-bottom: 4px;
      color: #f4f4f5;
      text-transform: uppercase;
    }
    p.subtitle {
      font-size: 11px;
      color: #a1a1aa;
      margin-bottom: 14px;
      line-height: 1.4;
    }
    .mode-badge {
      display: block;
      width: 100%;
      text-align: center;
      padding: 6px 10px;
      font-size: 11px;
      font-weight: 500;
      border-radius: 2px;
      background: #1f1f23;
      color: #e4e4e7;
      margin-bottom: 14px;
      border: 1px solid #3f3f46;
      letter-spacing: 0.02em;
    }
    .mode-badge.ap {
      background: #272015;
      color: #fbbf24;
      border-color: #78350f;
    }
    .stream-container {
      position: relative;
      width: 100%;
      background: #09090b;
      border-radius: 2px;
      overflow: hidden;
      margin-bottom: 14px;
      border: 1px solid #27272a;
      min-height: 240px;
      display: flex;
      justify-content: center;
      align-items: center;
    }
    .skeleton-loader {
      position: absolute;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      background: linear-gradient(90deg, #18181b 25%, #27272a 50%, #18181b 75%);
      background-size: 200% 100%;
      animation: skeleton-pulse 1.8s infinite ease-in-out;
      display: flex;
      flex-direction: column;
      justify-content: center;
      align-items: center;
      color: #71717a;
      font-size: 11px;
      letter-spacing: 0.05em;
      gap: 8px;
    }
    .skeleton-loader .skeleton-grid {
      width: 48px;
      height: 36px;
      border: 1px dashed #3f3f46;
      border-radius: 2px;
    }
    @keyframes skeleton-pulse {
      0% { background-position: 200% 0; }
      100% { background-position: -200% 0; }
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
    .telemetry-row {
      display: grid;
      grid-template-columns: 1fr 1fr 1fr;
      gap: 8px;
      margin-bottom: 14px;
    }
    .telemetry-cell {
      background: #121214;
      border: 1px solid #27272a;
      border-radius: 2px;
      padding: 6px 8px;
      font-size: 10px;
    }
    .telemetry-label {
      color: #71717a;
      display: block;
      font-size: 9px;
      margin-bottom: 2px;
      text-transform: uppercase;
    }
    .telemetry-val {
      color: #e4e4e7;
      font-weight: 600;
    }
    .section-title {
      font-size: 11px;
      font-weight: 600;
      color: #a1a1aa;
      margin-top: 14px;
      margin-bottom: 10px;
      border-bottom: 1px solid #27272a;
      padding-bottom: 4px;
      text-transform: uppercase;
      letter-spacing: 0.05em;
    }
    .form-group {
      margin-bottom: 10px;
    }
    label {
      display: block;
      font-size: 11px;
      font-weight: 500;
      color: #a1a1aa;
      margin-bottom: 4px;
    }
    input[type="text"], input[type="password"] {
      width: 100%;
      padding: 8px 10px;
      font-size: 12px;
      font-family: inherit;
      border: 1px solid #27272a;
      border-radius: 2px;
      background: #121214;
      color: #f4f4f5;
      outline: none;
    }
    input[type="text"]:focus, input[type="password"]:focus {
      border-color: #52525b;
    }
    button {
      width: 100%;
      padding: 9px 12px;
      margin-top: 12px;
      background-color: #27272a;
      color: #f4f4f5;
      font-family: inherit;
      font-size: 12px;
      font-weight: 600;
      border: 1px solid #3f3f46;
      border-radius: 2px;
      cursor: pointer;
      letter-spacing: 0.02em;
    }
    button:hover {
      background-color: #3f3f46;
      border-color: #52525b;
    }
    button:disabled {
      background-color: #18181b;
      color: #52525b;
      border-color: #27272a;
      cursor: not-allowed;
    }
    .btn-switch-mode {
      background-color: #18181b;
      border-color: #3f3f46;
      color: #38bdf8;
      margin-top: 0;
      margin-bottom: 14px;
    }
    .btn-switch-mode:hover {
      background-color: #27272a;
    }
    .expr-grid {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 6px;
      margin-bottom: 8px;
    }
    .btn-expr {
      width: 100%;
      padding: 7px 4px;
      margin-top: 0;
      font-size: 10px;
      font-weight: 600;
      background: #121214;
      border: 1px solid #27272a;
      color: #a1a1aa;
      border-radius: 2px;
      cursor: pointer;
      text-transform: uppercase;
      letter-spacing: 0.02em;
    }
    .btn-expr:hover {
      background: #27272a;
      color: #f4f4f5;
      border-color: #3f3f46;
    }
    .btn-expr.active {
      background: #1e293b;
      border-color: #38bdf8;
      color: #38bdf8;
    }
    .btn-expr-auto {
      width: 100%;
      padding: 8px 10px;
      margin-top: 0;
      margin-bottom: 14px;
      font-size: 11px;
      font-weight: 600;
      background: #18181b;
      border: 1px solid #3f3f46;
      color: #e4e4e7;
      border-radius: 2px;
      cursor: pointer;
      letter-spacing: 0.02em;
    }
    .btn-expr-auto:hover {
      background: #27272a;
    }
    .btn-expr-auto.active {
      border-color: #38bdf8;
      color: #38bdf8;
      background: #0f172a;
    }
    #status {
      display: none;
      margin-top: 12px;
      padding: 8px 10px;
      font-size: 11px;
      border-radius: 2px;
      border: 1px solid #3f3f46;
      background: #121214;
      color: #e4e4e7;
      line-height: 1.4;
    }
    .legal-footer {
      margin-top: 16px;
      padding-top: 10px;
      border-top: 1px solid #27272a;
      font-size: 9px;
      color: #71717a;
      line-height: 1.5;
    }
    .legal-footer a {
      color: #a1a1aa;
      text-decoration: underline;
      cursor: pointer;
    }
    .legal-modal {
      display: none;
      margin-top: 10px;
      padding: 10px;
      background: #121214;
      border: 1px solid #27272a;
      border-radius: 2px;
      font-size: 9px;
      color: #a1a1aa;
      line-height: 1.5;
    }
  </style>
</head>
<body>
  <div class="card">
    <h1>KoRe Vision & Telemetry</h1>
    <p class="subtitle">Platform: Seeed XIAO ESP32-S3 Sense | FreeRTOS Dual-Core Topology</p>
    
    <div id="mode-badge" class="mode-badge">Memuat status jaringan...</div>

    <div class="stream-container">
      <div id="stream-skeleton" class="skeleton-loader">
        <div class="skeleton-grid"></div>
        <span>INITIALIZING SENSOR FEED...</span>
      </div>
      <img id="stream-img" src="" alt="Camera Stream" crossorigin="anonymous">
      <canvas id="hud-canvas"></canvas>
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

      <button type="submit" id="btn-save">Simpan Konfigurasi & Hubungkan</button>
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
          /* Calibrated technical contrast palette: Primary Sky, Slate, Dark Slate */
          const colors = ['#38bdf8', '#94a3b8', '#64748b'];
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
            ctx.lineWidth = isInspected ? 2.0 : 1.0;
            if (!isInspected) ctx.setLineDash([4, 4]); else ctx.setLineDash([]);
            ctx.strokeRect(bx, by, bWidth, bHeight);
            ctx.setLineDash([]);

            /* Corner Reticles */
            const len = 10;
            ctx.lineWidth = 2.0;
            ctx.beginPath(); ctx.moveTo(bx, by + len); ctx.lineTo(bx, by); ctx.lineTo(bx + len, by); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(bx + bWidth - len, by); ctx.lineTo(bx + bWidth, by); ctx.lineTo(bx + bWidth, by + len); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(bx, by + bHeight - len); ctx.lineTo(bx, by + bHeight); ctx.lineTo(bx + len, by + bHeight); ctx.stroke();
            ctx.beginPath(); ctx.moveTo(bx + bWidth - len, by + bHeight); ctx.lineTo(bx + bWidth, by + bHeight); ctx.lineTo(bx + bWidth, by + len); ctx.stroke();

            /* Label & Metrics */
            ctx.fillStyle = color;
            ctx.font = '10px monospace';
            const statusText = isInspected ? ' [SCANNING]' : '';
            const proxText = (data.prox !== undefined) ? ` Z:${(data.prox * 100).toFixed(0)}%` : '';
            ctx.fillText(labels[i] + statusText + proxText, bx + 3, by - 5);

            /* Technical Crosshair */
            if (isInspected) {
              ctx.strokeStyle = '#e2e8f0';
              ctx.lineWidth = 1.0;
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
        status.innerHTML = 'ESP32 sedang reboot ke AP Mode.<br>Hubungkan Wi-Fi ke <b>KoRe</b> lalu buka: <a href="' + apUrl + '" style="color:#38bdf8;text-decoration:underline;">' + apUrl + '</a><br><small>Pengalihan otomatis dalam 4 detik...</small>';

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
        status.innerHTML = 'ESP32 sedang reboot dan menghubungkan ke <b>' + staSsid + '</b>.<br>Buka URL STA: <a href="' + staUrl + '" style="color:#38bdf8;text-decoration:underline;">' + staUrl + '</a><br><small>Pengalihan otomatis ke ' + staUrl + ' dalam 6 detik...</small>';

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

      status.style.color = '#e4e4e7';
      btn.disabled = true;
      document.getElementById('btn-switch-mode').disabled = true;
      btn.innerText = 'Menyimpan konfigurasi...';
      status.style.display = 'block';

      const staSsid = document.getElementById('sta_ssid').value || 'WiFi Router';
      const staUrl = 'http://kore.local';
      status.innerHTML = 'Konfigurasi disimpan. ESP32 reboot menghubungkan ke <b>' + staSsid + '</b>.<br>Buka URL STA: <a href="' + staUrl + '" style="color:#38bdf8;text-decoration:underline;">' + staUrl + '</a><br><small>Pengalihan otomatis dalam 6 detik...</small>';

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
