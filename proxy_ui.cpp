// proxy_ui.cpp — In-game overlay panel host (borderless, topmost).
// Runs its own window on a dedicated thread; hotkey toggling is posted from any thread.
//
// Mouse strategy (ReShade-style observer, two parallel mouse systems):
//   - The panel runs its OWN pointer: while visible, a WH_MOUSE_LL hook only
//     OBSERVES system mouse events (never swallows) and drives a virtual
//     cursor clamped to the panel; the panel draws a software arrow for it.
//     Real mouse messages and the OS cursor are ignored on the panel while
//     the hook is active.
//   - The game's input pipeline is NEVER touched: no raw-input registration
//     (RegisterRawInputDevices holds one process-wide list — any call from us
//     silently clobbers the game's / DirectInput's own registration and its
//     mouse stays dead until the next focus change re-acquires it), no clip,
//     no cursor fiddling, no hooks on the game's windows. Closing the panel
//     just unhooks; the game's mouse simply continues.
//   - Keyboard focus never leaves the game (WS_EX_NOACTIVATE +
//     SW_SHOWNOACTIVATE) and the header drag is manual (no WM_NCLBUTTONDOWN
//     move loop), so the overlay never activates and never steals focus.
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
// Mouse capture — low-level mouse hook (WH_MOUSE_LL), observe-only.
//
// Why not raw input (the previous design): RegisterRawInputDevices holds ONE
// process-wide registration list and every call replaces the previous one.
// The game holds the mouse through DirectInput / its own raw registration, so
// ANY registration we did — on open, or "handing it back" on close — silently
// clobbered theirs; the game's mouse then stayed dead until the next focus
// change made it re-acquire (the alt-tab cure). There is no way to snapshot
// or restore another component's registration.
//
// The ReShade-style answer: never touch the registration at all. While the
// panel is visible we install a WH_MOUSE_LL hook that only OBSERVES system
// mouse events (always CallNextHookEx — the game's input keeps flowing
// untouched, even while the panel is open) and drives a virtual cursor
// clamped to the panel; the panel draws its own software arrow. Closing the
// panel removes the hook — nothing else in the pipeline was ever touched.
// ---------------------------------------------------------------------------
static HWND  g_hookTarget = nullptr;     // set while the capture is active
static HHOOK g_llHook     = nullptr;
static volatile LONG g_lastMoveTick = 0;     // move throttle (hook side)
static volatile LONG g_lastMovePt   = -1;    // last posted client pos (dedup)

static constexpr UINT WM_APP_VMOUSE = WM_APP + 0x53;   // virtual move / button event
static constexpr UINT WM_APP_VWHEEL = WM_APP + 0x54;   // virtual wheel event (wParam = delta<<16)

// Post the event asynchronously: the hook procedure must stay fast (system
// input latency depends on it), so no SendMessage here. Every event carries
// its own coordinates / wParam in the message itself — nothing shared to
// race on. IMPORTANT: events are posted as WM_APP_VMOUSE / WM_APP_VWHEEL,
// NEVER as real mouse messages — the window proc deliberately swallows real
// WM_MOUSEMOVE / WM_LBUTTONDOWN / ... while the capture is active (to avoid
// double handling), so real IDs would be dropped on arrival.
static void PostVirtual(UINT appMsg, WPARAM wp, const POINT& clientPt) {
    PostMessageW(g_hookTarget, appMsg, wp, MAKELPARAM(clientPt.x, clientPt.y));
    dlssnr_ui::SetSoftwareCursorPos(clientPt.x, clientPt.y);
}

static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_hookTarget && IsWindow(g_hookTarget)) {
        const MSLLHOOKSTRUCT& ms = *(const MSLLHOOKSTRUCT*)lp;
        UINT real = 0; WPARAM w = 0;
        switch (wp) {
        case WM_MOUSEMOVE:   real = WM_MOUSEMOVE;   break;
        case WM_LBUTTONDOWN: real = WM_LBUTTONDOWN; break;
        case WM_LBUTTONUP:   real = WM_LBUTTONUP;   break;
        case WM_RBUTTONDOWN: real = WM_RBUTTONDOWN; break;
        case WM_RBUTTONUP:   real = WM_RBUTTONUP;   break;
        case WM_MOUSEWHEEL:  real = WM_MOUSEWHEEL;
                             w = (WPARAM)(((DWORD)(SHORT)LOWORD(ms.mouseData)) << 16);
                             break;
        default: break;
        }
        if (real) {
            POINT pt = ms.pt;                      // absolute screen position
            ScreenToClient(g_hookTarget, &pt);
            RECT cr; GetClientRect(g_hookTarget, &cr);
            if (pt.x < 0) pt.x = 0;
            if (pt.y < 0) pt.y = 0;
            if (pt.x > cr.right)  pt.x = cr.right;
            if (pt.y > cr.bottom) pt.y = cr.bottom;
            bool isMove = (real == WM_MOUSEMOVE);
            if (isMove) {
                // Throttle + dedup: gaming mice report up to 1000 moves/s;
                // posting every one floods the UI thread with full repaints
                // and drags the whole system's input latency down. 125 Hz
                // and "position actually changed" is plenty for a pointer.
                DWORD now = GetTickCount();
                LONG packed = ((LONG)pt.x << 16) | (LONG)pt.y;
                LONG lastPt = InterlockedExchange(&g_lastMovePt, packed);
                if (now - g_lastMoveTick < 8 && lastPt == packed)
                    return CallNextHookEx(nullptr, code, wp, lp);
                InterlockedExchange(&g_lastMoveTick, now);
            } else {
                InterlockedExchange(&g_lastMovePt, -1);   // buttons always post
            }
            if (real == WM_MOUSEWHEEL)
                PostVirtual(WM_APP_VWHEEL, w, pt);
            else
                PostVirtual(WM_APP_VMOUSE, (WPARAM)real, pt);
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static void BeginMouseCapture(HWND hwnd) {
    if (g_hookTarget || !hwnd) return;
    // Observe-only low-level hook: no raw-input registration, no focus, no
    // cursor state, no mouse capture — nothing the game would ever notice.
    HMODULE hmodSelf = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&LowLevelMouseProc, &hmodSelf);
    g_llHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, hmodSelf, 0);
    if (!g_llHook) return;   // no hook → no reliable virtual cursor
    g_hookTarget = hwnd;
    InterlockedExchange(&g_lastMoveTick, 0);
    InterlockedExchange(&g_lastMovePt, -1);
    // Start the virtual cursor at the last real position if it is on the
    // panel, otherwise near the panel's upper content area.
    POINT pt; GetCursorPos(&pt);
    RECT rc; GetWindowRect(hwnd, &rc);
    if (!PtInRect(&rc, pt)) { pt.x = rc.left + (rc.right - rc.left) / 2; pt.y = rc.top + 90; }
    ScreenToClient(hwnd, &pt);
    dlssnr_ui::SetSoftwareCursorPos(pt.x, pt.y);
    dlssnr_ui::g_softCursor = true;
}

static void EndMouseCapture() {
    // Unhook first: no further events may target the dying capture session.
    if (g_llHook) { UnhookWindowsHookEx(g_llHook); g_llHook = nullptr; }
    dlssnr_ui::g_softCursor = false;
    // Defensive: never leave our window owning the (system-wide, single)
    // mouse capture after the panel goes away — a background window holding
    // it would silently steal it from the game.
    ReleaseCapture();
    g_hookTarget = nullptr;
}

static void SavePanelPos(HWND hwnd);   // fwd: persists position + size

// Hide + full teardown of an active capture session (close button, hotkey).
static void HidePanel(HWND hwnd) {
    // Remember where the user dragged the panel (before it disappears).
    if (IsWindowVisible(hwnd)) SavePanelPos(hwnd);   // position + size
    EndMouseCapture();
    g_panel.ResetInput(nullptr);   // no stale drag / hover into the next open
    ShowWindow(hwnd, SW_HIDE);
    InterlockedExchange(&g_visible, 0);
    KillTimer(hwnd, Panel::kTimer);
}

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

// Place the panel where the ini remembers it (PanelX / PanelY), clamped to
// the nearest monitor's work area; fall back to the top-right default when
// nothing was saved yet.
static void PlacePanel(HWND hwnd, int w, int h) {
    int x = 0, y = 0;
    if (g_hooks.loadPos && g_hooks.loadPos(x, y, g_hooks.user)) {
        RECT wa{ 0, 0, 0, 0 };
        MONITORINFO mi{ sizeof(mi) };
        HMONITOR mon = MonitorFromPoint(POINT{ x + w / 2, y + h / 2 },
                                        MONITOR_DEFAULTTONEAREST);
        if (mon && GetMonitorInfoW(mon, &mi)) wa = mi.rcWork;
        else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        if (x < wa.left) x = wa.left;
        if (y < wa.top) y = wa.top;
        if (x + w > wa.right)  x = wa.right - w;
        if (y + h > wa.bottom) y = wa.bottom - h;
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    } else {
        ClampToWorkArea(hwnd, w, h);
    }
}

static void SavePanelPos(HWND hwnd) {
    if (!g_hooks.savePos) return;
    RECT wr; GetWindowRect(hwnd, &wr);
    g_hooks.savePos(wr.left, wr.top, g_hooks.user);
    // Persist the free-resized client size alongside the position.
    if (g_hooks.saveSize) {
        RECT cr; GetClientRect(hwnd, &cr);
        if (cr.right >= Panel::kMinW && cr.bottom >= Panel::kMinH)
            g_hooks.saveSize(cr.right, cr.bottom, g_hooks.user);
    }
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_APP_SHOW: {
        if (IsWindowVisible(hwnd)) {
            HidePanel(hwnd);
        } else {
            g_panel.ResetInput(hwnd);              // fresh input state each open
            g_panel.PullNow(g_hooks);              // refresh from live config
            g_panel.scroll = 0;
            g_panel.RebuildLayout();
            // Window size: the user's last free-resized size ([DLSSNR_Proxy]
            // PanelW / PanelH) wins; otherwise auto-fit the current page.
            int wantW = kClientW, wantH = g_panel.contentH;
            int sw = 0, sh = 0;
            if (g_hooks.loadSize && g_hooks.loadSize(sw, sh, g_hooks.user) &&
                sw >= Panel::kMinW && sh >= Panel::kMinH) {
                wantW = sw; wantH = sh;
            }
            // Never exceed the nearest monitor's work area.
            RECT wa{ 0, 0, 0, 0 };
            MONITORINFO mi{ sizeof(mi) };
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (mon && GetMonitorInfoW(mon, &mi)) wa = mi.rcWork;
            else SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
            if (wantW > wa.right - wa.left - 28) wantW = wa.right - wa.left - 28;
            if (wantH > wa.bottom - wa.top - 28) wantH = wa.bottom - wa.top - 28;
            if (wantW < Panel::kMinW) wantW = Panel::kMinW;
            if (wantH < Panel::kMinH) wantH = Panel::kMinH;
            int curW = 0, curH = 0;
            RECT wr; GetWindowRect(hwnd, &wr);
            curW = wr.right - wr.left; curH = wr.bottom - wr.top;
            if (curW != wantW || curH != wantH) {
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, wantW, wantH,
                             SWP_NOMOVE | SWP_NOACTIVATE);
            }
            g_panel.W = wantW; g_panel.H = wantH;
            g_panel.RebuildLayout();
            PlacePanel(hwnd, wantW, wantH);
            SetTimer(hwnd, Panel::kTimer, 50, nullptr);
            // SW_SHOWNOACTIVATE keeps keyboard focus with the game; the
            // low-level mouse hook drives the panel's own virtual cursor.
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            BeginMouseCapture(hwnd);
            InterlockedExchange(&g_visible, 1);
        }
        return 0;
    }
    case WM_APP_QUIT:
        EndMouseCapture();
        DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE:
        HidePanel(hwnd);
        return 0;
    case WM_NCHITTEST:
        return HTCLIENT;   // never show resize/caption system affordances
    case WM_APP_VMOUSE: {
        // Virtual-cursor move / button event posted by the low-level hook.
        UINT real = (UINT)wp;
        g_panel.Handle(hwnd, real, 0, lp, g_hooks);
        // The × button sets closeHit; it must close no matter which channel
        // (virtual cursor or real mouse) delivered the click.
        if (real == WM_LBUTTONDOWN && g_panel.closeHit) {
            g_panel.closeHit = false;
            HidePanel(hwnd);
        }
        return 0;
    }
    case WM_APP_VWHEEL:
        // Virtual-cursor wheel event; wParam already carries delta<<16.
        g_panel.Handle(hwnd, WM_MOUSEWHEEL, wp, lp, g_hooks);
        return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MOUSEWHEEL:
        if (g_hookTarget) return 0;   // capture active: the virtual cursor owns the panel
        break;                        // otherwise fall through to normal handling
    case WM_SETCURSOR:
        if (g_hookTarget) {           // only the software arrow while capturing
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_TIMER:
        break;   // nothing extra here — the panel tick below handles WM_TIMER
    case WM_DESTROY:
        if (IsWindowVisible(hwnd)) SavePanelPos(hwnd);   // destroyed while open
        EndMouseCapture();
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
            HidePanel(hwnd);
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
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        cls, L"DLSS-NR Cost Scaler",
        WS_POPUP | WS_CLIPCHILDREN,
        0, 0, kClientW, 100, nullptr, nullptr, inst, nullptr);
    if (!g_wnd) {
        SetEvent(g_readyEvent);
        return 1;
    }
    g_panel.Init(PanelOpts{ true, false, L"Ctrl+Alt+F11 隐藏面板 · Hide panel" });
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
    EndMouseCapture();        // UI thread is about to exit; safe to unhook here
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
    // No EndMouseCapture here: the hook must be unhooked on the UI thread
    // that installed it. The loop-exit in UiThreadProc (below) does exactly
    // that once WM_QUIT arrives.
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