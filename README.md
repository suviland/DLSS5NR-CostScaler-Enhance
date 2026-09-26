# DLSS5NR-CostScaler

<p align="center">
  <a href="README-CN.md">简体中文</a> ｜ <b>English</b> ｜ <a href="README-RU.md">Русский</a> ｜ <a href="README-KO.md">한국어</a>
</p>

> **Field-tested**: ReShade (incl. RenoDX-family plugins) · The Elder Scrolls [Community Shaders](https://github.com/doodlegabe/CommunityShaders) · clshortfuse DLSS plugin (`renodx-dlss.addon64`)

---

## What this fork adds (vs upstream)

This repository is an enhanced fork of [xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler) (upstream v1.0.5 fully synced). Upstream is a pure algorithmic proxy driven by hand-edited INI files and game restarts; this fork turns it into a **WYSIWYG real-time tuning toolchain** and deeply reworks frame-alternating denoising:

| Capability | Upstream | This fork |
| --- | :-: | :-: |
| Tuning | Hand-edit ini + restart | **In-game overlay panel / console / manager — changes apply instantly** |
| Multi-end live sync | None | Panel ⇄ console ⇄ shared memory ⇄ proxy, millisecond-level |
| Graphical install / uninstall | Manual rename & copy | **Manager recursively scans game libraries; one-click install / remove / restore** |
| VRNR frame skipping | Fixed alternate frames | **Visual switch + full anti-flicker chain** (0.6.3 base triple → 0.7.1 weight floor / MV reprojection / silhouette guard → 0.7.3 FPS-adaptive strength) |
| FPS Governor | None | **Auto resolution tiering to hold a target FPS**, with Frame-Gen multiplier support (0.7.0, upstream sync) |
| UI | — | M3E style, light & dark themes, 中 / EN / RU / 한 |
| Safety | — | SEH crash shield; shader exceptions never take the game down |

## Highlights

### Three-end live tuning

- **In-game overlay panel** (`Ctrl+Alt+F11`): topmost, draggable, freely resizable; never steals focus or touches the game's input pipeline (low-level mouse hook, observer only) — game input recovers the instant the panel closes;
- **Standalone console `dlssnr_console.exe`**: runs outside the game process, live two-way sync via shared memory;
- **ReShade companion addon** (`dlssnr-companion.addon64`): a config page inside the ReShade Home menu.

Any change (including ini hot-reload) propagates to every other end instantly.

### Graphical manager `DLSS5NR-CostScaler-Manager.exe`

Pure Win32 + GDI+ custom-painted Material 3 Expressive UI (zero runtime dependencies), four pages: **Quick Install** (recursive library scan, one-click install / remove / restore) · **Installed** · **History** · **Panel Debug** (reads/writes the proxy ini directly, 500 ms debounced write-back).

### FPS Governor (0.7.0, upstream sync)

A dynamic-resolution tiering state machine: frame times are EWMA-smoothed (~30-frame window) and compared against the target FPS — below 95% of target for 1.0 s steps down 5%, above 115% for 3.0 s steps up 5%, each step followed by a cooldown dwell (default 2.0 s). Tier switches use a multi-slot pipeline cache for zero-wait instant switching; **Frame-Gen (FG) mode** evaluates the target against "display FPS = base × multiplier" for DLSS 3 / FSR 3 / Lossless Scaling 2x/3x/4x.

### Skip-frame anti-flicker chain (0.6.3 → 0.7.1 → 0.7.3, unique to this fork)

With alternating-frame VRNR enabled, inference frames (full-strength neural rebuild) and skipped frames (stale edit × decayed weight) alternate on screen — the lower the FPS and the larger the per-frame motion, the more visible the pulse becomes in the eye-sensitive band. This fork ships cumulative fixes (master switch `VrnrAntiFlicker`, on by default):

- **Temporal ramp (0.6.3, fixed in 0.7.1)**: with 2+ consecutive skipped frames the residual strength decays 100% → 90% → 72% → 55%; the alternating pattern (each skip frame's run count is always 1) no longer gets mis-discounted, removing the 10% periodic pulse;
- **Spatial smoothing (0.6.3)**: the fade-out weight is computed from a 3×3 neighborhood-averaged luma difference, so native sensor noise no longer jitters the weight every frame;
- **Symmetric highlight guard (0.6.3)**: skipped frames apply the same HDR highlight clamp, keeping both paths consistent in bright regions;
- **Weight floor (0.7.1, experimental)**: skip-frame edit weight never drops below `VrnrWeightFloor` (default 0.60), so moving regions no longer collapse to zero and snap back on the next inference frame;
- **MV reprojection (0.7.1, experimental)**: skipped frames align the 2-frames-old stale neural input to the current frame using the game's motion vectors before computing the luma difference, killing the full-screen fade-out pulse caused by "misaligned frame falsely detected as motion"; the better-aligned sample wins, so MV sign conventions don't matter;
- **Silhouette guard (0.7.1, experimental)**: at depth discontinuities (character / object outlines) the skip-frame edit weight is clamped to ≤0.10, so stale edits never ghost across silhouettes;
- **FPS-adaptive strength (0.7.3, experimental)**: at low FPS the **inference frames'** edit strength is adaptively dimmed (`VrnrAdapt`, on by default), shrinking the visual gap to skipped frames from both ends — pure CPU-side, zero new GPU resources.

### Algorithm layer (synced with upstream v1.0.5)

Hardware bilinear downsample (LDS tile cache) → low-res NR inference → **high-frequency matched residual compositing** back onto the native frame; **25%–200% supersampling**, **anamorphic scaling** (experimental), **depth-aware bilateral silhouette preservation**, HDR luminance clamp + RCAS sharpening, DRS dynamic sub-rect tracking, SDR / HDR10 PQ / scRGB / R11G11B10.
<img width="875" height="732" alt="image" src="https://github.com/user-attachments/assets/85a61997-9dcb-40b6-9a2a-71aafeb1b4f4" />
<img width="875" height="717" alt="image" src="https://github.com/user-attachments/assets/69b5c4f4-d2c3-4e04-b895-56311534a87c" />

---

## What is this

A standalone proxy DLL and toolset for NVIDIA DLSS-NR (DirectX 12): the neural reconstruction model runs its inference at a lower resolution while a "high-frequency matched residual" shader preserves native 1:1 geometry, fine textures, text and edges — **decoupling DLSS-NR's GPU cost from display resolution without introducing blur**. The design is host-agnostic: any game, engine or injector calling `nvngx_dlssnr.dll` through DirectX 12 works.

**How it works**: the original DLL is renamed `nvngx_dlssnr_real.dll`; the proxy intercepts NGX calls — downsamples the native frame (nearly free), hands it to DLSS-NR, then composites the network's delta back onto the untouched native frame as a matched residual.

---

## Artifacts

Build outputs land in `build\<version>\` (currently `build/0.7.3/`) with embedded version info:

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

## Configuration quick reference (`nvngx_dlssnr.ini`)

Read at startup, then hot-reloaded every second. Quick reference below; **per-switch purpose & internals in the next section, "Configuration deep dive"**.

```ini
[DLSSNR_Proxy]
EnableProxy = 1              ; 1 = proxy active; 0 = native passthrough
ResolutionScale = 0.75       ; inference resolution scale (0.25 ~ 2.00)
EnableAnamorphic = 0         ; anamorphic scaling (experimental)
ResolutionScaleX = 0.65      ; horizontal scale (0.25 ~ 2.00)
ResolutionScaleY = 0.85      ; vertical scale (0.25 ~ 2.00)
EnlargementMode = 1          ; 1 = Matched Residual; 0 = Bilinear
TransferStrength = 1.00      ; residual compositing strength (0 ~ 2)
ColorStrength = 1.00         ; chroma strength (0 ~ 1)
Sharpness = 0.20             ; RCAS sharpening (0 ~ 1)
EnableDepthAwareResolve = 1  ; depth-aware silhouette preservation
EnableAlternatingFrames = 0  ; alternating-frame VRNR (experimental, off by default)
VrnrAntiFlicker = 1          ; skip-frame anti-flicker master switch (on by default)
VrnrWeightFloor = 0.60       ; skip-frame edit weight floor (experimental, 0 ~ 1)
VrnrReproject = 1            ; motion-vector reprojection (experimental)
VrnrAdapt = 1                ; FPS-adaptive strength (experimental, 0.7.3)
VrnrAdaptAmount = 0.60       ; adaptive max dim fraction (0 ~ 1)
VrnrAdaptFpsHi = 45          ; dimming starts below this FPS
VrnrAdaptFpsLo = 25          ; full dimming at/below this FPS
EnableHotkeys = 1            ; in-game hotkey master switch
EnableUi = 1                 ; in-game overlay panel master switch
UiLanguage = 0               ; panel language: 0 zh / 1 EN / 2 RU / 3 한
PanelX/Y/W/H = ...           ; panel window position / size (auto-saved)

[DLSSNR_Settings]
UseCustomSettings = 0        ; 0 = pass through caller's NR params; 1 = override with the values below
Style = 0                    ; 0 = Balanced / 1 = Sharp / 2 = Cinematic
Intensity = 1.00             ; reconstruction strength (0 ~ 2)
LocalStructureStrength = 1.00  ; local structure preservation (0 ~ 2)
LocalToneStrength = 1.00       ; local tone (0 ~ 2)
SkinStructureStrength = -1.00  ; skin structure (-1 = auto, 0 ~ 2)
UseAutoMask = 0              ; auto-mask for fast motion / small elements

[Hotkeys]
RequireCtrlAlt = 1           ; whether hotkeys require the Ctrl+Alt prefix
KeyToggleProxy = 32          ; toggle proxy (VK code, 32 = Space)
KeyToggleMode = 35           ; toggle mode (35 = End)
KeyScaleUp = 33              ; scale up (33 = PageUp)
KeyScaleDown = 34            ; scale down (34 = PageDown)
KeyToggleUI = 122            ; toggle panel (122 = F11)

[Governor]
EnableGovernor = 0           ; dynamic resolution governor (off by default)
TargetFps = 60.0             ; target FPS budget (30 ~ 240)
MinScale = 0.50              ; minimum scale tier
MaxScale = 1.00              ; maximum scale tier
HysteresisSec = 2.0          ; tier-change cooldown dwell (seconds)
EnableFgMode = 0             ; frame-generation target mode
FgMultiplier = 2.0           ; FG multiplier (2/3/4)
```

---

## Configuration deep dive: what each switch does & how

### Proxy & scaling (`[DLSSNR_Proxy]`)

| Key | What it does | How it works |
| --- | --- | --- |
| `EnableProxy` | Proxy master switch. 0 = every NGX call passes through untouched, byte-for-byte stock behavior | Hotkeys / panel toggle it live; switching only rewrites the scaling decision on the interception path — no D3D resources are rebuilt |
| `ResolutionScale` | Neural inference resolution. 0.75 ≈ 40% inference savings (recommended sweet spot); 0.50 = DLSS Performance ratio; 2.00 = 200% supersampling (DLDSR / photo mode) | Inference cost is proportional to pixel count. The scale applies only to the **internal buffers fed to the model**; the final image is always composited back onto the full-resolution native frame, so downscaling ≠ losing detail |
| `EnableAnamorphic` + `ResolutionScaleX/Y` | (Experimental) independent horizontal / vertical scales replace the uniform one | In widescreen scenes vertical detail matters more than horizontal; 0.65×0.85 saves ~45% more inference load with rock-solid frame pacing. Each axis is downsampled and upsampled independently |
| `EnlargementMode` | 1 = Matched Residual (recommended); 0 = Bilinear direct + RCAS | Matched Residual: native 1:1 pixels are the anchor — only the neural output's **delta (edit)** is added back, so geometry, text and UI are loss-free by construction. Bilinear: the neural output is scaled up directly and RCAS rescues sharpness — more detail loss |
| `TransferStrength` | Residual compositing strength (0 ~ 2). 1.0 = pass the neural delta through as-is; 0 = pure native image | The residual = neural output − downsampled input. Scaling it scales the delta: lower looks more "native", higher (>1) exaggerates lighting changes |
| `ColorStrength` | Chroma strength (0 ~ 1). 0 = hue-preserving luminance scaling only | The neural delta is decomposed into a luminance direction and a chroma direction: 1.0 keeps the full neural color (indirect bounce lighting / material colors), 0 keeps only the luminance delta scaled along the native pixel's chromaticity vector, eliminating neural tinting / desaturation (upstream 15f09dd hue-preserving rework) |
| `Sharpness` | Contrast-adaptive RCAS sharpening (0 ~ 1) | FidelityFX RCAS: the sharpening weight adapts to the neighborhood luminance range (weak on flat areas, strong on edges) to avoid overshoot ringing. Auto-disabled when a multi-pass pipeline writes the same buffer, preventing exponential edge ringing |
| `EnableDepthAwareResolve` | Depth-aware bilateral silhouette preservation | Samples the native depth buffer's 4 neighbors to detect geometric silhouettes; at discontinuities the low-res neural delta's blend weight is pulled down (min 0.25) so low-res radiance never bleeds across foreground edges |

### VRNR alternating-frame chain (experimental)

| Key | What it does | How it works |
| --- | --- | --- |
| `EnableAlternatingFrames` | Runs DLSS-NR inference every 2nd frame; in-between frames reuse the previous neural edit, trading for higher average FPS | Inference frames run the full chain; skip frames only run the compositing shader, applying the **cached stale edit** attenuated by a luma-difference weight. The more the stale delta misaligns with the current frame, the lower the weight (image falls back to native pixels). Trade-offs: frame-pacing sawtooth and luminance pulsing → see the three anti-flicker rows below |
| `VrnrAntiFlicker` | Anti-flicker master switch (gates the 0.6.3 triple fix + 0.7.1/0.7.3 enhancements) | See the "Skip-frame anti-flicker chain" section above. Off = byte-for-byte 0.6.2 behavior |
| `VrnrWeightFloor` | (Experimental) skip-frame edit weight floor (default 0.60, 0 = off) | Heavy motion → large luma difference → weight → 0 → the next inference frame suddenly restores full strength: a "collapse ↔ restore" pop. With a floor, moving regions keep at least 60% of the residual, so recovery frames no longer pop. Raise for steadier image, lower for less ghosting |
| `VrnrReproject` | (Experimental) motion-vector reprojection (on by default) | During camera pans the 2-frames-old neural input misaligns with the current frame, so the luma difference is falsely detected as "motion" and the whole screen fades out. This switch first **aligns** the stale input to the current frame using the game's MVec, then computes the difference; when alignment is wrong the difference is naturally larger, and the better-aligned sample wins — MV sign conventions don't matter. Falls back automatically when MVec is unavailable |
| `VrnrAdapt` | (Experimental, 0.7.3) FPS-adaptive strength master switch | At low FPS the per-frame motion is larger, skip weights fall harder, and the gap to full-strength inference frames forms a luminance pulse in the eye-sensitive 15–22 Hz band. This switch **dims the inference frames** at low FPS, closing the gap from both ends. Pure CPU-side: an EWMA-smoothed FPS produces a 0–1 dim factor passed as one shader constant — zero new GPU resources |
| `VrnrAdaptAmount` | Max dim fraction (0 ~ 1, default 0.60). 1.00 = at very low FPS the inference-frame neural edit is fully suppressed | Balances against how much skip frames already attenuate: skip frames are attenuated by the weight chain, inference frames by this factor — the smaller the gap, the smaller the pulse |
| `VrnrAdaptFpsHi` / `VrnrAdaptFpsLo` | Smoothstep dimming window (default 45 / 25) | At EWMA FPS ≥ Hi nothing changes; Hi→Lo transitions smoothly (smoothstep avoids a stepped feel); ≤ Lo reaches full `VrnrAdaptAmount` dimming |

### FPS Governor (`[Governor]`)

| Key | What it does | How it works |
| --- | --- | --- |
| `EnableGovernor` | Dynamic-resolution tiering: auto-adjusts `ResolutionScale` to hold the target FPS | Frame times pass an outlier filter (>80 ms loading screens / ≤0.5 ms discarded) then a ~30-frame EWMA smoothing; effective FPS < 95% of target for 1.0 s steps down 5%, > 115% for 3.0 s steps up 5%, each change followed by a cooldown dwell to prevent oscillation |
| `TargetFps` | Target FPS budget (30 ~ 240) | The governor only tiers between `MinScale` and `MaxScale`; outside those bounds it stops |
| `MinScale` / `MaxScale` | Scale tier bounds (0.25 ~ 2.00) | Tiers step in 5%; switching uses a multi-slot pipeline cache for 0 ms instant swaps (intermediate buffers for different scales stay resident) |
| `HysteresisSec` | Tier-change cooldown dwell (0.5 ~ 10.0 s, default 2.0) | Prevents resolution "breathing" from rapid up/down tier flipping near the threshold |
| `EnableFgMode` | Frame-generation target mode | With frame generation, display FPS = base render FPS × multiplier. Enabled, `TargetFps` is judged against **display FPS**, preventing the governor from mis-downscaling during FG gameplay |
| `FgMultiplier` | FG multiplier (2.0 / 3.0 / 4.0) | Matches DLSS 3 / FSR 3 / Lossless Scaling 2x/3x/4x; effective FPS = measured FPS × multiplier |

### Official NVIDIA NR parameters (`[DLSSNR_Settings]`)

| Key | What it does | How it works |
| --- | --- | --- |
| `UseCustomSettings` | 0 = pass through the caller's (game / OptiScaler / RenoDX) NR params; 1 = override with this section | The proxy never touches model-internal parameters by default, keeping host behavior identical; open it only for personalized denoising styles |
| `Style` | 0 = Balanced (stock) / 1 = Sharp / 2 = Cinematic | Three official preset tunings biased toward micro-contrast, edge crispness and filmic softness respectively |
| `Intensity` | Overall denoise / reconstruction strength (0 ~ 2) | Directly scales the model's internal denoising intensity parameter |
| `LocalStructureStrength` | Local structure preservation (0 ~ 2) | How strongly the model preserves high-frequency geometry / texture structure |
| `LocalToneStrength` | Local tone (0 ~ 2) | How strongly local HDR luminance contrast and micro-transitions are preserved |
| `SkinStructureStrength` | Skin structure (-1 = auto, 0 ~ 2) | At -1 the model infers skin regions via semantic heuristics and protects their texture automatically |
| `UseAutoMask` | Auto-mask for fast motion / small elements | Internal heuristic mask: fast-moving or very thin elements get less denoising to reduce smearing |

### Hotkeys (`[Hotkeys]`)

| Key | What it does | Notes |
| --- | --- | --- |
| `RequireCtrlAlt` | Require the Ctrl+Alt modifier prefix | Avoids conflicts with game bindings |
| `KeyToggleProxy` | Toggle the proxy (default Space) | Runtime switch for `EnableProxy` |
| `KeyToggleMode` | Toggle Matched Residual / Bilinear (default End) | Runtime switch for `EnlargementMode` |
| `KeyScaleUp` / `KeyScaleDown` | Raise / lower inference resolution (default PageUp / PageDown) | Runtime adjustment of `ResolutionScale` |
| `KeyToggleUI` | Toggle the in-game panel (default F11; panel toggle always requires Ctrl+Alt) | Panel position / size / theme / language persist automatically |

---

## System requirements

- Windows 10 / 11 (64-bit)
- NVIDIA RTX GPU (RTX 20 / 30 / 40 / 50 series)
- A game or plugin that uses NVIDIA DLSS-NR (`nvngx_dlssnr.dll`) through DirectX 12

## Build (x64, MSVC)

```text
build.bat
```

Runs the full pipeline: HLSL shader compilation (fxc) → version resource → proxy DLL → console → manager. Outputs land in `build\<version>\` with intermediates in `build\obj\`; the version is maintained in `build.bat` (`VERSION`) and `app_version.rc`. The companion addon builds separately: `companion\build_companion.bat`.

## FAQ

- **Game folder is under Program Files?** Run the manager as administrator, or install manually.
- **Panel doesn't receive mouse input?** The panel uses a low-level mouse hook in observer mode and never competes with the game for input; if an anti-cheat blocks it, please report.
- **Is alternating-frame VRNR worth it?** Yes — with the anti-flicker chain left at defaults (`VrnrAntiFlicker` / `VrnrWeightFloor` / `VrnrReproject` / `VrnrAdapt` all on). If a faint pulse remains at low FPS, raise `VrnrAdaptAmount` to 0.8–1.0; if the image feels washed out, lower it.
- **Crash log shows `DXGI_ERROR_DEVICE_REMOVED`?** That is a GPU driver reset (TDR), unrelated to the panel (the panel never touches the game's D3D device); check overclocking / driver versions.

## Credits & license

- Upstream algorithm: [xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler) (MIT; this repo is an enhanced fork)
- Design tokens: [creeper-qt](https://github.com/creeper5820/creeper-qt) (MIT) BlueMiku theme
- Thanks to [Dagherbou/OptiScaler_DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR), [OptiScaler](https://github.com/optiscaler/OptiScaler), [clshortfuse/RenoDX](https://github.com/clshortfuse/renodx), [AMD FidelityFX](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) (RCAS), and the [Community Shaders](https://github.com/doodlegabe/CommunityShaders) team
