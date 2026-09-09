# DLSS5-NR-Boost

<p align="center">
  <a href="README-CN.md"><b>简体中文</b></a> ｜ <b>English</b>
</p>

A standalone proxy DLL and companion toolset for NVIDIA DLSS-NR (DirectX 12) that adds **resolution scaling** and **GPU cost control**. It runs the neural reconstruction model at a reduced resolution while preserving native 1:1 geometry, fine textures, text, and edges via a high-frequency matched-residual composite shader — **decoupling DLSS-NR's GPU cost from the display resolution without introducing blur**.

Designed to work alongside RenoDX addons, and with any game, engine, or injector that calls `nvngx_dlssnr.dll` over DirectX 12. Tested with clshortfuse's DLSS addon (`renodx-dlss.addon64`).
<img width="803" height="762" alt="image" src="https://github.com/user-attachments/assets/e4f5a83d-f4a0-44fd-95b4-e2b4f10658c0" />
<img width="803" height="1008" alt="image" src="https://github.com/user-attachments/assets/34ffa791-84ae-4019-92ef-19c7df4a80d8" />
<img width="793" height="902" alt="image" src="https://github.com/user-attachments/assets/5aad0b9d-d205-4791-a1b7-73b83bd83468" />
<img width="784" height="903" alt="image" src="https://github.com/user-attachments/assets/e3f8b7e2-4823-461a-a768-4b548b5d1886" />


---

## Artifacts

| File | Purpose |
| --- | --- |
| `nvngx_dlssnr.dll` | The proxy itself (drop into the game folder; forwards to the real `nvngx_dlssnr_real.dll`) |
| `DLSS5-NR-Boost-manager.exe` | GUI manager: quick install / installed-game management / operation history / **panel debug** |
| `dlssnr_console.exe` | Standalone debug console (live sync with the proxy outside the game) |
| `nvngx_dlssnr.ini` | Configuration (hot-reloaded in-game every second) |

The manager follows **Material 3 Expressive** (light/dark themes, design tokens from the [creeper-qt](https://github.com/creeper5820/creeper-qt) BlueMiku theme pack), switches between **zh / EN / RU / 한** instantly, and is fully painted with anti-aliased GDI+ — no external dependencies.

---

## Features

### Proxy DLL (core algorithms, synced with upstream v1.0.5)

- **Drop-in proxy** for `nvngx_dlssnr.dll`.
- Pre-inference downsampling shader (**hardware bilinear + LDS tile caching + in-place copy** — minimal GPU cost).
- **High-frequency matched-residual resolve**: composites the neural delta back onto the untouched native frame.
- **Super-sampling up to 200%**: `ResolutionScale` range 0.25 ~ 2.00 (DLDSR / photo mode, 4x sample density).
- **Depth-aware bilateral silhouette preservation**: uses the native depth buffer to stop low-res neural radiance bleeding across foreground edges.
- **Anamorphic / asymmetric neural scaling** (experimental): independent horizontal / vertical scaling.
- **Alternating-frame VRNR** (experimental, off by default): evaluate DLSS-NR every 2nd frame.
- **Official NVIDIA NR params**: passes through the caller's NR settings by default (OptiScaler / RenoDX / engine); optionally override via `[DLSSNR_Settings]` (Style: Balanced / Sharp / Cinematic, Intensity, Local structure / tone, Skin structure, Auto mask).
- **HDR luminance bounding** + integrated **AMD RCAS** sharpening.
- **In-game hot-reload**: ini changes take effect within ~1 second.
- **SEH crash shield** so shader failures don't take the game down.
- SDR (B8G8R8A8 / R8G8B8A8), HDR10 PQ (R10G10B10A2), scRGB (R16G16B16A16), and 3-channel HDR (R11G11B10).
- **Dynamic sub-rect tracking** for games with dynamic resolution scaling.

### In-game overlay panel

- `Ctrl+Alt+F11` summons a topmost panel: M3E sliders, Matched/Bilinear segments, toggles — fully mouse-driven, the game never stops;
- The panel never steals focus or touches the game's input pipeline (low-level mouse hook observer); the game recovers the instant it closes;
- Panel position is remembered; zh / EN / RU / 한 switchable from the header.

### DLSS5-NR-Boost manager (GUI)

1. **Quick Install**: auto-detects the proxy package → scans your game library (recursive, skips recycle bins / symlinks) → tick targets → **one-click install** (renames the stock DLL to `nvngx_dlssnr_real.dll`, copies proxy + ini) / **one-click uninstall** (fully restores);
2. **Installed games**: restores remembered installs, batch uninstall, status refresh, double-click to open the folder;
3. **History**: full install/uninstall audit trail (`dlssnr_manager_history.log`) plus a runtime log with a pop-out window;
4. **Panel debug**: edits the proxy package's `nvngx_dlssnr.ini` with native BlueMiku controls — every toggle, resolve-mode segments, sliders (super-sampling 25–200%), anamorphic X/Y, depth-aware, VRNR, the **NVIDIA NR settings block**, and **Ctrl+Alt-combo hotkey rebinding**; changes auto-commit with a 500 ms debounce.

Also: `F11` fullscreen, freely resizable adaptive layout, persisted language/theme, double-click rows to open folders.

---

## Requirements

- Windows 10 / 11 (64-bit)
- NVIDIA RTX GPU (RTX 20 / 30 / 40 / 50 series)
- A game or plugin that uses NVIDIA DLSS-NR (`nvngx_dlssnr.dll`) over DirectX 12

---

## Build (x64, MSVC)

```text
build.bat
```

Produces `nvngx_dlssnr.dll` (proxy), `dlssnr_console.exe` (debug console), and `DLSS5-NR-Boost-manager.exe` (manager, with the embedded icon from `app.ico` / `app.rc`).

---

## Installation (manager recommended)

1. Put `DLSS5-NR-Boost-manager.exe` next to the proxy `nvngx_dlssnr.dll` and `nvngx_dlssnr.ini` (auto-detected);
2. Open the manager → **Quick Install** → pick a game folder or library root → Scan → tick → **Install**;
3. Launch the game and press `Ctrl+Alt+F11` for the panel; or tweak settings from the manager's **Panel debug** page (the game hot-reloads the ini automatically).

<details>
<summary>Manual installation (equivalent)</summary>

1. In the game folder, rename the stock `nvngx_dlssnr.dll` to `nvngx_dlssnr_real.dll`;
2. Copy the proxy `nvngx_dlssnr.dll` and `nvngx_dlssnr.ini` into the same folder;
3. *(Optional)* Drop `dlssnr_console.exe` alongside for out-of-game tuning;
4. *(ReShade users)* Copy `dlssnr-companion.addon64` into the game folder for a live panel in the ReShade Home menu;
5. Launch the game and press `Ctrl+Alt+F11`.

</details>

---

## Configuration (`nvngx_dlssnr.ini`)

Read at startup and hot-reloaded every second:

```ini
[DLSSNR_Proxy]
EnableProxy = 1          ; 1 = proxy active; 0 = native passthrough
ResolutionScale = 0.75   ; inference scale (0.25 ~ 2.00; 2.00 = 200% super-sampling)
EnableAnamorphic = 0     ; experimental asymmetric scaling (uses X/Y below)
ResolutionScaleX = 0.65  ; horizontal scale
ResolutionScaleY = 0.85  ; vertical scale
EnlargementMode = 1      ; 1 = Matched Residual; 0 = Bilinear Direct
TransferStrength = 1.00  ; residual composite strength (0 ~ 2)
ColorStrength = 1.00     ; color strength (0 ~ 1)
Sharpness = 0.20         ; RCAS sharpening (0 ~ 1)
EnableDepthAwareResolve = 1 ; depth-aware silhouette preservation
EnableAlternatingFrames = 0 ; alternating-frame VRNR (experimental)
EnableHotkeys = 1        ; in-game hotkeys master switch
EnableUi = 1             ; in-game panel master switch
UiLanguage = 0           ; panel language: 0 zh / 1 en / 2 ru / 3 ko

[DLSSNR_Settings]
UseCustomSettings = 0    ; 0 = pass through caller's NR params; 1 = override below
Style = 0                ; 0 Balanced / 1 Sharp / 2 Cinematic
Intensity = 1.00         ; reconstruction intensity (0 ~ 2)
LocalStructureStrength = 1.00
LocalToneStrength = 1.00
SkinStructureStrength = -1.00  ; -1 = auto
UseAutoMask = 0

[Hotkeys]
RequireCtrlAlt = 1
KeyToggleProxy = 32
KeyToggleMode = 35
KeyScaleUp = 33
KeyScaleDown = 34
KeyToggleUI = 123        ; panel toggle (default F12)
```

> Everything above can be edited visually on the **Panel debug** page of `DLSS5-NR-Boost-manager.exe`.

---

## FAQ

- **Game folder under Program Files?** Run the manager as administrator, or install manually.
- **Panel gets no mouse input?** It uses an observer-only low-level mouse hook; report it if an anti-cheat interferes.
- **Crash log shows `DXGI_ERROR_DEVICE_REMOVED`?** That's a GPU driver reset (TDR) — unrelated to the panel (it never touches the game's D3D device). Check overclocking / drivers.
- **Is VRNR worth it?** Camera pans show 30 Hz sawtooth judder and ray-traced games may flicker — keep it off for smoothest gameplay.

---

## Credits & License

- Upstream algorithms: [xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler) (this repo is the Chinese-enhanced fork)
- Design tokens: [creeper-qt](https://github.com/creeper5820/creeper-qt) (MIT)
