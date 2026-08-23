/**
 * @file web_ui.h
 * @brief Production Web UI dashboard and telemetry control interface for KoRe.
 * @details Balanced Modern Bento Grid Design System:
 *          - Integrated Top Segmented Tab Control aligned with dashboard grid.
 *          - Balanced desktop height between camera feed and right telemetry/expression column.
 *          - Pure geometric typography with rounded numerals (robust offline system fallback).
 *          - Proportional 3-column metric tiles on both mobile and desktop screens.
 *          - Minimalist Apple Camera-style focus tracker on HTML5 HUD canvas.
 */

#ifndef WEB_UI_H
#define WEB_UI_H

#include <pgmspace.h>

static const char HTML_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>KoRe Vision & Control</title>
  <link rel="preconnect" href="https://fonts.googleapis.com">
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
  <link href="https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@500;600;700;800&display=swap" rel="stylesheet">
  <style>
    :root {
      /* Material 3 Expressive Color System (Light Baseline & Surfaces) */
      --md-sys-color-surface: #f8f9fd;
      --md-sys-color-surface-dim: #d9dbdf;
      --md-sys-color-surface-bright: #ffffff;
      --md-sys-color-surface-container-lowest: #ffffff;
      --md-sys-color-surface-container-low: #f3f4f8;
      --md-sys-color-surface-container: #eeeff3;
      --md-sys-color-surface-container-high: #e8e9ee;
      --md-sys-color-surface-container-highest: #e2e3e8;

      /* Material 3 Expressive On-Surface & Typography Roles */
      --md-sys-color-on-surface: #191c20;
      --md-sys-color-on-surface-variant: #43474e;
      --md-sys-color-outline: #73777f;
      --md-sys-color-outline-variant: #c3c7cf;

      /* Material 3 Expressive Primary & Accent Roles */
      --md-sys-color-primary: #191c20;
      --md-sys-color-on-primary: #ffffff;
      --md-sys-color-primary-container: #dce2ed;
      --md-sys-color-on-primary-container: #111827;

      /* Material 3 Expressive Secondary & Tertiary Roles */
      --md-sys-color-secondary: #565f6c;
      --md-sys-color-on-secondary: #ffffff;
      --md-sys-color-secondary-container: #dbe3ee;
      --md-sys-color-on-secondary-container: #131d27;
      --md-sys-color-tertiary: #386663;
      --md-sys-color-tertiary-container: #bcebe5;
      --md-sys-color-on-tertiary-container: #002023;

      /* Semantic System Mapping */
      --bg-page: var(--md-sys-color-surface);
      --bg-card: var(--md-sys-color-surface-bright);
      --bg-surface: var(--md-sys-color-surface-container);
      --bg-surface-hover: var(--md-sys-color-surface-container-high);
      --bg-subtle: var(--md-sys-color-surface-container-low);
      --text-main: var(--md-sys-color-on-surface);
      --text-muted: var(--md-sys-color-on-surface-variant);
      --text-subtle: var(--md-sys-color-outline);
      --border-card: var(--md-sys-color-outline-variant);
      --border-subtle: var(--md-sys-color-surface-container-highest);
      --accent-dark: var(--md-sys-color-primary);
      --accent-dark-hover: #2d3137;
      --shadow-card: 0 1px 3px rgba(0, 0, 0, 0.04), 0 1px 2px rgba(0, 0, 0, 0.02);
      
      /* Material 3 Expressive Corner Radius Scale */
      --radius-card: 20px;
      --radius-sub: 14px;
      --radius-control: 10px;
    }

    * {
      margin: 0;
      padding: 0;
      box-sizing: border-box;
      -webkit-tap-highlight-color: transparent;
      outline: none;
    }

    button {
      outline: none;
      border: none;
      background: none;
      -webkit-appearance: none;
      -moz-appearance: none;
      appearance: none;
    }

    button:focus, button:focus-visible, button:active {
      outline: none;
    }

    body {
      background-color: var(--bg-page);
      color: var(--text-main);
      font-family: "Plus Jakarta Sans", -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      min-height: 100vh;
      padding: 20px 16px;
      -webkit-font-smoothing: antialiased;
      -moz-osx-font-smoothing: grayscale;
      display: flex;
      flex-direction: column;
      align-items: center;
    }

    .app-wrapper {
      width: 100%;
      max-width: 920px;
      display: flex;
      flex-direction: column;
      gap: 14px;
    }

    /* Top Navigation Bar */
    .top-controls {
      display: flex;
      align-items: center;
      justify-content: space-between;
      width: 100%;
      gap: 12px;
    }

    /* Material 3 Expressive Tab Navigation */
    .tab-segmented-control {
      display: inline-flex;
      align-items: center;
      background-color: var(--bg-card);
      border: 1px solid var(--border-card);
      border-radius: var(--radius-card);
      padding: 4px;
      gap: 4px;
      box-shadow: var(--shadow-card);
      position: relative;
    }

    .tab-btn {
      position: relative;
      border: none;
      outline: none;
      background: transparent;
      padding: 8px 18px 10px;
      border-radius: 20px; /* Full Rounded Pill (proportional to height) */
      font-family: inherit;
      font-size: 12.5px;
      font-weight: 600;
      color: var(--text-muted);
      cursor: pointer;
      transition: color 0.2s ease,
                  background-color 0.2s ease,
                  border-radius 0.25s cubic-bezier(0.2, 0, 0, 1),
                  transform 0.15s ease;
      white-space: nowrap;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      gap: 7px;
      user-select: none;
    }

    .tab-btn .tab-icon {
      width: 15px;
      height: 15px;
      stroke: currentColor;
      transition: transform 0.2s ease;
    }

    .tab-btn:hover {
      background-color: var(--bg-surface);
      color: var(--text-main);
      border-radius: 12px; /* M3 Medium on Hover */
    }

    .tab-btn:active {
      transform: scale(0.97);
      border-radius: 9px; /* Small on Click */
    }

    .tab-btn.active {
      color: var(--text-main);
      font-weight: 700;
      background-color: var(--bg-surface);
      border-radius: 12px; /* M3 Medium when Selected */
    }

    /* M3 Expressive Inset Active Indicator Line */
    .tab-btn::after {
      content: '';
      position: absolute;
      bottom: 2px;
      left: 18%;
      right: 18%;
      height: 3px;
      background-color: var(--accent-dark);
      border-radius: 3px 3px 0 0;
      transform: scaleX(0);
      opacity: 0;
      transition: transform 0.28s cubic-bezier(0.2, 0, 0, 1), opacity 0.2s ease;
    }

    .tab-btn.active::after {
      transform: scaleX(1);
      opacity: 1;
    }

    .mode-pill {
      background-color: var(--bg-card);
      border: 1px solid var(--border-card);
      border-radius: var(--radius-control);
      padding: 6px 14px;
      font-size: 12px;
      font-weight: 600;
      color: var(--text-muted);
      white-space: nowrap;
      box-shadow: var(--shadow-card);
    }

    /* Bento Grid Structure */
    .bento-grid {
      display: grid;
      grid-template-columns: 1.28fr 1fr;
      gap: 14px;
      width: 100%;
      align-items: stretch;
    }

    .bento-col {
      display: flex;
      flex-direction: column;
      gap: 14px;
      justify-content: space-between;
    }

    .bento-card {
      background-color: var(--bg-card);
      border: 1px solid var(--border-card);
      border-radius: var(--radius-card);
      padding: 16px;
      box-shadow: var(--shadow-card);
      display: flex;
      flex-direction: column;
    }

    .bento-card-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 12px;
    }

    .card-title {
      font-size: 13.5px;
      font-weight: 700;
      color: var(--text-main);
      letter-spacing: -0.01em;
    }

    .card-badge {
      font-size: 11px;
      font-weight: 600;
      color: var(--text-muted);
      background-color: var(--bg-surface);
      border: none;
      padding: 3px 8px;
      border-radius: 5px;
    }

    /* Camera Stream Card */
    .stream-viewport {
      position: relative;
      width: 100%;
      aspect-ratio: 4 / 3;
      background-color: #090d16;
      border-radius: var(--radius-sub);
      overflow: hidden;
      display: flex;
      justify-content: center;
      align-items: center;
      border: none;
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
      letter-spacing: 0.04em;
      gap: 10px;
    }

    .skeleton-loader .skeleton-grid {
      width: 40px;
      height: 30px;
      border: 1.5px dashed #334155;
      border-radius: 6px;
    }

    .stream-viewport img {
      width: 100%;
      height: 100%;
      object-fit: contain;
      display: block;
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

    /* Telemetry Metrics Grid - Strokeless Inner Cards */
    .metrics-grid {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 8px;
    }

    .metric-box {
      background-color: var(--bg-surface);
      border: none; /* Strokeless */
      border-radius: var(--radius-sub);
      padding: 12px 10px;
      text-align: center;
      display: flex;
      flex-direction: column;
      justify-content: center;
      gap: 4px;
      transition: background-color 0.2s ease;
    }

    .metric-box:hover {
      background-color: var(--bg-surface-hover);
    }

    .metric-label {
      font-size: 10px;
      font-weight: 700;
      color: var(--text-muted);
      text-transform: uppercase;
      letter-spacing: 0.03em;
    }

    .metric-value-row {
      display: flex;
      align-items: baseline;
      justify-content: center;
      gap: 3px;
    }

    .metric-number {
      font-size: 18px;
      font-weight: 700;
      color: var(--text-main);
      font-family: inherit;
      font-variant-numeric: tabular-nums;
      line-height: 1.1;
      letter-spacing: -0.02em;
    }

    .metric-unit {
      font-size: 10.5px;
      font-weight: 600;
      color: var(--text-muted);
    }

    /* Expression Matrix Card - M3 Expressive Morphing Buttons */
    .expr-grid {
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 6px;
      margin-bottom: 8px;
    }

    .btn-expr {
      width: 100%;
      padding: 10px 4px;
      font-family: inherit;
      font-size: 11px;
      font-weight: 600;
      background-color: var(--bg-surface);
      border: none; /* Strokeless */
      outline: none;
      color: var(--text-muted);
      border-radius: 18px; /* Full Rounded Pill (proportional) */
      cursor: pointer;
      text-transform: uppercase;
      letter-spacing: 0.02em;
      transition: background-color 0.2s ease,
                  color 0.2s ease,
                  border-radius 0.25s cubic-bezier(0.2, 0, 0, 1),
                  transform 0.15s ease,
                  box-shadow 0.2s ease;
      display: flex;
      align-items: center;
      justify-content: center;
      user-select: none;
    }

    .btn-expr:hover {
      background-color: var(--bg-surface-hover);
      color: var(--text-main);
      border-radius: 12px;
    }

    .btn-expr:active {
      border-radius: 8px;
      transform: scale(0.96);
    }

    .btn-expr:focus, .btn-expr:focus-visible {
      outline: none;
    }

    .btn-expr.active {
      background-color: var(--accent-dark);
      color: #ffffff;
      border-radius: 12px;
      box-shadow: 0 2px 6px rgba(17, 24, 39, 0.22);
      font-weight: 700;
    }

    /* Material 3 Expressive Shape Library Morphing per Expression */
    .btn-expr[data-expr="0"]:hover, .btn-expr[data-expr="0"].active { border-radius: 12px; } /* IDLE: Balanced Squircle */
    .btn-expr[data-expr="1"]:hover, .btn-expr[data-expr="1"].active { border-radius: 16px 8px 16px 8px; } /* JOY: Clover / Flower */
    .btn-expr[data-expr="2"]:hover, .btn-expr[data-expr="2"].active { border-radius: 6px 16px 6px 16px; } /* ANGRY: Slanted / Sharp */
    .btn-expr[data-expr="3"]:hover, .btn-expr[data-expr="3"].active { border-radius: 16px 8px 16px 14px; } /* SMIRK: Asymmetric Wink */
    .btn-expr[data-expr="4"]:hover, .btn-expr[data-expr="4"].active { border-radius: 18px; } /* SHOCK: Bloom / Oval */
    .btn-expr[data-expr="5"]:hover, .btn-expr[data-expr="5"].active { border-radius: 8px 16px 8px 16px; } /* OVERLOAD: Gem / Diamond */
    .btn-expr[data-expr="6"]:hover, .btn-expr[data-expr="6"].active { border-radius: 16px 16px 8px 8px; } /* SEDIH: Arch / Droplet */
    .btn-expr[data-expr="7"]:hover, .btn-expr[data-expr="7"].active { border-radius: 8px; } /* DEADPAN: Flat Squircle */

    .btn-expr-auto {
      width: 100%;
      padding: 11px 16px;
      font-family: inherit;
      font-size: 12px;
      font-weight: 600;
      background-color: var(--bg-surface);
      border: none; /* Strokeless */
      outline: none;
      color: var(--text-main);
      border-radius: 22px; /* Full Rounded Pill (proportional) */
      cursor: pointer;
      transition: background-color 0.2s ease,
                  color 0.2s ease,
                  border-radius 0.25s cubic-bezier(0.2, 0, 0, 1),
                  transform 0.15s ease,
                  box-shadow 0.2s ease;
      display: flex;
      align-items: center;
      justify-content: center;
      user-select: none;
    }

    .btn-expr-auto:hover {
      background-color: var(--bg-surface-hover);
      border-radius: 12px; /* Morph to Medium on Hover */
    }

    .btn-expr-auto:active {
      border-radius: 9px;
      transform: scale(0.97);
    }

    .btn-expr-auto:focus, .btn-expr-auto:focus-visible {
      outline: none;
    }

    .btn-expr-auto.active {
      background-color: var(--accent-dark);
      color: #ffffff;
      border-radius: 12px; /* Morph to Medium when Selected */
      box-shadow: 0 2px 6px rgba(17, 24, 39, 0.22);
      font-weight: 700;
    }

    /* Network Configuration Panel - Strokeless Inner Fields */
    .network-container {
      width: 100%;
      max-width: 680px;
      margin: 0 auto;
    }

    .form-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 14px;
      margin-bottom: 14px;
    }

    .form-section {
      background-color: var(--bg-surface);
      border: none; /* Strokeless */
      border-radius: var(--radius-sub);
      padding: 14px;
    }

    .form-section-title {
      font-size: 12px;
      font-weight: 700;
      color: var(--text-main);
      margin-bottom: 10px;
    }

    .form-group {
      margin-bottom: 10px;
    }

    .form-group:last-child {
      margin-bottom: 0;
    }

    label {
      display: block;
      font-size: 10.5px;
      font-weight: 600;
      color: var(--text-muted);
      margin-bottom: 5px;
      text-transform: uppercase;
      letter-spacing: 0.02em;
    }

    input[type="text"], input[type="password"] {
      width: 100%;
      padding: 10px 14px;
      font-family: inherit;
      font-size: 13px;
      border: none; /* Strokeless */
      border-radius: var(--radius-control);
      background-color: var(--bg-card);
      color: var(--text-main);
      outline: none;
      transition: box-shadow 0.2s ease, background-color 0.2s ease;
      box-shadow: none;
    }

    input[type="text"]:focus, input[type="password"]:focus {
      box-shadow: 0 0 0 2px var(--accent-dark);
    }

    input::placeholder {
      color: var(--text-subtle);
    }

    .form-actions-row {
      display: flex;
      gap: 10px;
      align-items: center;
    }

    .btn-switch-mode {
      flex: 1;
      padding: 12px 18px;
      font-family: inherit;
      font-size: 12.5px;
      font-weight: 600;
      background-color: var(--bg-surface);
      border: none; /* Strokeless */
      outline: none;
      color: var(--text-main);
      border-radius: 22px; /* Full Rounded Pill (proportional) */
      cursor: pointer;
      transition: background-color 0.2s ease,
                  border-radius 0.25s cubic-bezier(0.2, 0, 0, 1),
                  transform 0.15s ease;
      text-align: center;
      user-select: none;
    }

    .btn-switch-mode:hover {
      background-color: var(--bg-surface-hover);
      border-radius: 12px; /* Morph to Medium on Hover */
    }

    .btn-switch-mode:active {
      border-radius: 9px;
      transform: scale(0.97);
    }

    .btn-switch-mode:focus, .btn-switch-mode:focus-visible {
      outline: none;
    }

    .btn-submit-main {
      flex: 1.2;
      padding: 12px 20px;
      background-color: var(--accent-dark);
      color: #ffffff;
      font-family: inherit;
      font-size: 12.5px;
      font-weight: 700;
      border: none; /* Strokeless */
      outline: none;
      border-radius: 22px; /* Full Rounded Pill (proportional) */
      cursor: pointer;
      box-shadow: 0 2px 6px rgba(17, 24, 39, 0.15);
      transition: background-color 0.2s ease,
                  border-radius 0.25s cubic-bezier(0.2, 0, 0, 1),
                  transform 0.15s ease,
                  box-shadow 0.2s ease;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 6px;
      user-select: none;
    }

    .btn-submit-main:hover {
      background-color: var(--accent-dark-hover);
      border-radius: 12px; /* Morph to Medium on Hover */
    }

    .btn-submit-main:active {
      border-radius: 9px;
      transform: scale(0.97);
    }

    .btn-submit-main:focus, .btn-submit-main:focus-visible {
      outline: none;
    }

    .btn-submit-main:disabled, .btn-switch-mode:disabled {
      opacity: 0.5;
      cursor: not-allowed;
      transform: none;
    }

    /* Status Alert Box - Strokeless */
    #status {
      display: none;
      margin-top: 12px;
      padding: 12px 14px;
      font-size: 12px;
      font-weight: 600;
      border-radius: var(--radius-control);
      background-color: var(--bg-surface);
      color: var(--text-main);
      line-height: 1.5;
      border: none; /* Strokeless */
    }

    /* Tab Pane Visibility */
    .tab-pane {
      display: none;
      width: 100%;
    }

    .tab-pane.active {
      display: block;
    }

    /* Footer */
    .app-footer {
      width: 100%;
      text-align: center;
      padding: 6px 0 16px;
      font-size: 11px;
      color: var(--text-muted);
    }

    .app-footer a {
      color: var(--text-main);
      text-decoration: none;
      font-weight: 600;
      cursor: pointer;
    }

    .app-footer a:hover {
      text-decoration: underline;
    }

    .legal-modal {
      display: none;
      margin-top: 8px;
      padding: 12px;
      background-color: var(--bg-surface);
      border: none; /* Strokeless */
      border-radius: var(--radius-control);
      font-size: 10.5px;
      color: var(--text-muted);
      line-height: 1.6;
      text-align: left;
    }

    /* Responsive Breakpoints */
    @media (max-width: 768px) {
      body {
        padding: 14px 10px;
      }
      .app-wrapper {
        gap: 12px;
      }
      .bento-grid {
        grid-template-columns: 1fr;
        gap: 12px;
      }
      .form-grid {
        grid-template-columns: 1fr;
        gap: 10px;
      }
      .form-actions-row {
        flex-direction: column;
      }
      .btn-switch-mode, .btn-submit-main {
        width: 100%;
      }
    }
  </style>
</head>
<body>
  <div class="app-wrapper">
    
    <!-- Top Navigation Controls -->
    <div class="top-controls">
      <div class="tab-segmented-control">
        <button type="button" class="tab-btn active" data-target="tab-vision">
          <svg class="tab-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z"></path>
            <circle cx="12" cy="13" r="4"></circle>
          </svg>
          <span>Vision & Rig</span>
        </button>
        <button type="button" class="tab-btn" data-target="tab-network">
          <svg class="tab-icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
            <path d="M5 12.55a11 11 0 0 1 14.08 0"></path>
            <path d="M1.42 9a16 16 0 0 1 21.16 0"></path>
            <path d="M8.53 16.11a6 6 0 0 1 6.95 0"></path>
            <line x1="12" y1="20" x2="12.01" y2="20"></line>
          </svg>
          <span>Network Setup</span>
        </button>
      </div>
      <div id="mode-badge" class="mode-pill">STA Online</div>
    </div>

    <!-- Tab 1: Vision & Telemetry Bento Grid -->
    <div id="tab-vision" class="tab-pane active">
      <div class="bento-grid">
        
        <!-- Left Bento Card: Camera Viewport -->
        <section class="bento-card">
          <div class="bento-card-header">
            <h2 class="card-title">Camera Feed</h2>
          </div>
          <div class="stream-viewport">
            <div id="stream-skeleton" class="skeleton-loader">
              <div class="skeleton-grid"></div>
              <span>CONNECTING SENSOR FEED...</span>
            </div>
            <img id="stream-img" src="" alt="KoRe Camera Feed" crossorigin="anonymous">
            <canvas id="hud-canvas"></canvas>
          </div>
        </section>

        <!-- Right Bento Column: AI Metrics & Expression Rig -->
        <div class="bento-col">
          
          <!-- Bento Card 1: AI Telemetry -->
          <section class="bento-card">
            <div class="bento-card-header">
              <h2 class="card-title">AI Telemetry</h2>
              <span class="card-badge">Dual-Core RTOS</span>
            </div>
            
            <div class="metrics-grid">
              <div class="metric-box">
                <span class="metric-label">Inference</span>
                <div class="metric-value-row">
                  <span id="tel-fps" class="metric-number">0.0</span>
                  <span class="metric-unit">FPS</span>
                </div>
              </div>

              <div class="metric-box">
                <span class="metric-label">Confidence</span>
                <div class="metric-value-row">
                  <span id="tel-conf" class="metric-number">0.00</span>
                </div>
              </div>

              <div class="metric-box">
                <span class="metric-label">Proximity</span>
                <div class="metric-value-row">
                  <span id="tel-prox" class="metric-number">0%</span>
                </div>
              </div>
            </div>
          </section>

          <!-- Bento Card 2: Facial Expression Rig -->
          <section class="bento-card">
            <div class="bento-card-header">
              <h2 class="card-title">Facial Expression Rig</h2>
            </div>

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

            <button type="button" id="btn-expr-auto" class="btn-expr-auto active">Default (Auto Mood)</button>
          </section>

        </div>

      </div>
    </div>

    <!-- Tab 2: Network Configuration -->
    <div id="tab-network" class="tab-pane">
      <div class="network-container">
        <section class="bento-card">
          <div class="bento-card-header">
            <h2 class="card-title">Network Configuration</h2>
            <span class="card-badge" id="net-badge-info">Local NVS</span>
          </div>

          <form id="wifi-form">
            <div class="form-grid">
              
              <!-- STA Column -->
              <div class="form-section">
                <div class="form-section-title">Wi-Fi Client (STA Mode)</div>
                <div class="form-group">
                  <label for="sta_ssid">SSID</label>
                  <input type="text" id="sta_ssid" name="sta_ssid" placeholder="Nama WiFi Router" required>
                </div>
                <div class="form-group">
                  <label for="sta_pass">Password</label>
                  <input type="password" id="sta_pass" name="sta_pass" placeholder="Password WiFi">
                </div>
              </div>

              <!-- AP Column -->
              <div class="form-section">
                <div class="form-section-title">Access Point (AP Mode)</div>
                <div class="form-group">
                  <label for="ap_ssid">SSID</label>
                  <input type="text" id="ap_ssid" name="ap_ssid" placeholder="SSID KoRe AP" required>
                </div>
                <div class="form-group">
                  <label for="ap_pass">Password</label>
                  <input type="password" id="ap_pass" name="ap_pass" placeholder="Minimal 8 karakter atau kosong">
                </div>
              </div>

            </div>

            <div class="form-actions-row">
              <button type="button" id="btn-switch-mode" class="btn-switch-mode" style="display:none;"></button>
              <button type="submit" id="btn-save" class="btn-submit-main">Simpan & Sambungkan →</button>
            </div>
          </form>

          <div id="status"></div>
        </section>
      </div>
    </div>

    <!-- Footer Disclosures -->
    <footer class="app-footer">
      <p>KoRe Biomechanical Engine &bull; On-Device Local Processing</p>
      <p style="margin-top:4px;"><a id="toggle-privacy">Kebijakan Privasi</a> &bull; <a id="toggle-terms">Ketentuan Layanan</a></p>
      <div id="legal-content" class="legal-modal">
        <strong>Ketentuan Layanan & Kebijakan Privasi:</strong><br>
        1. Pemrosesan Data: Seluruh inferensi visi komputer, klasifikasi YCbCr, dan dinamika okulomotor dieksekusi secara lokal pada memori internal ESP32-S3 (SRAM).<br>
        2. Privasi: Tidak ada data gambar, video feed, atau kredensial Wi-Fi yang dikirim ke server pihak ketiga atau cloud eksternal.<br>
        3. Kredensial: Kredensial Access Point dan STA disimpan secara lokal di NVS Flash perangkat.
      </div>
    </footer>

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

    /* Segmented Tab Switching */
    const tabBtns = document.querySelectorAll('.tab-btn');
    const tabPanes = document.querySelectorAll('.tab-pane');

    tabBtns.forEach(btn => {
      btn.addEventListener('click', function() {
        const targetId = this.dataset.target;
        tabBtns.forEach(b => b.classList.remove('active'));
        tabPanes.forEach(p => p.classList.remove('active'));
        this.classList.add('active');
        const targetPane = document.getElementById(targetId);
        if (targetPane) targetPane.classList.add('active');
        if (targetId === 'tab-vision') {
          setTimeout(resizeCanvas, 40);
        }
      });
    });

    /* Cyber VFX Motion Tracker State */
    let renderBoxes = [];
    let trajectoryTrail = [];   // Motion trajectory history [{cx, cy, rawX, rawY, time}]
    let sparkNodes = [];        // Dynamic spark/particle cluster sub-nodes
    let telemetryTimer = null;
    let lastData = null;

    /**
     * Draw directional chevron arrow along a curve or vector
     */
    function drawCurveArrow(ctx, x, y, angle, size = 6.5, color = 'rgba(255, 255, 255, 0.95)') {
      ctx.save();
      ctx.translate(x, y);
      ctx.rotate(angle);
      ctx.strokeStyle = color;
      ctx.fillStyle = color;
      ctx.lineWidth = 1.6;
      ctx.lineCap = 'round';
      ctx.lineJoin = 'miter';
      ctx.setLineDash([]);
      ctx.beginPath();
      ctx.moveTo(-size, -size * 0.55);
      ctx.lineTo(0, 0);
      ctx.lineTo(-size, size * 0.55);
      ctx.stroke();
      ctx.restore();
    }

    /**
     * Cyber Motion Tracker Bounding Box + Center Dot + Coordinates (x: ... y: ...)
     */
    function drawCyberTrackerBox(ctx, bx, by, bw, bh, rawX, rawY, isPrimary = true) {
      ctx.save();

      // 1. Crisp Green Bounding Box
      ctx.strokeStyle = '#00ff66';
      ctx.lineWidth = isPrimary ? 1.6 : 1.2;
      ctx.shadowColor = 'rgba(0, 255, 102, 0.6)';
      ctx.shadowBlur = 3;
      ctx.setLineDash([]);
      ctx.strokeRect(bx, by, bw, bh);

      // 2. Solid Green Center Dot
      const cx = bx + bw / 2;
      const cy = by + bh / 2;
      ctx.fillStyle = '#00ff66';
      ctx.beginPath();
      ctx.arc(cx, cy, isPrimary ? 3.0 : 2.2, 0, 2 * Math.PI);
      ctx.fill();

      // 3. Coordinate Label (x: ... y: ...)
      ctx.shadowColor = 'rgba(0, 0, 0, 0.95)';
      ctx.shadowBlur = 4;
      ctx.fillStyle = '#ffffff';
      ctx.font = '600 11.5px "Plus Jakarta Sans", monospace, sans-serif';
      ctx.textAlign = 'left';
      ctx.textBaseline = 'bottom';

      const coordText = `x: ${Math.round(rawX)}  y: ${Math.round(rawY)}`;
      ctx.fillText(coordText, bx, by - 3);

      ctx.restore();
    }

    /**
     * Draw dashed curved trajectory path connecting historical points with directional arrows
     */
    function drawTrajectoryTrail(ctx, trail) {
      if (!trail || trail.length < 2) return;

      ctx.save();
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.8)';
      ctx.lineWidth = 1.2;
      ctx.setLineDash([6, 5]);
      ctx.shadowColor = 'rgba(0, 0, 0, 0.6)';
      ctx.shadowBlur = 3;

      ctx.beginPath();
      ctx.moveTo(trail[0].cx, trail[0].cy);

      for (let i = 1; i < trail.length; i++) {
        const pPrev = trail[i - 1];
        const pCurr = trail[i];
        const midX = (pPrev.cx + pCurr.cx) / 2;
        const midY = (pPrev.cy + pCurr.cy) / 2;
        ctx.quadraticCurveTo(pPrev.cx, pPrev.cy, midX, midY);
      }
      ctx.stroke();

      // Draw directional arrows along trajectory segments
      const step = Math.max(1, Math.floor(trail.length / 3));
      for (let i = step; i < trail.length; i += step) {
        const p1 = trail[i - 1];
        const p2 = trail[i];
        const dx = p2.cx - p1.cx;
        const dy = p2.cy - p1.cy;
        const dist = Math.hypot(dx, dy);
        if (dist > 10) {
          const angle = Math.atan2(dy, dx);
          const midX = (p1.cx + p2.cx) / 2;
          const midY = (p1.cy + p2.cy) / 2;
          drawCurveArrow(ctx, midX, midY, angle, 6.5, 'rgba(255, 255, 255, 0.95)');
        }
      }

      ctx.restore();
    }

    /**
     * Draw arched dashed curves connecting multiple detected targets/candidates with flow arrows
     */
    function drawInterTargetArcs(ctx, targets) {
      if (!targets || targets.length < 2) return;

      ctx.save();
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.75)';
      ctx.lineWidth = 1.2;
      ctx.setLineDash([6, 5]);

      for (let i = 0; i < targets.length; i++) {
        for (let j = i + 1; j < targets.length; j++) {
          const t1 = targets[i];
          const t2 = targets[j];
          const x1 = t1.cx, y1 = t1.cy;
          const x2 = t2.cx, y2 = t2.cy;
          const dx = x2 - x1, dy = y2 - y1;
          const dist = Math.hypot(dx, dy);
          if (dist < 20) continue;

          const mx = (x1 + x2) / 2;
          const my = (y1 + y2) / 2;
          const nx = -dy / dist;
          const ny = dx / dist;

          // Upper Arc (curved upward)
          const archH = Math.min(65, Math.max(25, dist * 0.22));
          const cpx1 = mx + nx * archH;
          const cpy1 = my + ny * archH;

          ctx.beginPath();
          ctx.moveTo(x1, y1);
          ctx.quadraticCurveTo(cpx1, cpy1, x2, y2);
          ctx.stroke();

          // Arrow on upper arc
          const arrowAngle1 = Math.atan2(y2 - y1, x2 - x1);
          const apexX1 = 0.25 * x1 + 0.5 * cpx1 + 0.25 * x2;
          const apexY1 = 0.25 * y1 + 0.5 * cpy1 + 0.25 * y2;
          drawCurveArrow(ctx, apexX1, apexY1, arrowAngle1 + Math.PI, 6.5, 'rgba(255, 255, 255, 0.9)');

          // If 2 targets, also draw lower arc (elliptical loop like in Photo 4)
          if (targets.length === 2) {
            const cpx2 = mx - nx * archH;
            const cpy2 = my - ny * archH;

            ctx.beginPath();
            ctx.moveTo(x1, y1);
            ctx.quadraticCurveTo(cpx2, cpy2, x2, y2);
            ctx.stroke();

            const apexX2 = 0.25 * x1 + 0.5 * cpx2 + 0.25 * x2;
            const apexY2 = 0.25 * y1 + 0.5 * cpy2 + 0.25 * y2;
            drawCurveArrow(ctx, apexX2, apexY2, arrowAngle1, 6.5, 'rgba(255, 255, 255, 0.9)');
          }
        }
      }

      ctx.restore();
    }

    /**
     * Generate & render spark sub-nodes for dense multi-point cluster visual
     */
    function renderSparkClusters(ctx, cands, primaryTarget, scaleX, scaleY, now) {
      const activeCands = cands.filter(c => c.cx > 0);
      if (activeCands.length === 0 && (!primaryTarget || !primaryTarget.detected)) {
        sparkNodes = [];
        return;
      }

      const totalNodes = activeCands.length > 1 ? 6 : (primaryTarget && primaryTarget.detected ? 4 : 0);
      if (totalNodes === 0) return;

      while (sparkNodes.length < totalNodes) {
        const pIdx = sparkNodes.length % (activeCands.length || 1);
        const parent = activeCands[pIdx] || primaryTarget;
        const angle = (sparkNodes.length * 1.57) + Math.random() * 0.8;
        const dist = 24 + Math.random() * 45;
        sparkNodes.push({
          ox: Math.cos(angle) * dist,
          oy: Math.sin(angle) * dist,
          bw: 22 + Math.floor(Math.random() * 20),
          bh: 22 + Math.floor(Math.random() * 20),
          parentIdx: pIdx,
          phase: Math.random() * 6.28
        });
      }
      if (sparkNodes.length > totalNodes) sparkNodes.length = totalNodes;

      for (let i = 0; i < sparkNodes.length; i++) {
        const node = sparkNodes[i];
        const pIdx = node.parentIdx % (activeCands.length || 1);
        const parent = activeCands[pIdx] || primaryTarget;

        const wobbleX = Math.sin(now * 0.003 + node.phase) * 6;
        const wobbleY = Math.cos(now * 0.003 + node.phase) * 6;

        const rawX = Math.max(10, Math.min(630, parent.cx + node.ox + wobbleX));
        const rawY = Math.max(10, Math.min(470, parent.cy + node.oy + wobbleY));

        const screenX = rawX * scaleX;
        const screenY = rawY * scaleY;
        const screenW = node.bw * scaleX;
        const screenH = node.bh * scaleY;

        drawCyberTrackerBox(ctx, screenX - screenW / 2, screenY - screenH / 2, screenW, screenH, rawX, rawY, false);

        // Dashed connector line with arrow towards spark node
        ctx.save();
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.6)';
        ctx.lineWidth = 1.0;
        ctx.setLineDash([4, 4]);
        ctx.beginPath();
        ctx.moveTo(parent.cx * scaleX, parent.cy * scaleY);
        ctx.lineTo(screenX, screenY);
        ctx.stroke();

        const midX = (parent.cx * scaleX + screenX) / 2;
        const midY = (parent.cy * scaleY + screenY) / 2;
        const angle = Math.atan2(screenY - parent.cy * scaleY, screenX - parent.cx * scaleX);
        drawCurveArrow(ctx, midX, midY, angle, 5.0, 'rgba(255, 255, 255, 0.8)');
        ctx.restore();
      }
    }

    async function updateTelemetry() {
      if (document.visibilityState === 'hidden') {
        telemetryTimer = setTimeout(updateTelemetry, 1000);
        return;
      }

      try {
        const res = await fetch('http://' + host + '/telemetry');
        const data = await res.json();
        lastData = data;
        ctx.clearRect(0, 0, canvas.width, canvas.height);

        document.getElementById('tel-fps').innerText = (data.fps_ai || 0).toFixed(1);
        document.getElementById('tel-conf').innerText = (data.conf || 0).toFixed(2);
        document.getElementById('tel-prox').innerText = ((data.prox || 0) * 100).toFixed(0) + '%';

        const now = Date.now();

        if (data.detected && data.fw > 0 && data.fh > 0) {
          resizeCanvas();
          const scaleX = canvas.width / data.fw;
          const scaleY = canvas.height / data.fh;

          const numCands = data.num_cands || 1;
          const cands = [
            { cx: data.c0_cx || data.cx, cy: data.c0_cy || data.cy, w: data.c0_w || data.w || 160, h: data.c0_h || data.h || 200, p: data.c0_p || 100 },
            { cx: data.c1_cx || 0, cy: data.c1_cy || 0, w: data.c1_w || 140, h: data.c1_h || 160, p: data.c1_p || 0 },
            { cx: data.c2_cx || 0, cy: data.c2_cy || 0, w: data.c2_w || 140, h: data.c2_h || 160, p: data.c2_p || 0 }
          ];

          // Update motion trajectory history trail for primary target
          const primCx = data.cx || cands[0].cx;
          const primCy = data.cy || cands[0].cy;
          const primScreenX = primCx * scaleX;
          const primScreenY = primCy * scaleY;

          trajectoryTrail.push({
            cx: primScreenX,
            cy: primScreenY,
            rawX: primCx,
            rawY: primCy,
            time: now
          });

          // Keep recent trajectory trail (last 16 points or 1.2 seconds)
          trajectoryTrail = trajectoryTrail.filter(p => now - p.time < 1200);
          if (trajectoryTrail.length > 18) trajectoryTrail.shift();

          // 1. Draw Trajectory Path with directional arrows
          drawTrajectoryTrail(ctx, trajectoryTrail);

          // 2. Draw Inter-Target connection arcs if multiple candidates detected
          const detectedTargetsForArcs = [];
          for (let i = 0; i < numCands; i++) {
            if (cands[i].cx > 0) {
              detectedTargetsForArcs.push({
                cx: cands[i].cx * scaleX,
                cy: cands[i].cy * scaleY,
                rawX: cands[i].cx,
                rawY: cands[i].cy
              });
            }
          }
          drawInterTargetArcs(ctx, detectedTargetsForArcs);

          // 3. Render dynamic spark cluster sub-nodes (photos 1, 2, 3)
          renderSparkClusters(ctx, cands, data, scaleX, scaleY, now);

          // 4. Render Main Target & Candidate Bounding Boxes
          for (let i = 0; i < numCands; i++) {
            const cand = cands[i];
            if (cand.cx <= 0) continue;

            const bw = cand.w || 160;
            const bh = cand.h || 200;

            const targetBx = (cand.cx - bw / 2) * scaleX;
            const targetBy = (cand.cy - bh / 2) * scaleY;
            const targetBw = bw * scaleX;
            const targetBh = bh * scaleY;

            if (!renderBoxes[i]) {
              renderBoxes[i] = { bx: targetBx, by: targetBy, bw: targetBw, bh: targetBh, rawX: cand.cx, rawY: cand.cy };
            } else {
              renderBoxes[i].bx = renderBoxes[i].bx * 0.25 + targetBx * 0.75;
              renderBoxes[i].by = renderBoxes[i].by * 0.25 + targetBy * 0.75;
              renderBoxes[i].bw = renderBoxes[i].bw * 0.25 + targetBw * 0.75;
              renderBoxes[i].bh = renderBoxes[i].bh * 0.25 + targetBh * 0.75;
              renderBoxes[i].rawX = cand.cx;
              renderBoxes[i].rawY = cand.cy;
            }

            const rBox = renderBoxes[i];
            const isPrimary = (i === data.insp_idx || i === 0);

            drawCyberTrackerBox(ctx, rBox.bx, rBox.by, rBox.bw, rBox.bh, rBox.rawX, rBox.rawY, isPrimary);
          }
        } else {
          renderBoxes = [];
          if (trajectoryTrail.length > 0) {
            trajectoryTrail = trajectoryTrail.filter(p => now - p.time < 600);
            drawTrajectoryTrail(ctx, trajectoryTrail);
          }
          sparkNodes = [];
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
      telemetryTimer = setTimeout(updateTelemetry, 80);
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
        if (data.ap_ssid) document.getElementById('ap_ssid').value = data.ap_ssid;
        document.getElementById('sta_pass').placeholder = 'Tersimpan (kosongkan jika tidak diubah)';
        document.getElementById('ap_pass').placeholder = 'Tersimpan (kosongkan jika tidak diubah)';

        const modeBadge = document.getElementById('mode-badge');
        const netBadgeInfo = document.getElementById('net-badge-info');
        const btnSwitch = document.getElementById('btn-switch-mode');
        
        if (data.is_ap) {
          modeBadge.innerText = 'AP Mode';
          netBadgeInfo.innerText = 'Access Point Active';
          btnSwitch.innerText = 'Beralih ke STA Mode';
          btnSwitch.dataset.targetMode = 'STA';
        } else {
          modeBadge.innerText = 'STA Online';
          netBadgeInfo.innerText = (data.sta_ssid || 'Local Wi-Fi');
          btnSwitch.innerText = 'Beralih ke AP Mode';
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

      const unifiedUrl = 'http://192.168.18.16';

      if (targetMode === 'AP') {
        btn.innerText = 'Beralih ke AP Mode...';
        status.innerHTML = 'ESP32 sedang reboot ke AP Mode.<br>Hubungkan Wi-Fi ke <b>KoRe</b> lalu buka: <a href="' + unifiedUrl + '" style="color:#111827;font-weight:700;">' + unifiedUrl + '</a><br><small>Pengalihan otomatis dalam 4 detik...</small>';

        fetch('/switch_mode', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ mode: targetMode })
        }).finally(() => {
          setTimeout(() => { window.location.href = unifiedUrl; }, 4000);
        });
      } else {
        const staSsid = document.getElementById('sta_ssid').value || 'WiFi Router';
        btn.innerText = 'Beralih ke STA Mode...';
        status.innerHTML = 'ESP32 sedang reboot dan menghubungkan ke <b>' + staSsid + '</b>.<br>Buka URL: <a href="' + unifiedUrl + '" style="color:#111827;font-weight:700;">' + unifiedUrl + '</a><br><small>Pengalihan otomatis dalam 6 detik...</small>';

        fetch('/switch_mode', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ mode: targetMode })
        }).finally(() => {
          setTimeout(() => { window.location.href = unifiedUrl; }, 6000);
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
      btn.innerHTML = '<span>Menyimpan...</span>';
      status.style.display = 'block';

      const staSsid = document.getElementById('sta_ssid').value || 'WiFi Router';
      const unifiedUrl = 'http://192.168.18.16';
      status.innerHTML = 'Konfigurasi disimpan. ESP32 reboot menghubungkan ke <b>' + staSsid + '</b>.<br>Buka URL: <a href="' + unifiedUrl + '" style="color:#111827;font-weight:700;">' + unifiedUrl + '</a><br><small>Pengalihan otomatis dalam 6 detik...</small>';

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
        setTimeout(() => { window.location.href = unifiedUrl; }, 6000);
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
