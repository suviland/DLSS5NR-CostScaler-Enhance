// ================================================================================================
// proxy_main.cpp — nvngx_dlssnr.dll 代理本体（本项目核心）
// ================================================================================================
// 职责：
//   1. 以「代理 DLL」身份被游戏加载（文件名 nvngx_dlssnr.dll），同目录的
//      nvngx_dlssnr_real.dll 是 NVIDIA 原版，由 forwarders.h 负责加载与函数转发。
//   2. Hook NGX 入口（Init/CreateFeature/Evaluate/ReleaseFeature），把游戏的
//      DLSS-NR 输入帧降采样到 work 分辨率（省算力），跑完 NR 后用
//      CS_Resolve 着色器把神经网络的「编辑量」合成回原生分辨率输出。
//   3. INI（nvngx_dlssnr.ini）配置 + 共享内存（dlssnr_shared.h）三端联动 +
//      热键 + 内嵌游戏面板（proxy_ui.cpp / ui_panel.h）。
//
// 关键流程（EvaluateInternal）：
//   游戏提交原生帧 → CS_Downsample 降采样到 colorSmall → 真实 NR（work 分辨率）
//   → CS_Resolve 用 outputSmall 与 colorSmall 的差值（edit）叠加回 origOutput。
//   isSkipFrame（隔帧推理 VRNR）时跳过 NR，直接用陈旧编辑量衰减淡出。
//
// 崩溃防线（重要教训，勿删）：
//   - 所有 real_* 调用点的失败分支绝不能用「游戏手里已被 park 的旧句柄」转发
//     real_Evaluate——那是 use-after-free（0.7.x 时代的 0xC0000005 血案）。
//   - ParkNrFeature 只是把句柄挂进延迟回收列表；ReleaseFeature 可能对同一
//     句柄再次调用，注意不要双重 real_Release。
//
// 日志：nvngx_dlssnr_proxy.log 写在 DLL 所在目录；排查游戏崩溃先看它。
// 上游基线：算法层与上游 DLSSNR-Cost-Scaler main 分支一致（_upstream_ref/ 有参照）。
// ================================================================================================

#include <mutex>
#include <vector>
#include <unordered_map>
#include "forwarders.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <stdio.h>
#include <math.h>
#include <atomic>
#include <string>

#include "nvsdk_ngx_params.h"

#include "Downsample_Shader.h"
#include "Resolve_Shader.h"
#include "dlssnr_shared.h"
#include "ui_panel.h"
#include "proxy_ui.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

static bool EnsureRealModuleLoaded();

static void GetCurrentModulePath(wchar_t* outPath, DWORD maxLen) {
    HMODULE hSelf = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)&EnsureRealModuleLoaded, &hSelf) && hSelf) {
        GetModuleFileNameW(hSelf, outPath, maxLen);
    } else {
        GetModuleFileNameW(GetModuleHandleW(L"nvngx_dlssnr.dll"), outPath, maxLen);
    }
}

static FILE* g_logFile = nullptr;

static void Log(const char* fmt, ...) {
    if (!g_logFile) {
        wchar_t modulePath[MAX_PATH] = { 0 };
        GetCurrentModulePath(modulePath, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(modulePath, L'\\');
        if (lastSlash) *(lastSlash + 1) = L'\0';
        wcscat_s(modulePath, L"nvngx_dlssnr_proxy.log");
        g_logFile = _wfsopen(modulePath, L"w", _SH_DENYNO);
        if (!g_logFile) {
            wchar_t tempPath[MAX_PATH] = { 0 };
            GetTempPathW(MAX_PATH, tempPath);
            wcscat_s(tempPath, L"nvngx_dlssnr_proxy.log");
            g_logFile = _wfsopen(tempPath, L"w", _SH_DENYNO);
        }
    }
    if (g_logFile) {
        va_list args;
        va_start(args, fmt);
        vfprintf(g_logFile, fmt, args);
        va_end(args);
        fprintf(g_logFile, "\n");
        fflush(g_logFile);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 运行时配置（原子变量）：游戏渲染线程每帧读取，UI/共享内存线程热更新。
// 字段与 dlssnr_shared.h 的 DlssnrSharedConfig 一一对应；ini 键见 LoadConfig()。
// ⚠️ 新增设置项：这里 + DlssnrSharedConfig + LoadConfig + PushProxyToSharedMemory
//    + UiPullConfig/UiApplyLive/UiCommitConfig 五处同步（详见 ui_panel.h 维护速查）。
// ─────────────────────────────────────────────────────────────────────────────
static std::atomic<bool>     g_enableProxy(true);
static std::atomic<float>    g_scale(0.75f);
static std::atomic<bool>     g_enableAnamorphic(false);
static std::atomic<float>    g_scaleX(0.65f);
static std::atomic<float>    g_scaleY(0.85f);
static std::atomic<uint32_t> g_enlargementMode(1);     // 1 = Matched Residual, 0 = Classic Bilinear
static std::atomic<float>    g_transferStrength(1.0f); // 0.0 to 2.0
static std::atomic<float>    g_sharpness(0.20f);       // 0.0 to 1.0 (RCAS)
static std::atomic<float>    g_colorStrength(1.00f);   // 0.0 to 1.0 (Issue #5)
static std::atomic<bool>     g_enableHotkeys(true);
static std::atomic<bool>     g_requireCtrlAlt(true);
static std::atomic<int>      g_keyToggleProxy(VK_SPACE);
static std::atomic<int>      g_keyToggleMode(VK_END);
static std::atomic<int>      g_keyScaleUp(VK_PRIOR);
static std::atomic<int>      g_keyScaleDown(VK_NEXT);
static std::atomic<bool>     g_enableUi(true);       // allow Ctrl+Alt+F11 overlay toggle
static std::atomic<int>      g_keyToggleUI(VK_F11);  // base key of the overlay toggle combo
static std::atomic<int>      g_uiLanguage((int)dlssnr_ui::L_ZH);  // UI display language (zh/en/ru/ko)
static std::atomic<bool>     g_enableVrnr(false);
static std::atomic<bool>     g_enableDepthAware(true);
static std::atomic<bool>     g_vrnrAntiFlicker(true);  // 0.6.3 跳帧防闪烁总开关
// 0.7.1 VRNR 防闪烁 v2（实验）：
//   权重下限——skip 帧 edit 权重不再低于该值（运动区不整段归零，消除恢复帧突变）
//   MV 重投影——用游戏运动矢量把 2 帧前的神经输入对齐到本帧位置再做亮度差，
//   消除「错位画面误判运动」导致的整屏淡出脉冲。符号约定未知，着色器按
//   对齐后亮度差更小者自动择优，无需关心 MV 正负号。
static std::atomic<float>    g_vrnrWeightFloor(0.60f);
static std::atomic<bool>     g_vrnrReproject(true);
// 0.7.3 帧时自适应强度（实验）：低帧率时按 EWMA 帧率衰减推理帧 edit 强度，
// 缩小推理帧（满强度）与 skip 帧（陈旧 edit×低权重）的视觉差，消除脉动。
// 纯 CPU 侧计算，无新增 GPU 资源；关闭时 gAdaptDim 恒为 0（与 0.7.1 行为一致）。
static std::atomic<bool>     g_vrnrAdapt(true);
static std::atomic<float>    g_vrnrAdaptAmount(0.60f);
static std::atomic<float>    g_vrnrAdaptFpsHi(45.0f);
static std::atomic<float>    g_vrnrAdaptFpsLo(25.0f);
// Dynamic FPS Budget Scale Governor（上游 2026-09-12 b09bcca + c7e9b0b 同步）：
// 以 5% 离散档位自动升降分辨率，维持目标帧率；FG 模式按「显示帧率 = 基础 × 倍率」判定。
static std::atomic<bool>     g_enableGovernor(false);
static std::atomic<float>    g_governorTargetFps(60.0f);
static std::atomic<float>    g_governorMinScale(0.50f);
static std::atomic<float>    g_governorMaxScale(1.00f);
static std::atomic<float>    g_governorHysteresisSec(2.0f);
static std::atomic<uint32_t> g_governorCurrentTier(0);
static std::atomic<bool>     g_enableGovernorFgMode(false);
static std::atomic<float>    g_governorFgMultiplier(2.0f);
static std::atomic<uint32_t> g_nrStyle(0);
static std::atomic<float>    g_nrIntensity(1.00f);
static std::atomic<float>    g_nrLocalStructureStrength(1.00f);
static std::atomic<float>    g_nrLocalToneStrength(1.00f);
static std::atomic<float>    g_nrSkinStructureStrength(-1.00f);
static std::atomic<uint32_t> g_nrUseAutoMask(0);
static std::atomic<bool>     g_useCustomNR(false);    // false = passthrough caller's NR params
static wchar_t               g_iniPath[MAX_PATH] = { 0 };
static FILETIME              g_lastIniWriteTime = { 0 };

static HANDLE              g_hProxySharedMem = nullptr;
static DlssnrSharedConfig* g_proxySharedConfig = nullptr;
static uint32_t            s_lastProxySharedVersion = 0;

// Serialises INI persistence + shared-memory pushes between the render thread
// (hotkeys / hot-reload) and the overlay UI thread (panel commits).
static std::recursive_mutex g_cfgLock;

static void PushProxyToSharedMemory();   // defined below — used to seed a fresh mapping
static void FlushSecondaryTierSlots();   // defined below — Governor 关闭时回收档位槽位

static void InitProxySharedMemory() {
    if (g_proxySharedConfig) return;
    g_hProxySharedMem = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(DlssnrSharedConfig), DLSSNR_SHARED_MEM_NAME);
    if (g_hProxySharedMem) {
        DWORD createErr = GetLastError();
        g_proxySharedConfig = (DlssnrSharedConfig*)MapViewOfFile(g_hProxySharedMem, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(DlssnrSharedConfig));
        if (g_proxySharedConfig && (createErr != ERROR_ALREADY_EXISTS || g_proxySharedConfig->magic != DLSSNR_MAGIC)) {
            // We own a fresh or stale/unseeded mapping — seed it from the live
            // atomics so a standalone console / companion launched later always
            // finds valid data (and UI commits that happened before any feature
            // call are not lost).
            ZeroMemory(g_proxySharedConfig, sizeof(DlssnrSharedConfig));
            g_proxySharedConfig->magic = DLSSNR_MAGIC;
            PushProxyToSharedMemory();
        }
    }
}

static void SaveConfigValueEx(const wchar_t* section, const wchar_t* key, const wchar_t* value) {
    if (g_iniPath[0] == L'\0') return;
    std::lock_guard<std::recursive_mutex> cfgLock(g_cfgLock);
    WritePrivateProfileStringW(section, key, value, g_iniPath);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, g_iniPath);
    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExW(g_iniPath, GetFileExInfoStandard, &fileInfo)) {
        g_lastIniWriteTime = fileInfo.ftLastWriteTime;
    }
}

static void SaveConfigValue(const wchar_t* key, const wchar_t* value) {
    SaveConfigValueEx(L"DLSSNR_Proxy", key, value);
}

// Governor 专用持久化（独立 [Governor] 区段，键名与上游一致）。
static void SaveGovernorConfigValue(const wchar_t* key, const wchar_t* value) {
    SaveConfigValueEx(L"Governor", key, value);
}

static void PushProxyToSharedMemory();

// LoadConfig：从 DLL 同目录的 nvngx_dlssnr.ini 读全部配置（启动时一次）。
// 运行中的热更新走 CheckConfigHotReload()（共享内存 version 变化时重读）。
static void LoadConfig() {
    if (g_iniPath[0] == L'\0') {
        wchar_t exePath[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) *(lastSlash + 1) = L'\0';
        wcscat_s(exePath, L"nvngx_dlssnr.ini");
        if (GetFileAttributesW(exePath) != INVALID_FILE_ATTRIBUTES) {
            wcscpy_s(g_iniPath, exePath);
        } else {
            GetCurrentModulePath(g_iniPath, MAX_PATH);
            wchar_t* lastSlash2 = wcsrchr(g_iniPath, L'\\');
            if (lastSlash2) *(lastSlash2 + 1) = L'\0';
            wcscat_s(g_iniPath, L"nvngx_dlssnr.ini");
        }
    }

    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExW(g_iniPath, GetFileExInfoStandard, &fileInfo)) {
        g_lastIniWriteTime = fileInfo.ftLastWriteTime;
    }

    wchar_t scaleBuf[64] = { 0 };
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"ResolutionScale", L"0.75", scaleBuf, 64, g_iniPath);
    float val = (float)_wtof(scaleBuf);
    if (val < 0.25f) val = 0.25f;
    if (val > 2.00f) val = 2.00f;
    g_scale.store(val);

    g_enableAnamorphic.store(GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableAnamorphic", 0, g_iniPath) != 0);

    wchar_t scaleXBuf[64] = { 0 };
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"ResolutionScaleX", L"0.65", scaleXBuf, 64, g_iniPath);
    float sxVal = (float)_wtof(scaleXBuf);
    if (sxVal < 0.25f) sxVal = 0.25f;
    if (sxVal > 2.00f) sxVal = 2.00f;
    g_scaleX.store(sxVal);

    wchar_t scaleYBuf[64] = { 0 };
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"ResolutionScaleY", L"0.85", scaleYBuf, 64, g_iniPath);
    float syVal = (float)_wtof(scaleYBuf);
    if (syVal < 0.25f) syVal = 0.25f;
    if (syVal > 2.00f) syVal = 2.00f;
    g_scaleY.store(syVal);

    g_enableProxy.store(GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableProxy", 1, g_iniPath) != 0);
    g_enableHotkeys.store(GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableHotkeys", 1, g_iniPath) != 0);
    g_enableUi.store(GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableUi", 1, g_iniPath) != 0);

    g_enlargementMode.store((uint32_t)GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnlargementMode", 1, g_iniPath));

    wchar_t transferBuf[64] = { 0 };
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"TransferStrength", L"1.00", transferBuf, 64, g_iniPath);
    float tVal = (float)_wtof(transferBuf);
    if (tVal < 0.0f) tVal = 0.0f;
    if (tVal > 2.0f) tVal = 2.0f;
    g_transferStrength.store(tVal);

    wchar_t sharpBuf[64] = { 0 };
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"Sharpness", L"0.20", sharpBuf, 64, g_iniPath);
    float sVal = (float)_wtof(sharpBuf);
    if (sVal < 0.0f) sVal = 0.0f;
    if (sVal > 1.0f) sVal = 1.0f;
    g_sharpness.store(sVal);

    wchar_t colorBuf[64] = { 0 };
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"ColorStrength", L"1.00", colorBuf, 64, g_iniPath);
    float cVal = (float)_wtof(colorBuf);
    if (cVal < 0.0f) cVal = 0.0f;
    if (cVal > 1.0f) cVal = 1.0f;
    g_colorStrength.store(cVal);

    g_requireCtrlAlt.store(GetPrivateProfileIntW(L"Hotkeys", L"RequireCtrlAlt", 1, g_iniPath) != 0);
    g_keyToggleProxy.store(GetPrivateProfileIntW(L"Hotkeys", L"KeyToggleProxy", VK_SPACE, g_iniPath));
    g_keyToggleMode.store(GetPrivateProfileIntW(L"Hotkeys", L"KeyToggleMode",  VK_END,   g_iniPath));
    g_keyScaleUp.store(GetPrivateProfileIntW(L"Hotkeys", L"KeyScaleUp",     VK_PRIOR, g_iniPath));
    g_keyScaleDown.store(GetPrivateProfileIntW(L"Hotkeys", L"KeyScaleDown",   VK_NEXT,  g_iniPath));
    g_keyToggleUI.store(GetPrivateProfileIntW(L"Hotkeys", L"KeyToggleUI", VK_F11, g_iniPath));
    g_uiLanguage.store(GetPrivateProfileIntW(L"DLSSNR_Proxy", L"UiLanguage", (int)dlssnr_ui::L_ZH, g_iniPath));
    if (g_uiLanguage.load() < 0 || g_uiLanguage.load() >= dlssnr_ui::L_COUNT) g_uiLanguage.store((int)dlssnr_ui::L_ZH);
    dlssnr_ui::SetLang(g_uiLanguage.load());  // keep the global display language in sync

    Log("[Proxy] Config loaded: EnableProxy = %d, ResolutionScale = %.2f, EnlargementMode = %u, TransferStrength = %.2f, Sharpness = %.2f, ColorStrength = %.2f, EnableHotkeys = %d, EnableUi = %d, UiLanguage = %d",
        g_enableProxy.load() ? 1 : 0, val, g_enlargementMode.load(), g_transferStrength.load(), g_sharpness.load(), g_colorStrength.load(), g_enableHotkeys.load() ? 1 : 0, g_enableUi.load() ? 1 : 0, g_uiLanguage.load());
    g_enableVrnr.store(GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableAlternatingFrames", 0, g_iniPath) != 0);
    g_enableDepthAware.store(GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableDepthAwareResolve", 1, g_iniPath) != 0);
    g_vrnrAntiFlicker.store(GetPrivateProfileIntW(L"DLSSNR_Proxy", L"VrnrAntiFlicker", 1, g_iniPath) != 0);
    {
        wchar_t floorBuf[64];
        GetPrivateProfileStringW(L"DLSSNR_Proxy", L"VrnrWeightFloor", L"0.60", floorBuf, 64, g_iniPath);
        float floorVal = (float)_wtof(floorBuf);
        if (floorVal < 0.0f) floorVal = 0.0f;
        if (floorVal > 1.0f) floorVal = 1.0f;
        g_vrnrWeightFloor.store(floorVal);
    }
    g_vrnrReproject.store(GetPrivateProfileIntW(L"DLSSNR_Proxy", L"VrnrReproject", 1, g_iniPath) != 0);
    {
        // 0.7.3 帧时自适应强度（实验）
        wchar_t adaptBuf[64];
        GetPrivateProfileStringW(L"DLSSNR_Proxy", L"VrnrAdaptAmount", L"0.60", adaptBuf, 64, g_iniPath);
        float amt = (float)_wtof(adaptBuf);
        if (amt < 0.0f) amt = 0.0f;
        if (amt > 1.0f) amt = 1.0f;
        g_vrnrAdaptAmount.store(amt);
        GetPrivateProfileStringW(L"DLSSNR_Proxy", L"VrnrAdaptFpsHi", L"45", adaptBuf, 64, g_iniPath);
        float fpsHi = (float)_wtof(adaptBuf);
        GetPrivateProfileStringW(L"DLSSNR_Proxy", L"VrnrAdaptFpsLo", L"25", adaptBuf, 64, g_iniPath);
        float fpsLo = (float)_wtof(adaptBuf);
        if (fpsHi < 10.0f)  fpsHi = 10.0f;
        if (fpsHi > 240.0f) fpsHi = 240.0f;
        if (fpsLo < 5.0f)   fpsLo = 5.0f;
        if (fpsLo > fpsHi - 1.0f) fpsLo = fpsHi - 1.0f;
        g_vrnrAdaptFpsHi.store(fpsHi);
        g_vrnrAdaptFpsLo.store(fpsLo);
        g_vrnrAdapt.store(GetPrivateProfileIntW(L"DLSSNR_Proxy", L"VrnrAdapt", 1, g_iniPath) != 0);
    }

    g_nrStyle.store((uint32_t)GetPrivateProfileIntW(L"DLSSNR_Settings", L"Style", 0, g_iniPath));
    wchar_t nrBuf[64] = { 0 };
    GetPrivateProfileStringW(L"DLSSNR_Settings", L"Intensity", L"1.00", nrBuf, 64, g_iniPath);
    g_nrIntensity.store((float)_wtof(nrBuf));

    GetPrivateProfileStringW(L"DLSSNR_Settings", L"LocalStructureStrength", L"1.00", nrBuf, 64, g_iniPath);
    g_nrLocalStructureStrength.store((float)_wtof(nrBuf));

    GetPrivateProfileStringW(L"DLSSNR_Settings", L"LocalToneStrength", L"1.00", nrBuf, 64, g_iniPath);
    g_nrLocalToneStrength.store((float)_wtof(nrBuf));

    GetPrivateProfileStringW(L"DLSSNR_Settings", L"SkinStructureStrength", L"-1.00", nrBuf, 64, g_iniPath);
    g_nrSkinStructureStrength.store((float)_wtof(nrBuf));

    g_nrUseAutoMask.store((uint32_t)GetPrivateProfileIntW(L"DLSSNR_Settings", L"UseAutoMask", 0, g_iniPath));
    g_useCustomNR.store(GetPrivateProfileIntW(L"DLSSNR_Settings", L"UseCustomSettings", 0, g_iniPath) != 0);

    // [Governor] 动态帧率预算调节器（上游同步；默认全关，行为与未同步前一致）
    g_enableGovernor.store(GetPrivateProfileIntW(L"Governor", L"EnableGovernor", 0, g_iniPath) != 0);

    wchar_t govBuf[64] = { 0 };
    GetPrivateProfileStringW(L"Governor", L"TargetFps", L"60.0", govBuf, 64, g_iniPath);
    float targetFps = (float)_wtof(govBuf);
    if (targetFps < 30.0f) targetFps = 30.0f;
    if (targetFps > 240.0f) targetFps = 240.0f;
    g_governorTargetFps.store(targetFps);

    GetPrivateProfileStringW(L"Governor", L"MinScale", L"0.50", govBuf, 64, g_iniPath);
    float govMinScale = (float)_wtof(govBuf);
    if (govMinScale < 0.25f) govMinScale = 0.25f;
    if (govMinScale > 2.00f) govMinScale = 2.00f;
    g_governorMinScale.store(govMinScale);

    GetPrivateProfileStringW(L"Governor", L"MaxScale", L"1.00", govBuf, 64, g_iniPath);
    float govMaxScale = (float)_wtof(govBuf);
    if (govMaxScale < govMinScale) govMaxScale = govMinScale;
    if (govMaxScale > 2.00f) govMaxScale = 2.00f;
    g_governorMaxScale.store(govMaxScale);

    GetPrivateProfileStringW(L"Governor", L"HysteresisSec", L"2.0", govBuf, 64, g_iniPath);
    float hystSec = (float)_wtof(govBuf);
    if (hystSec < 0.5f) hystSec = 0.5f;
    if (hystSec > 10.0f) hystSec = 10.0f;
    g_governorHysteresisSec.store(hystSec);

    g_enableGovernorFgMode.store(GetPrivateProfileIntW(L"Governor", L"EnableFgMode", 0, g_iniPath) != 0);

    GetPrivateProfileStringW(L"Governor", L"FgMultiplier", L"2.0", govBuf, 64, g_iniPath);
    float fgMult = (float)_wtof(govBuf);
    if (fgMult < 1.0f) fgMult = 1.0f;
    if (fgMult > 10.0f) fgMult = 10.0f;
    g_governorFgMultiplier.store(fgMult);

    Log("[Proxy] Config loaded: EnableProxy = %d, ResolutionScale = %.2f (Anamorphic=%d, ScaleX=%.2f, ScaleY=%.2f), EnlargementMode = %u, TransferStrength = %.2f, Sharpness = %.2f, ColorStrength = %.2f, EnableVrnr = %d, DepthAware = %d, UseCustomNR = %d, Style = %u, Intensity = %.2f, Governor = %d (Target=%.0f FPS, Min=%.2f, Max=%.2f, Hyst=%.1fs, FGMode=%d, FGMult=%.1fx)",
        g_enableProxy.load() ? 1 : 0, val, g_enableAnamorphic.load() ? 1 : 0, g_scaleX.load(), g_scaleY.load(), g_enlargementMode.load(), g_transferStrength.load(), g_sharpness.load(), g_colorStrength.load(), g_enableVrnr.load() ? 1 : 0, g_enableDepthAware.load() ? 1 : 0, g_useCustomNR.load() ? 1 : 0, g_nrStyle.load(), g_nrIntensity.load(),
        g_enableGovernor.load() ? 1 : 0, g_governorTargetFps.load(), g_governorMinScale.load(), g_governorMaxScale.load(), g_governorHysteresisSec.load(),
        g_enableGovernorFgMode.load() ? 1 : 0, g_governorFgMultiplier.load());

    PushProxyToSharedMemory();
}

// PushProxyToSharedMemory：把当前原子变量快照写进共享内存（writerSource=0=proxy）。
// console / 管理器在「采用」时以 proxy 的值为准。三端字段同步规则见 dlssnr_shared.h 头。
static void PushProxyToSharedMemory() {
    if (!g_proxySharedConfig || g_proxySharedConfig->magic != DLSSNR_MAGIC) return;
    std::lock_guard<std::recursive_mutex> cfgLock(g_cfgLock);
    g_proxySharedConfig->enableProxy = g_enableProxy.load() ? 1 : 0;
    g_proxySharedConfig->resolutionScale = g_scale.load();
    g_proxySharedConfig->enableAnamorphic = g_enableAnamorphic.load() ? 1 : 0;
    g_proxySharedConfig->scaleX = g_scaleX.load();
    g_proxySharedConfig->scaleY = g_scaleY.load();
    g_proxySharedConfig->enlargementMode = g_enlargementMode.load();
    g_proxySharedConfig->transferStrength = g_transferStrength.load();
    g_proxySharedConfig->colorStrength = g_colorStrength.load();
    g_proxySharedConfig->sharpness = g_sharpness.load();
    g_proxySharedConfig->enableHotkeys = g_enableHotkeys.load() ? 1 : 0;
    g_proxySharedConfig->requireCtrlAlt = g_requireCtrlAlt.load() ? 1 : 0;
    g_proxySharedConfig->keyToggleProxy = g_keyToggleProxy.load();
    g_proxySharedConfig->keyToggleMode = g_keyToggleMode.load();
    g_proxySharedConfig->keyScaleUp = g_keyScaleUp.load();
    g_proxySharedConfig->keyScaleDown = g_keyScaleDown.load();
    g_proxySharedConfig->enableUi = g_enableUi.load() ? 1 : 0;
    g_proxySharedConfig->keyToggleUi = g_keyToggleUI.load();
    g_proxySharedConfig->uiLanguage = (uint32_t)g_uiLanguage.load();
    g_proxySharedConfig->enableHotkeys = g_enableHotkeys ? 1 : 0;
    g_proxySharedConfig->requireCtrlAlt = g_requireCtrlAlt ? 1 : 0;
    g_proxySharedConfig->keyToggleProxy = g_keyToggleProxy;
    g_proxySharedConfig->keyToggleMode = g_keyToggleMode;
    g_proxySharedConfig->keyScaleUp = g_keyScaleUp;
    g_proxySharedConfig->keyScaleDown = g_keyScaleDown;

    g_proxySharedConfig->enableVrnr = g_enableVrnr.load() ? 1 : 0;
    g_proxySharedConfig->enableDepthAware = g_enableDepthAware.load() ? 1 : 0;
    g_proxySharedConfig->vrnrAntiFlicker = g_vrnrAntiFlicker.load() ? 1 : 0;
    g_proxySharedConfig->vrnrWeightFloor = g_vrnrWeightFloor.load();
    g_proxySharedConfig->vrnrReproject   = g_vrnrReproject.load() ? 1 : 0;
    g_proxySharedConfig->vrnrAdapt       = g_vrnrAdapt.load() ? 1 : 0;
    g_proxySharedConfig->vrnrAdaptAmount = g_vrnrAdaptAmount.load();
    g_proxySharedConfig->vrnrAdaptFpsHi  = g_vrnrAdaptFpsHi.load();
    g_proxySharedConfig->vrnrAdaptFpsLo  = g_vrnrAdaptFpsLo.load();
    g_proxySharedConfig->nrStyle = g_nrStyle.load();
    g_proxySharedConfig->nrIntensity = g_nrIntensity.load();
    g_proxySharedConfig->nrLocalStructureStrength = g_nrLocalStructureStrength.load();
    g_proxySharedConfig->nrLocalToneStrength = g_nrLocalToneStrength.load();
    g_proxySharedConfig->nrSkinStructureStrength = g_nrSkinStructureStrength.load();
    g_proxySharedConfig->nrUseAutoMask = g_nrUseAutoMask.load();
    g_proxySharedConfig->useCustomNR = g_useCustomNR.load() ? 1 : 0;

    g_proxySharedConfig->enableGovernor = g_enableGovernor.load() ? 1 : 0;
    g_proxySharedConfig->governorTargetFps = g_governorTargetFps.load();
    g_proxySharedConfig->governorMinScale = g_governorMinScale.load();
    g_proxySharedConfig->governorMaxScale = g_governorMaxScale.load();
    g_proxySharedConfig->governorHysteresisSec = g_governorHysteresisSec.load();
    g_proxySharedConfig->governorCurrentTier = g_governorCurrentTier.load();
    g_proxySharedConfig->enableGovernorFgMode = g_enableGovernorFgMode.load() ? 1 : 0;
    g_proxySharedConfig->governorFgMultiplier = g_governorFgMultiplier.load();

    g_proxySharedConfig->writerSource = 2; // Proxy/Hotkey
    g_proxySharedConfig->version++;
    s_lastProxySharedVersion = g_proxySharedConfig->version;
}

// ---------------------------------------------------------------------------
// Overlay UI <-> live config bridge (callbacks run on the overlay UI thread).
// All fields are std::atomic so cross-thread access stays race-free.
// ---------------------------------------------------------------------------
// ── 内嵌面板桥接（proxy_ui.cpp 经 PanelHooks 调用）──────────────────────────
// UiPullConfig   = 面板打开时取当前值；UiApplyLive = 单项实时生效（拖动滑条）；
// UiCommitConfig = 点「应用」时全量生效并写 INI。位置/大小回调持久化 PanelX/Y/W/H。
static void UiPullConfig(dlssnr_ui::UiValues& out, void*) {
    out.enableProxy    = g_enableProxy.load();
    out.scale          = g_scale.load();
    out.mode           = (int)g_enlargementMode.load();
    out.transfer       = g_transferStrength.load();
    out.color          = g_colorStrength.load();
    out.sharpness      = g_sharpness.load();
    out.enableHotkeys  = g_enableHotkeys.load();
    out.requireCtrlAlt = g_requireCtrlAlt.load();
    out.enableUi       = g_enableUi.load();
    out.keyToggleProxy = g_keyToggleProxy.load();
    out.keyToggleMode  = g_keyToggleMode.load();
    out.keyScaleUp     = g_keyScaleUp.load();
    out.keyScaleDown   = g_keyScaleDown.load();
    out.keyToggleUi    = g_keyToggleUI.load();
    out.uiLanguage     = g_uiLanguage.load();
    out.depthAware     = g_enableDepthAware.load();
    out.vrnr           = g_enableVrnr.load();
    out.vrnrAntiFlicker = g_vrnrAntiFlicker.load();
    out.vrnrWeightFloor = g_vrnrWeightFloor.load();
    out.vrnrReproject   = g_vrnrReproject.load();
    out.vrnrAdapt       = g_vrnrAdapt.load();
    out.vrnrAdaptAmount = g_vrnrAdaptAmount.load();
    out.vrnrAdaptFpsHi  = g_vrnrAdaptFpsHi.load();
    out.vrnrAdaptFpsLo  = g_vrnrAdaptFpsLo.load();
    out.anamorphic     = g_enableAnamorphic.load();
    out.scaleX         = g_scaleX.load();
    out.scaleY         = g_scaleY.load();
    out.useCustomNR    = g_useCustomNR.load();
    out.nrStyle        = (int)g_nrStyle.load();
    out.nrIntensity    = g_nrIntensity.load();
    out.nrLocalStruct  = g_nrLocalStructureStrength.load();
    out.nrLocalTone    = g_nrLocalToneStrength.load();
    out.nrSkinStruct   = g_nrSkinStructureStrength.load();
    out.nrAutoMask     = g_nrUseAutoMask.load() != 0;
    out.govEnable      = g_enableGovernor.load();
    out.govTargetFps   = g_governorTargetFps.load();
    out.govMinScale    = g_governorMinScale.load();
    out.govMaxScale    = g_governorMaxScale.load();
    out.govHysteresis  = g_governorHysteresisSec.load();
    out.govFgMode      = g_enableGovernorFgMode.load();
    out.govFgMult      = g_governorFgMultiplier.load();
    dlssnr_ui::SetLang(out.uiLanguage);  // mirror into the global display lang
}

static void UiApplyLive(const dlssnr_ui::UiValues& v, int field, void*) {
    switch ((dlssnr_ui::Field)field) {
    case dlssnr_ui::F_ENABLE_PROXY: g_enableProxy.store(v.enableProxy); break;
    case dlssnr_ui::F_SCALE:        g_scale.store(v.scale); break;
    case dlssnr_ui::F_MODE:         g_enlargementMode.store((uint32_t)v.mode); break;
    case dlssnr_ui::F_TRANSFER:     g_transferStrength.store(v.transfer); break;
    case dlssnr_ui::F_COLOR:        g_colorStrength.store(v.color); break;
    case dlssnr_ui::F_SHARP:        g_sharpness.store(v.sharpness); break;
    case dlssnr_ui::F_ENABLE_HOTKEYS: g_enableHotkeys.store(v.enableHotkeys); break;
    case dlssnr_ui::F_REQUIRE_CTRLALT: g_requireCtrlAlt.store(v.requireCtrlAlt); break;
    case dlssnr_ui::F_ENABLE_UI:    g_enableUi.store(v.enableUi); break;
    case dlssnr_ui::F_KEY_TOGGLE_PROXY: g_keyToggleProxy.store(v.keyToggleProxy); break;
    case dlssnr_ui::F_KEY_TOGGLE_MODE:  g_keyToggleMode.store(v.keyToggleMode); break;
    case dlssnr_ui::F_KEY_SCALEUP:      g_keyScaleUp.store(v.keyScaleUp); break;
    case dlssnr_ui::F_KEY_SCALEDOWN:    g_keyScaleDown.store(v.keyScaleDown); break;
    case dlssnr_ui::F_KEY_TOGGLEUI:     g_keyToggleUI.store(v.keyToggleUi); break;
    case dlssnr_ui::F_DEPTH_AWARE:      g_enableDepthAware.store(v.depthAware); break;
    case dlssnr_ui::F_VRNR:             g_enableVrnr.store(v.vrnr); break;
    case dlssnr_ui::F_VRNR_AF:          g_vrnrAntiFlicker.store(v.vrnrAntiFlicker); break;
    case dlssnr_ui::F_VRNR_FLOOR:       g_vrnrWeightFloor.store(v.vrnrWeightFloor); break;
    case dlssnr_ui::F_VRNR_REPROJECT:   g_vrnrReproject.store(v.vrnrReproject); break;
    case dlssnr_ui::F_VRNR_ADAPT:        g_vrnrAdapt.store(v.vrnrAdapt); break;
    case dlssnr_ui::F_VRNR_ADAPT_AMOUNT: g_vrnrAdaptAmount.store(v.vrnrAdaptAmount); break;
    case dlssnr_ui::F_VRNR_ADAPT_FPS_HI: g_vrnrAdaptFpsHi.store(v.vrnrAdaptFpsHi); break;
    case dlssnr_ui::F_VRNR_ADAPT_FPS_LO: g_vrnrAdaptFpsLo.store(v.vrnrAdaptFpsLo); break;
    case dlssnr_ui::F_ANAMORPHIC:       g_enableAnamorphic.store(v.anamorphic); break;
    case dlssnr_ui::F_SCALE_X:          g_scaleX.store(v.scaleX); break;
    case dlssnr_ui::F_SCALE_Y:          g_scaleY.store(v.scaleY); break;
    case dlssnr_ui::F_USE_CUSTOM_NR:    g_useCustomNR.store(v.useCustomNR); break;
    case dlssnr_ui::F_NR_STYLE:         g_nrStyle.store((uint32_t)v.nrStyle); break;
    case dlssnr_ui::F_NR_INTENSITY:     g_nrIntensity.store(v.nrIntensity); break;
    case dlssnr_ui::F_NR_LOCAL_STRUCT:  g_nrLocalStructureStrength.store(v.nrLocalStruct); break;
    case dlssnr_ui::F_NR_LOCAL_TONE:    g_nrLocalToneStrength.store(v.nrLocalTone); break;
    case dlssnr_ui::F_NR_SKIN_STRUCT:   g_nrSkinStructureStrength.store(v.nrSkinStruct); break;
    case dlssnr_ui::F_NR_AUTO_MASK:     g_nrUseAutoMask.store(v.nrAutoMask ? 1u : 0u); break;
    case dlssnr_ui::F_GOV_ENABLE:       g_enableGovernor.store(v.govEnable); break;
    case dlssnr_ui::F_GOV_TARGET:       g_governorTargetFps.store(v.govTargetFps); break;
    case dlssnr_ui::F_GOV_MIN:          g_governorMinScale.store(v.govMinScale); break;
    case dlssnr_ui::F_GOV_MAX:          g_governorMaxScale.store(v.govMaxScale); break;
    case dlssnr_ui::F_GOV_HYST:         g_governorHysteresisSec.store(v.govHysteresis); break;
    case dlssnr_ui::F_GOV_FGMODE:       g_enableGovernorFgMode.store(v.govFgMode); break;
    case dlssnr_ui::F_GOV_FGMULT:       g_governorFgMultiplier.store(v.govFgMult); break;
    default: break;
    }
}

static void UiCommitConfig(const dlssnr_ui::UiValues& v, void*) {
    // Mirror every field into the live atomics first (keeps behaviour identical
    // whether the edit arrived via applyLive or directly through commit).
    g_enableProxy.store(v.enableProxy);
    g_scale.store(v.scale);
    g_enlargementMode.store((uint32_t)v.mode);
    g_transferStrength.store(v.transfer);
    g_colorStrength.store(v.color);
    g_sharpness.store(v.sharpness);
    g_enableHotkeys.store(v.enableHotkeys);
    g_requireCtrlAlt.store(v.requireCtrlAlt);
    g_enableUi.store(v.enableUi);
    g_keyToggleProxy.store(v.keyToggleProxy);
    g_keyToggleMode.store(v.keyToggleMode);
    g_keyScaleUp.store(v.keyScaleUp);
    g_keyScaleDown.store(v.keyScaleDown);
    g_keyToggleUI.store(v.keyToggleUi);
    g_uiLanguage.store(v.uiLanguage);
    dlssnr_ui::SetLang(v.uiLanguage);
    g_enableDepthAware.store(v.depthAware);
    g_enableVrnr.store(v.vrnr);
    g_vrnrAntiFlicker.store(v.vrnrAntiFlicker);
    g_vrnrWeightFloor.store(v.vrnrWeightFloor);
    g_vrnrReproject.store(v.vrnrReproject);
    g_vrnrAdapt.store(v.vrnrAdapt);
    g_vrnrAdaptAmount.store(v.vrnrAdaptAmount);
    g_vrnrAdaptFpsHi.store(v.vrnrAdaptFpsHi);
    g_vrnrAdaptFpsLo.store(v.vrnrAdaptFpsLo);
    g_enableAnamorphic.store(v.anamorphic);
    g_scaleX.store(v.scaleX);
    g_scaleY.store(v.scaleY);
    g_useCustomNR.store(v.useCustomNR);
    g_nrStyle.store((uint32_t)v.nrStyle);
    g_nrIntensity.store(v.nrIntensity);
    g_nrLocalStructureStrength.store(v.nrLocalStruct);
    g_nrLocalToneStrength.store(v.nrLocalTone);
    g_nrSkinStructureStrength.store(v.nrSkinStruct);
    g_nrUseAutoMask.store(v.nrAutoMask ? 1u : 0u);
    bool prevGov = g_enableGovernor.load();
    g_enableGovernor.store(v.govEnable);
    g_governorTargetFps.store(v.govTargetFps);
    g_governorMinScale.store(v.govMinScale);
    g_governorMaxScale.store(v.govMaxScale);
    g_governorHysteresisSec.store(v.govHysteresis);
    g_enableGovernorFgMode.store(v.govFgMode);
    g_governorFgMultiplier.store(v.govFgMult);
    if (prevGov && !v.govEnable) {
        FlushSecondaryTierSlots();  // 关闭 Governor 时回收多余档位槽位
    }

    // Persist to the INI (sections match LoadConfig) then push to shared memory.
    wchar_t buf[32];
    SaveConfigValueEx(L"DLSSNR_Proxy", L"EnableProxy", v.enableProxy ? L"1" : L"0");
    swprintf_s(buf, L"%.2f", v.scale);             SaveConfigValueEx(L"DLSSNR_Proxy", L"ResolutionScale", buf);
    swprintf_s(buf, L"%u", (uint32_t)v.mode);      SaveConfigValueEx(L"DLSSNR_Proxy", L"EnlargementMode", buf);
    swprintf_s(buf, L"%.2f", v.transfer);          SaveConfigValueEx(L"DLSSNR_Proxy", L"TransferStrength", buf);
    swprintf_s(buf, L"%.2f", v.color);             SaveConfigValueEx(L"DLSSNR_Proxy", L"ColorStrength", buf);
    swprintf_s(buf, L"%.2f", v.sharpness);         SaveConfigValueEx(L"DLSSNR_Proxy", L"Sharpness", buf);
    SaveConfigValueEx(L"DLSSNR_Proxy", L"EnableHotkeys", v.enableHotkeys ? L"1" : L"0");
    SaveConfigValueEx(L"DLSSNR_Proxy", L"EnableUi", v.enableUi ? L"1" : L"0");
    swprintf_s(buf, L"%u", (uint32_t)v.uiLanguage); SaveConfigValueEx(L"DLSSNR_Proxy", L"UiLanguage", buf);
    SaveConfigValueEx(L"Hotkeys", L"RequireCtrlAlt", v.requireCtrlAlt ? L"1" : L"0");
    swprintf_s(buf, L"%d", v.keyToggleProxy); SaveConfigValueEx(L"Hotkeys", L"KeyToggleProxy", buf);
    swprintf_s(buf, L"%d", v.keyToggleMode);  SaveConfigValueEx(L"Hotkeys", L"KeyToggleMode", buf);
    swprintf_s(buf, L"%d", v.keyScaleUp);     SaveConfigValueEx(L"Hotkeys", L"KeyScaleUp", buf);
    swprintf_s(buf, L"%d", v.keyScaleDown);   SaveConfigValueEx(L"Hotkeys", L"KeyScaleDown", buf);
    swprintf_s(buf, L"%d", v.keyToggleUi);    SaveConfigValueEx(L"Hotkeys", L"KeyToggleUI", buf);
    SaveConfigValueEx(L"DLSSNR_Proxy", L"EnableDepthAwareResolve", v.depthAware ? L"1" : L"0");
    SaveConfigValueEx(L"DLSSNR_Proxy", L"EnableAlternatingFrames", v.vrnr ? L"1" : L"0");
    SaveConfigValueEx(L"DLSSNR_Proxy", L"VrnrAntiFlicker", v.vrnrAntiFlicker ? L"1" : L"0");
    {
        wchar_t fbuf[16];
        swprintf_s(fbuf, L"%.2f", v.vrnrWeightFloor);
        SaveConfigValueEx(L"DLSSNR_Proxy", L"VrnrWeightFloor", fbuf);
    }
    SaveConfigValueEx(L"DLSSNR_Proxy", L"VrnrReproject", v.vrnrReproject ? L"1" : L"0");
    SaveConfigValueEx(L"DLSSNR_Proxy", L"VrnrAdapt", v.vrnrAdapt ? L"1" : L"0");
    swprintf_s(buf, L"%.2f", v.vrnrAdaptAmount);
    SaveConfigValueEx(L"DLSSNR_Proxy", L"VrnrAdaptAmount", buf);
    swprintf_s(buf, L"%.0f", v.vrnrAdaptFpsHi);
    SaveConfigValueEx(L"DLSSNR_Proxy", L"VrnrAdaptFpsHi", buf);
    swprintf_s(buf, L"%.0f", v.vrnrAdaptFpsLo);
    SaveConfigValueEx(L"DLSSNR_Proxy", L"VrnrAdaptFpsLo", buf);
    SaveConfigValueEx(L"DLSSNR_Proxy", L"EnableAnamorphic", v.anamorphic ? L"1" : L"0");
    swprintf_s(buf, L"%.2f", v.scaleX);           SaveConfigValueEx(L"DLSSNR_Proxy", L"ResolutionScaleX", buf);
    swprintf_s(buf, L"%.2f", v.scaleY);           SaveConfigValueEx(L"DLSSNR_Proxy", L"ResolutionScaleY", buf);
    SaveConfigValueEx(L"DLSSNR_Settings", L"UseCustomSettings", v.useCustomNR ? L"1" : L"0");
    swprintf_s(buf, L"%u", (uint32_t)v.nrStyle);  SaveConfigValueEx(L"DLSSNR_Settings", L"Style", buf);
    swprintf_s(buf, L"%.2f", v.nrIntensity);      SaveConfigValueEx(L"DLSSNR_Settings", L"Intensity", buf);
    swprintf_s(buf, L"%.2f", v.nrLocalStruct);    SaveConfigValueEx(L"DLSSNR_Settings", L"LocalStructureStrength", buf);
    swprintf_s(buf, L"%.2f", v.nrLocalTone);      SaveConfigValueEx(L"DLSSNR_Settings", L"LocalToneStrength", buf);
    swprintf_s(buf, L"%.2f", v.nrSkinStruct);     SaveConfigValueEx(L"DLSSNR_Settings", L"SkinStructureStrength", buf);
    swprintf_s(buf, L"%u", v.nrAutoMask ? 1u : 0u); SaveConfigValueEx(L"DLSSNR_Settings", L"UseAutoMask", buf);
    SaveGovernorConfigValue(L"EnableGovernor", v.govEnable ? L"1" : L"0");
    swprintf_s(buf, L"%.0f", v.govTargetFps);     SaveGovernorConfigValue(L"TargetFps", buf);
    swprintf_s(buf, L"%.2f", v.govMinScale);      SaveGovernorConfigValue(L"MinScale", buf);
    swprintf_s(buf, L"%.2f", v.govMaxScale);      SaveGovernorConfigValue(L"MaxScale", buf);
    swprintf_s(buf, L"%.1f", v.govHysteresis);    SaveGovernorConfigValue(L"HysteresisSec", buf);
    SaveGovernorConfigValue(L"EnableFgMode", v.govFgMode ? L"1" : L"0");
    swprintf_s(buf, L"%.1f", v.govFgMult);        SaveGovernorConfigValue(L"FgMultiplier", buf);
    PushProxyToSharedMemory();
    Log("[Proxy] Overlay UI committed settings to INI + shared memory");
}

static dlssnr_ui::PanelHooks g_uiHooks;
static bool g_uiHooksRegistered = false;

// Panel position persistence — backed by nvngx_dlssnr.ini ([DLSSNR_Proxy]
// PanelX / PanelY). Screen coordinates of the panel's top-left corner.
static bool UiLoadPanelPos(int& x, int& y, void*) {
    if (g_iniPath[0] == L'\0') return false;
    int dx = (int)GetPrivateProfileIntW(L"DLSSNR_Proxy", L"PanelX", -1, g_iniPath);
    int dy = (int)GetPrivateProfileIntW(L"DLSSNR_Proxy", L"PanelY", -1, g_iniPath);
    if (dx < 0 || dy < 0) return false;   // never saved (or dragged off-screen)
    x = dx; y = dy;
    return true;
}
static void UiSavePanelPos(int x, int y, void*) {
    if (g_iniPath[0] == L'\0') return;
    wchar_t b[24];
    swprintf_s(b, L"%d", x);
    WritePrivateProfileStringW(L"DLSSNR_Proxy", L"PanelX", b, g_iniPath);
    swprintf_s(b, L"%d", y);
    WritePrivateProfileStringW(L"DLSSNR_Proxy", L"PanelY", b, g_iniPath);
}
// Panel size persistence for the free-resize window ([DLSSNR_Proxy]
// PanelW / PanelH, client pixels). No value saved yet -> load returns false.
static bool UiLoadPanelSize(int& w, int& h, void*) {
    if (g_iniPath[0] == L'\0') return false;
    int pw = (int)GetPrivateProfileIntW(L"DLSSNR_Proxy", L"PanelW", -1, g_iniPath);
    int ph = (int)GetPrivateProfileIntW(L"DLSSNR_Proxy", L"PanelH", -1, g_iniPath);
    if (pw <= 0 || ph <= 0) return false;
    w = pw; h = ph;
    return true;
}
static void UiSavePanelSize(int w, int h, void*) {
    if (g_iniPath[0] == L'\0') return;
    wchar_t b[24];
    swprintf_s(b, L"%d", w);
    WritePrivateProfileStringW(L"DLSSNR_Proxy", L"PanelW", b, g_iniPath);
    swprintf_s(b, L"%d", h);
    WritePrivateProfileStringW(L"DLSSNR_Proxy", L"PanelH", b, g_iniPath);
}

static void EnsureUiHooksRegistered() {
    if (g_uiHooksRegistered) return;
    g_uiHooks.user = nullptr;
    g_uiHooks.pull = UiPullConfig;
    g_uiHooks.applyLive = UiApplyLive;
    g_uiHooks.commit = UiCommitConfig;
    g_uiHooks.loadPos = UiLoadPanelPos;
    g_uiHooks.savePos = UiSavePanelPos;
    g_uiHooks.loadSize = UiLoadPanelSize;
    g_uiHooks.saveSize = UiSavePanelSize;
    dlssnr_proxyui::OverlaySetHooks(g_uiHooks);
    g_uiHooksRegistered = true;
}

// CheckConfigHotReload：每帧开头调用。共享内存 version 变化（console/管理器
// 改了设置）→ 重读 INI + 采纳共享内存值 → 缩放类参数变化会触发重建 NGX 功能。
// CheckHotkeys：每帧轮询热键（代理/模式/缩放/面板开关）。
static void CheckConfigHotReload() {
    InitProxySharedMemory();

    if (g_proxySharedConfig && g_proxySharedConfig->magic == DLSSNR_MAGIC) {
        if (g_proxySharedConfig->version != s_lastProxySharedVersion) {
            if (g_proxySharedConfig->writerSource != 2) {
                g_enableProxy.store(g_proxySharedConfig->enableProxy != 0);
                g_scale.store(g_proxySharedConfig->resolutionScale);
                g_enableAnamorphic.store(g_proxySharedConfig->enableAnamorphic != 0);
                g_scaleX.store(g_proxySharedConfig->scaleX);
                g_scaleY.store(g_proxySharedConfig->scaleY);
                g_enlargementMode.store(g_proxySharedConfig->enlargementMode);
                g_transferStrength.store(g_proxySharedConfig->transferStrength);
                g_colorStrength.store(g_proxySharedConfig->colorStrength);
                g_sharpness.store(g_proxySharedConfig->sharpness);
                g_enableHotkeys.store(g_proxySharedConfig->enableHotkeys != 0);
                g_requireCtrlAlt.store(g_proxySharedConfig->requireCtrlAlt != 0);
                g_keyToggleProxy.store(g_proxySharedConfig->keyToggleProxy);
                g_keyToggleMode.store(g_proxySharedConfig->keyToggleMode);
                g_keyScaleUp.store(g_proxySharedConfig->keyScaleUp);
                g_keyScaleDown.store(g_proxySharedConfig->keyScaleDown);
                g_enableUi.store(g_proxySharedConfig->enableUi != 0);
                g_keyToggleUI.store(g_proxySharedConfig->keyToggleUi);
                int newLang = (int)g_proxySharedConfig->uiLanguage;
                if (newLang >= 0 && newLang < dlssnr_ui::L_COUNT) {
                    g_uiLanguage.store(newLang);
                    dlssnr_ui::SetLang(newLang);  // refresh overlay display immediately
                }
                g_enableHotkeys = (g_proxySharedConfig->enableHotkeys != 0);
                g_requireCtrlAlt = (g_proxySharedConfig->requireCtrlAlt != 0);
                g_keyToggleProxy = g_proxySharedConfig->keyToggleProxy;
                g_keyToggleMode  = g_proxySharedConfig->keyToggleMode;
                g_keyScaleUp     = g_proxySharedConfig->keyScaleUp;
                g_keyScaleDown   = g_proxySharedConfig->keyScaleDown;

                g_enableVrnr.store(g_proxySharedConfig->enableVrnr != 0);
                g_enableDepthAware.store(g_proxySharedConfig->enableDepthAware != 0);
                g_vrnrAntiFlicker.store(g_proxySharedConfig->vrnrAntiFlicker != 0);
                g_vrnrWeightFloor.store(g_proxySharedConfig->vrnrWeightFloor);
                g_vrnrReproject.store(g_proxySharedConfig->vrnrReproject != 0);
                g_vrnrAdapt.store(g_proxySharedConfig->vrnrAdapt != 0);
                g_vrnrAdaptAmount.store(g_proxySharedConfig->vrnrAdaptAmount);
                g_vrnrAdaptFpsHi.store(g_proxySharedConfig->vrnrAdaptFpsHi);
                g_vrnrAdaptFpsLo.store(g_proxySharedConfig->vrnrAdaptFpsLo);
                g_nrStyle.store(g_proxySharedConfig->nrStyle);
                g_nrIntensity.store(g_proxySharedConfig->nrIntensity);
                g_nrLocalStructureStrength.store(g_proxySharedConfig->nrLocalStructureStrength);
                g_nrLocalToneStrength.store(g_proxySharedConfig->nrLocalToneStrength);
                g_nrSkinStructureStrength.store(g_proxySharedConfig->nrSkinStructureStrength);
                g_nrUseAutoMask.store(g_proxySharedConfig->nrUseAutoMask != 0);
                g_useCustomNR.store(g_proxySharedConfig->useCustomNR != 0);

                bool prevGov = g_enableGovernor.load();
                bool newGov = (g_proxySharedConfig->enableGovernor != 0);
                g_enableGovernor.store(newGov);
                g_governorTargetFps.store(g_proxySharedConfig->governorTargetFps);
                g_governorMinScale.store(g_proxySharedConfig->governorMinScale);
                g_governorMaxScale.store(g_proxySharedConfig->governorMaxScale);
                g_governorHysteresisSec.store(g_proxySharedConfig->governorHysteresisSec);
                g_enableGovernorFgMode.store(g_proxySharedConfig->enableGovernorFgMode != 0);
                float compFgMult = g_proxySharedConfig->governorFgMultiplier;
                if (compFgMult < 1.0f) compFgMult = 1.0f;
                if (compFgMult > 10.0f) compFgMult = 10.0f;
                g_governorFgMultiplier.store(compFgMult);
                if (prevGov && !newGov) {
                    FlushSecondaryTierSlots();
                }
            }
            s_lastProxySharedVersion = g_proxySharedConfig->version;
        }
    }

    static ULONGLONG s_lastCheck = 0;
    ULONGLONG now = GetTickCount64();
    if (now - s_lastCheck < 100) return;
    s_lastCheck = now;

    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (GetFileAttributesExW(g_iniPath, GetFileExInfoStandard, &fileInfo)) {
        if (CompareFileTime(&fileInfo.ftLastWriteTime, &g_lastIniWriteTime) != 0) {
            LoadConfig();
        }
    }
}

static void CheckHotkeys() {
    static ULONGLONG s_lastPress = 0;
    ULONGLONG now = GetTickCount64();
    if (now - s_lastPress < 250) return;

    // UI overlay toggle: Ctrl+Alt+<KeyToggleUI>, independent of EnableHotkeys /
    // RequireCtrlAlt so the panel can always be summoned while EnableUi is on.
    if (g_enableUi.load()) {
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt  = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        int  uiKey = g_keyToggleUI.load();
        if (ctrl && alt && uiKey > 0 && (GetAsyncKeyState(uiKey) & 0x8000) != 0) {
            s_lastPress = now;
            EnsureUiHooksRegistered();
            dlssnr_proxyui::OverlayToggle();
            Log("[Proxy] Hotkey UI toggle pressed (Ctrl+Alt+F11)");
            return;
        }
    }

    if (!g_enableHotkeys.load()) return;

    bool modifiersOk = true;
    if (g_requireCtrlAlt.load()) {
        bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        bool alt  = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        modifiersOk = (ctrl && alt);
    }

    if (modifiersOk) {
        float current = g_scale.load();
        if ((GetAsyncKeyState(g_keyToggleProxy.load()) & 0x8000) != 0) {
            bool newState = !g_enableProxy.load();
            g_enableProxy.store(newState);
            wchar_t buf[16];
            swprintf_s(buf, L"%d", newState ? 1 : 0);
            SaveConfigValue(L"EnableProxy", buf);
            PushProxyToSharedMemory();
            Log("[Proxy] Hotkey ToggleProxy: Proxy is now %s (synced to INI)", newState ? "ENABLED" : "DISABLED (Native Passthrough)");
            s_lastPress = now;
        }
        else if ((GetAsyncKeyState(g_keyToggleMode.load()) & 0x8000) != 0) {
            uint32_t newMode = (g_enlargementMode.load() == 1) ? 0 : 1;
            g_enlargementMode.store(newMode);
            wchar_t buf[16];
            swprintf_s(buf, L"%u", newMode);
            SaveConfigValue(L"EnlargementMode", buf);
            PushProxyToSharedMemory();
            Log("[Proxy] Hotkey ToggleMode: EnlargementMode changed to %s (synced to INI)", newMode == 1 ? "Matched Residual" : "Classic Bilinear");
            s_lastPress = now;
        }
        else if ((GetAsyncKeyState(g_keyScaleUp.load()) & 0x8000) != 0) {
            if (g_enableGovernor.load()) {
                // Governor 接管分辨率时手动变焦热键失效（上游同步行为）
                Log("[Proxy] Scale hotkey ignored: Dynamic Governor is active");
                s_lastPress = now;
            } else {
                float next = (float)(floor((current + 0.051f) * 20.0f) / 20.0f);
                if (next > 2.00f) next = 2.00f;
                if (next != current) {
                    g_scale.store(next);
                    wchar_t buf[16];
                    swprintf_s(buf, L"%.2f", next);
                    SaveConfigValue(L"ResolutionScale", buf);
                    PushProxyToSharedMemory();
                    Log("[Proxy] Hotkey ScaleUp: Scale changed from %.2f to %.2f (synced to INI)", current, next);
                    s_lastPress = now;
                }
            }
        }
        else if ((GetAsyncKeyState(g_keyScaleDown.load()) & 0x8000) != 0) {
            if (g_enableGovernor.load()) {
                Log("[Proxy] Scale hotkey ignored: Dynamic Governor is active");
                s_lastPress = now;
            } else {
                float next = (float)(floor((current - 0.049f) * 20.0f) / 20.0f);
                if (next < 0.25f) next = 0.25f;
                if (next != current) {
                    g_scale.store(next);
                    wchar_t buf[16];
                    swprintf_s(buf, L"%.2f", next);
                    SaveConfigValue(L"ResolutionScale", buf);
                    PushProxyToSharedMemory();
                    Log("[Proxy] Hotkey ScaleDown: Scale changed from %.2f to %.2f (synced to INI)", current, next);
                    s_lastPress = now;
                }
            }
        }
    }
}

typedef int(__cdecl *PFN_InitExt)(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath, ID3D12Device* InDevice, int InVersion, const void* InFeatureInfo);
typedef int(__cdecl *PFN_Create)(ID3D12GraphicsCommandList* InCmdList, int InFeatureId, const void* InParameters, void** OutHandle);
typedef int(__cdecl *PFN_Evaluate)(ID3D12GraphicsCommandList* InCmdList, const void* InFeatureHandle, const void* InParameters, void* InCallback);
typedef int(__cdecl *PFN_Release)(void* InFeatureHandle);

static HMODULE g_realModule = nullptr;
static PFN_InitExt real_InitExt = nullptr;
static PFN_Create real_Create = nullptr;
static PFN_Evaluate real_Evaluate = nullptr;
static PFN_Release real_Release = nullptr;

typedef DWORD (WINAPI *PFN_GetModuleFileNameW)(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);
static PFN_GetModuleFileNameW g_origGetModuleFileNameW = nullptr;

static DWORD WINAPI Hooked_GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
    if (g_origGetModuleFileNameW) {
        g_origGetModuleFileNameW(hModule, lpFilename, nSize);
    } else {
        GetModuleFileNameW(hModule, lpFilename, nSize);
    }
    wchar_t sysDir[MAX_PATH] = { 0 };
    if (GetSystemDirectoryW(sysDir, MAX_PATH) > 0) {
        swprintf_s(lpFilename, nSize, L"%ls\\nvngx.dll", sysDir);
    } else {
        wcscpy_s(lpFilename, nSize, L"C:\\Windows\\System32\\nvngx.dll");
    }
    return (DWORD)wcslen(lpFilename);
}

static void HookSnippetCallerCheck(HMODULE targetModule) {
    if (!targetModule) return;

    __try {
        BYTE* base = (BYTE*)targetModule;
        IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)base;
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return;

        IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)(base + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return;

        IMAGE_DATA_DIRECTORY importDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (importDir.VirtualAddress == 0 || importDir.Size == 0) return;

        IMAGE_IMPORT_DESCRIPTOR* importDesc = (IMAGE_IMPORT_DESCRIPTOR*)(base + importDir.VirtualAddress);

        for (; importDesc->Name != 0; ++importDesc) {
            const char* modName = (const char*)(base + importDesc->Name);
            if (_stricmp(modName, "KERNEL32.dll") == 0) {
                IMAGE_THUNK_DATA* thunkOrig = (IMAGE_THUNK_DATA*)(base + (importDesc->OriginalFirstThunk ? importDesc->OriginalFirstThunk : importDesc->FirstThunk));
                IMAGE_THUNK_DATA* thunk = (IMAGE_THUNK_DATA*)(base + importDesc->FirstThunk);

                for (; thunk->u1.Function != 0; ++thunk, ++thunkOrig) {
                    if (!(thunkOrig->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                        IMAGE_IMPORT_BY_NAME* importByName = (IMAGE_IMPORT_BY_NAME*)(base + thunkOrig->u1.AddressOfData);
                        if (strcmp(importByName->Name, "GetModuleFileNameW") == 0) {
                            g_origGetModuleFileNameW = (PFN_GetModuleFileNameW)thunk->u1.Function;
                            DWORD oldProtect = 0;
                            if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
                                thunk->u1.Function = (ULONG_PTR)&Hooked_GetModuleFileNameW;
                                VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &oldProtect);
                                Log("[Proxy] Installed GetModuleFileNameW hook in real module IAT");
                            }
                            return;
                        }
                    }
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[Proxy] SEH caught exception in HookSnippetCallerCheck");
    }
}

static bool EnsureRealModuleLoaded() {
    if (g_realModule) return true;

    wchar_t path[MAX_PATH] = { 0 };
    GetCurrentModulePath(path, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(path, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';
    wcscat_s(path, L"nvngx_dlssnr_real.dll");

    g_realModule = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    HookSnippetCallerCheck(g_realModule);
    if (!g_realModule) {
        Log("[Proxy] Failed to load real module from: %ls (error=%lu)", path, GetLastError());
        return false;
    }

    real_InitExt  = (PFN_InitExt)GetProcAddress(g_realModule, "NVSDK_NGX_D3D12_Init_Ext");
    real_Create   = (PFN_Create)GetProcAddress(g_realModule, "NVSDK_NGX_D3D12_CreateFeature");
    real_Evaluate = (PFN_Evaluate)GetProcAddress(g_realModule, "NVSDK_NGX_D3D12_EvaluateFeature");
    real_Release  = (PFN_Release)GetProcAddress(g_realModule, "NVSDK_NGX_D3D12_ReleaseFeature");

    Log("[Proxy] Loaded real module successfully (Init=%p, Create=%p, Eval=%p, Rel=%p)",
        real_InitExt, real_Create, real_Evaluate, real_Release);
    return true;
}

static DXGI_FORMAT ToNonTypeless(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default: return format;
    }
}

static DXGI_FORMAT ToUavCompatibleFormat(DXGI_FORMAT format) {
    format = ToNonTypeless(format);
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    // R11G11B10_FLOAT natively supports typed UAV writes in D3D12
    default: return format;
    }
}

static DXGI_FORMAT GetUavSafeScratchFormat(DXGI_FORMAT format) {
    return ToUavCompatibleFormat(format);
}

// 0.7.1：游戏运动矢量纹理 → SRV 可采样格式（DLSS 常见 RG 半精度/全精度）
static DXGI_FORMAT ToMVecSrvFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_SNORM:
        return DXGI_FORMAT_R16G16_FLOAT;
    case DXGI_FORMAT_R32G32_TYPELESS:
        return DXGI_FORMAT_R32G32_FLOAT;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_UNKNOWN:
        return DXGI_FORMAT_UNKNOWN;
    default:
        return format;   // 已是类型化格式（R16G16_FLOAT 等），直接可用
    }
}

static DXGI_FORMAT ToDepthSrvFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
        return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_D16_UNORM:
        return DXGI_FORMAT_R16_UNORM;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

struct DownsampleConstants {
    uint32_t srcWidth;
    uint32_t srcHeight;
    uint32_t dstWidth;
    uint32_t dstHeight;
};

struct ResolveConstants {
    uint32_t nativeWidth;
    uint32_t nativeHeight;
    uint32_t workWidth;
    uint32_t workHeight;
    float    transferStrength;
    float    sharpness;
    uint32_t enlargementMode;
    float    colorStrength;
    uint32_t isSkipFrame;
    uint32_t hasDepth;
    float    vrnrRamp;    // 0.6.3 防闪烁：连续 skip 帧的 edit 强度坡道（1.0 = 满强度）
    uint32_t vrnrAf;      // 0.6.3 防闪烁总开关（0 = 与 0.6.2 行为完全一致）
    // 0.7.1 VRNR 防闪烁 v2（实验）——与 shaders.hlsl cbuffer 尾部严格对应
    float    vrnrFloor;     // skip 帧 edit 权重下限（0 = 关闭下限）
    uint32_t vrnrReproject; // 1 = 用 MV 把陈旧神经输入对齐到本帧再算亮度差
    uint32_t hasMVec;       // 本帧游戏运动矢量纹理是否可用
    float    mvScaleX;      // 原始 MV → native 像素（未乘 work 缩放系数）
    float    mvScaleY;
    // 0.7.3 帧时自适应强度（实验）——与 shaders.hlsl cbuffer 尾部严格对应。
    // CPU 由 EWMA 帧率算出（0=不衰减），skip 帧恒为 0。
    float    adaptDim;
};

static ID3D12Device*             g_device = nullptr;
static ID3D12RootSignature*      g_rootSigDownsample = nullptr;
static ID3D12RootSignature*      g_rootSigResolve = nullptr;
static ID3D12PipelineState*      g_psoDownsample = nullptr;
static ID3D12PipelineState*      g_psoResolve = nullptr;
static ID3D12DescriptorHeap*     g_descHeap = nullptr;

// Multi-Slot Feature Cache to support concurrent viewports and hooks (e.g. MSFS 2024 Upscaled + Present)
struct FeatureSlot {
    bool                inUse = false;
    uint32_t            passIndex = 0;
    const void*         origGameHandle = nullptr;
    void*               activeFeature = nullptr;
    uint32_t            nativeW = 0;
    uint32_t            nativeH = 0;
    uint32_t            workW = 0;
    uint32_t            workH = 0;
    DXGI_FORMAT         colorFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT         scratchFormat = DXGI_FORMAT_UNKNOWN;
    float               scale = 1.0f;
    int                 skipEvaluateFrames = 0;

    ID3D12Resource*     colorSmall = nullptr;
    ID3D12Resource*     outputSmall = nullptr;
    ID3D12Resource*     nativeScratch = nullptr;
    D3D12_RESOURCE_STATES colorSmallState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES outputSmallState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_STATES nativeScratchState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    ULONGLONG           lastUsedTick = 0;
    ULONGLONG           lastAllocAttemptTick = 0;
    ULONGLONG           lastCreateAttemptTick = 0;
    bool                allocFailed = false;
    bool                hasEvaluatedOnce = false;
};

static constexpr size_t MAX_FEATURE_SLOTS = 8;
static FeatureSlot g_slots[MAX_FEATURE_SLOTS] = {};

static std::recursive_mutex g_proxyMutex;

static constexpr int RETIRE_FRAME_DELAY = 64;

struct NrRetired {
    void* feature = nullptr;
    ID3D12Resource* resource = nullptr;
    int framesLeft = RETIRE_FRAME_DELAY;
};
static std::vector<NrRetired> g_retiredList;

// ── NGX 功能生命周期管理 ────────────────────────────────────────────────────
// ParkNrFeature：把真实 NGX feature 句柄挂进延迟回收列表（64 帧后 real_Release），
//   给游戏留出切换到新 feature 的缓冲期。
// ⚠️ 句柄一经 park 就【不再代表有效功能】——之后任何路径都不得拿它调 real_Evaluate
//    （0.7.x 的 use-after-free 崩溃根因，见文件头「崩溃防线」）。
// TickRetired：每帧倒数，到期的 feature / scratch 资源在这里真正 Release。
static void ParkNrFeature(void*& feature) {
    if (!feature) return;
    NrRetired r;
    r.feature = feature;
    r.framesLeft = RETIRE_FRAME_DELAY;
    feature = nullptr;
    g_retiredList.push_back(r);
}

static void ReleaseSlotScratch(FeatureSlot& slot) {
    if (slot.colorSmall) {
        NrRetired r; r.resource = slot.colorSmall; r.framesLeft = RETIRE_FRAME_DELAY; g_retiredList.push_back(r);
        slot.colorSmall = nullptr;
    }
    if (slot.outputSmall) {
        NrRetired r; r.resource = slot.outputSmall; r.framesLeft = RETIRE_FRAME_DELAY; g_retiredList.push_back(r);
        slot.outputSmall = nullptr;
    }
    slot.colorSmallState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    slot.outputSmallState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

static void ReleaseSlotResources(FeatureSlot& slot) {
    if (slot.activeFeature) {
        ParkNrFeature(slot.activeFeature);
    }
    ReleaseSlotScratch(slot);
    if (slot.nativeScratch) {
        NrRetired r; r.resource = slot.nativeScratch; r.framesLeft = RETIRE_FRAME_DELAY; g_retiredList.push_back(r);
        slot.nativeScratch = nullptr;
    }
    slot.nativeScratchState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
}

// FlushSecondaryTierSlots：Governor 关闭时调用——每个 pass 只保留 scale 最接近
// 当前值的槽位，其余（多余档位的缓存）立即回收，恢复基线 VRAM 占用。
static void FlushSecondaryTierSlots() {
    float curScale = g_scale.load();
    for (uint32_t pass = 0; pass < MAX_FEATURE_SLOTS; ++pass) {
        FeatureSlot* keepSlot = nullptr;
        for (size_t i = 0; i < MAX_FEATURE_SLOTS; ++i) {
            if (g_slots[i].inUse && g_slots[i].passIndex == pass) {
                if (!keepSlot) {
                    keepSlot = &g_slots[i];
                } else if (fabsf(g_slots[i].scale - curScale) < fabsf(keepSlot->scale - curScale)) {
                    ReleaseSlotResources(*keepSlot);
                    keepSlot->inUse = false;
                    keepSlot = &g_slots[i];
                } else {
                    ReleaseSlotResources(g_slots[i]);
                    g_slots[i].inUse = false;
                }
            }
        }
    }
    Log("[Proxy] Governor toggled OFF: Flushed secondary tier slots to restore baseline VRAM");
}

static void TickRetired() {
    for (size_t i = 0; i < g_retiredList.size();) {
        if (--g_retiredList[i].framesLeft > 0) {
            ++i;
            continue;
        }
        if (g_retiredList[i].feature) {
            if (real_Release) {
                int res = real_Release(g_retiredList[i].feature);
                Log("[Proxy] Retired DLSS-NR feature %p released (res=0x%X)", g_retiredList[i].feature, res);
            }
        }
        if (g_retiredList[i].resource) {
            g_retiredList[i].resource->Release();
        }
        g_retiredList.erase(g_retiredList.begin() + i);
    }
}

static void TransitionBarrier(ID3D12GraphicsCommandList* cmd, ID3D12Resource* res, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    if (from == to || !res) return;
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    cmd->ResourceBarrier(1, &b);
}

static void ReleaseD3D12Pipeline() {
    if (g_psoDownsample) { g_psoDownsample->Release(); g_psoDownsample = nullptr; }
    if (g_psoResolve) { g_psoResolve->Release(); g_psoResolve = nullptr; }
    if (g_rootSigDownsample) { g_rootSigDownsample->Release(); g_rootSigDownsample = nullptr; }
    if (g_rootSigResolve) { g_rootSigResolve->Release(); g_rootSigResolve = nullptr; }
    if (g_descHeap) { g_descHeap->Release(); g_descHeap = nullptr; }
    g_device = nullptr;
}

// InitD3D12Pipeline：创建 compute 管线（根签名×2 + PSO×2 + 描述符堆）。
// 描述符堆容量 = 64 帧 × 每帧槽位步长 8 + 余量 = 512；改 resolve 的 SRV/UAV
// 数量时同步更新帧槽步长（s_frameSlot * 8）与堆容量。
static bool InitD3D12Pipeline(ID3D12Device* device) {
    if (!device) return false;
    if (g_device != nullptr && g_device != device) {
        Log("[Proxy] D3D12 device changed (%p -> %p), resetting pipeline and slots", g_device, device);
        ReleaseD3D12Pipeline();
        for (size_t i = 0; i < MAX_FEATURE_SLOTS; ++i) {
            ReleaseSlotResources(g_slots[i]);
            g_slots[i].inUse = false;
        }
    }
    if (g_rootSigDownsample && g_rootSigResolve && g_psoDownsample && g_psoResolve && g_descHeap) return true;
    g_device = device;

    {
        D3D12_DESCRIPTOR_RANGE downRanges[2] = {};
        downRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        downRanges[0].NumDescriptors = 1;
        downRanges[0].BaseShaderRegister = 0;
        downRanges[0].RegisterSpace = 0;
        downRanges[0].OffsetInDescriptorsFromTableStart = 0;

        downRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        downRanges[1].NumDescriptors = 1;
        downRanges[1].BaseShaderRegister = 0;
        downRanges[1].RegisterSpace = 0;
        downRanges[1].OffsetInDescriptorsFromTableStart = 1;

        D3D12_ROOT_PARAMETER downParams[2] = {};
        downParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        downParams[0].Constants.ShaderRegister = 0;
        downParams[0].Constants.RegisterSpace = 0;
        downParams[0].Constants.Num32BitValues = 4;
        downParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        downParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        downParams[1].DescriptorTable.NumDescriptorRanges = 2;
        downParams[1].DescriptorTable.pDescriptorRanges = downRanges;
        downParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_STATIC_SAMPLER_DESC downSampler = {};
        downSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        downSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        downSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        downSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        downSampler.ShaderRegister = 0;
        downSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC downRootDesc = {};
        downRootDesc.NumParameters = 2;
        downRootDesc.pParameters = downParams;
        downRootDesc.NumStaticSamplers = 1;
        downRootDesc.pStaticSamplers = &downSampler;

        ID3DBlob* signatureBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(&downRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
        if (FAILED(hr)) {
            Log("[Proxy] D3D12SerializeRootSignature (down) failed (hr=0x%08X)", hr);
            if (errorBlob) {
                Log("[Proxy] > %s", (const char*)errorBlob->GetBufferPointer());
                errorBlob->Release();
            }
            return false;
        }

        hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&g_rootSigDownsample));
        if (FAILED(hr)) {
            Log("[Proxy] CreateRootSignature (down) failed (hr=0x%08X)", hr);
            signatureBlob->Release();
            return false;
        }
        signatureBlob->Release();
    }

    {
        D3D12_DESCRIPTOR_RANGE resolveRanges[2] = {};
        resolveRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        resolveRanges[0].NumDescriptors = 5; // t0: colorSmall, t1: outputSmall, t2: nativeColor, t3: depth, t4: mvec (0.7.1)
        resolveRanges[0].BaseShaderRegister = 0;
        resolveRanges[0].RegisterSpace = 0;
        resolveRanges[0].OffsetInDescriptorsFromTableStart = 0;

        resolveRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        resolveRanges[1].NumDescriptors = 1;
        resolveRanges[1].BaseShaderRegister = 0;
        resolveRanges[1].RegisterSpace = 0;
        resolveRanges[1].OffsetInDescriptorsFromTableStart = 5;

        D3D12_ROOT_PARAMETER resolveParams[2] = {};
        resolveParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        resolveParams[0].Constants.ShaderRegister = 0;
        resolveParams[0].Constants.RegisterSpace = 0;
        resolveParams[0].Constants.Num32BitValues = 18;   // ResolveConstants（0.7.3 +adaptDim；0.7.1 已有 vrnrFloor/vrnrReproject/hasMVec/mvScaleXY）
        resolveParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        resolveParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        resolveParams[1].DescriptorTable.NumDescriptorRanges = 2;
        resolveParams[1].DescriptorTable.pDescriptorRanges = resolveRanges;
        resolveParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC resolveRootDesc = {};
        resolveRootDesc.NumParameters = 2;
        resolveRootDesc.pParameters = resolveParams;
        resolveRootDesc.NumStaticSamplers = 1;
        resolveRootDesc.pStaticSamplers = &sampler;

        ID3DBlob* signatureBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(&resolveRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
        if (FAILED(hr)) {
            Log("[Proxy] D3D12SerializeRootSignature (resolve) failed (hr=0x%08X)", hr);
            if (errorBlob) {
                Log("[Proxy] > %s", (const char*)errorBlob->GetBufferPointer());
                errorBlob->Release();
            }
            return false;
        }

        hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(), IID_PPV_ARGS(&g_rootSigResolve));
        if (FAILED(hr)) {
            Log("[Proxy] CreateRootSignature (resolve) failed (hr=0x%08X)", hr);
            signatureBlob->Release();
            return false;
        }
        signatureBlob->Release();
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC downPsoDesc = {};
    downPsoDesc.pRootSignature = g_rootSigDownsample;
    downPsoDesc.CS = { g_DownsampleShader, sizeof(g_DownsampleShader) };
    HRESULT hrPso = device->CreateComputePipelineState(&downPsoDesc, IID_PPV_ARGS(&g_psoDownsample));
    if (FAILED(hrPso)) {
        Log("[Proxy] CreateComputePipelineState (down) failed (hr=0x%08X)", hrPso);
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC resolvePsoDesc = {};
    resolvePsoDesc.pRootSignature = g_rootSigResolve;
    resolvePsoDesc.CS = { g_ResolveShader, sizeof(g_ResolveShader) };
    hrPso = device->CreateComputePipelineState(&resolvePsoDesc, IID_PPV_ARGS(&g_psoResolve));
    if (FAILED(hrPso)) {
        Log("[Proxy] CreateComputePipelineState (resolve) failed (hr=0x%08X)", hrPso);
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 512;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    HRESULT hrHeap = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&g_descHeap));
    if (FAILED(hrHeap)) {
        Log("[Proxy] CreateDescriptorHeap failed (hr=0x%08X)", hrHeap);
        return false;
    }

    Log("[Proxy] D3D12 compute pipeline initialized (512 descriptors)");
    return true;
}

static ID3D12Resource* CreateScratchTexture(ID3D12Device* device, DXGI_FORMAT format, uint32_t width, uint32_t height) {
    if (!device) return nullptr;
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    ID3D12Resource* res = nullptr;
    HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&res));
    if (FAILED(hr)) {
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&res));
    }
    if (FAILED(hr)) {
        HRESULT reason = device->GetDeviceRemovedReason();
        static HRESULT s_lastLoggedHr = S_OK;
        static ULONGLONG s_lastLogTime = 0;
        ULONGLONG now = GetTickCount64();
        if (hr != s_lastLoggedHr || (now - s_lastLogTime) > 3000) {
            Log("[Proxy] Failed to allocate scratch texture %ux%u (hr=0x%08X, reason=0x%08X)", width, height, hr, reason);
            s_lastLoggedHr = hr;
            s_lastLogTime = now;
        }
        return nullptr;
    }
    return res;
}

// ── NGX D3D12 导出入口（D3D12 系列不转发，全部由本文件实现拦截）────────────
// Init_Ext / CreateFeature / EvaluateFeature / ReleaseFeature 四个是核心拦截点，
// 其余导出（CUDA/D3D11 等）由 forwarders.h 的链接器指令直接转发到 _real.dll。
// CUDA/DXGI 之外的失败只打日志，绝不让异常越出 __except 边界。
extern "C" {

__declspec(dllexport) int __cdecl NVSDK_NGX_D3D12_Init_Ext(
    unsigned long long InApplicationId,
    const wchar_t* InApplicationDataPath,
    ID3D12Device* InDevice,
    int InVersion,
    const void* InFeatureInfo)
{
    std::lock_guard<std::recursive_mutex> lock(g_proxyMutex);
    EnsureRealModuleLoaded();
    LoadConfig();
    EnsureUiHooksRegistered(); // make the Ctrl+Alt+F11 overlay panel available

    Log("[Proxy] NVSDK_NGX_D3D12_Init_Ext (AppId=0x%llX, Device=%p)", InApplicationId, InDevice);
    if (!real_InitExt) return -1;
    return real_InitExt(InApplicationId, InApplicationDataPath, InDevice, InVersion, InFeatureInfo);
}

__declspec(dllexport) int __cdecl NVSDK_NGX_D3D12_CreateFeature(
    ID3D12GraphicsCommandList* InCmdList,
    int InFeatureId,
    const void* InParameters,
    void** OutHandle)
{
    std::lock_guard<std::recursive_mutex> lock(g_proxyMutex);
    EnsureRealModuleLoaded();
    CheckConfigHotReload();

    Log("[Proxy] NVSDK_NGX_D3D12_CreateFeature (FeatureId=%d)", InFeatureId);

    if (!real_Create) return -1;
    return real_Create(InCmdList, InFeatureId, InParameters, OutHandle);
}

// ─────────────────────────────────────────────────────────────────────────────
// EvaluateFeatureInternal — 核心一帧流程（游戏每帧、每个 NGX 槽位调用一次）
//
//   [热更新] CheckConfigHotReload → [跳帧判定] VRNR 隔帧 →
//   pass1 CS_Downsample（原生帧→colorSmall）→ 真实 NR（work 分辨率）→
//   pass2 CS_Resolve（编辑量合成回 origOutput）
//
// 失败处理铁律（0.7.x 血案总结）：
//   ① real_Create 失败 → 置 activeFeature=nullptr 后走「游戏句柄透传」
//      real_Evaluate 是安全的【仅当该句柄未被 park】；创建失败分支里我们
//      刚 park 了旧句柄，因此透传用的是 InFeatureHandle 本身，不要混淆。
//   ② slot 资源缺失 / feature 不存在 → 直接透传 real_Evaluate。
//   ③ SEH __except 兜底 → 透传，绝不让异常逃出 DLL（游戏会直接崩）。
// 帧槽步长 8：0-1 downsample / 2-6 resolve(SRV×4+UAV)（详见 InitD3D12Pipeline）。
// ─────────────────────────────────────────────────────────────────────────────
static int EvaluateFeatureInternal(
    ID3D12GraphicsCommandList* InCmdList,
    const void* InFeatureHandle,
    const void* InParameters,
    void* InCallback)
{
    CheckConfigHotReload();
    CheckHotkeys();
    TickRetired();

    static ID3D12GraphicsCommandList* s_lastCmdList = nullptr;
    static LARGE_INTEGER s_qpcFreq = {};
    static LARGE_INTEGER s_lastPassQpc = {};
    static uint32_t s_passCountThisFrame = 0;
    static ID3D12Resource* s_lastResolveOutput = nullptr;

    if (s_qpcFreq.QuadPart == 0) {
        QueryPerformanceFrequency(&s_qpcFreq);
    }

    LARGE_INTEGER nowQpc;
    QueryPerformanceCounter(&nowQpc);

    double deltaMs = 0.0;
    if (s_lastPassQpc.QuadPart > 0) {
        deltaMs = (double)(nowQpc.QuadPart - s_lastPassQpc.QuadPart) * 1000.0 / (double)s_qpcFreq.QuadPart;
    }
    s_lastPassQpc = nowQpc;

    // Consecutive evaluate calls within the same frame execute on the CPU within < 1.0 ms.
    // Inter-frame intervals (even at 240 FPS = 4.16 ms, or 500 FPS = 2.0 ms) are >= 1.0 ms.
    if (InCmdList == s_lastCmdList && deltaMs > 0.0 && deltaMs < 1.0) {
        s_passCountThisFrame++;
    } else {
        s_passCountThisFrame = 0;
        s_lastCmdList = InCmdList;
        s_lastResolveOutput = nullptr;
    }

    // ── Governor：帧时间测量 + 状态机（上游 b09bcca/c7e9b0b 同步）─────────────
    // 仅在每帧第一个 pass 执行（s_passCountThisFrame == 0）。
    // 异常帧过滤（>80ms 加载画面 / ≤0.5ms）→ EWMA(~30帧) 平滑 → 非对称滞后：
    // 有效帧率 < 95% 目标持续 1.0s → 降 5%；> 115% 目标持续 3.0s → 升 5%；
    // 每次调整后进入冷却（默认 2.0s），档位切换靠多槽位缓存实现 0ms 瞬时切换。
    static LARGE_INTEGER s_lastFrameStartQpc = {};
    static float s_smoothedFrameTimeMs = 16.667f;
    static float s_smoothedFps = 60.0f;
    static float s_smoothedEffectiveFps = 60.0f;
    static bool s_hasValidFrameTime = false;
    static float s_cooldownRemainingSec = 0.0f;
    static float s_deficitDurationSec = 0.0f;
    static float s_surplusDurationSec = 0.0f;
    static uint32_t s_governorState = 0; // 0=Disabled, 1=Stable, 2=Cooldown, 3=Downscaling, 4=Upscaling
    static bool s_prevGovState = false;

    bool currentGov = g_enableGovernor.load();
    if (s_prevGovState && !currentGov) {
        FlushSecondaryTierSlots();
    }
    s_prevGovState = currentGov;

    if (s_passCountThisFrame == 0) {
        double rawFrameTimeMs = 0.0;
        if (s_lastFrameStartQpc.QuadPart > 0) {
            rawFrameTimeMs = (double)(nowQpc.QuadPart - s_lastFrameStartQpc.QuadPart) * 1000.0 / (double)s_qpcFreq.QuadPart;
        }
        s_lastFrameStartQpc = nowQpc;

        // Outlier filter: discard frame deltas > 80ms (loading screens, pause menus, alt-tab) to avoid false downscaling
        bool isOutlier = (rawFrameTimeMs <= 0.5 || rawFrameTimeMs > 80.0);

        if (!isOutlier && rawFrameTimeMs > 0.0) {
            if (!s_hasValidFrameTime) {
                s_smoothedFrameTimeMs = (float)rawFrameTimeMs;
                s_hasValidFrameTime = true;
            } else {
                // EWMA over ~30 frames: alpha = 2 / (30 + 1) = ~0.0645
                constexpr float EWMA_ALPHA = 0.0645f;
                s_smoothedFrameTimeMs = s_smoothedFrameTimeMs * (1.0f - EWMA_ALPHA) + (float)rawFrameTimeMs * EWMA_ALPHA;
            }
            s_smoothedFps = (s_smoothedFrameTimeMs > 0.1f) ? (1000.0f / s_smoothedFrameTimeMs) : 0.0f;

            float dt = (float)(rawFrameTimeMs / 1000.0);
            if (dt > 0.1f) dt = 0.1f;

            float effectiveFps = s_smoothedFps;
            bool fgMode = g_enableGovernorFgMode.load();
            float fgMult = g_governorFgMultiplier.load();
            if (fgMult < 1.0f) fgMult = 1.0f;
            if (fgMode) {
                effectiveFps = s_smoothedFps * fgMult;
            }
            s_smoothedEffectiveFps = effectiveFps;

            if (currentGov) {
                float targetFps = g_governorTargetFps.load();
                if (targetFps < 30.0f) targetFps = 30.0f;
                if (targetFps > 240.0f) targetFps = 240.0f;

                float minScale = g_governorMinScale.load();
                if (minScale < 0.25f) minScale = 0.25f;
                if (minScale > 2.00f) minScale = 2.00f;

                float maxScale = g_governorMaxScale.load();
                if (maxScale < minScale) maxScale = minScale;
                if (maxScale > 2.00f) maxScale = 2.00f;

                float hystSec = g_governorHysteresisSec.load();
                if (hystSec < 0.5f) hystSec = 0.5f;

                float curScale = g_scale.load();
                float snappedScale = roundf(curScale * 20.0f) / 20.0f;
                if (snappedScale < minScale) snappedScale = minScale;
                if (snappedScale > maxScale) snappedScale = maxScale;
                if (fabsf(snappedScale - curScale) > 0.001f) {
                    curScale = snappedScale;
                    g_scale.store(curScale);
                }

                if (s_cooldownRemainingSec > 0.0f) {
                    s_cooldownRemainingSec -= dt;
                    if (s_cooldownRemainingSec < 0.0f) s_cooldownRemainingSec = 0.0f;
                    s_deficitDurationSec = 0.0f;
                    s_surplusDurationSec = 0.0f;
                    s_governorState = (s_cooldownRemainingSec > 0.0f) ? 2 : 1; // 2=Cooldown, 1=Stable
                } else {
                    float deficitThreshold = 0.95f * targetFps;
                    float surplusThreshold = 1.15f * targetFps;

                    bool canStepDown = (curScale > minScale + 0.02f);
                    bool canStepUp = (curScale < maxScale - 0.02f);

                    if (effectiveFps < deficitThreshold && canStepDown) {
                        s_deficitDurationSec += dt;
                        s_surplusDurationSec = 0.0f;
                        s_governorState = 3; // Downscaling

                        if (s_deficitDurationSec >= 1.0f - 0.005f) {
                            float nextScale = roundf((curScale - 0.05f) * 20.0f) / 20.0f;
                            if (nextScale < minScale) nextScale = minScale;
                            curScale = nextScale;
                            g_scale.store(curScale);

                            s_deficitDurationSec = 0.0f;
                            s_cooldownRemainingSec = hystSec;
                            s_governorState = 2; // Cooldown
                            if (fgMode) {
                                Log("[Proxy] Governor: Stepped DOWN to %.2f (Display FPS=%.1f [Base=%.1f, FG=%.1fx] < Target=%.1f)",
                                    curScale, effectiveFps, s_smoothedFps, fgMult, targetFps);
                            } else {
                                Log("[Proxy] Governor: Stepped DOWN to %.2f (FPS=%.1f < Target=%.1f)", curScale, s_smoothedFps, targetFps);
                            }
                        }
                    }
                    else if (effectiveFps > surplusThreshold && canStepUp) {
                        s_surplusDurationSec += dt;
                        s_deficitDurationSec = 0.0f;
                        s_governorState = 4; // Upscaling

                        if (s_surplusDurationSec >= 3.0f - 0.005f) {
                            float nextScale = roundf((curScale + 0.05f) * 20.0f) / 20.0f;
                            if (nextScale > maxScale) nextScale = maxScale;
                            curScale = nextScale;
                            g_scale.store(curScale);

                            s_surplusDurationSec = 0.0f;
                            s_cooldownRemainingSec = hystSec;
                            s_governorState = 2; // Cooldown
                            if (fgMode) {
                                Log("[Proxy] Governor: Stepped UP to %.2f (Display FPS=%.1f [Base=%.1f, FG=%.1fx] > Target=%.1f)",
                                    curScale, effectiveFps, s_smoothedFps, fgMult, targetFps);
                            } else {
                                Log("[Proxy] Governor: Stepped UP to %.2f (FPS=%.1f > Target=%.1f)", curScale, s_smoothedFps, targetFps);
                            }
                        }
                    }
                    else {
                        s_deficitDurationSec = 0.0f;
                        s_surplusDurationSec = 0.0f;
                        s_governorState = 1; // Stable
                    }
                }

                int rawTier = (int)roundf((curScale - minScale) * 20.0f);
                uint32_t currentTier = (rawTier > 0) ? (uint32_t)rawTier : 0u;
                g_governorCurrentTier.store(currentTier);
            } else {
                s_governorState = 0; // Disabled
                s_cooldownRemainingSec = 0.0f;
                s_deficitDurationSec = 0.0f;
                s_surplusDurationSec = 0.0f;
                g_governorCurrentTier.store(0);
            }
        } else if (isOutlier) {
            s_deficitDurationSec = 0.0f;
            s_surplusDurationSec = 0.0f;
        }

        if (g_proxySharedConfig && g_proxySharedConfig->magic == DLSSNR_MAGIC) {
            g_proxySharedConfig->debugMeasuredFps = s_smoothedFps;
            g_proxySharedConfig->debugMeasuredFrameTimeMs = s_smoothedFrameTimeMs;
            g_proxySharedConfig->debugEffectiveFps = s_smoothedEffectiveFps;
            g_proxySharedConfig->debugGovernorState = s_governorState;
            g_proxySharedConfig->debugGovernorCooldownLeft = s_cooldownRemainingSec;
            if (currentGov) {
                g_proxySharedConfig->resolutionScale = g_scale.load();
                g_proxySharedConfig->governorCurrentTier = g_governorCurrentTier.load();
            } else {
                g_proxySharedConfig->governorCurrentTier = 0;
            }
        }
    }

    NVSDK_NGX_Parameter* params = (NVSDK_NGX_Parameter*)InParameters;

    ID3D12Resource* origColor = nullptr;
    ID3D12Resource* origOutput = nullptr;
    if (params) {
        params->Get("DLSSNR.Color", &origColor);
        params->Get("DLSSNR.Output", &origOutput);
        if (!origColor) params->Get("Color", &origColor);
        if (!origOutput) params->Get("Output", &origOutput);
    }

    if (!origColor || !origOutput || !InCmdList) {
        static bool s_loggedNoRes = false;
        if (!s_loggedNoRes) {
            Log("[Proxy] EvaluateInternal: Bailout (no res): origColor=%p, origOutput=%p, InCmdList=%p", origColor, origOutput, InCmdList);
            s_loggedNoRes = true;
        }
        return real_Evaluate(InCmdList, InFeatureHandle, InParameters, InCallback);
    }

    D3D12_RESOURCE_DESC colorDesc = origColor->GetDesc();
    D3D12_RESOURCE_DESC outDesc = origOutput->GetDesc();

    if (colorDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        outDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        colorDesc.Width == 0 || colorDesc.Height == 0)
    {
        static bool s_loggedDim = false;
        if (!s_loggedDim) {
            Log("[Proxy] EvaluateInternal: Bailout (bad dim): colorDim=%d (%llux%u), outDim=%d (%llux%u)",
                colorDesc.Dimension, colorDesc.Width, colorDesc.Height,
                outDesc.Dimension, outDesc.Width, outDesc.Height);
            s_loggedDim = true;
        }
        return real_Evaluate(InCmdList, InFeatureHandle, InParameters, InCallback);
    }

    uint32_t nativeW = (uint32_t)colorDesc.Width;
    uint32_t nativeH = colorDesc.Height;
    DXGI_FORMAT typedColorFormat = ToNonTypeless(colorDesc.Format);
    float currentScale = g_scale.load();
    bool isGovActive = g_enableGovernor.load();
    // Governor 激活时接管缩放，非均匀缩放被覆盖（上游同步行为）
    bool isAnamorphic = !isGovActive && g_enableAnamorphic.load();
    float currentScaleX = isAnamorphic ? g_scaleX.load() : currentScale;
    float currentScaleY = isAnamorphic ? g_scaleY.load() : currentScale;

    // Match the native-size tolerance used for workW/workH in both scaling modes.
    // (上游 7dc3dac：修复等比超采样 100% 判定容差不一致导致绕过缩放 NR 路径)
    bool isNativePassthrough =
        (fabsf(currentScaleX - 1.0f) < 0.005f && fabsf(currentScaleY - 1.0f) < 0.005f);

    // Pass through directly to real DLL when proxy is disabled OR scale is 100% native
    if (!g_enableProxy.load() || isNativePassthrough) {
        if (g_proxySharedConfig && g_proxySharedConfig->magic == DLSSNR_MAGIC) {
            g_proxySharedConfig->debugNativeW = nativeW;
            g_proxySharedConfig->debugNativeH = nativeH;
            g_proxySharedConfig->debugWorkW = nativeW;
            g_proxySharedConfig->debugWorkH = nativeH;
            g_proxySharedConfig->debugFormat = (uint32_t)typedColorFormat;
            g_proxySharedConfig->debugHasDepth = 0;
            g_proxySharedConfig->debugDepthW = 0;
            g_proxySharedConfig->debugDepthH = 0;
            g_proxySharedConfig->debugHasMVec = 0;
            g_proxySharedConfig->debugMvW = 0;
            g_proxySharedConfig->debugMvH = 0;
            g_proxySharedConfig->debugActiveSlot = 0;
        }
        return real_Evaluate(InCmdList, InFeatureHandle, InParameters, InCallback);
    }

    static int s_evalLogCount = 0;
    if (s_evalLogCount < 10) {
        s_evalLogCount++;
        Log("[Proxy] EvaluateInternal: scale=%.2f (Anamorphic=%d, scaleX=%.2f, scaleY=%.2f), params=%p, color=%p, output=%p, cmdList=%p",
            currentScale, isAnamorphic ? 1 : 0, currentScaleX, currentScaleY, params, origColor, origOutput, InCmdList);
    }

    ID3D12Device* device = nullptr;
    InCmdList->GetDevice(IID_PPV_ARGS(&device));
    if (!device) {
        static bool s_loggedDev = false;
        if (!s_loggedDev) {
            Log("[Proxy] EvaluateInternal: Bailout (GetDevice failed)");
            s_loggedDev = true;
        }
        return real_Evaluate(InCmdList, InFeatureHandle, InParameters, InCallback);
    }

    if (!InitD3D12Pipeline(device)) {
        static bool s_loggedPipe = false;
        if (!s_loggedPipe) {
            Log("[Proxy] EvaluateInternal: Bailout (InitD3D12Pipeline failed)");
            s_loggedPipe = true;
        }
        device->Release();
        return real_Evaluate(InCmdList, InFeatureHandle, InParameters, InCallback);
    }

    uint32_t workW = (fabsf(currentScaleX - 1.0f) < 0.005f) ? nativeW : (((uint32_t)roundf(nativeW * currentScaleX)) & ~1);
    uint32_t workH = (fabsf(currentScaleY - 1.0f) < 0.005f) ? nativeH : (((uint32_t)roundf(nativeH * currentScaleY)) & ~1);
    if (workW < 64) workW = 64;
    if (workH < 64) workH = 64;

    DXGI_FORMAT typedOutFormat = ToNonTypeless(outDesc.Format);
    DXGI_FORMAT scratchFormat = GetUavSafeScratchFormat(typedColorFormat);

    uint32_t currentPass = (s_passCountThisFrame < MAX_FEATURE_SLOTS) ? s_passCountThisFrame : (MAX_FEATURE_SLOTS - 1);

    // Multi-Slot Lookup: Find slot matching this game handle, pass index, resolution, and color format
    FeatureSlot* slot = nullptr;
    if (isGovActive) {
        // Multi-tier slot caching: match tier resolution (workW, workH) for instantaneous 0ms pointer swaps
        if (InFeatureHandle) {
            for (size_t i = 0; i < MAX_FEATURE_SLOTS; ++i) {
                if (g_slots[i].inUse &&
                    g_slots[i].origGameHandle == InFeatureHandle &&
                    g_slots[i].passIndex == currentPass &&
                    g_slots[i].nativeW == nativeW &&
                    g_slots[i].nativeH == nativeH &&
                    g_slots[i].colorFormat == typedColorFormat &&
                    g_slots[i].workW == workW &&
                    g_slots[i].workH == workH)
                {
                    slot = &g_slots[i];
                    break;
                }
            }
        }
        if (!slot) {
            for (size_t i = 0; i < MAX_FEATURE_SLOTS; ++i) {
                if (g_slots[i].inUse &&
                    g_slots[i].passIndex == currentPass &&
                    g_slots[i].nativeW == nativeW &&
                    g_slots[i].nativeH == nativeH &&
                    g_slots[i].colorFormat == typedColorFormat &&
                    g_slots[i].workW == workW &&
                    g_slots[i].workH == workH)
                {
                    slot = &g_slots[i];
                    break;
                }
            }
        }
    } else {
        // Governor OFF: Single slot per pass (0 extra VRAM)
        if (InFeatureHandle) {
            for (size_t i = 0; i < MAX_FEATURE_SLOTS; ++i) {
                if (g_slots[i].inUse &&
                    g_slots[i].origGameHandle == InFeatureHandle &&
                    g_slots[i].passIndex == currentPass &&
                    g_slots[i].nativeW == nativeW &&
                    g_slots[i].nativeH == nativeH &&
                    g_slots[i].colorFormat == typedColorFormat)
                {
                    slot = &g_slots[i];
                    break;
                }
            }
        }
        if (!slot) {
            for (size_t i = 0; i < MAX_FEATURE_SLOTS; ++i) {
                if (g_slots[i].inUse &&
                    g_slots[i].passIndex == currentPass &&
                    g_slots[i].nativeW == nativeW &&
                    g_slots[i].nativeH == nativeH &&
                    g_slots[i].colorFormat == typedColorFormat)
                {
                    slot = &g_slots[i];
                    break;
                }
            }
        }
    }

    // Allocate slot if not found
    if (!slot) {
        for (size_t i = 0; i < MAX_FEATURE_SLOTS; ++i) {
            if (!g_slots[i].inUse) {
                slot = &g_slots[i];
                break;
            }
        }
        if (!slot) {
            size_t lruIdx = 0;
            ULONGLONG oldest = g_slots[0].lastUsedTick;
            for (size_t i = 1; i < MAX_FEATURE_SLOTS; ++i) {
                if (g_slots[i].lastUsedTick < oldest) {
                    oldest = g_slots[i].lastUsedTick;
                    lruIdx = i;
                }
            }
            slot = &g_slots[lruIdx];
            ReleaseSlotResources(*slot);
        }
        slot->inUse = true;
        slot->passIndex = currentPass;
        slot->origGameHandle = InFeatureHandle;
        slot->nativeW = nativeW;
        slot->nativeH = nativeH;
        slot->colorFormat = typedColorFormat;
        slot->scratchFormat = scratchFormat;
        slot->scale = 0.0f;
        slot->workW = 0;
        slot->workH = 0;
    }

    slot->origGameHandle = InFeatureHandle;
    slot->lastUsedTick = GetTickCount64();

    bool scaleChanged = (fabsf(slot->scale - currentScale) > 0.005f) || (slot->workW != workW) || (slot->workH != workH);
    bool formatChanged = (slot->colorFormat != typedColorFormat || slot->scratchFormat != scratchFormat);
    bool sizeChanged = (slot->nativeW != nativeW || slot->nativeH != nativeH);
    bool needRecreate = scaleChanged || formatChanged || sizeChanged || !slot->activeFeature;

    slot->colorFormat = typedColorFormat;
    slot->scratchFormat = scratchFormat;
    slot->nativeW = nativeW;
    slot->nativeH = nativeH;

    bool isScalingActive = (workW != nativeW || workH != nativeH);

    if (isScalingActive) {
        if (!slot->colorSmall || slot->workW != workW || slot->workH != workH) {
            ULONGLONG now = GetTickCount64();
            if (slot->allocFailed && (now - slot->lastAllocAttemptTick < 2000)) {
                // Wait during backoff after previous allocation failure
            } else {
                ReleaseSlotScratch(*slot);
                bool inPlace = (origColor == origOutput);
                bool outHasUav = (outDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;
                bool needsNativeScratch = (inPlace || !outHasUav);

                if (needsNativeScratch) {
                    if (!slot->nativeScratch || slot->nativeW != nativeW || slot->nativeH != nativeH) {
                        if (slot->nativeScratch) {
                            NrRetired r; r.resource = slot->nativeScratch; r.framesLeft = RETIRE_FRAME_DELAY; g_retiredList.push_back(r);
                            slot->nativeScratch = nullptr;
                        }
                        slot->nativeScratch = CreateScratchTexture(device, scratchFormat, nativeW, nativeH);
                    }
                } else if (slot->nativeScratch) {
                    NrRetired r; r.resource = slot->nativeScratch; r.framesLeft = RETIRE_FRAME_DELAY; g_retiredList.push_back(r);
                    slot->nativeScratch = nullptr;
                }
                slot->colorSmall = CreateScratchTexture(device, scratchFormat, workW, workH);
                slot->outputSmall = CreateScratchTexture(device, scratchFormat, workW, workH);
                if (!slot->colorSmall || !slot->outputSmall) {
                    slot->allocFailed = true;
                    slot->lastAllocAttemptTick = now;
                    Log("[Proxy] Scratch texture allocation failed for %ux%u, backing off for 2s", workW, workH);
                } else {
                    slot->allocFailed = false;
                    slot->lastAllocAttemptTick = 0;
                    slot->workW = workW;
                    slot->workH = workH;
                    slot->scratchFormat = scratchFormat;
                    needRecreate = true;
                    Log("[Proxy] Allocated slot %u textures: work=%ux%u, native=%ux%u (Format=%d, ScratchFormat=%d, Scale=%.2f, ScaleX=%.2f, ScaleY=%.2f)",
                        currentPass, workW, workH, nativeW, nativeH, typedColorFormat, scratchFormat, currentScale, currentScaleX, currentScaleY);
                }
            }
        }
    } else {
        if (slot->colorSmall) {
            ReleaseSlotResources(*slot);
            needRecreate = true;
        }
    }

    if (!slot->colorSmall || !slot->outputSmall) {
        device->Release();
        return real_Evaluate(InCmdList, InFeatureHandle, InParameters, InCallback);
    }

    if (needRecreate) {
        if (slot->activeFeature) {
            ParkNrFeature(slot->activeFeature);
        }

        uint32_t origDlssW = nativeW, origDlssH = nativeH;
        params->Get("DLSSNR.Width", &origDlssW);
        params->Get("DLSSNR.Height", &origDlssH);

        uint32_t origColorSubW = nativeW, origColorSubH = nativeH;
        params->Get("DLSSNR.ColorSubrectWidth", &origColorSubW);
        params->Get("DLSSNR.ColorSubrectHeight", &origColorSubH);

        uint32_t origOutSubW = nativeW, origOutSubH = nativeH;
        params->Get("DLSSNR.OutputSubrectWidth", &origOutSubW);
        params->Get("DLSSNR.OutputSubrectHeight", &origOutSubH);

        params->Set("DLSSNR.Width", workW);
        params->Set("DLSSNR.Height", workH);
        params->Set("DLSSNR.ColorSubrectBaseX", 0u);
        params->Set("DLSSNR.ColorSubrectBaseY", 0u);
        params->Set("DLSSNR.ColorSubrectWidth", workW);
        params->Set("DLSSNR.ColorSubrectHeight", workH);
        params->Set("DLSSNR.OutputSubrectBaseX", 0u);
        params->Set("DLSSNR.OutputSubrectBaseY", 0u);
        params->Set("DLSSNR.OutputSubrectWidth", workW);
        params->Set("DLSSNR.OutputSubrectHeight", workH);

        // Guide buffer subrect synchronization during feature creation
        ID3D12Resource* depthResCreate = nullptr;
        if (params->Get("DLSSNR.Depth", &depthResCreate) != 0 || !depthResCreate) {
            params->Get("Depth", &depthResCreate);
        }
        uint32_t origDepthSubW = 0, origDepthSubH = 0;
        if (depthResCreate) {
            params->Get("DLSSNR.DepthSubrectWidth", &origDepthSubW);
            params->Get("DLSSNR.DepthSubrectHeight", &origDepthSubH);
            D3D12_RESOURCE_DESC dDesc = depthResCreate->GetDesc();
            uint32_t dW = origDepthSubW ? origDepthSubW : (uint32_t)dDesc.Width;
            uint32_t dH = origDepthSubH ? origDepthSubH : dDesc.Height;
            params->Set("DLSSNR.DepthSubrectBaseX", 0u);
            params->Set("DLSSNR.DepthSubrectBaseY", 0u);
            params->Set("DLSSNR.DepthSubrectWidth", dW);
            params->Set("DLSSNR.DepthSubrectHeight", dH);
        }

        ID3D12Resource* mvecResCreate = nullptr;
        if (params->Get("DLSSNR.MotionVectors", &mvecResCreate) != 0 || !mvecResCreate) {
            if (params->Get("DLSSNR.MVec", &mvecResCreate) != 0 || !mvecResCreate) {
                params->Get("MotionVectors", &mvecResCreate);
            }
        }
        uint32_t origMvSubW = 0, origMvSubH = 0;
        if (mvecResCreate) {
            params->Get("DLSSNR.MVecSubrectWidth", &origMvSubW);
            params->Get("DLSSNR.MVecSubrectHeight", &origMvSubH);
            D3D12_RESOURCE_DESC mDesc = mvecResCreate->GetDesc();
            uint32_t mW = origMvSubW ? origMvSubW : (uint32_t)mDesc.Width;
            uint32_t mH = origMvSubH ? origMvSubH : mDesc.Height;
            params->Set("DLSSNR.MVecSubrectBaseX", 0u);
            params->Set("DLSSNR.MVecSubrectBaseY", 0u);
            params->Set("DLSSNR.MVecSubrectWidth", mW);
            params->Set("DLSSNR.MVecSubrectHeight", mH);
        }

        if (isScalingActive) {
            params->Set("DLSSNR.Color", slot->colorSmall);
            params->Set("DLSSNR.Output", slot->outputSmall);
        } else {
            params->Set("DLSSNR.Color", origColor);
            params->Set("DLSSNR.Output", origOutput);
        }

        // Apply official DLSS-NR model settings (only when user opts in; otherwise caller's values pass through)
        if (g_useCustomNR.load()) {
            params->Set("DLSSNR.Style", g_nrStyle.load());
            params->Set("DLSSNR.Intensity", g_nrIntensity.load());
            params->Set("DLSSNR.LocalStructureStrength", g_nrLocalStructureStrength.load());
            params->Set("DLSSNR.LocalToneStrength", g_nrLocalToneStrength.load());
            if (g_nrSkinStructureStrength.load() >= 0.0f) {
                params->Set("DLSSNR.SkinStructureStrength", g_nrSkinStructureStrength.load());
            }
            params->Set("DLSSNR.UseAutoMask", g_nrUseAutoMask.load());
        }

        int createRes = real_Create(InCmdList, 18, params, &slot->activeFeature);
        Log("[Proxy] Created neural feature in slot %u (%ux%u -> native %ux%u, scale=%.2f, scaleX=%.2f, scaleY=%.2f): res=0x%X, handle=%p",
            currentPass, workW, workH, nativeW, nativeH, currentScale, currentScaleX, currentScaleY, createRes, slot->activeFeature);

        params->Set("DLSSNR.Color", origColor);
        params->Set("DLSSNR.Output", origOutput);
        params->Set("DLSSNR.Width", origDlssW ? origDlssW : nativeW);
        params->Set("DLSSNR.Height", origDlssH ? origDlssH : nativeH);
        params->Set("DLSSNR.ColorSubrectWidth", origColorSubW ? origColorSubW : nativeW);
        params->Set("DLSSNR.ColorSubrectHeight", origColorSubH ? origColorSubH : nativeH);
        params->Set("DLSSNR.OutputSubrectWidth", origOutSubW ? origOutSubW : nativeW);
        params->Set("DLSSNR.OutputSubrectHeight", origOutSubH ? origOutSubH : nativeH);
        if (depthResCreate && origDepthSubW > 0) {
            params->Set("DLSSNR.DepthSubrectWidth", origDepthSubW);
            params->Set("DLSSNR.DepthSubrectHeight", origDepthSubH);
        }
        if (mvecResCreate && origMvSubW > 0) {
            params->Set("DLSSNR.MVecSubrectWidth", origMvSubW);
            params->Set("DLSSNR.MVecSubrectHeight", origMvSubH);
        }

        if (NVSDK_NGX_FAILED(createRes) || !slot->activeFeature) {
            static ULONGLONG s_lastCreateFailTick = 0;
            ULONGLONG now = GetTickCount64();
            if (now - s_lastCreateFailTick > 2000) {
                Log("[Proxy] real_Create failed (0x%X), falling back to native passthrough", createRes);
                s_lastCreateFailTick = now;
            }
            slot->activeFeature = nullptr;
            slot->scale = currentScale;
            device->Release();
            return real_Evaluate(InCmdList, InFeatureHandle, InParameters, InCallback);
        }

        slot->scale = currentScale;
        slot->skipEvaluateFrames = 0;
    }

    device->Release();

    if (!slot->colorSmall || !slot->outputSmall || !slot->activeFeature) {
        return real_Evaluate(InCmdList, InFeatureHandle, InParameters, InCallback);
    }

    // Save original parameters
    float origMvX = 1.0f, origMvY = 1.0f;
    uint32_t origDlssW = nativeW, origDlssH = nativeH;

    uint32_t origColorBaseX = 0, origColorBaseY = 0, origColorSubW = nativeW, origColorSubH = nativeH;
    uint32_t origOutBaseX = 0, origOutBaseY = 0, origOutSubW = nativeW, origOutSubH = nativeH;
    uint32_t origDepthBaseX = 0, origDepthBaseY = 0, origDepthSubW = 0, origDepthSubH = 0;
    uint32_t origMvBaseX = 0, origMvBaseY = 0, origMvSubW = 0, origMvSubH = 0;

    if (params->Get("DLSSNR.MVecScaleX", &origMvX) != 0) params->Get("MVecScaleX", &origMvX);
    if (params->Get("DLSSNR.MVecScaleY", &origMvY) != 0) params->Get("MVecScaleY", &origMvY);
    params->Get("DLSSNR.Width", &origDlssW);
    params->Get("DLSSNR.Height", &origDlssH);

    params->Get("DLSSNR.ColorSubrectBaseX", &origColorBaseX);
    params->Get("DLSSNR.ColorSubrectBaseY", &origColorBaseY);
    params->Get("DLSSNR.ColorSubrectWidth", &origColorSubW);
    params->Get("DLSSNR.ColorSubrectHeight", &origColorSubH);

    params->Get("DLSSNR.OutputSubrectBaseX", &origOutBaseX);
    params->Get("DLSSNR.OutputSubrectBaseY", &origOutBaseY);
    params->Get("DLSSNR.OutputSubrectWidth", &origOutSubW);
    params->Get("DLSSNR.OutputSubrectHeight", &origOutSubH);

    params->Get("DLSSNR.DepthSubrectBaseX", &origDepthBaseX);
    params->Get("DLSSNR.DepthSubrectBaseY", &origDepthBaseY);
    params->Get("DLSSNR.DepthSubrectWidth", &origDepthSubW);
    params->Get("DLSSNR.DepthSubrectHeight", &origDepthSubH);

    params->Get("DLSSNR.MVecSubrectBaseX", &origMvBaseX);
    params->Get("DLSSNR.MVecSubrectBaseY", &origMvBaseY);
    params->Get("DLSSNR.MVecSubrectWidth", &origMvSubW);
    params->Get("DLSSNR.MVecSubrectHeight", &origMvSubH);

    // Query G-buffers if present (e.g. RenoDX Upscaled hook)
    ID3D12Resource* depthRes = nullptr;
    if (params->Get("DLSSNR.Depth", &depthRes) != 0 || !depthRes) {
        params->Get("Depth", &depthRes);
    }

    ID3D12Resource* mvecRes = nullptr;
    if (params->Get("DLSSNR.MVec", &mvecRes) != 0 || !mvecRes) {
        if (params->Get("MotionVectors", &mvecRes) != 0 || !mvecRes) {
            params->Get("DLSSNR.MotionVectors", &mvecRes);
        }
    }

    uint32_t actualDepthW = origDepthSubW, actualDepthH = origDepthSubH;
    if (depthRes) {
        D3D12_RESOURCE_DESC dDesc = depthRes->GetDesc();
        actualDepthW = origDepthSubW ? origDepthSubW : (uint32_t)dDesc.Width;
        actualDepthH = origDepthSubH ? origDepthSubH : dDesc.Height;
    }

    uint32_t actualMvW = origMvSubW, actualMvH = origMvSubH;
    if (mvecRes) {
        D3D12_RESOURCE_DESC mDesc = mvecRes->GetDesc();
        actualMvW = origMvSubW ? origMvSubW : (uint32_t)mDesc.Width;
        actualMvH = origMvSubH ? origMvSubH : mDesc.Height;
    }

    static bool s_loggedGbuffers = false;
    if (!s_loggedGbuffers && (depthRes || mvecRes)) {
        s_loggedGbuffers = true;
        Log("[Proxy] G-buffers detected on evaluate: Depth=%p (%ux%u), MVec=%p (%ux%u)",
            depthRes, actualDepthW, actualDepthH, mvecRes, actualMvW, actualMvH);
    }

    static uint64_t s_evaluateFrameIndex = 0;
    if (s_passCountThisFrame == 0) {
        s_evaluateFrameIndex++;
    }

    bool isVrnrActive = g_enableProxy.load() && g_enableVrnr.load() && isScalingActive;
    bool isSkipFrame = (isVrnrActive && slot->hasEvaluatedOnce && ((s_evaluateFrameIndex % 2) == 1));

    // 0.7.1 防闪烁修复（P0 时间坡道）：坡道只在【连续 ≥2 帧 skip】时生效。
    // 隔帧交替模式（skip 间隔 2）下每张 skip 帧的连续计数恒为 1，旧逻辑给每张
    // skip 帧打 0.9 折，与 NR 帧的 1.0 形成 10% 周期脉动（人物闪烁主因）。
    // 现在 run==1 → 1.0 满强度；run>=2（仅间隔 3+ 模式）→ 90% / 72% / 55% 缓降。
    static int s_vrnrSkipRun = 0;
    if (isVrnrActive) {
        if (s_passCountThisFrame == 0) {
            if (isSkipFrame) ++s_vrnrSkipRun;
            else             s_vrnrSkipRun = 0;
        }
    } else {
        s_vrnrSkipRun = 0;
    }
    float vrnrRamp = 1.0f;
    if (g_vrnrAntiFlicker.load() && s_vrnrSkipRun >= 2) {
        if      (s_vrnrSkipRun == 2) vrnrRamp = 0.90f;
        else if (s_vrnrSkipRun == 3) vrnrRamp = 0.72f;
        else                         vrnrRamp = 0.55f;
    }

    // Update live telemetry for companion UI
    if (g_proxySharedConfig && g_proxySharedConfig->magic == DLSSNR_MAGIC) {
        g_proxySharedConfig->debugNativeW = nativeW;
        g_proxySharedConfig->debugNativeH = nativeH;
        g_proxySharedConfig->debugWorkW = workW;
        g_proxySharedConfig->debugWorkH = workH;
        g_proxySharedConfig->debugFormat = (uint32_t)typedColorFormat;
        g_proxySharedConfig->debugHasDepth = depthRes ? 1 : 0;
        g_proxySharedConfig->debugDepthW = depthRes ? actualDepthW : 0;
        g_proxySharedConfig->debugDepthH = depthRes ? actualDepthH : 0;
        g_proxySharedConfig->debugHasMVec = mvecRes ? 1 : 0;
        g_proxySharedConfig->debugMvW = mvecRes ? actualMvW : 0;
        g_proxySharedConfig->debugMvH = mvecRes ? actualMvH : 0;
        g_proxySharedConfig->debugActiveSlot = (uint32_t)(slot - g_slots);
        g_proxySharedConfig->debugVrnrSkippedThisFrame = isSkipFrame ? 1 : 0;
        g_proxySharedConfig->debugMeasuredFps = s_smoothedFps;
        g_proxySharedConfig->debugMeasuredFrameTimeMs = s_smoothedFrameTimeMs;
        g_proxySharedConfig->debugEffectiveFps = s_smoothedEffectiveFps;
        g_proxySharedConfig->debugGovernorState = s_governorState;
        g_proxySharedConfig->debugGovernorCooldownLeft = s_cooldownRemainingSec;
        if (currentGov) {
            g_proxySharedConfig->resolutionScale = g_scale.load();
            g_proxySharedConfig->governorCurrentTier = g_governorCurrentTier.load();
        } else {
            g_proxySharedConfig->governorCurrentTier = 0;
        }
    }

    float mvFactorX = (float)workW / (float)nativeW;
    float mvFactorY = (float)workH / (float)nativeH;

    auto RestoreParameters = [&]() {
        params->Set("DLSSNR.Color", origColor);
        params->Set("DLSSNR.Output", origOutput);

        params->Set("DLSSNR.Width", origDlssW ? origDlssW : nativeW);
        params->Set("DLSSNR.Height", origDlssH ? origDlssH : nativeH);

        params->Set("DLSSNR.ColorSubrectBaseX", origColorBaseX);
        params->Set("DLSSNR.ColorSubrectBaseY", origColorBaseY);
        params->Set("DLSSNR.ColorSubrectWidth", origColorSubW ? origColorSubW : nativeW);
        params->Set("DLSSNR.ColorSubrectHeight", origColorSubH ? origColorSubH : nativeH);

        params->Set("DLSSNR.OutputSubrectBaseX", origOutBaseX);
        params->Set("DLSSNR.OutputSubrectBaseY", origOutBaseY);
        params->Set("DLSSNR.OutputSubrectWidth", origOutSubW ? origOutSubW : nativeW);
        params->Set("DLSSNR.OutputSubrectHeight", origOutSubH ? origOutSubH : nativeH);

        if (depthRes) {
            params->Set("DLSSNR.DepthSubrectBaseX", origDepthBaseX);
            params->Set("DLSSNR.DepthSubrectBaseY", origDepthBaseY);
            params->Set("DLSSNR.DepthSubrectWidth", origDepthSubW ? origDepthSubW : actualDepthW);
            params->Set("DLSSNR.DepthSubrectHeight", origDepthSubH ? origDepthSubH : actualDepthH);
        }

        if (mvecRes) {
            params->Set("DLSSNR.MVecSubrectBaseX", origMvBaseX);
            params->Set("DLSSNR.MVecSubrectBaseY", origMvBaseY);
            params->Set("DLSSNR.MVecSubrectWidth", origMvSubW ? origMvSubW : actualMvW);
            params->Set("DLSSNR.MVecSubrectHeight", origMvSubH ? origMvSubH : actualMvH);
        }

        params->Set("DLSSNR.MVecScaleX", origMvX);
        params->Set("DLSSNR.MVecScaleY", origMvY);
    };

    // Multi-pass transition barrier:
    // If this is a subsequent pass on the same command list and origColor was written
    // as a UAV output in the immediately preceding pass, transition it from UAV to SRV.
    bool origColorWasUavOutput = (s_passCountThisFrame > 0 && s_lastResolveOutput && origColor == s_lastResolveOutput);
    if (origColorWasUavOutput) {
        TransitionBarrier(InCmdList, origColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    int result = 1; // NVSDK_NGX_Result_Success

    UINT descSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    static uint32_t s_frameSlot = 0;
    s_frameSlot = (s_frameSlot + 1) % 64;
    uint32_t baseSlot = s_frameSlot * 8;

    D3D12_CPU_DESCRIPTOR_HANDLE heapCpuStart = g_descHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE heapGpuStart = g_descHeap->GetGPUDescriptorHandleForHeapStart();
    ID3D12DescriptorHeap* heaps[] = { g_descHeap };

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    if (!isSkipFrame) {
        // Pass 1: Downsample native color to scratch input
        TransitionBarrier(InCmdList, slot->colorSmall, slot->colorSmallState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        slot->colorSmallState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle0 = { heapCpuStart.ptr + (baseSlot + 0) * descSize };
        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle1 = { heapCpuStart.ptr + (baseSlot + 1) * descSize };
        D3D12_GPU_DESCRIPTOR_HANDLE gpuHandleDown = { heapGpuStart.ptr + (baseSlot + 0) * descSize };

        srvDesc.Format = typedColorFormat;
        g_device->CreateShaderResourceView(origColor, &srvDesc, cpuHandle0);

        uavDesc.Format = scratchFormat;
        g_device->CreateUnorderedAccessView(slot->colorSmall, nullptr, &uavDesc, cpuHandle1);

        InCmdList->SetComputeRootSignature(g_rootSigDownsample);
        InCmdList->SetDescriptorHeaps(1, heaps);

        DownsampleConstants downConstants = { nativeW, nativeH, workW, workH };
        InCmdList->SetComputeRoot32BitConstants(0, 4, &downConstants, 0);
        InCmdList->SetComputeRootDescriptorTable(1, gpuHandleDown);
        InCmdList->SetPipelineState(g_psoDownsample);
        InCmdList->Dispatch((workW + 7) / 8, (workH + 7) / 8, 1);

        TransitionBarrier(InCmdList, slot->colorSmall, slot->colorSmallState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        slot->colorSmallState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        TransitionBarrier(InCmdList, slot->outputSmall, slot->outputSmallState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        slot->outputSmallState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        // Apply official DLSS-NR model settings (only when user opts in; otherwise caller's values pass through)
        if (g_useCustomNR.load()) {
            params->Set("DLSSNR.Style", g_nrStyle.load());
            params->Set("DLSSNR.Intensity", g_nrIntensity.load());
            params->Set("DLSSNR.LocalStructureStrength", g_nrLocalStructureStrength.load());
            params->Set("DLSSNR.LocalToneStrength", g_nrLocalToneStrength.load());
            if (g_nrSkinStructureStrength.load() >= 0.0f) {
                params->Set("DLSSNR.SkinStructureStrength", g_nrSkinStructureStrength.load());
            }
            params->Set("DLSSNR.UseAutoMask", g_nrUseAutoMask.load());
        }

        params->Set("DLSSNR.Color", slot->colorSmall);
        params->Set("DLSSNR.Output", slot->outputSmall);

        params->Set("DLSSNR.Width", workW);
        params->Set("DLSSNR.Height", workH);

        params->Set("DLSSNR.ColorSubrectBaseX", 0u);
        params->Set("DLSSNR.ColorSubrectBaseY", 0u);
        params->Set("DLSSNR.ColorSubrectWidth", workW);
        params->Set("DLSSNR.ColorSubrectHeight", workH);

        params->Set("DLSSNR.OutputSubrectBaseX", 0u);
        params->Set("DLSSNR.OutputSubrectBaseY", 0u);
        params->Set("DLSSNR.OutputSubrectWidth", workW);
        params->Set("DLSSNR.OutputSubrectHeight", workH);

        if (depthRes && actualDepthW > 0 && actualDepthH > 0) {
            params->Set("DLSSNR.DepthSubrectBaseX", origDepthBaseX);
            params->Set("DLSSNR.DepthSubrectBaseY", origDepthBaseY);
            params->Set("DLSSNR.DepthSubrectWidth", actualDepthW);
            params->Set("DLSSNR.DepthSubrectHeight", actualDepthH);
        }

        if (mvecRes && actualMvW > 0 && actualMvH > 0) {
            params->Set("DLSSNR.MVecSubrectBaseX", origMvBaseX);
            params->Set("DLSSNR.MVecSubrectBaseY", origMvBaseY);
            params->Set("DLSSNR.MVecSubrectWidth", actualMvW);
            params->Set("DLSSNR.MVecSubrectHeight", actualMvH);
        }

        params->Set("DLSSNR.MVecScaleX", origMvX * mvFactorX);
        params->Set("DLSSNR.MVecScaleY", origMvY * mvFactorY);

        result = real_Evaluate(InCmdList, slot->activeFeature, InParameters, InCallback);

        // If neural evaluate failed, skip resolve pass to avoid corrupting output
        if (NVSDK_NGX_FAILED(result)) {
            Log("[Proxy] real_Evaluate failed (0x%X), skipping resolve pass", result);
            RestoreParameters();
            return result;
        }

        slot->hasEvaluatedOnce = true;

        TransitionBarrier(InCmdList, slot->outputSmall, slot->outputSmallState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        slot->outputSmallState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    bool inPlace = (origColor == origOutput);
    bool outHasUav = (outDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0;

    ID3D12Resource* resolveReadSource = origColor;
    ID3D12Resource* resolveWriteDest = origOutput;
    DXGI_FORMAT resolveReadFormat = typedColorFormat;
    DXGI_FORMAT resolveWriteFormat = ToUavCompatibleFormat(typedOutFormat);

    if (inPlace && slot->nativeScratch && colorDesc.Format == slot->nativeScratch->GetDesc().Format) {
        TransitionBarrier(InCmdList, origColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        TransitionBarrier(InCmdList, slot->nativeScratch, slot->nativeScratchState, D3D12_RESOURCE_STATE_COPY_DEST);
        slot->nativeScratchState = D3D12_RESOURCE_STATE_COPY_DEST;
        InCmdList->CopyResource(slot->nativeScratch, origColor);

        TransitionBarrier(InCmdList, slot->nativeScratch, slot->nativeScratchState, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        slot->nativeScratchState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        TransitionBarrier(InCmdList, origColor, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        resolveReadSource = slot->nativeScratch;
        resolveReadFormat = scratchFormat;
    }
    else if (!outHasUav && slot->nativeScratch) {
        TransitionBarrier(InCmdList, slot->nativeScratch, slot->nativeScratchState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        slot->nativeScratchState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        resolveWriteDest = slot->nativeScratch;
        resolveWriteFormat = scratchFormat;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE cpuRes0 = { heapCpuStart.ptr + (baseSlot + 2) * descSize };
    D3D12_CPU_DESCRIPTOR_HANDLE cpuRes1 = { heapCpuStart.ptr + (baseSlot + 3) * descSize };
    D3D12_CPU_DESCRIPTOR_HANDLE cpuRes2 = { heapCpuStart.ptr + (baseSlot + 4) * descSize };
    D3D12_CPU_DESCRIPTOR_HANDLE cpuRes3 = { heapCpuStart.ptr + (baseSlot + 5) * descSize };
    D3D12_CPU_DESCRIPTOR_HANDLE cpuRes4 = { heapCpuStart.ptr + (baseSlot + 6) * descSize };
    D3D12_CPU_DESCRIPTOR_HANDLE cpuRes5 = { heapCpuStart.ptr + (baseSlot + 7) * descSize };
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandleResolve = { heapGpuStart.ptr + (baseSlot + 2) * descSize };

    srvDesc.Format = scratchFormat;
    g_device->CreateShaderResourceView(slot->colorSmall, &srvDesc, cpuRes0);
    g_device->CreateShaderResourceView(slot->outputSmall, &srvDesc, cpuRes1);

    srvDesc.Format = resolveReadFormat;
    g_device->CreateShaderResourceView(resolveReadSource, &srvDesc, cpuRes2);

    bool hasValidDepth = false;
    DXGI_FORMAT depthSrvFormat = DXGI_FORMAT_UNKNOWN;
    if (depthRes != nullptr && g_enableDepthAware.load()) {
        D3D12_RESOURCE_DESC dDesc = depthRes->GetDesc();
        depthSrvFormat = ToDepthSrvFormat(dDesc.Format);
        if (depthSrvFormat != DXGI_FORMAT_UNKNOWN &&
            (dDesc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0 &&
            (uint32_t)dDesc.Width == nativeW &&
            dDesc.Height == nativeH) {
            hasValidDepth = true;
        }
    }

    if (hasValidDepth) {
        D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
        depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        depthSrvDesc.Texture2D.MipLevels = 1;
        depthSrvDesc.Format = depthSrvFormat;
        g_device->CreateShaderResourceView(depthRes, &depthSrvDesc, cpuRes3);
    } else {
        srvDesc.Format = scratchFormat;
        g_device->CreateShaderResourceView(slot->colorSmall, &srvDesc, cpuRes3);
    }

    // 0.7.1 MV SRV (t4)：skip 帧重投影用。游戏把 MVec 传给 NGX 时必须已是
    // 可读状态（NGX 自身以 SRV 采样），故不加 barrier；不可用时绑 colorSmall 占位。
    bool hasValidMvec = false;
    DXGI_FORMAT mvecSrvFormat = DXGI_FORMAT_UNKNOWN;
    if (mvecRes != nullptr && isSkipFrame) {
        D3D12_RESOURCE_DESC mDescSrv = mvecRes->GetDesc();
        mvecSrvFormat = ToMVecSrvFormat(mDescSrv.Format);
        hasValidMvec = (mvecSrvFormat != DXGI_FORMAT_UNKNOWN) &&
                       (mDescSrv.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0;
    }
    if (hasValidMvec) {
        D3D12_SHADER_RESOURCE_VIEW_DESC mvSrvDesc = {};
        mvSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        mvSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        mvSrvDesc.Texture2D.MipLevels = 1;
        mvSrvDesc.Format = mvecSrvFormat;
        g_device->CreateShaderResourceView(mvecRes, &mvSrvDesc, cpuRes4);
    } else {
        srvDesc.Format = scratchFormat;
        g_device->CreateShaderResourceView(slot->colorSmall, &srvDesc, cpuRes4);
    }

    uavDesc.Format = resolveWriteFormat;
    g_device->CreateUnorderedAccessView(resolveWriteDest, nullptr, &uavDesc, cpuRes5);

    InCmdList->SetComputeRootSignature(g_rootSigResolve);
    InCmdList->SetDescriptorHeaps(1, heaps);

    ResolveConstants resConstants = {};
    resConstants.nativeWidth = nativeW;
    resConstants.nativeHeight = nativeH;
    resConstants.workWidth = workW;
    resConstants.workHeight = workH;
    // Multi-pass protection:
    // If this pass is writing to the same buffer in a multi-pass pipeline,
    // disable RCAS to prevent exponential edge ringing / shimmer.
    // Transfer strength is preserved full-power (never halved) to prevent multi-pass flickering.
    bool isSameBufferMultiPass = (s_passCountThisFrame > 0 && s_lastResolveOutput && origColor == s_lastResolveOutput);
    resConstants.transferStrength = g_transferStrength.load();
    resConstants.sharpness = isSameBufferMultiPass ? 0.0f : g_sharpness.load();
    resConstants.enlargementMode = g_enlargementMode.load();
    resConstants.colorStrength = g_colorStrength.load();
    resConstants.isSkipFrame = isSkipFrame ? 1 : 0;
    resConstants.hasDepth = hasValidDepth ? 1 : 0;
    resConstants.vrnrRamp = vrnrRamp;
    resConstants.vrnrAf = g_vrnrAntiFlicker.load() ? 1 : 0;
    resConstants.vrnrFloor = g_vrnrWeightFloor.load();
    resConstants.vrnrReproject = g_vrnrReproject.load() ? 1 : 0;
    resConstants.hasMVec = hasValidMvec ? 1 : 0;
    resConstants.mvScaleX = origMvX;
    resConstants.mvScaleY = origMvY;
    // 0.7.3 帧时自适应强度：EWMA 帧率低于 VrnrAdaptFpsHi 开始衰减推理帧 edit，
    // 低于 VrnrAdaptFpsLo 达到最大衰减量（smoothstep 过渡），skip 帧 dim=0
    // （skip 帧已有 weight 衰减链路，再减光会反向拉大两帧差距）。
    float adaptDim = 0.0f;
    if (g_vrnrAdapt.load() && isVrnrActive) {
        const float fpsHi = g_vrnrAdaptFpsHi.load();
        const float fpsLo = g_vrnrAdaptFpsLo.load();
        float t = (fpsHi - s_smoothedFps) / (fpsHi - fpsLo);
        t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
        adaptDim = t * t * (3.0f - 2.0f * t) * g_vrnrAdaptAmount.load();
        if (isSkipFrame) adaptDim = 0.0f;
    }
    resConstants.adaptDim = adaptDim;

    InCmdList->SetComputeRoot32BitConstants(0, 18, &resConstants, 0);
    InCmdList->SetComputeRootDescriptorTable(1, gpuHandleResolve);
    InCmdList->SetPipelineState(g_psoResolve);
    InCmdList->Dispatch((nativeW + 7) / 8, (nativeH + 7) / 8, 1);

    D3D12_RESOURCE_BARRIER uavFlush = {};
    uavFlush.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavFlush.UAV.pResource = resolveWriteDest;
    InCmdList->ResourceBarrier(1, &uavFlush);

    s_lastResolveOutput = resolveWriteDest;

    if (!outHasUav && slot->nativeScratch && resolveWriteDest == slot->nativeScratch && outDesc.Format == slot->nativeScratch->GetDesc().Format) {
        TransitionBarrier(InCmdList, slot->nativeScratch, slot->nativeScratchState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        slot->nativeScratchState = D3D12_RESOURCE_STATE_COPY_SOURCE;
        TransitionBarrier(InCmdList, origOutput, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
        InCmdList->CopyResource(origOutput, slot->nativeScratch);
        TransitionBarrier(InCmdList, origOutput, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    if (!isSkipFrame) {
        RestoreParameters();
    }

    return result;
}

// EvaluateFeature 导出：包一层 SEH __except 兜底，实际逻辑在 EvaluateFeatureInternal。
// 兜底分支同样只做透传——任何情况下不能让 C++/SEH 异常穿越 DLL 边界。
__declspec(dllexport) int __cdecl NVSDK_NGX_D3D12_EvaluateFeature(
    ID3D12GraphicsCommandList* InCmdList,
    const void* InFeatureHandle,
    const void* InParameters,
    void* InCallback)
{
    g_proxyMutex.lock();
    if (!real_Evaluate) {
        g_proxyMutex.unlock();
        return -1;
    }

    int result = -1;
    __try {
        result = EvaluateFeatureInternal(InCmdList, InFeatureHandle, InParameters, InCallback);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[Proxy] CRITICAL: SEH Exception 0x%08X caught in EvaluateFeature! Falling back to native passthrough.", GetExceptionCode());
        if (real_Evaluate) {
            result = real_Evaluate(InCmdList, InFeatureHandle, InParameters, InCallback);
        }
    }
    g_proxyMutex.unlock();
    return result;
}

__declspec(dllexport) void __cdecl NVSDK_NGX_D3D12_ReleaseFeature(void* InFeatureHandle)
{
    std::lock_guard<std::recursive_mutex> lock(g_proxyMutex);
    EnsureRealModuleLoaded();
    Log("[Proxy] NVSDK_NGX_D3D12_ReleaseFeature (Handle=%p)", InFeatureHandle);

    bool alreadyParked = false;
    for (size_t i = 0; i < MAX_FEATURE_SLOTS; ++i) {
        if (g_slots[i].inUse && (g_slots[i].origGameHandle == InFeatureHandle || g_slots[i].activeFeature == InFeatureHandle)) {
            if (g_slots[i].activeFeature == InFeatureHandle) {
                alreadyParked = true;
            }
            ReleaseSlotResources(g_slots[i]);
            g_slots[i].inUse = false;
            g_slots[i].origGameHandle = nullptr;
            // (上游 b09bcca 修复：移除 break——同一句柄可能命中多个档位槽位，
            //  原先只释放第一个匹配槽位会留下孤儿缓存)
        }
    }

    if (InFeatureHandle && !alreadyParked && real_Release) {
        void* f = InFeatureHandle;
        ParkNrFeature(f);
    }
}

}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
    } else if (fdwReason == DLL_PROCESS_DETACH && !lpvReserved) {
        dlssnr_proxyui::OverlayShutdown(); // stop UI thread + destroy panel window first
        ReleaseD3D12Pipeline();
        for (size_t i = 0; i < MAX_FEATURE_SLOTS; ++i) {
            if (g_slots[i].inUse) {
                ReleaseSlotResources(g_slots[i]);
                g_slots[i].inUse = false;
            }
        }
        if (g_logFile) {
            fclose(g_logFile);
            g_logFile = nullptr;
        }
        if (g_realModule) {
            FreeLibrary(g_realModule);
            g_realModule = nullptr;
        }
    }
    return TRUE;
}
