// proxy_ui.cpp — In-game overlay panel host (borderless, topmost).
// Runs its own window on a dedicated thread; hotkey toggling is posted from any thread.
// The panel deliberately takes foreground focus when shown so mouse clicks work
// over the game (no WS_EX_NOACTIVATE); hiding it hands focus back to the game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "proxy_ui.h"

namespace dlssnr_proxyui {

using namespace dlssnr_ui;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static CRITICAL_SECTION      g_lock;
static volatile LONG         g_lockReady = 0;
static HANDLE                g_thread     = nullptr;
static DWORD                 g_threadId   = 0;
static HANDLE                g_readyEvent = nullptr;
static HWND                  g_wnd        = nullptr;
static volatile LONG         g_visible    = 0;
static volatile LONG         g_shutdown   = 0;
static PanelHooks            g_hooks;
static bool                  g_hooksSet   = false;
static dlssnr_ui::Panel      g_panel;     // owned by the UI thread only

static void LockInit() {
    if (InterlockedCompareExchange(&g_lockReady, 1, 0) == 0)
        InitializeCriticalSection(&g_lock);
}

static constexpr int kClientW = 396;
static constexpr UINT WM_APP_SHOW = WM_APP + 0x51;
static constexpr UINT WM_APP_QUIT = WM_APP + 0x52;

// ---------------------------------------------------------------------------
// Window placement helpers
// ---------------------------------------------------------------------------
static void ClampToWorkArea(HWND hwnd, int w, int h) {
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
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_APP_SHOW: {
        if (IsWindowVisible(hwnd)) {
            ShowWindow(hwnd, SW_HIDE);
            InterlockedExchange(&g_visible, 0);
            KillTimer(hwnd, Panel::kTimer);
        } else {
            g_panel.closeHit = false;
            g_panel.PullNow(g_hooks);              // refresh from live config
            g_panel.scroll = 0;
            RECT cr; GetClientRect(hwnd, &cr);
            g_panel.H = cr.bottom; g_panel.W = cr.right;
            g_panel.RebuildLayout();
            int need = g_panel.contentH;
            int curW = 0, curH = 0;
            RECT wr; GetWindowRect(hwnd, &wr);
            curW = wr.right - wr.left; curH = wr.bottom - wr.top;
            if (curW != kClientW || curH != need) {
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, kClientW, need,
                             SWP_NOMOVE | SWP_NOACTIVATE);
            }
            ClampToWorkArea(hwnd, kClientW, need);
            SetTimer(hwnd, Panel::kTimer, 50, nullptr);
            // Take foreground so the panel receives mouse input over the game.
            // SW_SHOW activates; SetForegroundWindow/SetFocus make it stick even
            // when the game was holding raw-input/cursor capture.
            ShowWindow(hwnd, SW_SHOW);
            if (!SetForegroundWindow(hwnd)) SetActiveWindow(hwnd);
            SetFocus(hwnd);
            InterlockedExchange(&g_visible, 1);
        }
        return 0;
    }
    case WM_APP_QUIT:
        DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        InterlockedExchange(&g_visible, 0);
        KillTimer(hwnd, Panel::kTimer);
        return 0;
    case WM_NCHITTEST:
        return HTCLIENT;   // never show resize/caption system affordances
    case WM_DESTROY:
        PostQuitMessage(0);
        g_wnd = nullptr;   // window is gone; keep the shared handle coherent
        return 0;
    default:
        break;
    }
    bool handled = g_panel.Handle(hwnd, msg, wp, lp, g_hooks);
    if (handled) {
        if (msg == WM_LBUTTONDOWN && g_panel.closeHit) {
            g_panel.closeHit = false;
            ShowWindow(hwnd, SW_HIDE);
            InterlockedExchange(&g_visible, 0);
            KillTimer(hwnd, Panel::kTimer);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// UI thread
// ---------------------------------------------------------------------------
static DWORD WINAPI UiThreadProc(LPVOID) {
    // Register a per-process class name (safe if the DLL is loaded twice).
    static wchar_t cls[] = L"DLSSNR_OverlayPanel_Class";
    HINSTANCE inst = (HINSTANCE)GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc   = OverlayWndProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = cls;
    RegisterClassW(&wc);

    g_wnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        cls, L"DLSS-NR Cost Scaler",
        WS_POPUP | WS_CLIPCHILDREN,
        0, 0, kClientW, 100, nullptr, nullptr, inst, nullptr);
    if (!g_wnd) {
        SetEvent(g_readyEvent);
        return 1;
    }
    g_panel.Init(PanelOpts{ true, false, L"Ctrl+Alt+F10 隐藏面板 · Hide panel" });
    g_panel.W = kClientW;
    RECT cr; GetClientRect(g_wnd, &cr);
    g_panel.H = cr.bottom;
    g_panel.RebuildLayout();
    SetEvent(g_readyEvent);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_panel.ReleaseFonts();
    if (g_wnd) {           // already destroyed by WM_APP_QUIT -> skip
        DestroyWindow(g_wnd);
        g_wnd = nullptr;
    }
    UnregisterClassW(cls, inst);
    return 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void OverlaySetHooks(const PanelHooks& hk) {
    LockInit();
    EnterCriticalSection(&g_lock);
    g_hooks = hk;
    g_hooksSet = true;
    LeaveCriticalSection(&g_lock);
}

void OverlayToggle() {
    if (InterlockedCompareExchange(&g_shutdown, 0, 0) != 0) return;
    LockInit();
    bool haveHooks = false;
    EnterCriticalSection(&g_lock);
    haveHooks = g_hooksSet;
    LeaveCriticalSection(&g_lock);
    if (!haveHooks) return;   // proxy not initialized yet

    if (!g_thread) {
        g_readyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        g_shutdown = 0;
        g_thread = CreateThread(nullptr, 0, UiThreadProc, nullptr, 0, &g_threadId);
        if (!g_thread) { CloseHandle(g_readyEvent); g_readyEvent = nullptr; return; }
    }
    if (WaitForSingleObject(g_readyEvent, 3000) == WAIT_TIMEOUT) return;
    if (!g_wnd) return;
    PostMessageW(g_wnd, WM_APP_SHOW, 0, 0);
}

void OverlayShutdown() {
    LockInit();
    if (InterlockedExchange(&g_shutdown, 1) != 0) return;
    if (g_thread) {
        PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);
        WaitForSingleObject(g_thread, 3000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_readyEvent) { CloseHandle(g_readyEvent); g_readyEvent = nullptr; }
    if (InterlockedExchange(&g_lockReady, 0) == 1) DeleteCriticalSection(&g_lock);
}

bool OverlayVisible() { return g_visible != 0; }

} // namespace dlssnr_proxyui
