# LowCut Police User Manual

A professional-grade, high-precision high-pass filter and parametric equalizer plugin designed to clean up muddy low-end frequencies while maintaining absolute phase linearity (Zero-Phase).

---

## Table of Contents
1.  **Product Overview**
    *   Concept
    *   Zero-Phase IIR Technology
2.  **Section & Parameter Guide**
    *   LowCut (High-Pass) Filter Section
    *   Bell EQ Section (Bands 1–4)
    *   Global Controls & Monitor Modes
3.  **Advanced Monitoring Features**
    *   Listen Diff (Difference Audition)
    *   Solo-Sweep (Bandpass Solo)
4.  **Shortcuts & Control Summary**
5.  **10 Dynamic Color Palettes**
6.  **Technical Specifications & Requirements**

---

## 1. Product Overview

### Concept
**LowCut Police** is designed to police and manage muddy low-end frequencies (sub-bass buildup, kick/bass overlaps, microphone rumble, etc.). By cleaning up these problematic regions, LowCut Police ensures you reclaim maximum headroom and punch in your digital mix.

LowCut Police is not a generic, all-purpose equalizer. Instead, it is a highly specialized, practical, and razor-sharp professional tool engineered to achieve one single goal: **detecting and eradicating muddy low-end resonances ("Mud") to make the foundation of your mix completely clean.**

### Filter Specification (Linear-Phase & Minimum-Phase Hybrid)
LowCut Police employs distinct phase topologies depending on the filter type for optimal acoustic results:
*   **High-Pass Filter (LowCut)**: **Zero-Phase (Linear-Phase / Flat Phase)**.
*   **4 Bell EQs**: **Minimum-Phase (Standard Biquad)**.

### Zero-Phase IIR Technology (LowCut Filter Only)
When traditional minimum-phase equalizers apply steep high-pass filters, they introduce severe phase shifts around the cutoff frequency. This phase smearing causes sub-bass signals to lose their transient punch and contour.

LowCut Police's high-pass filter utilizes a **bidirectional Overlap-Add (OLA) biquad filter system**. By processing the audio stream both forwards and backwards in time, it cancels out phase distortion mathematically. The result is a perfectly flat, linear phase response (Zero-Phase) that slices away unwanted low-end while preserving the original shape and transient impact of your audio.
For the parametric Bell equalizers, a standard minimum-phase topology is used to ensure natural time-domain response and punch.

---

## 2. Section & Parameter Guide

### LowCut (High-Pass) Filter Section
The primary filter tool for removing sub-frequency noise.
*   **Cutoff Frequency (Hz)**: Sets the boundary frequency of the cut (Range: 20 Hz to 500 Hz, logarithmic scale).
*   **Slope (dB/oct)**: Selects the steepness of the filter. Available options are 12, 24, 36, 48, 60, 72, 84, and 96 dB/oct (mapped to 1st to 8th-order SVF cascades).
*   **Gain (dB)**: Attenuates the depth of the filter (Range: 0 dB to -10 dB).
*   **Enable (ON/OFF)**: Activates or bypasses the LowCut filter.

### Bell EQ Section (Bands 1–4)
Surgical parametric equalizers for notch filtering or corrective frequency boosts.
*   **Freq (Hz)**: Sets the target center frequency (Range: 20 Hz to 20,000 Hz).
*   **Gain (dB)**: Sets the boost or cut amount (Range: -18 dB to +18 dB). Ideal for removing resonant mud or boosting high frequencies to compensate for the cut.
*   **Q**: Sets the bandwidth of the filter (Range: 0.1 to 120.0). Higher Q values create surgical notches for precise frequency isolation.
*   **Enable (ON/OFF)**: Individually activates or bypasses each Bell band.

### Global Controls & Monitor Modes
*   **Bypass**: Bypasses the entire plugin (glows red when bypassed).
*   **Diff (Listen Diff)**: Activates the difference listening mode (glows blue when active). See details below.
*   **Color**: Cycles through the 10 available GUI color themes.
*   **Analyze (Display Mode Switcher)**: Changes the visual display:
    *   `Normal`: EQ response curve layered with a real-time FFT spectrum analyzer.
    *   `Waveform`: Stereo oscilloscope display (Left: Dry / Right: Wet).
    *   `Phase`: Phase response visualization (Left: Standard minimum-phase IIR / Right: Ideal Zero-Phase response, combined with a real-time energy spectrum).

---

## 3. Advanced Monitoring Features

### Listen Diff (Difference Audition)
Turning the "Diff" button ON outputs the latency-aligned difference signal ($Dry - Wet$).
This allows you to hear **exactly what the plugin is cutting or boosting**.
*   **Best Practice**: When applying a steep low-cut, use Diff to check if you are accidentally removing musical elements, such as the body of the bassline or the transient weight of a kick drum. If you only hear low-frequency rumble and wind noise, your filter settings are perfect.

### Solo-Sweep (Solo Monitoring)
Holding the **`Shift` key while clicking or dragging any EQ point** temporarily overrides all processing and applies a narrow bandpass filter around the selected frequency.
*   **Best Practice**: Sweep the mouse left and right while holding Shift to scan the audio. You can quickly isolate harsh resonances, ringing frequencies, or boxy ranges. Releasing the mouse instantly returns the plugin to its normal state.

---

## 4. Shortcuts & Control Summary

Boost your workflow with these hardware-accelerated GUI shortcuts:

| Shortcut / Interaction | Action |
| :--- | :--- |
| **`Ctrl` + Drag** | **Fine-Tuning**: Knobs or EQ points move at 1/10th speed, allowing surgical adjustments down to **0.1 Hz** accuracy. |
| **`Alt` + Mousewheel** | **Direct Value Adjust**: Hover over an EQ point or knob and scroll. Changes **Q-factor** for Bell bands, or **Slope (dB/oct)** for the LowCut band. |
| **Double-Click EQ Point** | **Bypass Toggle**: Double-clicking an EQ point toggles that band ON or OFF instantly. Works on bypassed (ghosted) points as well. |
| **`Shift` + Click / Drag** | **Solo-Sweep**: Triggers a narrow bandpass solo filter around the selected EQ band. |
| **Mousewheel (Open Space)** | **Vertical Offset (Normal Mode)**: Adjusts the vertical offset of the background FFT analyzer (±30 dB).<br>**Vertical Zoom (Phase Mode)**: Zooms the phase angle vertical scale. |

---

## 5. 10 Dynamic Color Palettes

Click the "Color" button to instantly adapt the plugin theme to your environment or DAW. The theme updates the background, grid, curves, analyzer gradient, and control rings:

1.  **Studio Neon**: Saturated cyber-studio palette (Default).
2.  **Chic Mono**: Sleek, distraction-free monochrome.
3.  **Vivid Future**: Bright, neon-saturated colors.
4.  **Warm Retro**: Soft vintage brown and orange tones.
5.  **Pastel Dream**: Gentle, soothing pastel gradients.
6.  **Cyberpunk**: Bold neon yellow and glowing pink.
7.  **Ocean Abyss**: Cool deep-sea blues and greens.
8.  **Forest Zenith**: Natural forest green tones.
9.  **Sunset Glow**: Saturated crimson and gold sunset colors.
10. **Midnight Gold**: Luxury gold-on-black aesthetic.

---

## 6. Technical Specifications & Requirements

*   **Platform**: Windows 10 / Windows 11 (64-bit only) - **Exclusively for Windows**
*   **Tested & Verified DAW**: **Ableton Live 11+ (Fully verified and tested)** / Cubase 12+ / Reaper 6+
*   **Format**: VST3
*   **Processing Depth**: 64-bit double precision floating-point DSP
*   **Latency**: Approx. 480ms (@ 96kHz) via Lookahead buffers (fully compensated by DAW Plugin Delay Compensation).
    *   **WARNING**: Due to the nature of the bidirectional Overlap-Add (OLA) zero-phase processing, this plugin introduces a massive latency. While modern DAWs automatically compensate for this delay via Plugin Delay Compensation (PDC) during playback, it is absolutely not suitable for live recording or real-time performance. LowCut Police is designed strictly for mixing and mastering scenarios.
*   **Stability Engineering**:
    *   **Zero Heap Allocation**: Guaranteed no dynamic memory allocation (`new` or `malloc`) in the real-time audio thread or the background FFT analyzer. Prevents CPU spikes and multithreaded mutex locks.
    *   **Anti-Explosion DSP**: Active protection against division-by-zero and `NaN` (Not a Number) generation. The plugin remains stable even at extreme Q values or during rapid automation.

---

## Disclaimer (Ear & Speaker Protection)

**IMPORTANT WARNING**: This plugin contains specialized audio features such as "Listen Diff" (difference listening) and "Solo-Sweep" (bandpass solo monitor). Rapidly sweeping frequencies or adjusting extreme Q values can produce abrupt transients and volume spikes.

**To protect your hearing (ears), speakers, and studio monitors, always ensure you keep your master monitoring volume at a safe level.**
The developers accept no liability for any equipment damage, hearing loss, or physical injury resulting from the use of this software. Use it entirely at your own risk.
