# LowCut Police — User Manual (English)

**Precision IIR EQ** — a zero-latency, high-precision minimum-phase equalizer with a high-resolution analyzer that can see all the way down to 1 Hz.

Version 1.0.0 / OTODESK

---

## Table of Contents
1.  **Product Overview**
    *   Concept
    *   Minimum-Phase Design & Zero Latency
    *   "What You Set Is What You Get" Philosophy
2.  **High-Resolution Analyzer**
3.  **Sections & Parameters**
    *   Low Cut (High-Pass) Section
    *   High Cut (Low-Pass) Section
    *   Bell EQ Section (Bands 1–4)
    *   Global Controls / Monitoring
4.  **Advanced Monitoring Features**
    *   Listen Diff
    *   Solo-Sweep
    *   Peak Hold
5.  **Shortcuts / Zoom & Navigation**
6.  **GUI Color Palettes (10 Types)**
7.  **Technical Specifications & System Requirements**

---

## 1. Product Overview

### Concept
**LowCut Police** is a precision equalizer built to take full control of the low end of your mix — sub-bass buildup, kick-drum mud, mic rumble and wind noise. It combines an ultra-low-frequency analyzer that displays from 1 Hz, steep low-cut/high-cut filters, and four surgical bell bands, making the invisible low end visible so you can treat it accurately.

### Minimum-Phase Design & Zero Latency
Every filter in this plugin (Low Cut / High Cut / Bells) uses a **minimum-phase IIR design**.

*   **Latency is 0 samples.** No delay compensation required — safe to insert while tracking or performing live.
*   **No pre-echo**, by principle. Unlike linear-phase EQs, transients such as kick attacks stay perfectly clean with no reverse smearing before the hit.
*   The cut filters use a Butterworth cascade with a **hybrid Q arrangement** (reduced Q in the leading sections) that suppresses post-ringing at steep slope settings.
*   All parameters are continuously smoothed internally, and slope/structure changes are crossfaded over ~10 ms. **No zipper noise or clicks, even under heavy automation.**

### "What You Set Is What You Get" Philosophy
This plugin never silently "corrects" your settings. The frequency, gain and Q you dial in are exactly what goes into the filter coefficients, and the displayed curve always matches the actual processing (measured error below 0.01 dB). When adjacent bands overlap, their curves sum naturally — the same behavior as classic analog and professional EQs.

---

## 2. High-Resolution Analyzer

Typical EQ analyzers use FFT (e.g. 4096 points), which limits low-frequency resolution to the bin width — about 10.8 Hz at 44.1 kHz. Below 20 Hz that leaves only 2–3 data points, making the sub region essentially unreadable.

The LowCut Police analyzer is an **800-band analog-style filter bank — no FFT involved**.

*   **1–60 Hz analyzed in 0.2 Hz steps** (296 bands), 60–200 Hz in 1 Hz steps, and 200 Hz–25 kHz log-spaced, all running continuously.
*   An 8th-order Butterworth anti-aliasing filter precedes each decimation stage, so mid-band content never folds into the sub region as false spectrum (alias rejection better than -110 dB).
*   Display levels are calibrated so that a **0 dBFS sine reads 0 dB** (within ±0.25 dB across the entire range).
*   Each band has ballistics optimized for its frequency: slow and accurate in the lows, fast in the highs.
*   Multirate processing keeps CPU usage remarkably low for a filter bank of this size (a few percent of one core).

> **Note**: The 1 Hz band takes a second or two to settle. This is a physical limit imposed by the uncertainty principle — no analyzer of any type can avoid it, and an FFT analyzer cannot even resolve 1 Hz in the first place.

---

## 3. Sections & Parameters

Use the band selector buttons (LC / HC / B1–B4) at the bottom to connect the knobs to a band. Each band's "ON" button toggles it on and off.

### Low Cut (High-Pass) Section [LC]
The main filter for removing unwanted low end.
*   **Frequency (Hz)**: Cutoff frequency (range: **1 Hz – 500 Hz**, logarithmic, default 80 Hz).
*   **Slope (dB/oct)**: Filter steepness, 8 steps: **12 / 24 / 36 / 48 / 60 / 72 / 84 / 96 dB/oct**.
*   **Enable (ON/OFF)**: Enables/bypasses the entire low-cut filter.

### High Cut (Low-Pass) Section [HC]
For cleaning up the top end.
*   **Frequency (Hz)**: Cutoff frequency (range: **1 Hz – 25,000 Hz**, default 20 kHz).
*   **Slope (dB/oct)**: 12 – 96 dB/oct in 8 steps.
*   **Enable (ON/OFF)**: Enables/bypasses the high cut.

### Bell EQ Section (Bands 1–4) [B1–B4]
Parametric bands for pinpoint cuts and boosts. The bells use an **Orfanidis (analog-matched) design**, so the curve does not cramp near the Nyquist frequency.
*   **Frequency (Hz)**: Target frequency (range: **10 Hz – 25,000 Hz**).
*   **Gain (dB)**: Boost/cut amount (range: **-12 dB – +12 dB**).
*   **Q**: Bandwidth (range: **0.3 – 120.0**). Q=120 allows extremely sharp notches. The response curve is rendered with supersampling, so even the narrowest peak or notch is drawn accurately at its full depth, at any zoom level.
*   **Enable (ON/OFF)**: Per-band enable/bypass. Toggling never clicks — the gain glides smoothly to/from 0 dB.

### Global Controls / Monitoring
*   **Bypass**: Bypasses the entire plugin.
*   **Diff (Listen Diff)**: Delta-listening mode (see below).
*   **Hold**: Enables the analyzer peak-hold display.
*   **Color**: Cycles through 10 color palettes.
*   **Analyze (display mode)**: Each click switches the display:
    *   `Normal`: EQ response curve + real-time analyzer.
    *   `Waveform`: Audio waveform display (Dry / Wet).
    *   `Phase`: Phase response display.

---

## 4. Advanced Monitoring Features

### Listen Diff
Turning "Diff" on outputs **Dry − Wet** — literally **the sound being removed by the equalizer**.
*   **How to use it**: With a strong low cut engaged, use Diff to check whether the body of the kick or an important bass line is being thrown away along with the rumble. If all you hear in Diff is unwanted noise and mud, your EQ decision is correct. Because the plugin is zero-latency, the difference signal is always sample-accurate.

### Solo-Sweep
While **`Shift`-clicking or `Shift`-dragging** an EQ point, you monitor through a narrow band-pass filter centered on that point.
*   **How to use it**: Sweep the point left and right to hunt down harsh resonances and boxy frequencies. Release to instantly return to the normal EQ sound, then notch out what you found.

### Peak Hold
Turning "Hold" on keeps the maximum level of every band displayed as a thin line. Play the whole song through and it will capture momentary low-end peaks and resonances that only appear occasionally. Turning it off resets the hold.

---

## 5. Shortcuts / Zoom & Navigation

### EQ Point Handling

| Action | Result |
| :--- | :--- |
| **Drag** | Changes frequency (and gain, vertically, for bells). LC/HC points sit on the actual curve (at the ~-3 dB point of the cutoff). |
| **Mouse wheel (over a point)** | Adjusts **Q** on bells, or **Slope** on LC/HC. |
| **Double-click (on a point)** | Instantly toggles that band on/off. |
| **`Shift` + click/drag** | Solo-Sweep (see above). |

### Display Zoom / Navigation

| Action | Result |
| :--- | :--- |
| **H+ / H− buttons** | Zoom the frequency axis in/out. |
| **V+ / V− buttons** | Zoom the gain axis in/out (±3 dB to ±48 dB). |
| **`Ctrl` + mouse wheel** | Zooms the frequency axis **centered on the cursor** — ideal for inspecting the sub region. |
| **Right-drag** | Horizontal = frequency zoom, vertical = gain zoom. |
| **Left-drag (empty space)** | Horizontal = pan the view, vertical = shift the analyzer reference level. |
| **Mouse wheel (empty space)** | Shifts the analyzer reference level (±40 dB). |
| **Double-click (empty space)** | Resets zoom and display offsets. |
| **Hover** | Shows frequency and note name at the cursor (e.g. `55.0 Hz / A1`). |

> **Tip**: When zoomed in far, dragging a point below 60 Hz snaps the frequency to a 0.2 Hz grid for precise sub-bass work. You can zoom all the way down to 1 Hz.

---

## 6. GUI Color Palettes (10 Types)

Click the "Color" button to instantly restyle the whole UI. Background, grid and analyzer colors change together, and the active control knobs follow automatically.

1.  **Studio Neon** — cyber-styled neon colors (default).
2.  **Chic Mono** — calm, mature monochrome.
3.  **Vivid Future** — bright futuristic colors.
4.  **Warm Retro** — warm browns and oranges.
5.  **Pastel Dream** — soft, easy-on-the-eyes pastels.
6.  **Cyberpunk** — flashy yellow and pink.
7.  **Ocean Abyss** — cool deep-sea blue gradients.
8.  **Forest Zenith** — soothing green earth tones.
9.  **Sunset Glow** — beautiful orange-to-red gradients.
10. **Midnight Gold** — luxurious black and gold.

---

## 7. Technical Specifications & System Requirements

*   **OS**: Windows 10 / Windows 11 (64-bit only) — **Windows-only plugin**
*   **CPU**: **AVX2 required** (Intel Haswell 2013+ / AMD Excavator+, due to SIMD optimization)
*   **Supported host (DAW)**: **Ableton Live 11+ only** (tested and verified). Other VST3 hosts may work but are not tested or supported.
*   **Formats**: VST3 / Standalone
*   **Filter design**:
    *   Cuts: Butterworth cascades built on TPT (Topology-Preserving Transform) SVFs, with a hybrid Q arrangement for reduced ringing
    *   Bells: Orfanidis (Nyquist-matched) biquads
*   **Internal precision**: Double precision (64-bit float), stereo processed simultaneously via AVX2 SIMD
*   **Latency**: **0 samples** (no PDC needed; safe for tracking and live use)
*   **Analyzer**: 800-band SVF filter bank (non-FFT), 1 Hz – 25 kHz, multirate processing with built-in 8th-order anti-aliasing, calibrated to 0 dBFS sine reference
*   **Reliability engineering**:
    *   **No heap allocation** in the audio path or the analyzer thread.
    *   Lock-free parameter handoff — the audio thread never waits on a lock.
    *   Full parameter smoothing plus structure-change crossfading: **no clicks or zipper noise under automation** (verified by measurement).
    *   NaN-prevention guards keep the plugin stable even at extreme Q settings.
    *   Processing auto-suspends after 1.5 s of silence to save CPU.

---

## Disclaimer (Protect Your Speakers and Hearing)

**[WARNING — IMPORTANT]**
This plugin includes special monitoring features such as Listen Diff and Solo-Sweep. Steep EQ moves or aggressive sweeps at extreme Q settings can cause sudden level changes and high-level transients.

**To protect your hearing and your monitoring equipment, always keep your master monitoring level at a safe volume.**
The developer and rights holders accept no liability for any equipment damage or physical injury (including hearing loss) resulting from the use of this software. Use entirely at your own risk.
