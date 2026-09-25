// ================================================================================================
// dlssnr_shared.h — 三端共享内存协议（唯一事实来源）
// ================================================================================================
// 使用方：proxy_main.cpp（游戏内，读端 + INI 兜底）、dlssnr_console.cpp、
//         dlssnr_manager.cpp（两端均为写端）。命名管道式共享内存
//         Local\DLSSNR_Config_Shared_v1，写入方递增 version 触发读端热更新。
//
// ⚠️ 修改本结构体的规则：
//   1. 默认只允许在结构体【末尾】追加字段（共享内存按二进制布局映射，改中间
//      字段 = 三端版本错位时读到垃圾数据）。padding 保持 4 字节对齐。
//      例外：与上游共享的字段块（如 0.7.0 的 Governor 组）按上游顺序插入，
//      但必须同一次构建里同步重编全部四端（proxy/console/manager/companion）。
//   2. 任何字段增删必须同步四处：proxy_main.cpp 轮询/推送、proxy_ui.h 的
//      UiValues 桥接、dlssnr_console.cpp 的 LoadIni/SaveIni/PushShared/
//      AdoptFromShared/ClampAll、dlssnr_manager.cpp 的 PCfg + 调试页。
//   3. ini 键名与字段一一对应，CHANGELOG 记录新键。
// ================================================================================================

#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

#define DLSSNR_SHARED_MEM_NAME L"Local\\DLSSNR_Config_Shared_v1"
#define DLSSNR_MAGIC 0x524E5344 // 'DSNR'

#pragma pack(push, 4)
struct DlssnrSharedConfig {
    uint32_t magic;            // DLSSNR_MAGIC
    uint32_t version;          // Monotonically increasing version counter
    uint32_t enableProxy;      // 1 = Active, 0 = Bypassed
    float    resolutionScale;  // 0.25 to 1.00
    uint32_t enlargementMode;  // 1 = Matched Residual, 0 = Bilinear Direct
    float    transferStrength; // 0.0 to 2.0
    float    colorStrength;    // 0.0 to 1.0
    float    sharpness;        // 0.0 to 1.0
    uint32_t enableHotkeys;    // 1 or 0
    uint32_t requireCtrlAlt;   // 1 or 0
    uint32_t keyToggleProxy;
    uint32_t keyToggleMode;
    uint32_t keyScaleUp;
    uint32_t keyScaleDown;
    uint32_t writerSource;     // 1 = Companion UI, 2 = Proxy/Hotkey, 3 = Disk INI

    // Frame Alternation / VRNR (Variable Rate Neural Reconstruction)
    uint32_t enableVrnr;               // 0 = Off (every frame), 1 = On (alternate frames)
    uint32_t enableDepthAware;         // 0 = Off, 1 = On (Depth-Aware Bilateral Silhouette Preservation)

    // Anamorphic / Asymmetric Neural Scaling
    uint32_t enableAnamorphic;         // 0 = Off (uniform scale), 1 = On (asymmetric scale)
    float    scaleX;                   // Horizontal scale (0.25 to 2.00)
    float    scaleY;                   // Vertical scale (0.25 to 2.00)

    // Official DLSS-NR Model Settings
    uint32_t nrStyle;                  // 0 = Balanced, 1 = Sharp, 2 = Cinematic
    float    nrIntensity;              // 0.0 to 2.0 (Default 1.0)
    float    nrLocalStructureStrength; // 0.0 to 2.0 (Default 1.0)
    float    nrLocalToneStrength;      // 0.0 to 2.0 (Default 1.0)
    float    nrSkinStructureStrength;  // -1.0 to 2.0 (-1.0 = Auto)
    uint32_t nrUseAutoMask;            // 0 = Off, 1 = On
    uint32_t useCustomNR;              // 0 = Passthrough caller's NR params, 1 = Override with proxy values

    // Dynamic FPS Budget Scale Governor（上游 2026-09-12 同步）
    uint32_t enableGovernor;            // 0 = Off, 1 = On
    float    governorTargetFps;         // e.g. 60.0f (30-240)
    float    governorMinScale;          // e.g. 0.50f (0.25-2.00)
    float    governorMaxScale;          // e.g. 1.00f (0.25-2.00)
    float    governorHysteresisSec;     // e.g. 2.0f (0.5-10.0)
    uint32_t governorCurrentTier;       // Active tier index (0.05 steps above minScale)
    float    debugMeasuredFps;          // Live smoothed FPS (EWMA)
    float    debugMeasuredFrameTimeMs;  // Live smoothed frame time in ms
    uint32_t debugGovernorState;        // 0=Disabled, 1=Stable, 2=Cooldown, 3=Downscaling, 4=Upscaling
    float    debugGovernorCooldownLeft; // Seconds remaining in dwell cooldown
    uint32_t enableGovernorFgMode;      // 0 = Off (Native Base FPS), 1 = On (FrameGen Display FPS)
    float    governorFgMultiplier;      // e.g. 2.0f, 3.0f, 4.0f (1.0-10.0, Default 2.0f)
    float    debugEffectiveFps;         // Live effective FPS (after FG multiplier if enabled)

    // Overlay panel & UI sync (CN fork)
    uint32_t enableUi;                 // 0 = Off, 1 = On
    uint32_t keyToggleUi;              // VK code
    uint32_t uiLanguage;               // 0 zh / 1 EN / 2 RU / 3 ko

    // Telemetry & Diagnostics
    uint32_t debugNativeW;
    uint32_t debugNativeH;
    uint32_t debugWorkW;
    uint32_t debugWorkH;
    uint32_t debugFormat;
    uint32_t debugHasDepth;
    uint32_t debugDepthW;
    uint32_t debugDepthH;
    uint32_t debugHasMVec;
    uint32_t debugMvW;
    uint32_t debugMvH;
    uint32_t debugActiveSlot;
    uint32_t debugVrnrSkippedThisFrame;// 1 if real_Evaluate was skipped this frame

    // CN fork 0.6.3: VRNR anti-flicker (temporal ramp + spatial-smooth weight
    // + symmetric highlight guard on skip frames). 0 = exact 0.6.2 behavior.
    uint32_t vrnrAntiFlicker;          // 0 = Off, 1 = On (default)

    // CN fork 0.7.1: VRNR anti-flicker v2 (experimental)
    float    vrnrWeightFloor;          // 0.0-1.0 skip-frame edit weight floor (default 0.60)
    uint32_t vrnrReproject;            // 1 = MV-aligned stale input sampling on skip frames

    // CN fork 0.7.3: frame-time adaptive strength (experimental, CPU-side only,
    // no new GPU resources). Dims inference-frame edit at low FPS so the visual
    // gap to skip frames shrinks (anti-pulse). 0 = exact 0.7.1 behavior.
    uint32_t vrnrAdapt;                // 0 = Off, 1 = On (default 1)
    float    vrnrAdaptAmount;          // 0.0-1.0 max dim fraction at low fps (default 0.60)
    float    vrnrAdaptFpsHi;           // start dimming below this FPS (default 45)
    float    vrnrAdaptFpsLo;           // full dim below this FPS (default 25)
};
#pragma pack(pop)

