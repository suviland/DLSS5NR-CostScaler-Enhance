// ui_panel.h — Shared bilingual (中文/EN) dark GDI configuration panel.
// Used by both the in-game overlay panel (proxy DLL) and the standalone console EXE.
// Header-only: compile into exactly one translation unit per module.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#pragma comment(lib, "shell32.lib")   // ShellExecuteW for the GitHub link

// ============================================================================
// Shared value set (mirrors nvngx_dlssnr.ini / DlssnrSharedConfig)
// ============================================================================
namespace dlssnr_ui {

enum Field {
    F_NONE = -1,
    F_ENABLE_PROXY = 0, F_SCALE, F_MODE, F_TRANSFER, F_COLOR, F_SHARP,
    F_ENABLE_HOTKEYS, F_REQUIRE_CTRLALT, F_ENABLE_UI,
    F_KEY_TOGGLE_PROXY, F_KEY_TOGGLE_MODE, F_KEY_SCALEUP, F_KEY_SCALEDOWN, F_KEY_TOGGLEUI,
    F_COUNT
};

struct UiValues {
    bool   enableProxy     = true;
    float  scale           = 0.75f;  // 0.25 .. 1.00
    int    mode            = 1;      // 1 = Matched Residual, 0 = Bilinear
    float  transfer        = 1.0f;   // 0 .. 2
    float  color           = 1.0f;   // 0 .. 1
    float  sharpness       = 0.20f;  // 0 .. 1
    bool   enableHotkeys   = true;
    bool   requireCtrlAlt  = true;
    bool   enableUi        = true;
    int    keyToggleProxy  = VK_SPACE;
    int    keyToggleMode   = VK_END;
    int    keyScaleUp      = VK_PRIOR;
    int    keyScaleDown    = VK_NEXT;
    int    keyToggleUi     = VK_F11;

    bool Equals(const UiValues& o) const {
        return enableProxy == o.enableProxy && scale == o.scale && mode == o.mode &&
               transfer == o.transfer && color == o.color && sharpness == o.sharpness &&
               enableHotkeys == o.enableHotkeys && requireCtrlAlt == o.requireCtrlAlt &&
               enableUi == o.enableUi && keyToggleProxy == o.keyToggleProxy &&
               keyToggleMode == o.keyToggleMode && keyScaleUp == o.keyScaleUp &&
               keyScaleDown == o.keyScaleDown && keyToggleUi == o.keyToggleUi;
    }
};

// Key name table (drawing + rebind capture)
struct KeyEntry { int vk; const wchar_t* name; };
static const KeyEntry kKeys[] = {
    { VK_SPACE, L"Space" }, { VK_PRIOR, L"Page Up" }, { VK_NEXT, L"Page Down" },
    { VK_HOME, L"Home" }, { VK_END, L"End" }, { VK_INSERT, L"Insert" },
    { VK_DELETE, L"Delete" }, { VK_F1, L"F1" }, { VK_F2, L"F2" }, { VK_F3, L"F3" },
    { VK_F4, L"F4" }, { VK_F5, L"F5" }, { VK_F6, L"F6" }, { VK_F7, L"F7" },
    { VK_F8, L"F8" }, { VK_F9, L"F9" }, { VK_F10, L"F10" }, { VK_F11, L"F11" },
    { VK_F12, L"F12" },
    { 'A', L"A" }, { 'B', L"B" }, { 'C', L"C" }, { 'D', L"D" }, { 'E', L"E" },
    { 'F', L"F" }, { 'G', L"G" }, { 'H', L"H" }, { 'I', L"I" }, { 'J', L"J" },
    { 'K', L"K" }, { 'L', L"L" }, { 'M', L"M" }, { 'N', L"N" }, { 'O', L"O" },
    { 'P', L"P" }, { 'Q', L"Q" }, { 'R', L"R" }, { 'S', L"S" }, { 'T', L"T" },
    { 'U', L"U" }, { 'V', L"V" }, { 'W', L"W" }, { 'X', L"X" }, { 'Y', L"Y" },
    { 'Z', L"Z" },
    { '0', L"0" }, { '1', L"1" }, { '2', L"2" }, { '3', L"3" }, { '4', L"4" },
    { '5', L"5" }, { '6', L"6" }, { '7', L"7" }, { '8', L"8" }, { '9', L"9" },
    { VK_OEM_3, L"`" }, { VK_OEM_MINUS, L"-" }, { VK_OEM_PLUS, L"=" },
    { VK_OEM_4, L"[" }, { VK_OEM_6, L"]" }, { VK_OEM_5, L"\\" }, { VK_OEM_1, L";" },
    { VK_OEM_7, L"'" }, { VK_OEM_COMMA, L"," }, { VK_OEM_PERIOD, L"." },
    { VK_OEM_2, L"/" },
    { VK_LEFT, L"Left" }, { VK_RIGHT, L"Right" }, { VK_UP, L"Up" }, { VK_DOWN, L"Down" },
    { VK_NUMPAD0, L"Num 0" }, { VK_NUMPAD1, L"Num 1" }, { VK_NUMPAD2, L"Num 2" },
    { VK_NUMPAD3, L"Num 3" }, { VK_NUMPAD4, L"Num 4" }, { VK_NUMPAD5, L"Num 5" },
    { VK_NUMPAD6, L"Num 6" }, { VK_NUMPAD7, L"Num 7" }, { VK_NUMPAD8, L"Num 8" },
    { VK_NUMPAD9, L"Num 9" }, { VK_MULTIPLY, L"Num *" }, { VK_ADD, L"Num +" },
    { VK_SUBTRACT, L"Num -" }, { VK_DECIMAL, L"Num ." }, { VK_DIVIDE, L"Num /" }
};
static const wchar_t* KeyName(int vk) {
    for (const auto& e : kKeys) if (e.vk == vk) return e.name;
    return L"?";
}
inline void FormatKeyCombo(int vk, bool withMods, wchar_t* out, size_t cap) {
    if (withMods) swprintf_s(out, cap, L"Ctrl+Alt+%s", KeyName(vk));
    else          swprintf_s(out, cap, L"%s", KeyName(vk));
}

// Project homepage (clickable "GitHub" row shared by overlay & console)
static const wchar_t kGithubUrl[]  = L"https://github.com/suviland/DLSSNR-Cost-Scaler-panel";
static const wchar_t kGithubShow[] = L"github.com/suviland/DLSSNR-Cost-Scaler-panel";

// ============================================================================
// Theme + GDI helpers
// ============================================================================
struct Theme {
    COLORREF bg       = RGB(0x12, 0x14, 0x1a);
    COLORREF headerBg = RGB(0x0d, 0x0f, 0x14);
    COLORREF strip    = RGB(0x1b, 0x1f, 0x29);
    COLORREF line     = RGB(0x2c, 0x33, 0x42);
    COLORREF text     = RGB(0xe7, 0xea, 0xf1);
    COLORREF dim      = RGB(0x8d, 0x95, 0xa7);
    COLORREF accent   = RGB(0x41, 0x8c, 0xff);
    COLORREF accentHi = RGB(0x64, 0xa6, 0xff);
    COLORREF ok       = RGB(0x4a, 0xdd, 0x86);
    COLORREF warn     = RGB(0xf5, 0xb8, 0x4d);
    COLORREF bad      = RGB(0xf0, 0x74, 0x74);
    COLORREF thumbBg  = RGB(0xf2, 0xf5, 0xfa);
};
static const Theme& GetTheme() { static Theme t; return t; }

inline HBRUSH MakeBrush(COLORREF c) { return CreateSolidBrush(c); }
inline void BlitFill(HDC dc, int x, int y, int w, int h, COLORREF c) {
    HBRUSH b = MakeBrush(c);
    RECT r{ x, y, x + w, y + h };
    FillRect(dc, &r, b);
    DeleteObject(b);
}
inline void BlitRound(HDC dc, int x, int y, int w, int h, int rad, COLORREF c) {
    HBRUSH b = MakeBrush(c);
    HRGN rgn = CreateRoundRectRgn(x, y, x + w + 1, y + h + 1, rad * 2, rad * 2);
    FillRgn(dc, rgn, b);
    DeleteObject(rgn);
    DeleteObject(b);
}
inline void BlitFrame(HDC dc, int x, int y, int w, int h, int rad, COLORREF c) {
    HBRUSH b = MakeBrush(c);
    HRGN rgn = CreateRoundRectRgn(x, y, x + w + 1, y + h + 1, rad * 2, rad * 2);
    FrameRgn(dc, rgn, b, 1, 1);
    DeleteObject(rgn);
    DeleteObject(b);
}
inline void BlitText(HDC dc, int x, int y, int w, int h, const wchar_t* s, COLORREF c, HFONT f, UINT fmt) {
    if (!s) return;
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ o = SelectObject(dc, f ? f : GetStockObject(DEFAULT_GUI_FONT));
    RECT r{ x, y, x + w, y + h };
    DrawTextW(dc, s, -1, &r, fmt | DT_NOPREFIX);
    SelectObject(dc, o);
}
// Bilingual label: Chinese primary, English secondary (smaller/dim), side by side.
inline void BlitLabel(HDC dc, int x, int y, int maxW, const wchar_t* zh, const wchar_t* en,
                      COLORREF cZh, COLORREF cEn, HFONT fZh, HFONT fEn) {
    if (zh && zh[0]) {
        SetTextColor(dc, cZh); SetBkMode(dc, TRANSPARENT);
        HGDIOBJ o = SelectObject(dc, fZh);
        RECT r{ x, y, x + maxW, y + 64 };
        DrawTextW(dc, zh, -1, &r, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, o);
        // measure Chinese width so English follows
        RECT m{ 0,0,0,0 };
        HGDIOBJ ob = SelectObject(dc, fZh);
        DrawTextW(dc, zh, -1, &m, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
        SelectObject(dc, ob);
        int zx = m.right - m.left;
        if (en && en[0]) {
            int ex = x + zx + 8;
            if (ex < x + maxW) {
                SetTextColor(dc, cEn);
                HGDIOBJ o2 = SelectObject(dc, fEn);
                RECT r2{ ex, y + 1, x + maxW, y + 64 };
                DrawTextW(dc, en, -1, &r2, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                SelectObject(dc, o2);
            }
        }
    }
}
// Persistent font cache (module lifetime). Caller frees at exit.
struct Fonts {
    HFONT title = nullptr, base = nullptr, bold = nullptr, sub = nullptr;
    void Ensure() {
        if (base) return;
        title = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Microsoft YaHei UI");
        base  = CreateFontW(-13, 0, 0, 0, FW_NORMAL,  0, 0, 0, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Microsoft YaHei UI");
        bold  = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Microsoft YaHei UI");
        sub   = CreateFontW(-11, 0, 0, 0, FW_NORMAL,  0, 0, 0, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Microsoft YaHei UI");
    }
    void Release() {
        if (title) { DeleteObject(title); title = nullptr; }
        if (base)  { DeleteObject(base);  base  = nullptr; }
        if (bold)  { DeleteObject(bold);  bold  = nullptr; }
        if (sub)   { DeleteObject(sub);   sub   = nullptr; }
    }
};

// ============================================================================
// Row model + bilingual labels
// ============================================================================
enum RowKind : int { RK_OVERVIEW = 0, RK_SECTION, RK_TOGGLE, RK_SLIDER, RK_CHIPS, RK_SEG, RK_KEY, RK_FOOTER, RK_LINK };

struct RowText { const wchar_t* zh; const wchar_t* en; };

static const RowText* RowTextFor(Field f) {
    static const RowText kMap[] = {
        { L"启用代理", L"Enable Proxy" },          // F_ENABLE_PROXY
        { L"分辨率缩放", L"Resolution Scale" },    // F_SCALE
        { L"重建模式", L"Resolve Mode" },          // F_MODE (segment, unused label)
        { L"细节传递强度", L"Transfer Strength" }, // F_TRANSFER
        { L"色彩传递强度", L"Color Strength" },    // F_COLOR
        { L"边缘锐化 RCAS", L"Sharpness" },        // F_SHARP
        { L"游戏内快捷键", L"In-Game Hotkeys" },   // F_ENABLE_HOTKEYS
        { L"要求 Ctrl+Alt", L"Require Ctrl+Alt" }, // F_REQUIRE_CTRLALT
        { L"启用面板热键", L"Panel Hotkey" },      // F_ENABLE_UI
        { L"开关代理", L"Toggle Proxy" },          // F_KEY_TOGGLE_PROXY
        { L"切换模式", L"Toggle Mode" },           // F_KEY_TOGGLE_MODE
        { L"提高缩放", L"Scale Up" },              // F_KEY_SCALEUP
        { L"降低缩放", L"Scale Down" },            // F_KEY_SCALEDOWN
        { L"开关面板", L"Toggle Panel" },          // F_KEY_TOGGLEUI
    };
    int i = (int)f;
    if (i < 0 || i >= (int)(sizeof(kMap) / sizeof(kMap[0]))) return nullptr;
    return &kMap[i];
}
// Section ids used as Item.field for RK_SECTION rows
enum Sec : int { S_PROXY = 0x1000, S_QUALITY = 0x1001, S_KEYS = 0x1002 };

struct Item { int kind; int field; RECT rc; };

// Section caption helper (RK_SECTION rows)
inline const RowText* SecText(int sec) {
    static const RowText p = { L"代理", L"Proxy" };
    static const RowText q = { L"画质", L"Quality" };
    static const RowText k = { L"快捷键", L"Hotkeys" };
    if (sec == S_QUALITY) return &q;
    if (sec == S_KEYS) return &k;
    return &p;
}

// Presets for the scale chips row
struct ChipDef { float v; };
static const ChipDef kScaleChips[] = { {1.00f}, {0.85f}, {0.80f}, {0.75f}, {0.67f}, {0.50f} };

// Build the ordered row layout for the current width. Returns content height.
inline int BuildLayout(int w, bool overlay, bool allowKeyEdit, bool withGithub, std::vector<Item>& out) {
    out.clear();
    const int kX = 16;
    const int kRight = w - 16;
    int y = overlay ? 48 : 8;
    auto push = [&](int kind, int field, int h) {
        Item it; it.kind = kind; it.field = field;
        it.rc = { kX, y, kRight, y + h };
        y += h;
        out.push_back(it);
    };
    auto gap = [&](int g) { y += g; };

    push(RK_OVERVIEW, F_NONE, 30);
    gap(4);
    push(RK_SECTION, S_PROXY, 24);                // 代理 Proxy
    push(RK_TOGGLE, F_ENABLE_PROXY, 32);
    push(RK_SLIDER, F_SCALE, 44);
    push(RK_CHIPS, F_SCALE, 28);
    push(RK_SEG, F_MODE, 54);
    gap(8);
    push(RK_SECTION, S_QUALITY, 24);              // 画质 Quality
    push(RK_SLIDER, F_TRANSFER, 42);
    push(RK_SLIDER, F_COLOR, 42);
    push(RK_SLIDER, F_SHARP, 42);
    gap(8);
    push(RK_SECTION, S_KEYS, 24);                 // 快捷键 Hotkeys
    push(RK_TOGGLE, F_ENABLE_HOTKEYS, 32);
    push(RK_TOGGLE, F_REQUIRE_CTRLALT, 32);
    push(RK_KEY, F_KEY_TOGGLE_PROXY, 34);
    push(RK_KEY, F_KEY_TOGGLE_MODE, 34);
    push(RK_KEY, F_KEY_SCALEUP, 34);
    push(RK_KEY, F_KEY_SCALEDOWN, 34);
    push(RK_TOGGLE, F_ENABLE_UI, 32);
    push(RK_KEY, F_KEY_TOGGLEUI, 34);
    gap(8);
    push(RK_FOOTER, F_NONE, 22);
    y += 2;
    if (withGithub) { push(RK_LINK, F_NONE, 24); y += 6; }
    else y += 4;
    (void)allowKeyEdit;
    return y;
}

// ============================================================================
// Host hooks: Panel reads current values, applies live edits, commits to INI.
// ============================================================================
struct PanelHooks {
    void* user = nullptr;
    void (*pull)(UiValues& out, void* user) = nullptr;
    void (*applyLive)(const UiValues& v, int field, void* user) = nullptr;
    void (*commit)(const UiValues& v, void* user) = nullptr;
};
struct PanelOpts {
    bool overlay = false;         // draws own dark header (title + drag + ×)
    bool allowKeyEdit = true;     // click a key row to rebind
    const wchar_t* footer = L"修改后自动保存 · Auto-saved to nvngx_dlssnr.ini";
    bool showGithub = true;       // append the clickable GitHub row
};
enum HitSpecial : int { RI_NONE = -2, RI_CLOSE = -3, RI_DRAG = -4, RI_OVERVIEW = -5 };

inline void FieldRange(Field f, float& lo, float& hi) {
    lo = 0.0f; hi = 1.0f;
    if (f == F_SCALE)      { lo = 0.25f; hi = 1.00f; }
    else if (f == F_TRANSFER) { lo = 0.0f; hi = 2.0f; }
}
inline bool FieldIsBool(Field f) {
    return f == F_ENABLE_PROXY || f == F_ENABLE_HOTKEYS || f == F_REQUIRE_CTRLALT || f == F_ENABLE_UI;
}
inline bool FieldIsKey(Field f) {
    return f == F_KEY_TOGGLE_PROXY || f == F_KEY_TOGGLE_MODE || f == F_KEY_SCALEUP ||
           f == F_KEY_SCALEDOWN || f == F_KEY_TOGGLEUI;
}

// Generic row panel. One instance per window.
struct Panel {
    static constexpr UINT_PTR kTimer = 0x5A11u;
    static constexpr ULONGLONG kCommitDelay = 450;   // ms of idle before hooks.commit
    static constexpr ULONGLONG kPullDelay   = 350;   // ms between external pulls

    UiValues cur, prev;
    int W = 396, H = 100, contentH = 100, scroll = 0;
    std::vector<Item> items;
    bool overlay = false, allowKeyEdit = true, showGithub = true;
    int hot = -1;             // hovered item index
    int drag = -1;            // slider item index being dragged
    int capIdx = -1;          // key row index awaiting a key
    unsigned char keyPrev[256];
    bool dirty = false;
    bool closeHit = false;    // header × clicked (overlay)
    ULONGLONG dirtySince = 0, flashAt = 0, lastPull = 0;
    Fonts fonts; bool fReady = false;
    const wchar_t* footer = nullptr;
    PanelHooks hooks;   // cached copy refreshed on every Handle() call

    void Init(const PanelOpts& o) {
        overlay = o.overlay; allowKeyEdit = o.allowKeyEdit; footer = o.footer;
        showGithub = o.showGithub;
    }
    bool IsCapturing() const { return capIdx >= 0; } // a key-rebind is awaiting input
    void EnsureFonts() { if (!fReady) { fonts.Ensure(); fReady = true; } }
    void ReleaseFonts() { if (fReady) { fonts.Release(); fReady = false; } }

    void RebuildLayout();
    void PullNow(const PanelHooks& hk);
    void SetDirty(Field f, ULONGLONG now, const PanelHooks& hk);
    void CommitIfDue(ULONGLONG now, const PanelHooks& hk, HWND hwnd);
    void CaptureScan(ULONGLONG now, const PanelHooks& hk, HWND hwnd);
    void Tick(ULONGLONG now, const PanelHooks& hk, HWND hwnd);

    bool Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, const PanelHooks& hk);
    void Paint(HWND hwnd);
    void OnLButtonDown(HWND hwnd, int x, int y);
    void OnLButtonUp(HWND hwnd, int x, int y);
    void OnMouseMove(HWND hwnd, int x, int y);
    int HitIndex(int x, int yc) const;
};

// Drawing helpers shared by Paint (free functions)
void DrawHeaderBar(HDC dc, int W, const Fonts& f, const Theme& t);
void DrawOverviewRow(HDC dc, const RECT& rc, const UiValues& v, const Fonts& f, const Theme& t);
void DrawSectionRow(HDC dc, const RECT& rc, int sec, const Fonts& f, const Theme& t);
void DrawToggleRow(HDC dc, const RECT& rc, Field field, bool val, bool hot, const Fonts& f, const Theme& t);
void DrawSliderRow(HDC dc, const RECT& rc, Field field, float val, bool hot, const Fonts& f, const Theme& t);
void DrawChipsRow(HDC dc, const RECT& rc, float scale, const Fonts& f, const Theme& t);
void DrawSegRow(HDC dc, const RECT& rc, int mode, const Fonts& f, const Theme& t);
void DrawKeyRow(HDC dc, const RECT& rc, Field field, int vk, bool requireMods, bool hot, bool capturing,
                const Fonts& f, const Theme& t);
void DrawFooterRow(HDC dc, const RECT& rc, const Fonts& f, const Theme& t, bool dirty,
                   ULONGLONG now, ULONGLONG flashAt, const wchar_t* footer);
void DrawLinkRow(HDC dc, const RECT& rc, const Fonts& f, const Theme& t, bool hot);

// Field accessors -----------------------------------------------------------
inline bool GetBool(const UiValues& v, Field f) {
    switch (f) {
    case F_ENABLE_PROXY: return v.enableProxy;
    case F_ENABLE_HOTKEYS: return v.enableHotkeys;
    case F_REQUIRE_CTRLALT: return v.requireCtrlAlt;
    case F_ENABLE_UI: return v.enableUi;
    default: return false;
    }
}
inline void SetBool(UiValues& v, Field f, bool b) {
    switch (f) {
    case F_ENABLE_PROXY: v.enableProxy = b; break;
    case F_ENABLE_HOTKEYS: v.enableHotkeys = b; break;
    case F_REQUIRE_CTRLALT: v.requireCtrlAlt = b; break;
    case F_ENABLE_UI: v.enableUi = b; break;
    default: break;
    }
}
inline float GetFloat(const UiValues& v, Field f) {
    switch (f) {
    case F_SCALE: return v.scale;
    case F_TRANSFER: return v.transfer;
    case F_COLOR: return v.color;
    case F_SHARP: return v.sharpness;
    default: return 0.0f;
    }
}
inline void SetFloat(UiValues& v, Field f, float x) {
    switch (f) {
    case F_SCALE: v.scale = x; break;
    case F_TRANSFER: v.transfer = x; break;
    case F_COLOR: v.color = x; break;
    case F_SHARP: v.sharpness = x; break;
    default: break;
    }
}
inline int GetKey(const UiValues& v, Field f) {
    switch (f) {
    case F_KEY_TOGGLE_PROXY: return v.keyToggleProxy;
    case F_KEY_TOGGLE_MODE: return v.keyToggleMode;
    case F_KEY_SCALEUP: return v.keyScaleUp;
    case F_KEY_SCALEDOWN: return v.keyScaleDown;
    case F_KEY_TOGGLEUI: return v.keyToggleUi;
    default: return 0;
    }
}
inline void SetKey(UiValues& v, Field f, int k) {
    switch (f) {
    case F_KEY_TOGGLE_PROXY: v.keyToggleProxy = k; break;
    case F_KEY_TOGGLE_MODE: v.keyToggleMode = k; break;
    case F_KEY_SCALEUP: v.keyScaleUp = k; break;
    case F_KEY_SCALEDOWN: v.keyScaleDown = k; break;
    case F_KEY_TOGGLEUI: v.keyToggleUi = k; break;
    default: break;
    }
}
// slider geometry -----------------------------------------------------------
inline float SliderFromX(Field f, const RECT& rc, int x) {
    float lo = 0, hi = 1; FieldRange(f, lo, hi);
    int w = rc.right - rc.left; if (w <= 0) return lo;
    float r = (float)(x - rc.left) / (float)w;
    if (r < 0.0f) r = 0.0f; if (r > 1.0f) r = 1.0f;
    return lo + r * (hi - lo);
}
inline int SliderToX(Field f, const RECT& rc, float v) {
    float lo = 0, hi = 1; FieldRange(f, lo, hi);
    int w = rc.right - rc.left;
    float r = (hi > lo) ? (v - lo) / (hi - lo) : 0.0f;
    if (r < 0.0f) r = 0.0f; if (r > 1.0f) r = 1.0f;
    return rc.left + (int)(r * (float)w + 0.5f);
}

// ============================================================================
// Panel implementation
// ============================================================================
inline void Panel::RebuildLayout() {
    contentH = BuildLayout(W, overlay, allowKeyEdit, showGithub, items);
    int maxScroll = contentH > H ? contentH - H : 0;
    if (scroll > maxScroll) scroll = maxScroll;
    if (scroll < 0) scroll = 0;
}
// ============================ row painters ================================
inline void DrawHeaderBar(HDC dc, int W, const Fonts& f, const Theme& t) {
    BlitFill(dc, 0, 0, W, 40, t.headerBg);
    BlitFill(dc, 0, 39, W, 1, t.line);
    BlitLabel(dc, 16, 9, W - 90, L"DLSSNR Cost Scaler", nullptr,
              t.text, t.dim, f.bold, f.sub);
    BlitText(dc, W - 44, 2, 34, 36, L"\u00d7", t.dim, f.base, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
inline void DrawOverviewRow(HDC dc, const RECT& rc, const UiValues& v, const Fonts& f, const Theme& t) {
    int h = rc.bottom - rc.top;
    int y = rc.top + (h - 26) / 2;
    BlitRound(dc, rc.left, y, rc.right - rc.left, 26, 7, t.strip);
    COLORREF c = v.enableProxy ? t.ok : t.bad;
    int cx = rc.left + 18;
    BlitRound(dc, cx - 4, y + 9, 8, 8, 4, c);   // status dot
    BlitLabel(dc, cx + 12, y + 5, rc.right - rc.left - 60,
              v.enableProxy ? L"已启用" : L"已禁用",
              v.enableProxy ? L"ACTIVE" : L"BYPASSED",
              c, t.dim, f.bold, f.sub);
}
inline void DrawSectionRow(HDC dc, const RECT& rc, int sec, const Fonts& f, const Theme& t) {
    const RowText* rt = SecText(sec);
    int h = rc.bottom - rc.top;
    BlitLabel(dc, rc.left + 2, rc.top + (h - 18) / 2, rc.right - rc.left - 40,
              rt ? rt->zh : L"", rt ? rt->en : L"", t.dim, t.dim, f.bold, f.sub);
}
inline void DrawToggleRow(HDC dc, const RECT& rc, Field field, bool val, bool hot, const Fonts& f, const Theme& t) {
    const RowText* rt = RowTextFor(field);
    int h = rc.bottom - rc.top;
    BlitLabel(dc, rc.left, rc.top + (h - 22) / 2, (rc.right - rc.left) - 70,
              rt ? rt->zh : L"", rt ? rt->en : L"", t.text, t.dim, f.base, f.sub);
    int pw = 46, ph = 22, px = rc.right - pw, py = rc.top + (h - ph) / 2;
    int cy = py + ph / 2;
    if (val) {
        BlitRound(dc, px, py, pw, ph, ph / 2, hot ? t.accentHi : t.accent);
        BlitRound(dc, px + pw - 20, cy - 6, 12, 12, 6, t.thumbBg);
    } else {
        BlitRound(dc, px, py, pw, ph, ph / 2, t.strip);
        BlitFrame(dc, px, py, pw - 1, ph - 1, ph / 2, hot ? t.accent : t.line);
        BlitRound(dc, px + 8, cy - 6, 12, 12, 6, t.thumbBg);
    }
}
inline void DrawSliderRow(HDC dc, const RECT& rc, Field field, float val, bool hot, const Fonts& f, const Theme& t) {
    const RowText* rt = RowTextFor(field);
    wchar_t vb[24];
    if (field == F_SCALE) swprintf_s(vb, L"%d%%", (int)(val * 100.0f + 0.5f));
    else swprintf_s(vb, L"%.2f", val);
    BlitLabel(dc, rc.left, rc.top + 1, (rc.right - rc.left) - 74,
              rt ? rt->zh : L"", rt ? rt->en : L"", t.text, t.dim, f.base, f.sub);
    BlitText(dc, rc.right - 56, rc.top + 1, 56, 18, vb, t.accent, f.bold, DT_RIGHT | DT_SINGLELINE | DT_TOP);
    int ty = rc.bottom - 11;
    int px = SliderToX(field, rc, val);
    BlitRound(dc, rc.left, ty, rc.right - rc.left, 6, 3, t.line);
    if (px > rc.left) BlitRound(dc, rc.left, ty, px - rc.left, 6, 3, hot ? t.accentHi : t.accent);
    BlitRound(dc, px - 7, ty - 5, 14, 14, 7, hot ? t.accentHi : t.thumbBg);
    BlitFrame(dc, px - 7, ty - 5, 13, 13, 7, t.accent);
}
inline void DrawChipsRow(HDC dc, const RECT& rc, float scale, const Fonts& f, const Theme& t) {
    const int n = (int)(sizeof(kScaleChips) / sizeof(kScaleChips[0]));
    int gap = 6, area = rc.right - rc.left;
    int cw = (area - gap * (n - 1)) / n;
    int cy = rc.top + (rc.bottom - rc.top - 24) / 2;
    wchar_t buf[16];
    for (int i = 0; i < n; ++i) {
        int x = rc.left + i * (cw + gap);
        float v = kScaleChips[i].v;
        bool on = (scale > v - 0.005f && scale < v + 0.005f);
        if (on) BlitRound(dc, x, cy, cw, 24, 6, t.accent);
        else { BlitRound(dc, x, cy, cw, 24, 6, t.strip); BlitFrame(dc, x, cy, cw - 1, 23, 6, t.line); }
        swprintf_s(buf, L"%d%%", (int)(v * 100.0f + 0.5f));
        BlitText(dc, x, cy, cw, 24, buf, on ? RGB(0xff, 0xff, 0xff) : t.text, f.base, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}
inline void DrawSegRow(HDC dc, const RECT& rc, int mode, const Fonts& f, const Theme& t) {
    int gap = 8;
    int bw = (rc.right - rc.left - gap) / 2;
    auto seg = [&](int x0, bool active, const wchar_t* zh, const wchar_t* en) {
        int y0 = rc.top + 2, h = (rc.bottom - rc.top) - 4;
        if (active) BlitRound(dc, x0, y0, bw, h, 6, t.accent);
        else { BlitRound(dc, x0, y0, bw, h, 6, t.strip); BlitFrame(dc, x0, y0, bw - 1, h - 1, 6, t.line); }
        COLORREF c1 = active ? RGB(0xff, 0xff, 0xff) : t.text;
        BlitText(dc, x0, y0 + 4, bw, 22, zh, c1, f.base, DT_CENTER | DT_SINGLELINE | DT_TOP);
        BlitText(dc, x0, y0 + 24, bw, 16, en, active ? RGB(0xff, 0xff, 0xff) : t.dim, f.sub, DT_CENTER | DT_SINGLELINE | DT_TOP);
    };
    seg(rc.left, mode == 1, L"匹配残差", L"Matched Residual");
    seg(rc.left + bw + gap, mode == 0, L"双线性", L"Bilinear Direct");
}
inline void DrawKeyRow(HDC dc, const RECT& rc, Field field, int vk, bool requireMods, bool hot, bool capturing,
                       const Fonts& f, const Theme& t) {
    const RowText* rt = RowTextFor(field);
    int h = rc.bottom - rc.top;
    BlitLabel(dc, rc.left, rc.top + (h - 20) / 2, (rc.right - rc.left) - 190,
              rt ? rt->zh : L"", rt ? rt->en : L"", t.text, t.dim, f.base, f.sub);
    wchar_t kb[64];
    if (capturing) {
        wcscpy_s(kb, L"按新键… (Esc 取消)");
        BlitText(dc, rc.right - 190, rc.top + (h - 20) / 2, 190, 20, kb, t.warn, f.base, DT_RIGHT | DT_SINGLELINE | DT_TOP);
    } else {
        FormatKeyCombo(vk, requireMods, kb, 64);
        BlitText(dc, rc.right - 170, rc.top + (h - 20) / 2, 170, 20, kb, hot ? t.accent : t.text, f.bold, DT_RIGHT | DT_SINGLELINE | DT_TOP);
    }
}
inline void DrawFooterRow(HDC dc, const RECT& rc, const Fonts& f, const Theme& t, bool dirty,
                          ULONGLONG now, ULONGLONG flashAt, const wchar_t* footer) {
    const wchar_t* msg = footer ? footer : L"修改后自动保存 · Auto-saved to nvngx_dlssnr.ini";
    COLORREF c = t.dim;
    if (dirty) { msg = L"修改中… 稍候自动保存 · Saving..."; c = t.warn; }
    else if (flashAt && now - flashAt < 2000) { msg = L"已保存 · Saved to nvngx_dlssnr.ini"; c = t.ok; }
    int h = rc.bottom - rc.top;
    BlitText(dc, rc.left + 2, rc.top + (h - 16) / 2, rc.right - rc.left, 18, msg, c, f.sub,
             DT_LEFT | DT_SINGLELINE | DT_TOP | DT_END_ELLIPSIS);
}
inline void DrawLinkRow(HDC dc, const RECT& rc, const Fonts& f, const Theme& t, bool hot) {
    int h = rc.bottom - rc.top;
    int y = rc.top + (h - 18) / 2;
    COLORREF c1 = hot ? t.accentHi : t.accent;
    COLORREF c2 = hot ? t.accentHi : t.dim;
    BlitText(dc, rc.left + 2, y, 64, 18, L"GitHub", c1, f.bold, DT_LEFT | DT_SINGLELINE | DT_TOP);
    BlitText(dc, rc.left + 64, y + 1, (rc.right - rc.left) - 66, 18, kGithubShow, c2, f.sub,
             DT_LEFT | DT_SINGLELINE | DT_TOP | DT_END_ELLIPSIS);
}
inline int Panel::HitIndex(int x, int yc) const {
    for (int i = (int)items.size() - 1; i >= 0; --i) {
        const Item& it = items[i];
        if (yc >= it.rc.top && yc < it.rc.bottom && x >= it.rc.left && x <= it.rc.right) return i;
    }
    return -1;
}
inline void Panel::PullNow(const PanelHooks& hk) {
    if (!hk.pull) return;
    UiValues v = cur;
    hk.pull(v, hk.user);
    if (!v.Equals(cur)) { cur = v; prev = v; }
}
inline void Panel::SetDirty(Field f, ULONGLONG now, const PanelHooks& hk) {
    dirty = true; dirtySince = now;
    if (hk.applyLive) hk.applyLive(cur, (int)f, hk.user);
}
inline void Panel::CommitIfDue(ULONGLONG now, const PanelHooks& hk, HWND hwnd) {
    if (dirty && now - dirtySince >= kCommitDelay) {
        dirty = false; flashAt = now;
        if (hk.commit) hk.commit(cur, hk.user);
        prev = cur;
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    }
}
inline void Panel::CaptureScan(ULONGLONG now, const PanelHooks& hk, HWND hwnd) {
    (void)now;
    if (capIdx < 0 || capIdx >= (int)items.size()) return;
    Field f = (Field)items[capIdx].field;
    // Esc cancels
    if ((GetAsyncKeyState(VK_ESCAPE) & 1) != 0) { capIdx = -1; InvalidateRect(hwnd, nullptr, FALSE); return; }
    for (const auto& e : kKeys) {
        int vk = e.vk;
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool was  = (keyPrev[vk & 0xFF] != 0);
        if (down && !was) {
            SetKey(cur, f, vk);
            capIdx = -1;
            SetDirty(f, GetTickCount64(), hk);
            if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        keyPrev[vk & 0xFF] = down ? 1 : 0;
    }
}
inline void Panel::Tick(ULONGLONG now, const PanelHooks& hk, HWND hwnd) {
    if (capIdx >= 0) { CaptureScan(now, hk, hwnd); return; }
    CommitIfDue(now, hk, hwnd);
    if (dirty || drag >= 0) return;
    if (now - lastPull >= kPullDelay) {
        lastPull = now;
        UiValues v = cur;
        if (hk.pull) hk.pull(v, hk.user);
        if (!v.Equals(cur)) { cur = v; prev = v; InvalidateRect(hwnd, nullptr, FALSE); }
    }
}

inline void Panel::Paint(HWND hwnd) {
    EnsureFonts();
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT cr; GetClientRect(hwnd, &cr);
    W = cr.right; H = cr.bottom;
    if (W < 1) W = 1;
    if (H < 1) H = 1;
    RebuildLayout();
    const Theme& t = GetTheme();

    // Double-buffer the whole frame: draw every row into an off-screen DC
    // first, then present it with a single BitBlt. This eliminates the
    // visible mid-frame states (dark band repaints, per-row redraw) that
    // cause flicker while dragging sliders, hovering rows, or live-pulling.
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, W, H);
    HGDIOBJ oldBmp = bmp ? SelectObject(mem, bmp) : nullptr;

    BlitFill(mem, 0, 0, W, H, t.bg);
    if (overlay) DrawHeaderBar(mem, W, fonts, t);
    int clipTop = overlay ? 40 : 0;
    int saved = SaveDC(mem);
    IntersectClipRect(mem, 0, clipTop, W, H);
    SetViewportOrgEx(mem, 0, -scroll, nullptr);
    ULONGLONG now = GetTickCount64();
    for (int i = 0; i < (int)items.size(); ++i) {
        const Item& it = items[i];
        bool hov = (i == hot) || (i == drag);
        switch (it.kind) {
        case RK_OVERVIEW: DrawOverviewRow(mem, it.rc, cur, fonts, t); break;
        case RK_SECTION:  DrawSectionRow(mem, it.rc, it.field, fonts, t); break;
        case RK_TOGGLE:   DrawToggleRow(mem, it.rc, (Field)it.field, GetBool(cur, (Field)it.field), hov, fonts, t); break;
        case RK_SLIDER:   DrawSliderRow(mem, it.rc, (Field)it.field, GetFloat(cur, (Field)it.field), hov, fonts, t); break;
        case RK_CHIPS:    DrawChipsRow(mem, it.rc, cur.scale, fonts, t); break;
        case RK_SEG:      DrawSegRow(mem, it.rc, cur.mode, fonts, t); break;
        case RK_KEY:      DrawKeyRow(mem, it.rc, (Field)it.field, GetKey(cur, (Field)it.field), cur.requireCtrlAlt, hov, (capIdx == i), fonts, t); break;
        case RK_FOOTER:   DrawFooterRow(mem, it.rc, fonts, t, dirty, now, flashAt, footer); break;
        case RK_LINK:     DrawLinkRow(mem, it.rc, fonts, t, hov); break;
        default: break;
        }
    }
    RestoreDC(mem, saved);

    // One atomic present to the screen — nothing partially drawn is ever visible.
    if (bmp) BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    if (oldBmp) SelectObject(mem, oldBmp);
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

inline void Panel::OnMouseMove(HWND hwnd, int x, int y) {
    if (drag >= 0 && drag < (int)items.size()) {
        const Item& it = items[drag];
        float v = SliderFromX((Field)it.field, it.rc, x);
        SetFloat(cur, (Field)it.field, v);
        SetDirty((Field)it.field, GetTickCount64(), hooks);
        InvalidateRect(hwnd, nullptr, FALSE);
    } else {
        int idx = HitIndex(x, y + scroll);
        if (idx != hot) {
            hot = idx;
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
}

inline void Panel::OnLButtonDown(HWND hwnd, int x, int y) {
    ULONGLONG now = GetTickCount64();
    if (overlay && y < 40) {
        if (x >= W - 46) { closeHit = true; return; }   // ×
        ReleaseCapture();
        POINT pt{ x, y };
        ClientToScreen(hwnd, &pt);
        SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y)); // drag move
        return;
    }
    int idx = HitIndex(x, y + scroll);
    hot = idx;
    if (idx < 0 || idx >= (int)items.size()) {
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    Item& it = items[idx];
    switch (it.kind) {
    case RK_TOGGLE: {
        Field f = (Field)it.field;
        SetBool(cur, f, !GetBool(cur, f));
        SetDirty(f, now, hooks);
        break;
    }
    case RK_SLIDER: {
        drag = idx;
        SetCapture(hwnd);
        Field f = (Field)it.field;
        SetFloat(cur, f, SliderFromX(f, it.rc, x));
        SetDirty(f, now, hooks);
        break;
    }
    case RK_CHIPS: {
        int areaW = it.rc.right - it.rc.left;
        int gap = 6, n = (int)(sizeof(kScaleChips) / sizeof(kScaleChips[0]));
        int cw = (areaW - gap * (n - 1)) / n;
        int pos = x - it.rc.left;
        if (pos >= 0) {
            int c = pos / (cw + gap);
            if (c >= 0 && c < n) {
                int inside = pos - c * (cw + gap);
                if (inside <= cw) {
                    cur.scale = kScaleChips[c].v;
                    SetDirty(F_SCALE, now, hooks);
                }
            }
        }
        break;
    }
    case RK_SEG: {
        int half = (it.rc.right - it.rc.left) / 2;
        cur.mode = (x - it.rc.left < half) ? 1 : 0;   // [Matched Residual] [Bilinear]
        SetDirty(F_MODE, now, hooks);
        break;
    }
    case RK_KEY: {
        if (allowKeyEdit && FieldIsKey((Field)it.field)) {
            if (capIdx == idx) capIdx = -1;
            else { capIdx = idx; memset(keyPrev, 0, sizeof(keyPrev)); }
        }
        break;
    }
    case RK_LINK: {
        // Open the project homepage in the default browser.
        ShellExecuteW(nullptr, L"open", kGithubUrl, nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    default: break;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

inline void Panel::OnLButtonUp(HWND hwnd, int x, int y) {
    (void)x; (void)y;
    if (drag >= 0) ReleaseCapture();
    drag = -1;
    if (dirty) dirtySince = GetTickCount64();  // restart debounce so commit follows release
    InvalidateRect(hwnd, nullptr, FALSE);
}

inline bool Panel::Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, const PanelHooks& hk) {
    hooks = hk;
    switch (msg) {
    case WM_PAINT:        Paint(hwnd); return true;
    case WM_ERASEBKGND:   return true;
    case WM_MOUSEMOVE:    OnMouseMove(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return true;
    case WM_LBUTTONDOWN:  OnLButtonDown(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return true;
    case WM_LBUTTONUP:    OnLButtonUp(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return true;
    case WM_MOUSELEAVE:   hot = -1; InvalidateRect(hwnd, nullptr, FALSE); return true;
    case WM_CAPTURECHANGED: drag = -1; InvalidateRect(hwnd, nullptr, FALSE); return true;
    case WM_SETCURSOR: {
        if (LOWORD(lp) != HTCLIENT) return false;
        bool hand = (hot >= 0) || (drag >= 0);
        if (!hand && overlay) {
            POINT p; GetCursorPos(&p); ScreenToClient(hwnd, &p);
            if (p.y < 40) hand = true;
        }
        SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
        return true;
    }
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        scroll -= (d / 120) * 48;
        RebuildLayout();
        InvalidateRect(hwnd, nullptr, FALSE);
        return true;
    }
    case WM_TIMER:
        if (wp == kTimer) Tick(GetTickCount64(), hk, hwnd);
        return true;
    default: return false;
    }
}
} // namespace dlssnr_ui
// END ui_panel.h
