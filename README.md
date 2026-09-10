# DLSS5NR-CostScaler

<p align="center">
  <a href="README-CN.md">简体中文</a> ｜ <b>English</b> ｜ <a href="README-RU.md">Русский</a> ｜ <a href="README-KO.md">한국어</a>
</p>

> ✅ **Field-tested**: ReShade (incl. RenoDX-family plugins) · The Elder Scrolls [Community Shaders](https://github.com/doodlegabe/CommunityShaders) · clshortfuse DLSS plugin (`renodx-dlss.addon64`)

---

## ✨ What this fork adds (vs upstream)

This repository is an enhanced fork of [xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler) (upstream v1.0.5 fully synced). Upstream is a pure algorithmic proxy driven by hand-edited INI files and game restarts; this fork turns it into a **WYSIWYG real-time tuning toolchain** and deeply reworks frame-alternating denoising:

| Capability | Upstream | This fork |
| --- | :-: | :-: |
| Tuning | Hand-edit ini + restart | **In-game overlay panel / console / manager — changes apply instantly** |
| Multi-end live sync | None | Panel ⇄ console ⇄ shared memory ⇄ proxy, millisecond-level |
| Graphical install / uninstall | Manual rename & copy | **Manager recursively scans game libraries; one-click install / remove / restore** |
| VRNR frame skipping | Fixed alternate frames | **Visual switch + skip-frame anti-flicker** (temporal ramp / spatial smoothing / symmetric highlight guard) |
| UI | — | M3E style, light & dark themes, 中 / EN / RU / 한 |
| Safety | — | SEH crash shield; shader exceptions never take the game down |

## 🆕 Highlights

### Three-end live tuning

- **In-game overlay panel** (`Ctrl+Alt+F11`): topmost, draggable, freely resizable; never steals focus or touches the game's input pipeline (low-level mouse hook, observer only) — game input recovers the instant the panel closes;
- **Standalone console `dlssnr_console.exe`**: runs outside the game process, live two-way sync via shared memory;
- **ReShade companion addon** (`dlssnr-companion.addon64`): a config page inside the ReShade Home menu.

Any change (including ini hot-reload) propagates to every other end instantly.

### Graphical manager `DLSS5NR-CostScaler-Manager.exe`

Pure Win32 + GDI+ custom-painted Material 3 Expressive UI (zero runtime dependencies), four pages: **Quick Install** (recursive library scan, one-click install / remove / restore) · **Installed** · **History** · **Panel Debug** (reads/writes the proxy ini directly, 500 ms debounced write-back).

### Paged overlay panel = full console

**Basics / Advanced / NR / Hotkeys** pages cover every setting: 25%–200% scale chips, transfer / color / RCAS sliders, depth-aware resolve, anamorphic X/Y, the **official NVIDIA NR parameter block**, visual hotkey rebinding with Ctrl+Alt combos; light & dark themes, four languages, position & size persistence.

### Skip-frame anti-flicker (0.6.3, unique to this fork)

With alternating-frame VRNR enabled, inference frames and skipped frames alternate on screen, which tends to produce a 30 Hz luminance square wave and "noise breathing". This fork ships a three-fold fix (master switch `VrnrAntiFlicker`, on by default):

- **Temporal ramp**: while frames are skipped consecutively, the residual strength decays smoothly 100% → 90% → 72% → 55% and springs back when inference resumes — no more abrupt on/off at motion boundaries;
- **Spatial smoothing**: the fade-out weight is computed from a 3×3 neighborhood-averaged luma difference, so native sensor noise no longer jitters the weight every frame;
- **Symmetric highlight guard**: skipped frames apply the same HDR highlight clamp, keeping both paths consistent in bright regions.

Zero new GPU resources and untouched shader resource bindings; with the switch off, behavior is byte-for-byte 0.6.2.

### Algorithm layer (synced with upstream v1.0.5)

Hardware bilinear downsample (LDS tile cache) → low-res NR inference → **high-frequency matched residual compositing** back onto the native frame; **25%–200% supersampling**, **anamorphic scaling** (experimental), **depth-aware bilateral silhouette preservation**, HDR luminance clamp + RCAS sharpening, DRS dynamic sub-rect tracking, SDR / HDR10 PQ / scRGB / R11G11B10.

---

## What is this

A standalone proxy DLL and toolset for NVIDIA DLSS-NR (DirectX 12): the neural reconstruction model runs its inference at a lower resolution while a "high-frequency matched residual" shader preserves native 1:1 geometry, fine textures, text and edges — **decoupling DLSS-NR's GPU cost from display resolution without introducing blur**. The design is host-agnostic: any game, engine or injector calling `nvngx_dlssnr.dll` through DirectX 12 works.

**How it works**: the original DLL is renamed `nvngx_dlssnr_real.dll`; the proxy intercepts NGX calls — downsamples the native frame (nearly free), hands it to DLSS-NR, then composites the network's delta back onto the untouched native frame as a matched residual.

---

## Artifacts

Build outputs land in `build\<version>\` (currently `build/0.6.3/`) with embedded version info:

| File | Purpose |
| --- | --- |
| `nvngx_dlssnr.dll` | The proxy itself (into the game folder; forwards to the real `nvngx_dlssnr_real.dll`) |
| `DLSS5NR-CostScaler-Manager.exe` | Graphical manager (install / remove / installed / history / panel debug) |
| `dlssnr_console.exe` | Standalone debug console (live sync outside the game) |
| `nvngx_dlssnr.ini` | Config file (hot-reloaded every second in game) |
| `dlssnr-companion.addon64` | ReShade companion addon (optional) |

---

## Quick start

1. Put the manager together with the proxy `nvngx_dlssnr.dll` + `nvngx_dlssnr.ini` in one folder (auto-detected);
2. Open the manager → "Quick Install" → ② pick a game folder or library root → scan → tick → **Install**;
3. Launch the game, press `Ctrl+Alt+F11` for the panel; or tweak things in the manager's "Panel Debug" page (hot-reloaded in game within a second).

<details>
<summary>Manual install (equivalent)</summary>

1. In the game folder, rename the original `nvngx_dlssnr.dll` to `nvngx_dlssnr_real.dll`;
2. Copy the proxy `nvngx_dlssnr.dll` and `nvngx_dlssnr.ini` into the same folder;
3. *(Optional)* drop `dlssnr_console.exe` next to them for out-of-game tuning; drop `dlssnr-companion.addon64` for the ReShade menu;
4. Start the game, press `Ctrl+Alt+F11`.

</details>

---

## Configuration (`nvngx_dlssnr.ini`)

Read at startup, then hot-reloaded every second:

```ini
[DLSSNR_Proxy]
EnableProxy = 1          ; 1 = proxy active; 0 = native passthrough
ResolutionScale = 0.75   ; inference resolution scale (0.25 ~ 2.00; 2.00 = 200% supersampling)
EnableAnamorphic = 0     ; anamorphic scaling (experimental): enables the independent X/Y scales below
ResolutionScaleX = 0.65  ; horizontal scale (0.25 ~ 2.00)
ResolutionScaleY = 0.85  ; vertical scale (0.25 ~ 2.00)
EnlargementMode = 1      ; 1 = Matched Residual; 0 = Bilinear
TransferStrength = 1.00  ; residual compositing strength (0 ~ 2)
ColorStrength = 1.00     ; chroma strength (0 ~ 1)
Sharpness = 0.20         ; RCAS sharpening (0 ~ 1)
EnableDepthAwareResolve = 1 ; depth-aware silhouette preservation
EnableAlternatingFrames = 0 ; alternating-frame VRNR (experimental, off by default)
VrnrAntiFlicker = 1        ; skip-frame anti-flicker (0.6.3, on by default; off = exact 0.6.2 behavior)
EnableHotkeys = 1        ; in-game hotkey master switch
EnableUi = 1             ; in-game overlay panel master switch
UiLanguage = 0           ; panel language: 0 zh / 1 EN / 2 RU / 3 한
PanelX = 1500            ; panel window position / size (auto-saved; the console reads & writes them too)
PanelY = 120
PanelW = 396
PanelH = 640

[DLSSNR_Settings]
UseCustomSettings = 0    ; 0 = pass through caller's NR params; 1 = override with the values below
Style = 0                ; 0 = Balanced / 1 = Sharp / 2 = Cinematic
Intensity = 1.00         ; reconstruction strength (0 ~ 2)
LocalStructureStrength = 1.00  ; local structure preservation (0 ~ 2)
LocalToneStrength = 1.00       ; local tone (0 ~ 2)
SkinStructureStrength = -1.00  ; skin structure (-1 = auto, 0 ~ 2)
UseAutoMask = 0          ; auto-mask for fast motion / small elements

[Hotkeys]
RequireCtrlAlt = 1       ; whether hotkeys require the Ctrl+Alt prefix
KeyToggleProxy = 32      ; toggle proxy (VK code)
KeyToggleMode = 35       ; toggle mode
KeyScaleUp = 33          ; scale up
KeyScaleDown = 34        ; scale down
KeyToggleUI = 123        ; toggle panel (default F12)
```

> Every setting above can also be edited visually in the manager's "Panel Debug" page or the in-game panel; changes are saved automatically

---

## System requirements

- Windows 10 / 11 (64-bit)
- NVIDIA RTX GPU (RTX 20 / 30 / 40 / 50 series)
- A game or plugin that uses NVIDIA DLSS-NR (`nvngx_dlssnr.dll`) through DirectX 12

## Build (x64, MSVC)

```text
build.bat
```

Runs the full pipeline: HLSL shader compilation (fxc) → version resource → proxy DLL → console → manager. Outputs land in `build\<version>\` with intermediates in `build\obj\`; the version is maintained in `build.bat` (`VERSION`) and `app_version.rc`.

## FAQ

- **Game folder is under Program Files?** Run the manager as administrator, or install manually.
- **Panel doesn't receive mouse input?** The panel uses a low-level mouse hook in observer mode and never competes with the game for input; if an anti-cheat blocks it, please report.
- **Is alternating-frame VRNR worth it?** With **skip-frame anti-flicker** enabled (`VrnrAntiFlicker`, on by default) it is far better than bare frame skipping: noise breathing and motion-boundary popping are smoothed away. If it still looks off in a particular game, keep `EnableAlternatingFrames = 0`.
- **Crash log shows `DXGI_ERROR_DEVICE_REMOVED`?** That is a GPU driver reset (TDR), unrelated to the panel (the panel never touches the game's D3D device); check overclocking / driver versions.

## Credits & license

- Upstream algorithm: [xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler) (MIT; this repo is an enhanced fork)
- Design tokens: [creeper-qt](https://github.com/creeper5820/creeper-qt) (MIT) BlueMiku theme
- Thanks to [Dagherbou/OptiScaler_DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR), [OptiScaler](https://github.com/optiscaler/OptiScaler), [clshortfuse/RenoDX](https://github.com/clshortfuse/renodx), [AMD FidelityFX](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) (RCAS), and the [Community Shaders](https://github.com/doodlegabe/CommunityShaders) team

This project is open source under the MIT license — see [LICENSE](LICENSE).
