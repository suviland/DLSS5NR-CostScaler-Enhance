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
static const wchar_t*  kFooter = L"修改后自动保存 · Ctrl+Alt+F10 也能在游戏里呼出面板";

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
    if (s_cfg.scale > 1.00f) s_cfg.scale = 1.00f;
    s_cfg.scale = (float)((int)(s_cfg.scale * 100.0f + 0.5f)) / 100.0f;
    if (s_cfg.mode != 0 && s_cfg.mode != 1) s_cfg.mode = 1;
    if (s_cfg.transfer < 0.0f) s_cfg.transfer = 0.0f;
    if (s_cfg.transfer > 2.0f) s_cfg.transfer = 2.0f;
    if (s_cfg.color < 0.0f) s_cfg.color = 0.0f;
    if (s_cfg.color > 1.0f) s_cfg.color = 1.0f;
    if (s_cfg.sharpness < 0.0f) s_cfg.sharpness = 0.0f;
    if (s_cfg.sharpness > 1.0f) s_cfg.sharpness = 1.0f;
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
    s_cfg.keyToggleUi    = (int)GetPrivateProfileIntW(L"Hotkeys", L"KeyToggleUI", VK_F10, s_iniPath);

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
static PanelHooks g_hooks;
static Panel      g_panel;

static void PlaceWindow(HWND hwnd, int w, int h) {
    RECT wa{ 0, 0, 0, 0 };
    MONITORINFO mi{ sizeof(mi) };
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (mon && GetMonitorInfoW(mon, &mi)) wa = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.right - w - 14;
    int y = wa.top + 14;
    if (x < wa.left) x = wa.left;
    if (y + h > wa.bottom) y = wa.bottom - h;
    if (y < wa.top) y = wa.top;
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW);
}

static LRESULT CALLBACK ConsoleWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTCLIENT;
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
    int need = g_panel.contentH > 100 ? g_panel.contentH : 100;
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, kClientW, need, SWP_NOMOVE | SWP_NOACTIVATE);
    PlaceWindow(hwnd, kClientW, need);
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
