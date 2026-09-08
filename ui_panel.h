// ui_panel.h — Shared multi-language dark GDI configuration panel.
// Languages: zh (default) / en / ru / ko. Switchable from the header bar button.
// Used by both the in-game overlay panel (proxy DLL) and the standalone console EXE.
// Header-only: compile into exactly one translation unit per module.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
// GDI+ (flat API) — used only for anti-aliased rounded rects / frames.
// Must come after <windows.h>: GdiplusTypes.h relies on the min/max macros.
// ole2.h is required because WIN32_LEAN_AND_MEAN omits it, and gdiplus.h
// needs IStream from it.
#include <ole2.h>
#include <gdiplus.h>
// The Windows SDK hides the GDI+ flat API inside Gdiplus::DllExports and the
// Gp* stub types inside namespace Gdiplus; re-export the few we use.
using namespace Gdiplus::DllExports;
using Gdiplus::GpPath;
using Gdiplus::GpGraphics;
using Gdiplus::GpPen;
using Gdiplus::GpSolidFill;
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#pragma comment(lib, "shell32.lib")   // ShellExecuteW for the GitHub link
#pragma comment(lib, "gdiplus.lib")   // anti-aliased rounded corners

// ============================================================================
// Shared value set (mirrors nvngx_dlssnr.ini / DlssnrSharedConfig)
// ============================================================================
namespace dlssnr_ui {

// ----- Language selection (zh/en/ru/ko) ------------------------------------
// g_lang is an inline variable: all translation units share one instance so
// proxy_main / proxy_ui / console can read whatever proxy_main most recently set.
enum Lang : int { L_ZH = 0, L_EN = 1, L_RU = 2, L_KO = 3, L_COUNT = 4 };
inline int g_lang = L_ZH;     // current UI language, written from any TU
inline void SetLang(int l) { if (l >= 0 && l < L_COUNT) g_lang = l; }
inline void CycleLang()     { g_lang = (g_lang + 1) % L_COUNT; }
// Short label drawn on the header-bar switcher button.
inline const wchar_t* const kLangShort[L_COUNT] = {
    L"\u4e2d",      // 中
    L"EN",
    L"RU",
    L"\ud55c"       // 한
};
// Full native name shown in the tooltip / footer hint.
inline const wchar_t* const kLangName[L_COUNT] = {
    L"\u7b80\u4f53\u4e2d\u6587",     // 简体中文
    L"English",
    L"\u0420\u0443\u0441\u0441\u043a\u0438\u0439", // Русский
    L"\ud55c\uad6d\uc5b4"          // 한국어
};
// A multi-language string literal: usage RowText4{ L"启用代理", L"Enable Proxy", L"Включить прокси", L"프록시 활성화" }
struct RowText4 { const wchar_t* zh; const wchar_t* en; const wchar_t* ru; const wchar_t* ko; };
// Pick the field matching g_lang. Always returns a non-null pointer.
inline const wchar_t* LANG_PICK(const RowText4& t) {
    switch (g_lang) {
    case L_EN: return t.en ? t.en : t.zh;
    case L_RU: return t.ru ? t.ru : (t.en ? t.en : t.zh);
    case L_KO: return t.ko ? t.ko : (t.en ? t.en : t.zh);
    default:   return t.zh;
    }
}

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
    int    uiLanguage      = L_ZH;   // dlssnr_ui::Lang: 0=zh 1=en 2=ru 3=ko

    bool Equals(const UiValues& o) const {
        return enableProxy == o.enableProxy && scale == o.scale && mode == o.mode &&
               transfer == o.transfer && color == o.color && sharpness == o.sharpness &&
               enableHotkeys == o.enableHotkeys && requireCtrlAlt == o.requireCtrlAlt &&
               enableUi == o.enableUi && keyToggleProxy == o.keyToggleProxy &&
               keyToggleMode == o.keyToggleMode && keyScaleUp == o.keyScaleUp &&
               keyScaleDown == o.keyScaleDown && keyToggleUi == o.keyToggleUi &&
               uiLanguage == o.uiLanguage;
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
static const wchar_t kGithubUrl[]  = L"https://github.com/suviland/DLSSNR-Cost-Scaler-with-control-panel";
static const wchar_t kGithubShow[] = L"github.com/suviland/DLSSNR-Cost-Scaler-with-control-panel";

// ============================================================================
// Theme + GDI helpers
// ============================================================================
struct Theme {
    // Material 3 Expressive — dark scheme
    COLORREF bg       = RGB(0x14, 0x12, 0x18);  // surface
    COLORREF headerBg = RGB(0x0f, 0x0d, 0x13);  // surfaceContainerLow
    COLORREF strip    = RGB(0x26, 0x23, 0x2e);  // surfaceContainer
    COLORREF line     = RGB(0x3a, 0x36, 0x43);  // outlineVariant
    COLORREF text     = RGB(0xe6, 0xe0, 0xe9);  // onSurface
    COLORREF dim      = RGB(0xca, 0xc4, 0xd0);  // onSurfaceVariant
    COLORREF accent   = RGB(0xd0, 0xbc, 0xff);  // primary
    COLORREF accentHi = RGB(0xe8, 0xdc, 0xff);  // primary (hover)
    COLORREF ok       = RGB(0x8d, 0xd3, 0xa8);
    COLORREF warn     = RGB(0xff, 0xd8, 0xa4);
    COLORREF bad      = RGB(0xff, 0xb4, 0xab);
    COLORREF thumbBg  = RGB(0xf2, 0xf5, 0xfa);
    COLORREF knob     = RGB(0x1d, 0x1b, 0x20);  // knob sitting on primary track
    COLORREF onAccent = RGB(0x38, 0x1e, 0x72);  // onPrimary (text on accent)
};
static const Theme& GetTheme() { static Theme t; return t; }

inline HBRUSH MakeBrush(COLORREF c) { return CreateSolidBrush(c); }
inline void BlitFill(HDC dc, int x, int y, int w, int h, COLORREF c) {
    HBRUSH b = MakeBrush(c);
    RECT r{ x, y, x + w, y + h };
    FillRect(dc, &r, b);
    DeleteObject(b);
}
// ---------------------------------------------------------------------------
// GDI+ bootstrap (flat API). All paths/brushes/pens are created and deleted
// per call, so the startup token is simply kept for the module lifetime.
// ---------------------------------------------------------------------------
inline ULONG_PTR g_gdipToken = 0;
inline void EnsureGdiplus() {
    if (g_gdipToken) return;
    Gdiplus::GdiplusStartupInput si;
    ULONG_PTR tok = 0;
    if (GdiplusStartup(&tok, &si, nullptr) == Gdiplus::Ok) g_gdipToken = tok;
}
inline DWORD ToArgb(COLORREF c) {
    return 0xFF000000u | (DWORD(GetRValue(c)) << 16) | (DWORD(GetGValue(c)) << 8) | DWORD(GetBValue(c));
}
// Rounded-rect path via four arcs (the 19041 SDK flat header lacks
// GdipAddPathRoundRect; arcs are exactly equivalent).
inline void AddRoundRectPath(GpPath* path, float x, float y, float w, float h, float r) {
    float m = (w < h ? w : h) * 0.5f;
    if (r > m) r = m;
    if (r < 0.5f) r = 0.5f;
    GdipAddPathArc(path, x, y, r * 2, r * 2, 180.0f, 90.0f);            // top-left
    GdipAddPathArc(path, x + w - r * 2, y, r * 2, r * 2, 270.0f, 90.0f); // top-right
    GdipAddPathArc(path, x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0.0f, 90.0f); // bottom-right
    GdipAddPathArc(path, x, y + h - r * 2, r * 2, r * 2, 90.0f, 90.0f); // bottom-left
    GdipAddPathLine(path, x, y + h - r, x, y + r);                      // close left edge
}
// Anti-aliased filled rounded rect (GDI regions are hard-edged; GDI+ is not).
inline void BlitRound(HDC dc, int x, int y, int w, int h, int rad, COLORREF c) {
    if (w <= 0 || h <= 0) return;
    EnsureGdiplus();
    GpPath* path = nullptr;
    if (GdipCreatePath(Gdiplus::FillModeAlternate, &path) != Gdiplus::Ok) return;
    AddRoundRectPath(path, (float)x, (float)y, (float)w, (float)h, (float)rad);
    GpGraphics* gfx = nullptr;
    if (GdipCreateFromHDC(dc, &gfx) == Gdiplus::Ok) {
        GdipSetSmoothingMode(gfx, Gdiplus::SmoothingModeAntiAlias);
        GpSolidFill* br = nullptr;
        if (GdipCreateSolidFill(ToArgb(c), &br) == Gdiplus::Ok) {
            GdipFillPath(gfx, br, path);
            GdipDeleteBrush(br);
        }
        GdipDeleteGraphics(gfx);
    }
    GdipDeletePath(path);
}
// Anti-aliased 1px rounded-rect outline (inset half a pixel so the pen fits).
inline void BlitFrame(HDC dc, int x, int y, int w, int h, int rad, COLORREF c) {
    if (w <= 0 || h <= 0) return;
    EnsureGdiplus();
    GpPath* path = nullptr;
    if (GdipCreatePath(Gdiplus::FillModeAlternate, &path) != Gdiplus::Ok) return;
    AddRoundRectPath(path, (float)x + 0.5f, (float)y + 0.5f,
                     (float)w - 1.0f, (float)h - 1.0f, (float)rad);
    GpGraphics* gfx = nullptr;
    if (GdipCreateFromHDC(dc, &gfx) == Gdiplus::Ok) {
        GdipSetSmoothingMode(gfx, Gdiplus::SmoothingModeAntiAlias);
        GpPen* pen = nullptr;
        if (GdipCreatePen1(ToArgb(c), 1.0f, Gdiplus::UnitPixel, &pen) == Gdiplus::Ok) {
            GdipDrawPath(gfx, pen, path);
            GdipDeletePen(pen);
        }
        GdipDeleteGraphics(gfx);
    }
    GdipDeletePath(path);
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
// Single-language label, vertically centered inside the given band.
// NOTE: Microsoft YaHei UI has a very tall line box (~1.8x the font size), so
// hand-tuned offsets + DT_TOP draw text visibly too low and clip descenders.
// DT_VCENTER lets DrawTextW center on real glyph metrics instead.
inline void BlitLabel(HDC dc, int x, int y, int w, int h, const wchar_t* s,
                      COLORREF c, HFONT f) {
    if (!s || !s[0]) return;
    SetTextColor(dc, c); SetBkMode(dc, TRANSPARENT);
    HGDIOBJ o = SelectObject(dc, f ? f : GetStockObject(DEFAULT_GUI_FONT));
    RECT r{ x, y, x + w, y + h };
    DrawTextW(dc, s, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, o);
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

// Note: RowText4 is defined at the top of this file (line ~43) for LANG_PICK.

static const RowText4* RowTextFor(Field f) {
    static const RowText4 kMap[] = {
        { L"\u542f\u7528\u4ee3\u7406",                                              L"Enable Proxy",                              L"\u0412\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u043f\u0440\u043e\u043a\u0441\u0438", L"\ud504\ub85d\uc2dc \ud65c\uc131\ud654" },                                  // F_ENABLE_PROXY
        { L"\u5206\u8fa8\u7387\u7f29\u653e",                                         L"Resolution Scale",                          L"\u041c\u0430\u0441\u0448\u0442\u0430\u0431 \u0440\u0430\u0437\u0440\u0435\u0448\u0435\u043d\u0438\u044f", L"\ud574\uc0c1\ub3c4 \ube44\uc728" },                       // F_SCALE
        { L"\u91cd\u5efa\u6a21\u5f0f",                                               L"Resolve Mode",                              L"\u0420\u0435\u0436\u0438\u043c \u043e\u0431\u0440\u0430\u0431\u043e\u0442\u043a\u0438",         L"\ucc98\ub9ac \ubaa8\ub4dc" },                                  // F_MODE
        { L"\u7ec6\u8282\u4f20\u9012\u5f3a\u5ea6",                                   L"Transfer Strength",                         L"\u0421\u0438\u043b\u0430 \u043f\u0435\u0440\u0435\u0434\u0430\u0447\u0438 \u0434\u0435\u0442\u0430\u043b\u0435\u0439", L"\ub514\ud14c\uc77c \uc804\ub2ec \uac15\ub3c4" }, // F_TRANSFER
        { L"\u8272\u5f69\u4f20\u9012\u5f3a\u5ea6",                                   L"Color Strength",                            L"\u0421\u0438\u043b\u0430 \u0446\u0432\u0435\u0442\u0430",                                  L"\uc0c9\uc0c1 \uc804\ub2ec \uac15\ub3c4" },                  // F_COLOR
        { L"\u8fb9\u7f18\u9510\u5316 RCAS",                                          L"Sharpness",                                 L"\u0420\u0435\u0437\u043a\u043e\u0441\u0442\u044c",                                L"\uc120\uba85\ub3c4" },                                       // F_SHARP
        { L"\u6e38\u620f\u5185\u5feb\u6377\u952e",                                   L"In-Game Hotkeys",                            L"\u0418\u0433\u0440\u043e\u0432\u044b\u0435 \u0433\u043e\u0440\u044f\u0447\u0438\u0435 \u043a\u043b\u0430\u0432\u0438\u0448\u0438", L"\uc778\uac8c\uc784 \ub2e8\ucd95\ud0a4" }, // F_ENABLE_HOTKEYS
        { L"\u8981\u6c42 Ctrl+Alt",                                                  L"Require Ctrl+Alt",                          L"\u0422\u0440\u0435\u0431\u043e\u0432\u0430\u0442\u044c Ctrl+Alt",                       L"Ctrl+Alt \ud544\uc694" },                                // F_REQUIRE_CTRLALT
        { L"\u542f\u7528\u9762\u677f\u70ed\u952e",                                   L"Panel Hotkey",                              L"\u0413\u043e\u0440\u044f\u0447\u0430\u044f \u043a\u043b\u0430\u0432\u0438\u0448\u0430 \u043f\u0430\u043d\u0435\u043b\u0438", L"\ud328\ub108 \ub2e8\ucd95\ud0a4" }, // F_ENABLE_UI
        { L"\u5f00\u5173\u4ee3\u7406",                                               L"Toggle Proxy",                              L"\u041f\u0435\u0440\u0435\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u043f\u0440\u043e\u043a\u0441\u0438", L"\ud504\ub85d\uc2dc \ud1a0\uae00" }, // F_KEY_TOGGLE_PROXY
        { L"\u5207\u6362\u6a21\u5f0f",                                               L"Toggle Mode",                               L"\u041f\u0435\u0440\u0435\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u0440\u0435\u0436\u0438\u043c", L"\ubaa8\ub4dc \uc804\ud658" },   // F_KEY_TOGGLE_MODE
        { L"\u63d0\u9ad8\u7f29\u653e",                                               L"Scale Up",                                  L"\u0423\u0432\u0435\u043b\u0438\u0447\u0438\u0442\u044c \u043c\u0430\u0441\u0448\u0442\u0430\u0431", L"\ube44\uc728 \ub192\uc774\uae30" },                     // F_KEY_SCALEUP
        { L"\u964d\u4f4e\u7f29\u653e",                                               L"Scale Down",                                L"\u0423\u043c\u0435\u043d\u044c\u0448\u0438\u0442\u044c \u043c\u0430\u0441\u0448\u0442\u0430\u0431", L"\ube44\uc728 \ub0ae\ucd94\uae30" },                     // F_KEY_SCALEDOWN
        { L"\u5f00\u5173\u9762\u677f",                                               L"Toggle Panel",                              L"\u041f\u0435\u0440\u0435\u043a\u043b\u044e\u0447\u0438\u0442\u044c \u043f\u0430\u043d\u0435\u043b\u044c", L"\ud328\ub110 \ud1a0\uae00" },   // F_KEY_TOGGLEUI
    };
    int i = (int)f;
    if (i < 0 || i >= (int)(sizeof(kMap) / sizeof(kMap[0]))) return nullptr;
    return &kMap[i];
}
// Section ids used as Item.field for RK_SECTION rows
enum Sec : int { S_PROXY = 0x1000, S_QUALITY = 0x1001, S_KEYS = 0x1002 };

struct Item { int kind; int field; RECT rc; };

// Section caption helper (RK_SECTION rows)
inline const RowText4* SecText(int sec) {
    static const RowText4 p = { L"\u4ee3\u7406", L"Proxy", L"\u041f\u0440\u043e\u043a\u0441\u0438", L"\ud504\ub85d\uc2dc" };
    static const RowText4 q = { L"\u753b\u8d28", L"Quality", L"\u041a\u0430\u0447\u0435\u0441\u0442\u0432\u043e", L"\ud488\uc9c8" };
    static const RowText4 k = { L"\u5feb\u6377\u952e", L"Hotkeys", L"\u0413\u043e\u0440\u044f\u0447\u0438\u0435 \u043a\u043b\u0430\u0432\u0438\u0448\u0438", L"\ub2e8\ucd95\ud0a4" };
    if (sec == S_QUALITY) return &q;
    if (sec == S_KEYS) return &k;
    return &p;
}

// Status / button labels reused across rows.
inline const RowText4 kEnabled   = { L"\u5df2\u542f\u7528", L"ACTIVE",         L"\u0410\u043a\u0442\u0438\u0432\u043d\u043e",     L"\ud65c\uc131\ud654\ub428" };
inline const RowText4 kDisabled  = { L"\u5df2\u7981\u7528", L"BYPASSED",       L"\u041e\u0442\u043a\u043b\u044e\u0447\u0435\u043d\u043e", L"\ube44\ud65c\uc131\ud654\ub428" };
inline const RowText4 kMatched   = { L"\u5339\u914d\u6b8b\u5dee", L"Matched Residual", L"\u0421\u043e\u0433\u043b\u0430\u0441\u043e\u0432\u0430\u043d\u043d\u044b\u0439 \u043e\u0441\u0442\u0430\u0442\u043e\u043a", L"\ub9e4\uce29 \uc7ac\ucc28" };
inline const RowText4 kBilinear  = { L"\u53cc\u7ebf\u6027", L"Bilinear Direct",  L"\u0411\u0438\u043b\u0438\u043d\u0435\u0439\u043d\u044b\u0439 \u043f\u0440\u044f\u043c\u043e\u0439", L"\ubc14\uc774\ub9ac\ub2dc\uc5b4 \uc9c1\uc811" };
inline const RowText4 kCapturing = { L"\u6309\u65b0\u952e\u2026 (Esc \u53d6\u6d88)", L"Press new key\u2026 (Esc cancel)", L"\u041d\u0430\u0436\u043c\u0438\u0442\u0435 \u043a\u043b\u0430\u0432\u0438\u0448\u0443\u2026 (Esc \u043e\u0442\u043c\u0435\u043d\u0430)", L"\uc0c8 \ud0a4 \uc785\u8258\u2026 (Esc \ucde8\uc18c)" };
inline const RowText4 kSaving    = { L"\u4fee\u6539\u4e2d\u2026 \u7a0d\u5019\u81ea\u52a8\u4fdd\u5b58 \u00b7 Saving\u2026", L"Saving\u2026 auto-commit pending", L"\u0421\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u0438\u0435\u2026", L"\uc800\uc7a5 \uc911\u2026 \uc790\ub3d9 \uc800\uc7a5 \ub300\uae30" };
inline const RowText4 kSaved     = { L"\u5df2\u4fdd\u5b58 \u00b7 Saved to nvngx_dlssnr.ini", L"Saved to nvngx_dlssnr.ini", L"\u0421\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u043e \u0432 nvngx_dlssnr.ini", L"nvngx_dlssnr.ini\uc5d0 \uc800\uc7a5\ub428" };
inline const RowText4 kAutoSave  = { L"\u4fee\u6539\u540e\u81ea\u52a8\u4fdd\u5b58 \u00b7 Auto-saved to nvngx_dlssnr.ini", L"Auto-saved to nvngx_dlssnr.ini", L"\u0410\u0432\u0442\u043e-\u0441\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u0438\u0435 \u0432 nvngx_dlssnr.ini", L"nvngx_dlssnr.ini \uc790\ub3d9 \uc800\uc7a5" };

// Presets for the scale chips row
struct ChipDef { float v; };
static const ChipDef kScaleChips[] = { {1.00f}, {0.85f}, {0.80f}, {0.75f}, {0.67f}, {0.50f} };

// ---------------------------------------------------------------------------
// Software cursor (in-game overlay). When the proxy hijacks the mouse via raw
// input, the game may keep the OS cursor hidden or frozen (DirectInput /
// raw-input mouse-look); the panel draws its own arrow at the virtual cursor
// position instead. The console EXE never enables it.
// ---------------------------------------------------------------------------
inline bool g_softCursor = false;
inline int  g_softCursorX = 0, g_softCursorY = 0;
inline void SetSoftwareCursorPos(int x, int y) { g_softCursorX = x; g_softCursorY = y; }
inline void DrawArrowCursor(HDC dc, int x, int y) {
    EnsureGdiplus();
    GpPath* path = nullptr;
    if (GdipCreatePath(Gdiplus::FillModeAlternate, &path) != Gdiplus::Ok) return;
    Gdiplus::PointF pts[7] = {
        Gdiplus::PointF(0.0f, 0.0f),  Gdiplus::PointF(0.0f, 13.0f),
        Gdiplus::PointF(3.0f, 10.0f), Gdiplus::PointF(5.5f, 15.0f),
        Gdiplus::PointF(8.0f, 14.0f), Gdiplus::PointF(5.5f, 9.0f),
        Gdiplus::PointF(10.0f, 9.0f)
    };
    GdipAddPathLine2(path, pts, 7);
    GdipClosePathFigure(path);
    GpGraphics* gfx = nullptr;
    if (GdipCreateFromHDC(dc, &gfx) == Gdiplus::Ok) {
        GdipTranslateWorldTransform(gfx, (float)x, (float)y, Gdiplus::MatrixOrderPrepend);
        GdipSetSmoothingMode(gfx, Gdiplus::SmoothingModeAntiAlias);
        GpSolidFill* br = nullptr;
        if (GdipCreateSolidFill(0xFFFFFFFFu, &br) == Gdiplus::Ok) {   // white body
            GdipFillPath(gfx, br, path);
            GdipDeleteBrush(br);
        }
        GpPen* pen = nullptr;
        if (GdipCreatePen1(0xFF000000u, 1.2f, Gdiplus::UnitPixel, &pen) == Gdiplus::Ok) { // black outline
            GdipDrawPath(gfx, pen, path);
            GdipDeletePen(pen);
        }
        GdipDeleteGraphics(gfx);
    }
    GdipDeletePath(path);
}

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
    push(RK_SLIDER, F_SCALE, 48);
    push(RK_CHIPS, F_SCALE, 28);
    push(RK_SEG, F_MODE, 54);
    gap(8);
    push(RK_SECTION, S_QUALITY, 24);              // 画质 Quality
    push(RK_SLIDER, F_TRANSFER, 46);
    push(RK_SLIDER, F_COLOR, 46);
    push(RK_SLIDER, F_SHARP, 46);
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
    // Panel position persistence (backed by the host's ini file).
    bool (*loadPos)(int& x, int& y, void* user) = nullptr;   // false = none saved
    void (*savePos)(int x, int y, void* user) = nullptr;
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
    bool dragMove = false;    // header window-drag in progress (manual, no modal loop)
    POINT dragOff{};          // cursor offset inside the window at drag start
    int capIdx = -1;          // key row index awaiting a key
    bool langHot = false;     // header language-switcher is hovered
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
    void ResetInput(HWND hwnd);   // clear all transient input/drag state
    int HitIndex(int x, int yc) const;
};

// Drawing helpers shared by Paint (free functions)
void DrawHeaderBar(HDC dc, int W, const Fonts& f, const Theme& t, bool langHot);
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
inline void DrawHeaderBar(HDC dc, int W, const Fonts& f, const Theme& t, bool langHot) {
    BlitFill(dc, 0, 0, W, 40, t.headerBg);
    BlitFill(dc, 0, 39, W, 1, t.line);
    BlitLabel(dc, 16, 8, W - 130, 24, L"DLSSNR Cost Scaler", t.text, f.bold);
    // Language switcher chip — click cycles zh → en / ru / ko.
    // Solid rounded rectangle (accent fill + onPrimary label).
    int chipW = 40, chipH = 24, chipR = 7;
    int cx = W - 110, cy = 8;
    COLORREF lc = langHot ? t.accentHi : t.accent;
    BlitRound(dc, cx, cy, chipW, chipH, chipR, lc);
    BlitText(dc, cx, cy, chipW, chipH, kLangShort[g_lang], t.knob, f.bold,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    // × close button (overlay only)
    BlitText(dc, W - 44, 2, 34, 36, L"\u00d7", t.dim, f.base, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
inline void DrawOverviewRow(HDC dc, const RECT& rc, const UiValues& v, const Fonts& f, const Theme& t) {
    int h = rc.bottom - rc.top;
    int y = rc.top + (h - 26) / 2;
    BlitRound(dc, rc.left, y, rc.right - rc.left, 26, 13, t.strip);
    COLORREF c = v.enableProxy ? t.ok : t.bad;
    int cx = rc.left + 18;
    BlitRound(dc, cx - 4, y + 9, 8, 8, 4, c);   // status dot
    BlitLabel(dc, cx + 12, y, rc.right - rc.left - 60, 26,
              LANG_PICK(v.enableProxy ? kEnabled : kDisabled), c, f.bold);
}
inline void DrawSectionRow(HDC dc, const RECT& rc, int sec, const Fonts& f, const Theme& t) {
    const RowText4* rt = SecText(sec);
    int h = rc.bottom - rc.top;
    BlitLabel(dc, rc.left + 2, rc.top, rc.right - rc.left - 40, h,
              LANG_PICK(*rt), t.dim, f.bold);
}
inline void DrawToggleRow(HDC dc, const RECT& rc, Field field, bool val, bool hot, const Fonts& f, const Theme& t) {
    const RowText4* rt = RowTextFor(field);
    int h = rc.bottom - rc.top;
    BlitLabel(dc, rc.left, rc.top, (rc.right - rc.left) - 70, h,
              LANG_PICK(*rt), t.text, f.base);
    int pw = 46, ph = 22, px = rc.right - pw, py = rc.top + (h - ph) / 2;
    int cy = py + ph / 2;
    if (val) {
        BlitRound(dc, px, py, pw, ph, ph / 2, hot ? t.accentHi : t.accent);
        BlitRound(dc, px + pw - 20, cy - 6, 12, 12, 6, t.knob);
    } else {
        BlitRound(dc, px, py, pw, ph, ph / 2, t.strip);
        BlitFrame(dc, px, py, pw - 1, ph - 1, ph / 2, hot ? t.accent : t.line);
        BlitRound(dc, px + 8, cy - 6, 12, 12, 6, t.thumbBg);
    }
}
inline void DrawSliderRow(HDC dc, const RECT& rc, Field field, float val, bool hot, const Fonts& f, const Theme& t) {
    const RowText4* rt = RowTextFor(field);
    wchar_t vb[24];
    if (field == F_SCALE) swprintf_s(vb, L"%d%%", (int)(val * 100.0f + 0.5f));
    else swprintf_s(vb, L"%.2f", val);
    BlitLabel(dc, rc.left, rc.top, (rc.right - rc.left) - 74, 18,
              LANG_PICK(*rt), t.text, f.base);
    BlitText(dc, rc.right - 56, rc.top, 56, 18, vb, t.accent, f.bold, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    // M3 Expressive slider: one continuous pill track, no round thumb — the
    // filled end itself is the handle, with a grip bar while dragging.
    const int trackH = 14;
    int ty = rc.bottom - trackH - 8;
    int tc = ty + trackH / 2;
    int px = SliderToX(field, rc, val);
    int aw = px + trackH / 2;                       // rounded end centered on value
    if (aw > rc.right) aw = rc.right;
    if (aw > rc.left)
        BlitRound(dc, rc.left, ty, aw - rc.left, trackH, trackH / 2, hot ? t.accentHi : t.accent);
    int ix = px + trackH / 2 + 2;
    if (ix < rc.right - 6)
        BlitRound(dc, ix, ty, rc.right - ix, trackH, trackH / 2, t.line);
    BlitRound(dc, rc.right - 5, tc - 2, 4, 4, 2, t.dim);   // stop indicator
    // No grip knob — the filled track end itself is the handle (clean M3E).
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
        if (on) BlitRound(dc, x, cy, cw, 24, 12, t.accent);
        else { BlitRound(dc, x, cy, cw, 24, 12, t.strip); BlitFrame(dc, x, cy, cw - 1, 23, 12, t.line); }
        swprintf_s(buf, L"%d%%", (int)(v * 100.0f + 0.5f));
        BlitText(dc, x, cy, cw, 24, buf, on ? t.onAccent : t.text, f.base, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}
inline void DrawSegRow(HDC dc, const RECT& rc, int mode, const Fonts& f, const Theme& t) {
    int gap = 8;
    int bw = (rc.right - rc.left - gap) / 2;
    auto seg = [&](int x0, bool active, const RowText4& txt) {
        int y0 = rc.top + (rc.bottom - rc.top - 28) / 2, h = 28;
        if (active) BlitRound(dc, x0, y0, bw, h, 14, t.accent);
        else { BlitRound(dc, x0, y0, bw, h, 14, t.strip); BlitFrame(dc, x0, y0, bw - 1, h - 1, 14, t.line); }
        BlitLabel(dc, x0 + 12, y0, bw - 24, h, LANG_PICK(txt),
                  active ? t.onAccent : t.text, f.base);
    };
    seg(rc.left,             mode == 1, kMatched);
    seg(rc.left + bw + gap,  mode == 0, kBilinear);
}
inline void DrawKeyRow(HDC dc, const RECT& rc, Field field, int vk, bool requireMods, bool hot, bool capturing,
                       const Fonts& f, const Theme& t) {
    const RowText4* rt = RowTextFor(field);
    int h = rc.bottom - rc.top;
    BlitLabel(dc, rc.left, rc.top, (rc.right - rc.left) - 190, h,
              LANG_PICK(*rt), t.text, f.base);
    wchar_t kb[64];
    if (capturing) {
        BlitLabel(dc, rc.right - 190, rc.top, 190, h,
                  LANG_PICK(kCapturing), t.warn, f.base);
    } else {
        FormatKeyCombo(vk, requireMods, kb, 64);
        BlitText(dc, rc.right - 170, rc.top, 170, h, kb, hot ? t.accent : t.text, f.bold, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}
inline void DrawFooterRow(HDC dc, const RECT& rc, const Fonts& f, const Theme& t, bool dirty,
                          ULONGLONG now, ULONGLONG flashAt, const wchar_t* footer) {
    // Idle state stays blank — the permanent "auto-saved" hint is gone.
    // Only a transient saving/saved flash or an explicit hint text is shown.
    const RowText4* msg4 = nullptr;
    COLORREF c = t.dim;
    if (dirty) { msg4 = &kSaving; c = t.warn; }
    else if (flashAt && now - flashAt < 2000) { msg4 = &kSaved; c = t.ok; }
    const wchar_t* msg = footer ? footer : (msg4 ? LANG_PICK(*msg4) : L"");
    int h = rc.bottom - rc.top;
    BlitText(dc, rc.left + 2, rc.top, rc.right - rc.left, h, msg, c, f.sub,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}
inline void DrawLinkRow(HDC dc, const RECT& rc, const Fonts& f, const Theme& t, bool hot) {
    int h = rc.bottom - rc.top;
    COLORREF c1 = hot ? t.accentHi : t.accent;
    COLORREF c2 = hot ? t.accentHi : t.dim;
    BlitText(dc, rc.left + 2, rc.top, 64, h, L"GitHub", c1, f.bold, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    BlitText(dc, rc.left + 64, rc.top, (rc.right - rc.left) - 66, h, kGithubShow, c2, f.sub,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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
    if (overlay) DrawHeaderBar(mem, W, fonts, t, langHot);
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

    // Software cursor on top of everything (raw-input virtual cursor mode).
    if (g_softCursor) DrawArrowCursor(mem, g_softCursorX, g_softCursorY);

    // One atomic present to the screen — nothing partially drawn is ever visible.
    if (bmp) BitBlt(dc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    if (oldBmp) SelectObject(mem, oldBmp);
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

inline void Panel::OnMouseMove(HWND hwnd, int x, int y) {
    if (dragMove) {
        // Manual header drag: follow the pointer with SWP_NOACTIVATE so the
        // window never activates (ReShade-style focusless overlay). The
        // system move loop (WM_NCLBUTTONDOWN / HTCAPTION) would activate the
        // window, steal focus from the game and block the message pump.
        POINT pt{ x, y };
        ClientToScreen(hwnd, &pt);
        SetWindowPos(hwnd, nullptr, pt.x - dragOff.x, pt.y - dragOff.y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    if (drag >= 0 && drag < (int)items.size()) {
        const Item& it = items[drag];
        float v = SliderFromX((Field)it.field, it.rc, x);
        SetFloat(cur, (Field)it.field, v);
        SetDirty((Field)it.field, GetTickCount64(), hooks);
        InvalidateRect(hwnd, nullptr, FALSE);
    } else {
        bool newLangHot = overlay && y < 40 && x >= W - 110 && x < W - 70;
        int idx = (y < 40) ? -1 : HitIndex(x, y + scroll);
        bool hoverChanged = (idx != hot) || (newLangHot != langHot);
        if (hoverChanged) {
            hot = idx;
            langHot = newLangHot;
            TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        // The software cursor is drawn during Paint, so every move must
        // schedule a repaint — otherwise the arrow only updates when the
        // hover state changes and visibly stutters along row boundaries.
        if (g_softCursor || hoverChanged)
            InvalidateRect(hwnd, nullptr, FALSE);
    }
}

inline void Panel::OnLButtonDown(HWND hwnd, int x, int y) {
    ULONGLONG now = GetTickCount64();
    if (overlay && y < 40) {
        // Language switcher chip (only meaningful in overlay mode).
        if (x >= W - 110 && x < W - 70) {
            CycleLang();
            cur.uiLanguage = g_lang;
            InvalidateRect(hwnd, nullptr, FALSE);
            if (hooks.commit) hooks.commit(cur, hooks.user);  // persist immediately
            return;
        }
        if (x >= W - 46) { closeHit = true; return; }   // ×
        // Manual window drag — never enter the system move loop; it would
        // activate this (WS_EX_NOACTIVATE) window and take focus from the
        // game, and a modal loop would freeze the panel while it runs.
        dragMove = true;
        POINT pt{ x, y };
        ClientToScreen(hwnd, &pt);
        RECT wr; GetWindowRect(hwnd, &wr);
        dragOff.x = pt.x - wr.left;
        dragOff.y = pt.y - wr.top;
        // Overlay mode is driven by the low-level hook (global delivery) and
        // clamps the cursor to the panel — no SetCapture there. Capture would
        // STEAL the system's single mouse capture from the foreground game
        // (only the foreground window may own it). The console EXE still
        // needs it for drags that leave the window — but only take it if we
        // don't already hold it: re-capturing while captured keeps the state
        // machine dirty and breaks the NEXT drag.
        if (GetCapture() != hwnd) SetCapture(hwnd);
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
        if (GetCapture() != hwnd) SetCapture(hwnd);   // see header-drag note above
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
    dragMove = false;
    // ALWAYS release: BOTH slider drags and header window-drags take the
    // capture, and gating the release on (drag >= 0) alone leaked it after
    // every header drag — the window then held the system's single mouse
    // capture indefinitely, which broke every following drag. ReleaseCapture
    // is a harmless no-op when we don't own it (overlay hook mode).
    ReleaseCapture();
    drag = -1;
    if (dirty) dirtySince = GetTickCount64();  // restart debounce so commit follows release
    InvalidateRect(hwnd, nullptr, FALSE);
}

inline void Panel::ResetInput(HWND hwnd) {
    // Clear every piece of transient input state so a new panel session
    // cannot inherit a half-finished drag, hover, key-capture or pending
    // close from the previous one.
    dragMove = false;
    drag = -1;
    hot = -1;
    langHot = false;
    capIdx = -1;
    closeHit = false;
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
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
    case WM_CAPTURECHANGED:
        // Only react to a REAL capture steal (new owner differs from us);
        // self-triggered notifications must not wipe a drag in progress.
        if ((HWND)lp != hwnd) {
            drag = -1; dragMove = false; InvalidateRect(hwnd, nullptr, FALSE);
        }
        return true;
    case WM_SETCURSOR: {
        if (LOWORD(lp) != HTCLIENT) return false;
        bool hand = (hot >= 0) || (drag >= 0) || langHot;
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
