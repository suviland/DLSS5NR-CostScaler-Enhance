// dlssnr_console.cpp — Standalone DLSSNR Cost-Scaler console (GUI EXE).
//
// Purpose:
//   Adjust the proxy (nvngx_dlssnr.dll) settings without entering the game.
//   It reads/writes nvngx_dlssnr.ini next to the EXE and, when the game is
//   running, synchronises live through the shared-memory config (same protocol
//   as the in-game overlay and the ReShade companion).
//
//   writerSource = 1 for every push this EXE performs.
//
// Build (x64, MSVC):
//   cl /nologo /O2 /MT /EHsc /std:c++17 /D "NDEBUG" /D "UNICODE" /D "_UNICODE" ^
//       dlssnr_console.cpp /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib /OUT:dlssnr_console.exe

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include "dlssnr_shared.h"
#include "ui_panel.h"

using namespace dlssnr_ui;

// ===========================================================================
// Local config mirror (plain values, owned by the single UI thread).
// ===========================================================================
static UiValues        s_cfg;
static wchar_t         s_iniPath[MAX_PATH] = { 0 };
static const wchar_t*  kFooter = L"";   // idle footer stays blank (auto-save hint removed)

// ===========================================================================
// Shared memory (inter-process, mirrors DlssnrSharedConfig)
// ===========================================================================
static HANDLE              g_hSharedMem = nullptr;
static DlssnrSharedConfig* g_sh = nullptr;
static uint32_t            s_lastVersion = 0;

static void BuildIniPath() {
    GetModuleFileNameW(nullptr, s_iniPath, MAX_PATH);
    wchar_t* slash = wcsrchr(s_iniPath, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wcscat_s(s_iniPath, L"nvngx_dlssnr.ini");
}

static void ClampAll() {
    if (s_cfg.scale < 0.25f) s_cfg.scale = 0.25f;
    if (s_cfg.scale > 2.00f) s_cfg.scale = 2.00f;
    s_cfg.scale = (float)((int)(s_cfg.scale * 100.0f + 0.5f)) / 100.0f;
    if (s_cfg.mode != 0 && s_cfg.mode != 1) s_cfg.mode = 1;
    if (s_cfg.transfer < 0.0f) s_cfg.transfer = 0.0f;
    if (s_cfg.transfer > 2.0f) s_cfg.transfer = 2.0f;
    if (s_cfg.color < 0.0f) s_cfg.color = 0.0f;
    if (s_cfg.color > 1.0f) s_cfg.color = 1.0f;
    if (s_cfg.sharpness < 0.0f) s_cfg.sharpness = 0.0f;
    if (s_cfg.sharpness > 1.0f) s_cfg.sharpness = 1.0f;
    if (s_cfg.scaleX < 0.25f) s_cfg.scaleX = 0.25f;
    if (s_cfg.scaleX > 2.00f) s_cfg.scaleX = 2.00f;
    if (s_cfg.scaleY < 0.25f) s_cfg.scaleY = 0.25f;
    if (s_cfg.scaleY > 2.00f) s_cfg.scaleY = 2.00f;
    if (s_cfg.nrStyle < 0 || s_cfg.nrStyle > 2) s_cfg.nrStyle = 0;
    if (s_cfg.nrIntensity  < 0.0f) s_cfg.nrIntensity  = 0.0f;
    if (s_cfg.nrIntensity  > 2.0f) s_cfg.nrIntensity  = 2.0f;
    if (s_cfg.nrLocalStruct < 0.0f) s_cfg.nrLocalStruct = 0.0f;
    if (s_cfg.nrLocalStruct > 2.0f) s_cfg.nrLocalStruct = 2.0f;
    if (s_cfg.nrLocalTone  < 0.0f) s_cfg.nrLocalTone  = 0.0f;
    if (s_cfg.nrLocalTone  > 2.0f) s_cfg.nrLocalTone  = 2.0f;
    if (s_cfg.nrSkinStruct < -1.0f) s_cfg.nrSkinStruct = -1.0f;
    if (s_cfg.nrSkinStruct >  2.0f) s_cfg.nrSkinStruct =  2.0f;
    if (s_cfg.vrnrInterval < 1 || s_cfg.vrnrInterval > 3) s_cfg.vrnrInterval = 1;
}

static void LoadIni() {
    wchar_t buf[64];

    s_cfg.enableProxy    = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableProxy", 1, s_iniPath) != 0;
    s_cfg.enableHotkeys  = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableHotkeys", 1, s_iniPath) != 0;
    s_cfg.enableUi       = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableUi", 1, s_iniPath) != 0;
    s_cfg.mode           = (int)GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnlargementMode", 1, s_iniPath);

    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"ResolutionScale", L"0.75", buf, 64, s_iniPath);
    s_cfg.scale = (float)_wtof(buf);
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"TransferStrength", L"1.00", buf, 64, s_iniPath);
    s_cfg.transfer = (float)_wtof(buf);
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"Sharpness", L"0.20", buf, 64, s_iniPath);
    s_cfg.sharpness = (float)_wtof(buf);
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"ColorStrength", L"1.00", buf, 64, s_iniPath);
    s_cfg.color = (float)_wtof(buf);

    s_cfg.requireCtrlAlt = GetPrivateProfileIntW(L"Hotkeys", L"RequireCtrlAlt", 1, s_iniPath) != 0;
    s_cfg.keyToggleProxy = (int)GetPrivateProfileIntW(L"Hotkeys", L"KeyToggleProxy", VK_SPACE, s_iniPath);
    s_cfg.keyToggleMode  = (int)GetPrivateProfileIntW(L"Hotkeys", L"KeyToggleMode",  VK_END,   s_iniPath);
    s_cfg.keyScaleUp     = (int)GetPrivateProfileIntW(L"Hotkeys", L"KeyScaleUp",     VK_PRIOR, s_iniPath);
    s_cfg.keyScaleDown   = (int)GetPrivateProfileIntW(L"Hotkeys", L"KeyScaleDown",   VK_NEXT,  s_iniPath);
    s_cfg.keyToggleUi    = (int)GetPrivateProfileIntW(L"Hotkeys", L"KeyToggleUI", VK_F11, s_iniPath);

    int lang = (int)GetPrivateProfileIntW(L"DLSSNR_Proxy", L"UiLanguage", (int)L_ZH, s_iniPath);
    if (lang < 0 || lang >= L_COUNT) lang = (int)L_ZH;
    s_cfg.uiLanguage = lang;
    SetLang(lang);   // sync the global display language

    // v0.6.0 upstream features
    {
        int enableAlt = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableAlternatingFrames", 0, s_iniPath) != 0;
        int vi = (int)GetPrivateProfileIntW(L"DLSSNR_Proxy", L"VrnrInterval", 0, s_iniPath);
        if (vi < 1 || vi > 3) vi = enableAlt ? 2 : 1;   // legacy INIs carry only the on/off flag
        s_cfg.vrnrInterval = vi;
    }
    s_cfg.vrnrBlend  = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"VrnrBlend", 1, s_iniPath) != 0;
    s_cfg.vrnrSched  = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"VrnrAdaptiveSkip", 0, s_iniPath) != 0;
    s_cfg.depthAware  = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableDepthAwareResolve", 1, s_iniPath) != 0;
    s_cfg.anamorphic  = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableAnamorphic", 0, s_iniPath) != 0;
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"ResolutionScaleX", L"0.65", buf, 64, s_iniPath);
    s_cfg.scaleX = (float)_wtof(buf);
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"ResolutionScaleY", L"0.85", buf, 64, s_iniPath);
    s_cfg.scaleY = (float)_wtof(buf);
    s_cfg.useCustomNR = GetPrivateProfileIntW(L"DLSSNR_Settings", L"UseCustomSettings", 0, s_iniPath) != 0;
    s_cfg.nrStyle     = (int)GetPrivateProfileIntW(L"DLSSNR_Settings", L"Style", 0, s_iniPath);
    GetPrivateProfileStringW(L"DLSSNR_Settings", L"Intensity", L"1.00", buf, 64, s_iniPath);
    s_cfg.nrIntensity = (float)_wtof(buf);
    GetPrivateProfileStringW(L"DLSSNR_Settings", L"LocalStructureStrength", L"1.00", buf, 64, s_iniPath);
    s_cfg.nrLocalStruct = (float)_wtof(buf);
    GetPrivateProfileStringW(L"DLSSNR_Settings", L"LocalToneStrength", L"1.00", buf, 64, s_iniPath);
    s_cfg.nrLocalTone = (float)_wtof(buf);
    GetPrivateProfileStringW(L"DLSSNR_Settings", L"SkinStructureStrength", L"-1.00", buf, 64, s_iniPath);
    s_cfg.nrSkinStruct = (float)_wtof(buf);
    s_cfg.nrAutoMask   = GetPrivateProfileIntW(L"DLSSNR_Settings", L"UseAutoMask", 0, s_iniPath) != 0;

    ClampAll();
}

static void SaveIniValue(const wchar_t* section, const wchar_t* key, const wchar_t* value) {
    WritePrivateProfileStringW(section, key, value, s_iniPath);
}

static void SaveIni() {
    wchar_t buf[32];
    SaveIniValue(L"DLSSNR_Proxy", L"EnableProxy", s_cfg.enableProxy ? L"1" : L"0");
    swprintf_s(buf, L"%.2f", s_cfg.scale);             SaveIniValue(L"DLSSNR_Proxy", L"ResolutionScale", buf);
    swprintf_s(buf, L"%u", (uint32_t)s_cfg.mode);      SaveIniValue(L"DLSSNR_Proxy", L"EnlargementMode", buf);
    swprintf_s(buf, L"%.2f", s_cfg.transfer);          SaveIniValue(L"DLSSNR_Proxy", L"TransferStrength", buf);
    swprintf_s(buf, L"%.2f", s_cfg.color);             SaveIniValue(L"DLSSNR_Proxy", L"ColorStrength", buf);
    swprintf_s(buf, L"%.2f", s_cfg.sharpness);         SaveIniValue(L"DLSSNR_Proxy", L"Sharpness", buf);
    SaveIniValue(L"DLSSNR_Proxy", L"EnableHotkeys", s_cfg.enableHotkeys ? L"1" : L"0");
    SaveIniValue(L"DLSSNR_Proxy", L"EnableUi", s_cfg.enableUi ? L"1" : L"0");
    SaveIniValue(L"Hotkeys", L"RequireCtrlAlt", s_cfg.requireCtrlAlt ? L"1" : L"0");
    swprintf_s(buf, L"%d", s_cfg.keyToggleProxy); SaveIniValue(L"Hotkeys", L"KeyToggleProxy", buf);
    swprintf_s(buf, L"%d", s_cfg.keyToggleMode);  SaveIniValue(L"Hotkeys", L"KeyToggleMode", buf);
    swprintf_s(buf, L"%d", s_cfg.keyScaleUp);     SaveIniValue(L"Hotkeys", L"KeyScaleUp", buf);
    swprintf_s(buf, L"%d", s_cfg.keyScaleDown);   SaveIniValue(L"Hotkeys", L"KeyScaleDown", buf);
    swprintf_s(buf, L"%d", s_cfg.keyToggleUi);    SaveIniValue(L"Hotkeys", L"KeyToggleUI", buf);
    swprintf_s(buf, L"%u", (uint32_t)s_cfg.uiLanguage); SaveIniValue(L"DLSSNR_Proxy", L"UiLanguage", buf);
    SaveIniValue(L"DLSSNR_Proxy", L"EnableDepthAwareResolve", s_cfg.depthAware ? L"1" : L"0");
    SaveIniValue(L"DLSSNR_Proxy", L"EnableAlternatingFrames", (s_cfg.vrnrInterval > 1) ? L"1" : L"0");
    swprintf_s(buf, L"%u", (uint32_t)s_cfg.vrnrInterval); SaveIniValue(L"DLSSNR_Proxy", L"VrnrInterval", buf);
    SaveIniValue(L"DLSSNR_Proxy", L"VrnrBlend", s_cfg.vrnrBlend ? L"1" : L"0");
    SaveIniValue(L"DLSSNR_Proxy", L"VrnrAdaptiveSkip", s_cfg.vrnrSched ? L"1" : L"0");
    SaveIniValue(L"DLSSNR_Proxy", L"EnableAnamorphic", s_cfg.anamorphic ? L"1" : L"0");
    swprintf_s(buf, L"%.2f", s_cfg.scaleX);            SaveIniValue(L"DLSSNR_Proxy", L"ResolutionScaleX", buf);
    swprintf_s(buf, L"%.2f", s_cfg.scaleY);            SaveIniValue(L"DLSSNR_Proxy", L"ResolutionScaleY", buf);
    SaveIniValue(L"DLSSNR_Settings", L"UseCustomSettings", s_cfg.useCustomNR ? L"1" : L"0");
    swprintf_s(buf, L"%u", (uint32_t)s_cfg.nrStyle);   SaveIniValue(L"DLSSNR_Settings", L"Style", buf);
    swprintf_s(buf, L"%.2f", s_cfg.nrIntensity);       SaveIniValue(L"DLSSNR_Settings", L"Intensity", buf);
    swprintf_s(buf, L"%.2f", s_cfg.nrLocalStruct);     SaveIniValue(L"DLSSNR_Settings", L"LocalStructureStrength", buf);
    swprintf_s(buf, L"%.2f", s_cfg.nrLocalTone);       SaveIniValue(L"DLSSNR_Settings", L"LocalToneStrength", buf);
    swprintf_s(buf, L"%.2f", s_cfg.nrSkinStruct);      SaveIniValue(L"DLSSNR_Settings", L"SkinStructureStrength", buf);
    swprintf_s(buf, L"%u", s_cfg.nrAutoMask ? 1u : 0u); SaveIniValue(L"DLSSNR_Settings", L"UseAutoMask", buf);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, s_iniPath); // flush
}

static void PushShared() {
    if (!g_sh || g_sh->magic != DLSSNR_MAGIC) return;
    g_sh->enableProxy      = s_cfg.enableProxy ? 1 : 0;
    g_sh->resolutionScale  = s_cfg.scale;
    g_sh->enlargementMode  = (uint32_t)s_cfg.mode;
    g_sh->transferStrength = s_cfg.transfer;
    g_sh->colorStrength    = s_cfg.color;
    g_sh->sharpness        = s_cfg.sharpness;
    g_sh->enableHotkeys    = s_cfg.enableHotkeys ? 1 : 0;
    g_sh->requireCtrlAlt   = s_cfg.requireCtrlAlt ? 1 : 0;
    g_sh->keyToggleProxy   = (uint32_t)s_cfg.keyToggleProxy;
    g_sh->keyToggleMode    = (uint32_t)s_cfg.keyToggleMode;
    g_sh->keyScaleUp       = (uint32_t)s_cfg.keyScaleUp;
    g_sh->keyScaleDown     = (uint32_t)s_cfg.keyScaleDown;
    g_sh->enableUi         = s_cfg.enableUi ? 1 : 0;
    g_sh->keyToggleUi      = (uint32_t)s_cfg.keyToggleUi;
    g_sh->uiLanguage       = (uint32_t)s_cfg.uiLanguage;
    g_sh->enableDepthAware = s_cfg.depthAware ? 1 : 0;
    g_sh->enableVrnr       = (s_cfg.vrnrInterval > 1) ? 1 : 0;
    g_sh->vrnrInterval     = (uint32_t)s_cfg.vrnrInterval;
    g_sh->vrnrBlend        = s_cfg.vrnrBlend ? 1 : 0;
    g_sh->vrnrSched        = s_cfg.vrnrSched ? 1 : 0;
    g_sh->enableAnamorphic = s_cfg.anamorphic ? 1 : 0;
    g_sh->scaleX           = s_cfg.scaleX;
    g_sh->scaleY           = s_cfg.scaleY;
    g_sh->useCustomNR      = s_cfg.useCustomNR ? 1 : 0;
    g_sh->nrStyle          = (uint32_t)s_cfg.nrStyle;
    g_sh->nrIntensity      = s_cfg.nrIntensity;
    g_sh->nrLocalStructureStrength = s_cfg.nrLocalStruct;
    g_sh->nrLocalToneStrength      = s_cfg.nrLocalTone;
    g_sh->nrSkinStructureStrength  = s_cfg.nrSkinStruct;
    g_sh->nrUseAutoMask    = s_cfg.nrAutoMask ? 1 : 0;
    g_sh->writerSource     = 1;                 // standalone console
    g_sh->version++;
    s_lastVersion = g_sh->version;
}

static void AdoptFromShared() {
    s_cfg.enableProxy    = g_sh->enableProxy != 0;
    s_cfg.scale          = g_sh->resolutionScale;
    s_cfg.mode           = (int)g_sh->enlargementMode;
    s_cfg.transfer       = g_sh->transferStrength;
    s_cfg.color          = g_sh->colorStrength;
    s_cfg.sharpness      = g_sh->sharpness;
    s_cfg.enableHotkeys  = g_sh->enableHotkeys != 0;
    s_cfg.requireCtrlAlt = g_sh->requireCtrlAlt != 0;
    s_cfg.enableUi       = g_sh->enableUi != 0;
    s_cfg.keyToggleProxy = (int)g_sh->keyToggleProxy;
    s_cfg.keyToggleMode  = (int)g_sh->keyToggleMode;
    s_cfg.keyScaleUp     = (int)g_sh->keyScaleUp;
    s_cfg.keyScaleDown   = (int)g_sh->keyScaleDown;
    s_cfg.keyToggleUi    = (int)g_sh->keyToggleUi;
    s_cfg.uiLanguage     = (int)g_sh->uiLanguage;
    if (s_cfg.uiLanguage < 0 || s_cfg.uiLanguage >= L_COUNT) s_cfg.uiLanguage = (int)L_ZH;
    SetLang(s_cfg.uiLanguage);
    s_cfg.depthAware  = g_sh->enableDepthAware != 0;
    {
        int vi = (int)g_sh->vrnrInterval;
        if (vi < 1 || vi > 3) vi = g_sh->enableVrnr ? 2 : 1;
        s_cfg.vrnrInterval = vi;
    }
    s_cfg.vrnrBlend   = g_sh->vrnrBlend != 0;
    s_cfg.vrnrSched   = g_sh->vrnrSched != 0;
    s_cfg.anamorphic  = g_sh->enableAnamorphic != 0;
    s_cfg.scaleX      = g_sh->scaleX;
    s_cfg.scaleY      = g_sh->scaleY;
    s_cfg.useCustomNR = g_sh->useCustomNR != 0;
    s_cfg.nrStyle     = (int)g_sh->nrStyle;
    s_cfg.nrIntensity = g_sh->nrIntensity;
    s_cfg.nrLocalStruct = g_sh->nrLocalStructureStrength;
    s_cfg.nrLocalTone   = g_sh->nrLocalToneStrength;
    s_cfg.nrSkinStruct  = g_sh->nrSkinStructureStrength;
    s_cfg.nrAutoMask    = g_sh->nrUseAutoMask != 0;
    ClampAll();
}

static void InitSharedMemory() {
    g_hSharedMem = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                      sizeof(DlssnrSharedConfig), DLSSNR_SHARED_MEM_NAME);
    if (!g_hSharedMem) return;
    DWORD createErr = GetLastError();
    g_sh = (DlssnrSharedConfig*)MapViewOfFile(g_hSharedMem, FILE_MAP_ALL_ACCESS, 0, 0,
                                              sizeof(DlssnrSharedConfig));
    if (!g_sh) return;

    if (createErr != ERROR_ALREADY_EXISTS || g_sh->magic != DLSSNR_MAGIC) {
        // Fresh mapping (or stale/unseeded): seed it from the local config.
        ZeroMemory(g_sh, sizeof(DlssnrSharedConfig));
        g_sh->magic = DLSSNR_MAGIC;
        PushShared();
        g_sh->version = 1;             // keep the first seed clean
        s_lastVersion = 1;
    } else {
        s_lastVersion = g_sh->version;
        // Game/proxy already running: reflect its live config in the UI.
        AdoptFromShared();
    }
}

static void PollShared() {
    if (!g_sh || g_sh->magic != DLSSNR_MAGIC) return;
    if (g_sh->version == s_lastVersion) return;
    // writerSource 2 = proxy hotkey/panel, 3 = disk INI reload -> adopt.
    // 1 means an external console/companion; ignore to avoid echo loops.
    if (g_sh->writerSource != 1) {
        AdoptFromShared();
    }
    s_lastVersion = g_sh->version;
}

static void ShutdownSharedMemory() {
    if (g_sh) { UnmapViewOfFile(g_sh); g_sh = nullptr; }
    if (g_hSharedMem) { CloseHandle(g_hSharedMem); g_hSharedMem = nullptr; }
}

// ===========================================================================
// Panel hooks — bridge Panel <-> local config + INI + shared memory
// ===========================================================================
static void HooksPull(UiValues& out, void*) {
    PollShared();          // pick up proxy-side changes (game running)
    out = s_cfg;
}
static void HooksLive(const UiValues& v, int, void*) {
    s_cfg = v;
    ClampAll();
    PushShared();          // live-preview in the running game
}
static void HooksCommit(const UiValues& v, void*) {
    s_cfg = v;
    ClampAll();
    SaveIni();
    PushShared();
}

// ===========================================================================
// Window host (main thread)
// ===========================================================================
static constexpr int kClientW = 396;
static constexpr int kDefaultH = 640;   // roomier default height (was content-fit ~450)
static PanelHooks g_hooks;
static Panel      g_panel;

static void SavePanelPos(HWND hwnd) {
    RECT wr; GetWindowRect(hwnd, &wr);
    RECT cr; GetClientRect(hwnd, &cr);
    wchar_t b[24];
    swprintf_s(b, L"%d", (int)wr.left);
    WritePrivateProfileStringW(L"DLSSNR_Proxy", L"PanelX", b, s_iniPath);
    swprintf_s(b, L"%d", (int)wr.top);
    WritePrivateProfileStringW(L"DLSSNR_Proxy", L"PanelY", b, s_iniPath);
    // Remember the free-resized client size too (restored on next launch).
    swprintf_s(b, L"%d", (int)cr.right);
    WritePrivateProfileStringW(L"DLSSNR_Proxy", L"PanelW", b, s_iniPath);
    swprintf_s(b, L"%d", (int)cr.bottom);
    WritePrivateProfileStringW(L"DLSSNR_Proxy", L"PanelH", b, s_iniPath);
}
static void PlaceWindow(HWND hwnd, int w, int h) {
    // Honor the position remembered in the ini ([DLSSNR_Proxy] PanelX/PanelY),
    // clamped to the nearest monitor's work area; top-right default otherwise.
    int px = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"PanelX", -1, s_iniPath);
    int py = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"PanelY", -1, s_iniPath);
    RECT wa{ 0, 0, 0, 0 };
    MONITORINFO mi{ sizeof(mi) };
    POINT center{ px >= 0 ? px + w / 2 : 0, py >= 0 ? py + h / 2 : 0 };
    HMONITOR mon = MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST);
    if (mon && GetMonitorInfoW(mon, &mi)) wa = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x, y;
    if (px >= 0 && py >= 0) {
        x = px; y = py;
    } else {
        x = wa.right - w - 14;
        y = wa.top + 14;
    }
    if (x < wa.left) x = wa.left;
    if (y < wa.top) y = wa.top;
    if (x + w > wa.right)  x = wa.right - w;
    if (y + h > wa.bottom) y = wa.bottom - h;
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
}

static LRESULT CALLBACK ConsoleWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST: {
        // Borderless free-resize: expose the native sizing loop along the
        // window edges (the panel itself only ever sees HTCLIENT clicks).
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT wr; GetWindowRect(hwnd, &wr);
        const int Z = 6;
        bool L = pt.x < wr.left + Z, R = pt.x >= wr.right - Z;
        bool T = pt.y < wr.top + Z,  B = pt.y >= wr.bottom - Z;
        if (T && L) return HTTOPLEFT;
        if (T && R) return HTTOPRIGHT;
        if (B && L) return HTBOTTOMLEFT;
        if (B && R) return HTBOTTOMRIGHT;
        if (L) return HTLEFT;
        if (R) return HTRIGHT;
        if (T) return HTTOP;
        if (B) return HTBOTTOM;
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        // Keep the panel usable: tab bar + at least the footer stay visible.
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = Panel::kMinW;
        mmi->ptMinTrackSize.y = Panel::kMinH;
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);   // repaint at the new size
        break;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && !g_panel.IsCapturing()) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        // Persist position + size on every teardown path: WM_CLOSE, the
        // header × button, and the Esc key all funnel through here.
        SavePanelPos(hwnd);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    bool handled = g_panel.Handle(hwnd, msg, wp, lp, g_hooks);
    if (handled) {
        if (msg == WM_LBUTTONDOWN && g_panel.closeHit) {
            g_panel.closeHit = false;
            DestroyWindow(hwnd);        // × on the header exits the console
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    BuildIniPath();
    LoadIni();
    InitSharedMemory();       // seed fresh mapping or adopt live game config

    g_hooks.user = nullptr;
    g_hooks.pull = HooksPull;
    g_hooks.applyLive = HooksLive;
    g_hooks.commit = HooksCommit;

    static wchar_t cls[] = L"DLSSNR_Console_Class";
    WNDCLASSW wc{};
    wc.lpfnWndProc   = ConsoleWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = cls;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        cls, L"DLSSNR Cost Scaler Console",
        WS_POPUP | WS_CLIPCHILDREN,
        0, 0, kClientW, 100, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    g_panel.Init(PanelOpts{ true, true, kFooter });  // overlay look + key rebinding
    g_panel.W = kClientW;
    g_panel.H = 100;
    g_panel.RebuildLayout();
    // Restore the last free-resized size if one was saved; otherwise fit the
    // current page's content height.
    int useW = kClientW, useH = g_panel.contentH > 100 ? g_panel.contentH : 100;
    if (useH < kDefaultH) useH = kDefaultH;   // roomier default panel height
    {
        int pw = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"PanelW", -1, s_iniPath);
        int ph = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"PanelH", -1, s_iniPath);
        if (pw >= Panel::kMinW && ph >= Panel::kMinH) { useW = pw; useH = ph; }
    }
    // Never taller/wider than the primary work area (plus a small margin).
    {
        RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        if (useW > wa.right - wa.left - 28) useW = wa.right - wa.left - 28;
        if (useH > wa.bottom - wa.top - 28) useH = wa.bottom - wa.top - 28;
        if (useW < Panel::kMinW) useW = Panel::kMinW;
        if (useH < Panel::kMinH) useH = Panel::kMinH;
    }
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, useW, useH, SWP_NOMOVE | SWP_NOACTIVATE);
    g_panel.W = useW;
    g_panel.H = useH;
    g_panel.RebuildLayout();
    PlaceWindow(hwnd, useW, useH);
    SetTimer(hwnd, Panel::kTimer, 50, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(hwnd, Panel::kTimer);
    g_panel.ReleaseFonts();
    DestroyWindow(hwnd);
    UnregisterClassW(cls, hInst);
    ShutdownSharedMemory();
    return 0;
}
