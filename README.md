# DLSS5NR-CostScaler

<p align="center">
  <a href="README-CN.md"><b>简体中文</b></a> ｜ <b>English</b>
</p>

> ✅ **Tested and working**: ReShade (incl. RenoDX-based plugins) · Skyrim [Community Shaders](https://github.com/doodlegabe/CommunityShaders) · clshortfuse's DLSS addon (`renodx-dlss.addon64`)

---

## ✨ Why this fork (vs upstream)

This repository is an enhanced fork of [xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler) (upstream v1.0.5 fully merged). Upstream is a pure algorithm proxy configured by hand-editing an ini and restarting the game; this fork turns it into a **live, WYSIWYG tuning toolchain** and deeply reworks frame-skipped denoising:

| Capability | Upstream | This fork |
| --- | :-: | :-: |
| Tuning | Manual ini edits + game restart | **In-game overlay / console / manager — changes apply instantly** |
| Multi-client sync | None | Panel ⇄ console ⇄ shared memory ⇄ proxy, millisecond-level |
| Install / uninstall | Manual rename & copy | **Manager recursively scans your game library; one-click install / uninstall / restore** |
| VRNR frame skipping | Fixed alternate frames | **Adjustable interval (every 1/2/3 frames) + skip-frame blending + adaptive scheduling** |
| UI | — | M3E-style, light/dark themes, zh / EN / RU / 한 |
| Safety | — | SEH crash shield; shader faults never take the game down |

## 🆕 What's new

### VRNR 2.0: adjustable interval + motion-adaptive denoising (unique to this fork)

Skipping frames saves GPU time, but "NR frames look clean while skipped frames look dirty". This fork ships a complete answer — all three switches live in the panel:

- **Adjustable inference interval** (`VrnrInterval`): every frame / every 2nd / every 3rd;
- **Skip-frame blending** (`VrnrBlend`): downsampling still runs on skipped frames; the resolve shader blends per-pixel between the raw native pixel and a low-pass of the current frame based on the **per-pixel motion-vector magnitude** — static areas stay razor sharp, moving areas stop flickering with noise;
- **Adaptive scheduling** (`VrnrAdaptiveSkip`): a built-in MV motion-reduction pass (GPU reduction + lagged readback) forces per-frame NR while the scene moves, and only skips frames when the scene is static — in menus the NR almost stops spinning.

### Three live, synchronized tuning surfaces

- **In-game overlay panel** (`Ctrl+Alt+F11`): topmost, draggable, freely resizable. Never steals focus and never touches the game's input pipeline (observe-only low-level mouse hook); the game recovers the instant it closes.
- **Standalone console `dlssnr_console.exe`**: runs outside the game process, live two-way sync with the proxy over shared memory.
- **ReShade companion addon** (`dlssnr-companion.addon64`): a config page inside the ReShade Home menu.

Any change — from any surface, or from the ini file (hot-reloaded every second) — propagates to everything else immediately.

### GUI manager `DLSS5NR-CostScaler-Manager.exe`

Pure Win32 + GDI+ Material 3 Expressive UI (zero runtime dependencies), four pages: **Quick Install** (recursive library scan, one-click install / uninstall / restore) · **Installed games** · **History** · **Panel debug** (edits the proxy's `nvngx_dlssnr.ini` directly, auto-committed with a 500 ms debounce).

### The panel is a full console (not just toggles)

Paged layout (**Basic / Advanced / NR / Hotkeys**) covering every setting: scale 25%–200% quick chips, transfer / color / RCAS sliders, depth-aware resolve, anamorphic X/Y, the **official NVIDIA NR settings block**, and visual hotkey rebinding with Ctrl+Alt combos. Light/dark themes, four languages, position & size persistence.

### Algorithm layer (synced with upstream v1.0.5)

Hardware-bilinear downsampling (LDS tile cache) → low-res NR inference → **high-frequency matched-residual composite** back onto the native frame; **25%–200% super-sampling**, **anamorphic scaling** (experimental), **depth-aware bilateral silhouette preservation**, HDR luminance bounding + RCAS sharpening, DRS sub-rect tracking, SDR / HDR10 PQ / scRGB / R11G11B10.

---

## What is this

A standalone proxy DLL and companion toolset for NVIDIA DLSS-NR (DirectX 12) that adds **resolution scaling** and **GPU cost control**. It runs the neural reconstruction model at a reduced resolution while preserving native 1:1 geometry, fine textures, text, and edges via a high-frequency matched-residual composite shader — **decoupling DLSS-NR's GPU cost from the display resolution without introducing blur**. Host-agnostic by design: any game, engine, or injector calling `nvngx_dlssnr.dll` over DirectX 12 works.

**How it works**: the stock DLL is renamed to `nvngx_dlssnr_real.dll`; the proxy intercepts NGX calls — downsamples the native frame (near-free), hands it to DLSS-NR, then composites the neural delta back onto the untouched native frame as a "matched residual".

---

## Artifacts

Build outputs land in `build\<version>\` (currently `build/0.7.0/`) with embedded version info:

| File | Purpose |
| --- | --- |
| `nvngx_dlssnr.dll` | The proxy itself (drop into the game folder; forwards to the real `nvngx_dlssnr_real.dll`) |
| `DLSS5NR-CostScaler-Manager.exe` | GUI manager (install / uninstall / installed games / history / panel debug) |
| `dlssnr_console.exe` | Standalone debug console (live sync outside the game) |
| `nvngx_dlssnr.ini` | Configuration (hot-reloaded in-game every second) |
| `dlssnr-companion.addon64` | ReShade companion addon (optional) |

---

## Quick start

1. Put the manager next to the proxy `nvngx_dlssnr.dll` and `nvngx_dlssnr.ini` (auto-detected);
2. Open the manager → **Quick Install** → pick a game folder or library root in ② → Scan → tick → **Install**;
3. Launch the game and press `Ctrl+Alt+F11`; or tweak settings in the manager's **Panel debug** page (hot-reloaded into the game within a second).

<details>
<summary>Manual installation (equivalent)</summary>

1. In the game folder, rename the stock `nvngx_dlssnr.dll` to `nvngx_dlssnr_real.dll`;
2. Copy the proxy `nvngx_dlssnr.dll` and `nvngx_dlssnr.ini` into the same folder;
3. *(Optional)* drop `dlssnr_console.exe` alongside for out-of-game tuning, and `dlssnr-companion.addon64` for the ReShade page;
4. Launch the game and press `Ctrl+Alt+F11`.

</details>

---

## Configuration (`nvngx_dlssnr.ini`)

Read at startup, then hot-reloaded every second:

```ini
[DLSSNR_Proxy]
EnableProxy = 1          ; 1 = proxy active; 0 = native passthrough
ResolutionScale = 0.75   ; inference scale (0.25 ~ 2.00; 2.00 = 200% super-sampling)
EnableAnamorphic = 0     ; anamorphic scaling (experimental): uses X/Y below
ResolutionScaleX = 0.65  ; horizontal scale (0.25 ~ 2.00)
ResolutionScaleY = 0.85  ; vertical scale (0.25 ~ 2.00)
EnlargementMode = 1      ; 1 = Matched Residual; 0 = Bilinear Direct
TransferStrength = 1.00  ; residual transfer strength (0 ~ 2)
ColorStrength = 1.00     ; color strength (0 ~ 1)
Sharpness = 0.20         ; RCAS sharpening (0 ~ 1)
EnableDepthAwareResolve = 1 ; depth-aware silhouette preservation
VrnrInterval = 1         ; NR interval: 1 every frame (off) / 2 / 3
VrnrBlend = 1            ; skip-frame blend (per-pixel MV weighting)
VrnrAdaptiveSkip = 0     ; adaptive scheduling (per-frame NR while moving)
EnableHotkeys = 1        ; in-game hotkey master switch
EnableUi = 1             ; overlay panel master switch
UiLanguage = 0           ; panel language: 0 zh / 1 EN / 2 RU / 3 한
PanelX = 1500            ; panel position / size (auto-saved; console shares them)
PanelY = 120
PanelW = 396
PanelH = 640

[DLSSNR_Settings]
UseCustomSettings = 0    ; 0 = pass through the caller's NR params; 1 = override
Style = 0                ; 0 = Balanced / 1 = Sharp / 2 = Cinematic
Intensity = 1.00         ; reconstruction intensity (0 ~ 2)
LocalStructureStrength = 1.00  ; local structure (0 ~ 2)
LocalToneStrength = 1.00       ; local tone (0 ~ 2)
SkinStructureStrength = -1.00  ; skin structure (-1 = auto, 0 ~ 2)
UseAutoMask = 0          ; auto mask for fast motion / small elements

[Hotkeys]
RequireCtrlAlt = 1       ; hotkeys require the Ctrl+Alt prefix
KeyToggleProxy = 32      ; toggle proxy (VK code)
KeyToggleMode = 35       ; toggle mode
KeyScaleUp = 33          ; scale up
KeyScaleDown = 34        ; scale down
KeyToggleUI = 123        ; toggle panel (default F12)
```

> Every setting above is editable in the manager's **Panel debug** page or the in-game panel, auto-saved on change. `EnableAlternatingFrames` is a legacy key derived from `VrnrInterval > 1`.

---

## Requirements

- Windows 10 / 11 (64-bit)
- NVIDIA RTX GPU (RTX 20 / 30 / 40 / 50 series)
- A game or plugin that uses NVIDIA DLSS-NR (`nvngx_dlssnr.dll`) over DirectX 12

## Build (x64, MSVC)

```text
build.bat
```

Runs everything: HLSL shader compilation (fxc) → version resource → proxy DLL → console → manager. Outputs go to `build\<version>\`, intermediates to `build\obj\`; the version lives in `build.bat` (`VERSION`) and `app_version.rc`.

## FAQ

- **Game in Program Files?** Run the manager as administrator, or install manually.
- **Panel doesn't receive the mouse?** The panel uses an observe-only low-level mouse hook and never competes with the game for input; if an anti-cheat blocks it, please report.
- **VRNR — is it worth it?** With **skip-frame blending + adaptive scheduling** enabled it is far better than bare frame skipping: static scenes are near-identical, and heavy motion automatically falls back to per-frame NR. If you still notice noise pulsing, keep `VrnrInterval = 1`.
- **`DXGI_ERROR_DEVICE_REMOVED` in the log?** That's a GPU driver reset (TDR), unrelated to the panel (it never touches the game's D3D device). Check overclocks / drivers.

## Acknowledgments & License

- Upstream algorithm: [xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler) (MIT; this repo is an enhanced fork)
- Design tokens: [creeper-qt](https://github.com/creeper5820/creeper-qt) (MIT) BlueMiku theme pack
- Thanks to [Dagherbou/OptiScaler_DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR), [OptiScaler](https://github.com/optiscaler/OptiScaler), [clshortfuse/RenoDX](https://github.com/clshortfuse/renodx), [AMD FidelityFX](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) (RCAS), and the [Community Shaders](https://github.com/doodlegabe/CommunityShaders) team

Released under the MIT license — see [LICENSE](LICENSE).
