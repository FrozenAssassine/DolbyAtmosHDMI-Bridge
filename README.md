# Spatial Audio Atmos Bridge (5.1.2, 5.1.4, 7.1.2, 7.1.4)

A lightweight real-time audio bridge for Windows that maps multi-channel audio from DaVinci Resolve (or any DAW) to discrete Dolby Atmos bed and height channels over HDMI.

---

## Supported Speaker Configurations

- **5.1.2** (8 Channels: 5.1 Surround + 2 Front Heights)
- **5.1.4** (10 Channels: 5.1 Surround + 4 Top Heights)
- **7.1.2** (10 Channels: 7.1 Surround + 2 Front Heights)
- **7.1.4** (12 Channels: 7.1 Surround + 4 Top Heights)

> **Note on Subwoofers (.1 vs .2):** Dual subwoofers receive the same LFE stream channel. Your AV receiver handles individual subwoofer calibration internally.

---

## Channel Routing Map

| Channel      | 5.1.2                | 5.1.4                | 7.1.2                | 7.1.4                |
| :----------- | :------------------- | :------------------- | :------------------- | :------------------- |
| **Ch 1–6**   | L, R, C, LFE, Ls, Rs | L, R, C, LFE, Ls, Rs | L, R, C, LFE, Ls, Rs | L, R, C, LFE, Ls, Rs |
| **Ch 7–8**   | **Top Front L/R**    | **Top Front L/R**    | Back Left / Right    | Back Left / Right    |
| **Ch 9–10**  | —                    | **Top Back L/R**     | **Top Front L/R**    | **Top Front L/R**    |
| **Ch 11–12** | —                    | —                    | —                    | **Top Back L/R**     |

---

## Quick Setup Guide

### 1. Windows Setup

1. Open Windows Sound Settings $\rightarrow$ Right-click your HDMI / AVR Device $\rightarrow$ **Spatial Sound** $\rightarrow$ Select **Dolby Atmos for Home Theater**.
2. If using more than 8 channels (5.1.4 or 7.1.4), install **VB-Audio Hi-Fi Cable** or **16-Channel Cable**.

### 2. DaVinci Resolve Setup

1. Go to **Preferences** $\rightarrow$ **Video and Audio I/O**:
   - **Output Device:** Select your virtual cable (`CABLE Input`).
   - Uncheck **Automatic speaker configuration**.
2. In the **Fairlight** workspace:
   - Go to **Fairlight** menu $\rightarrow$ **Bus Format** $\rightarrow$ Set Bus 1 to your matching target (e.g. **5.1.2**, **7.1.2**, or **7.1.4**).
   - Go to **Fairlight** menu $\rightarrow$ **Patch Input/Output** $\rightarrow$ Patch Bus Out channels 1–N to the corresponding Virtual Cable outputs according to the table above.

### 3. Run the Bridge

1. Launch the compiled `SpatialAudioBridge.exe`.
2. Select your layout profile from the menu (`1` to `4`).
3. Press play in DaVinci Resolve. Your AVR will decode native **Dolby Atmos** and route the height signals directly to your ceiling speakers.
