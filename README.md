# DLSSNR-Cost-Scaler

> Language: English | [简体中文](README-CN.md)

A standalone proxy DLL for NVIDIA DLSS-NR (DirectX 12) that adds resolution scaling and cost control. It runs the neural reconstruction model at a reduced resolution while keeping native 1:1 geometry, fine textures, text, and edges intact using a high-frequency matched residual composite shader.

Designed primarily to work alongside RenoDX addons, this proxy decouples DLSS-NR's GPU performance cost from the display resolution without introducing blur.

Tested specifically with clshortfuse's DLSS addon (`renodx-dlss.addon64`), but architected to work with any game, engine, or injector that calls `nvngx_dlssnr.dll` over DirectX 12.

---

## Features

- Standalone drop-in proxy for `nvngx_dlssnr.dll`.
- Downsamples the frame using an area-weighted box filter before evaluating the neural model.
- High-frequency matched residual resolve pass: composites the neural delta back onto the untouched native frame.
- HDR luminance bounding prevents highlight clipping and shadow instability.
- Integrated AMD RCAS (Robust Contrast Adaptive Sharpening) pass.
- In-game hot-reloading: changes made to `nvngx_dlssnr.ini` take effect within one second without restarting.
- In-game hotkeys for live toggling, mode switching, and scale adjustments.
- **In-game overlay panel**: press `Ctrl+Alt+F10` to summon a bilingual topmost panel for mouse-driven adjustments (drag sliders, click presets/segments, toggle switches) while the game keeps running.
- **Standalone console EXE**: `dlssnr_console.exe` edits the same settings from outside the game (INI + live shared-memory sync).
- Handles SDR (B8G8R8A8 / R8G8B8A8), HDR10 PQ (R10G10B10A2), scRGB (R16G16B16A16_FLOAT), and 3-channel HDR (R11G11B10_FLOAT).
- Dynamic subrect tracking preserves viewport offsets for games using Dynamic Resolution Scaling (DRS).

---

## Requirements

- Windows 10/11 (64-bit)
- NVIDIA RTX GPU (RTX 20, 30, 40, or 50 series)
- A game or addon utilizing NVIDIA DLSS-NR (`nvngx_dlssnr.dll`) over DirectX 12

---

## Installation

1. Navigate to your game folder where `nvngx_dlssnr.dll` is located.
2. Rename the original `nvngx_dlssnr.dll` to:
   ```text
   nvngx_dlssnr_real.dll
   ```
3. Copy the proxy `nvngx_dlssnr.dll` and `nvngx_dlssnr.ini` from the release into that same folder.
4. *(Optional)* Copy `dlssnr_console.exe` into the same folder to tune settings from outside the game.
5. *(Optional for ReShade users)*: Copy `dlssnr-companion.addon64` into your game folder to get a live configuration overlay under the ReShade Home menu.
6. Launch the game. Press `Ctrl+Alt+F10` in-game (or run `dlssnr_console.exe`) to open the configuration panel.

---

## Configuration (`nvngx_dlssnr.ini`)

The configuration file is read at startup and automatically hot-reloaded every second when modified:

```ini
[DLSSNR_Proxy]
; Master toggle for the proxy
; 1 = Proxy enabled (applies ResolutionScale)
; 0 = Proxy disabled (100% native passthrough to real DLSS-NR)
EnableProxy = 1

; Internal model resolution scale (0.25 to 1.00)
; 1.00 = 100% Native
; 0.85 = 85% Resolution (~28% faster neural pass)
; 0.80 = 80% Resolution (~35% faster)
; 0.75 = 75% Resolution (~40% faster, recommended sweet spot)
; 0.67 = 67% Resolution (DLSS Quality ratio)
; 0.50 = 50% Resolution (DLSS Performance ratio)
ResolutionScale = 0.75

; Resolve algorithm
; 1 = Matched Residual (1:1 Native Anchor + Neural Detail Transfer, Ultra Crisp, Recommended)
; 0 = Direct Neural Reconstruction Bilinear + RCAS
EnlargementMode = 1

; Strength of the neural detail transfer (0.0 to 2.0, default 1.0)
TransferStrength = 1.00

; Neural color/tint transfer strength (0.0 to 1.0, default 1.0)
; 1.00 = Full neural color transfer
; 0.00 = Luminance-only transfer (eliminates neural color shifts/tint while keeping full lighting and detail)
ColorStrength = 1.00

; Contrast-adaptive edge sharpening (0.0 to 1.0, default 0.0)
Sharpness = 0.20

; Enable in-game hotkeys
EnableHotkeys = 1

; Enable the in-game overlay panel hotkey (Ctrl+Alt+F10)
EnableUi = 1

[Hotkeys]
; Require Ctrl + Alt modifiers held down with the hotkey (1 = yes, 0 = no)
RequireCtrlAlt = 1

; Virtual-Key codes (Decimal):
; Space=32, PageUp=33, PageDown=34, End=35, Home=36, Insert=45, Delete=46
; F1-F12 = 112-123, 0-9 = 48-57, A-Z = 65-90
KeyToggleProxy = 32
KeyToggleMode = 35
KeyScaleUp = 33
KeyScaleDown = 34

; Base key of the overlay panel combo (Ctrl+Alt+<key>), default F10 = 121
KeyToggleUI = 121
```

---

## In-Game Overlay Panel & Standalone Console

Both UIs share the same bilingual layout (Chinese primary / English secondary) and are fully mouse-driven:

- **Switch** — toggle proxy / hotkeys / panel hotkey.
- **Slider + presets** — resolution scale with quick chips (100 / 85 / 80 / 75 / 67 / 50 %) and a fine slider.
- **Segment** — resolve mode (匹配残差 Matched Residual / 双线性 Bilinear).
- **Key rows** — display the hotkey combos (the console EXE also lets you rebind them by clicking a row and pressing a key; Esc cancels).
- Every change is **auto-saved** to `nvngx_dlssnr.ini` and, while the game is running, pushed to the shared-memory config so it applies live.
- **GitHub row** — click the `GitHub` link at the bottom of the panel to open the project homepage (<https://github.com/suviland/DLSSNR-Cost-Scaler-CN>) in your browser.

### In-game (proxy DLL)

- Press `Ctrl+Alt+F10` (`KeyToggleUI`) to show/hide the panel. It is a borderless topmost tool window; when shown it **takes foreground focus** so it can be clicked normally over the game, the header can be dragged anywhere, and the `×` hides it again (focus returns to the game).
- Works even when `EnableHotkeys = 0`; disable it entirely with `EnableUi = 0`.

### Standalone console (`dlssnr_console.exe`)

- Place it in the same folder as `nvngx_dlssnr.ini` and run it (game may be running or not).
- When the game is not running it edits the INI for the next launch; when the game is running the panel stays live-synced with the in-game overlay/hotkeys through shared memory.
- Click the header `×` or press `Esc` to exit.

---

## ReShade Companion Addon (`dlssnr-companion.addon64`)

If using ReShade, drop `dlssnr-companion.addon64` into your game directory alongside ReShade.

- **Non-Invasive:** Does not hook graphics draw calls or pipeline passes; operates purely as an overlay tab in the ReShade Home menu.
- **Debounced Sliders:** Features interactive debouncing to ensure rapid slider adjustments never hitch or cause GPU model thrashing.
- **In-Game Hotkey Rebinding:** Rebind shortcut keys and modifier requirements directly in the UI.

---

## In-Game Hotkeys

When `EnableHotkeys = 1`, the default shortcuts are:

- `Ctrl + Alt + F10` — Toggle the overlay panel (independent of `EnableHotkeys`; gated by `EnableUi`).
- `Ctrl + Alt + Space` — Toggle proxy ON / OFF (switches between scaled proxy and native passthrough).
- `Ctrl + Alt + End` — Toggle EnlargementMode between Matched Residual (`1`) and Bilinear (`0`).
- `Ctrl + Alt + PageUp` — Increase resolution scale by +5%.
- `Ctrl + Alt + PageDown` — Decrease resolution scale by -5%.

---

## Building from Source

Prerequisites:
- Visual Studio 2022 or Build Tools with the Desktop C++ workload.
- Windows 10/11 SDK with `fxc.exe` (DirectX Shader Compiler).

To build:
1. Open the project folder.
2. Run `build.bat` from an x64 Developer Command Prompt or standard prompt (it auto-locates vcvars64 / fxc when installed in the default locations).
3. The compiled outputs are generated in the root folder: `nvngx_dlssnr.dll` (proxy, includes the in-game overlay) and `dlssnr_console.exe` (standalone console).

---

## Credits

- [Dagherbou / OptiScaler_DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR) — For pioneering DLSS-NR integration, the matched residual resolve concept, and feature lifecycle handling.
- [OptiScaler](https://github.com/optiscaler/OptiScaler) — For the parent upscaler framework.
- [clshortfuse / RenoDX](https://github.com/clshortfuse/renodx) — For the RenoDX framework and DLSS ReShade addon.
- [AMD](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) — For the Robust Contrast Adaptive Sharpening (RCAS) algorithm.

---

## License

This project is licensed under the [MIT License](LICENSE).
