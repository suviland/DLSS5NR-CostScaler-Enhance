// dlssnr_manager.cpp — "DLSSNR-cost-scaler manager"
//
// Fully custom-painted Material 3 Expressive manager (no native buttons,
// list views or scrollbars): every card, pill button, checkbox row, well and
// scrollbar is GDI+ anti-aliased and theme-aware (free light/dark switch),
// with 4 UI languages (中/EN/RU/한), adaptive layout, F11 fullscreen and
// persisted preferences.
//
// Workflow (mirrors README "安装方法"):
//   1. Source folder = wherever OUR proxy build (nvngx_dlssnr.dll + ini) is;
//      auto-detected from the exe's own folder. A game folder picked here is
//      auto-redirected to step ②.
//   2. Pick a game folder OR a library root; the picked folder itself counts
//      as a target when it contains nvngx_dlssnr*.dll; subfolders scanned
//      recursively. Click rows to tick them.
//   3. Install  = rename the folder's ORIGINAL NVIDIA nvngx_dlssnr.dll to
//                 nvngx_dlssnr_real.dll, then copy our proxy dll + ini in.
//      Uninstall = remove our proxy dll and rename the original back.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <ole2.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cwchar>


#pragma comment(lib, "version.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus::DllExports;
using Gdiplus::GpPath;
using Gdiplus::GpGraphics;
using Gdiplus::GpPen;
using Gdiplus::GpSolidFill;
using Gdiplus::GpRegion;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const wchar_t* kDll  = L"nvngx_dlssnr.dll";
static const wchar_t* kReal = L"nvngx_dlssnr_real.dll";
static const wchar_t* kIni  = L"nvngx_dlssnr.ini";

// Action ids (all painted; no child button controls)
enum {
    A_NONE = 0,
    A_SRC_BROWSE, A_ROOT_BROWSE, A_SCAN,
    A_ALL, A_SELNONE,
    A_INSTALL, A_UNINSTALL,
    A_THEME, A_LANG, A_POPOUT, A_CLEAR,
    A_PG0, A_PG1, A_PG2, A_PG3,   // nav pages
    A_REFRESH,             // installed page: re-classify from disk
    A_DBG_RELOAD, A_DBG_FOLDER,   // debug page actions
};

enum { LANG_ZH = 0, LANG_EN, LANG_RU, LANG_KO, LANG_COUNT };
static int g_lang = LANG_ZH;
static const wchar_t* kLangShort[LANG_COUNT] = { L"中", L"EN", L"RU", L"한" };

// Target states
enum { TS_ORIGINAL = 0, TS_INSTALLED, TS_PARTIAL, TS_PROXYONLY };

// ---------------------------------------------------------------------------
// BlueMiku palette (creeper-qt design resource, MIT) — used by the debug page
// ---------------------------------------------------------------------------
struct Miku {
    COLORREF primary, onPrimary, primContainer, onPrimContainer,
             surfLow, surfLowest, surfHigh, outline, outlineVar, onVar, onSurface;
};
static Miku MikuScheme(bool dark) {
    Miku k;
    if (dark) {   // kBlueMikuDarkColorScheme
        k.primary         = RGB(175, 198, 255);
        k.onPrimary       = RGB(0, 45, 108);
        k.primContainer   = RGB(0, 67, 152);
        k.onPrimContainer = RGB(217, 226, 255);
        k.surfLow         = RGB(34, 35, 42);
        k.surfLowest      = RGB(27, 27, 31);
        k.surfHigh        = RGB(43, 46, 56);
        k.outline         = RGB(143, 144, 153);
        k.outlineVar      = RGB(68, 70, 79);
        k.onVar           = RGB(197, 198, 208);
        k.onSurface       = RGB(227, 226, 230);
    } else {      // kBlueMikuLightColorScheme
        k.primary         = RGB(0, 89, 199);
        k.onPrimary       = RGB(255, 255, 255);
        k.primContainer   = RGB(217, 226, 255);
        k.onPrimContainer = RGB(0, 26, 67);
        k.surfLow         = RGB(242, 243, 252);
        k.surfLowest      = RGB(254, 251, 255);
        k.surfHigh        = RGB(226, 233, 249);
        k.outline         = RGB(117, 119, 128);
        k.outlineVar      = RGB(197, 198, 208);
        k.onVar           = RGB(68, 70, 79);
        k.onSurface       = RGB(27, 27, 31);
    }
    return k;
}

static const wchar_t* StatusText(int s) {
    static const wchar_t* T[LANG_COUNT][4] = {
        { L"未安装 · 原版", L"已安装", L"异常 · 请手动检查", L"异常 · 缺原版" },
        { L"Not installed · stock", L"Installed", L"Broken · check", L"Broken · no stock" },
        { L"Не уст. · оригинал", L"Установлено", L"Ошибка · проверьте", L"Ошибка · нет ориг." },
        { L"미설치 · 원본", L"설치됨", L"이상 · 확인 필요", L"이상 · 원본 없음" },
    };
    return T[g_lang][s];
}

struct Target {
    std::wstring dir;
    int  state = TS_ORIGINAL;
    bool check = false;
};

// ---------------------------------------------------------------------------
// M3E skin (light / dark)
// ---------------------------------------------------------------------------
struct Skin {
    COLORREF bg, card, cardHi, outline, text, sub,
             primary, onPrimary, primContainer, onPrimContainer,
             field, ok, warn;
    // Legacy aliases kept so call sites can use either name.
    COLORREF accent, border, editBg, listBg, hoverRow;
};

static Skin MakeSkin(bool dark) {
    Skin s;
    if (dark) {
        s.bg       = RGB(0x12, 0x10, 0x15);
        s.card     = RGB(0x1f, 0x1d, 0x24);
        s.cardHi   = RGB(0x2a, 0x27, 0x30);
        s.outline  = RGB(0x36, 0x33, 0x3e);
        s.text     = RGB(0xe6, 0xe0, 0xe9);
        s.sub      = RGB(0xa5, 0x9e, 0xae);
        s.primary  = RGB(0xd0, 0xbc, 0xff);
        s.onPrimary= RGB(0x38, 0x1e, 0x72);
        s.primContainer = RGB(0x4f, 0x37, 0x8b);
        s.onPrimContainer = RGB(0xe8, 0xdd, 0xff);
        s.field    = RGB(0x28, 0x25, 0x2e);
        s.ok       = RGB(0x8d, 0xd3, 0xa8);
        s.warn     = RGB(0xff, 0xd8, 0xa4);
    } else {
        s.bg       = RGB(0xf3, 0xf0, 0xf7);
        s.card     = RGB(0xff, 0xff, 0xff);
        s.cardHi   = RGB(0xf3, 0xed, 0xf7);
        s.outline  = RGB(0xe2, 0xdc, 0xe9);
        s.text     = RGB(0x1c, 0x1b, 0x1f);
        s.sub      = RGB(0x6c, 0x66, 0x7a);
        s.primary  = RGB(0x67, 0x50, 0xa4);
        s.onPrimary= RGB(0xff, 0xff, 0xff);
        s.primContainer = RGB(0xe8, 0xdd, 0xff);
        s.onPrimContainer = RGB(0x21, 0x00, 0x5d);
        s.field    = RGB(0xf1, 0xed, 0xf6);
        s.ok       = RGB(0x1f, 0x8a, 0x4d);
        s.warn     = RGB(0xb0, 0x6a, 0x00);
    }
    s.accent = s.primary;      // legacy alias
    s.border = s.outline;      // legacy alias
    s.editBg = s.field;        // legacy alias
    s.listBg = s.card;         // legacy alias
    s.hoverRow = s.cardHi;     // legacy alias
    return s;
}

static bool  g_dark  = true;
static Skin  g_skin  = MakeSkin(true);
static HWND  g_hwnd  = nullptr;
static HFONT g_font  = nullptr;    // -15 base
static HFONT g_fontBold = nullptr; // -15 semibold
static HFONT g_fontTitle = nullptr; // -21 semibold
static HFONT g_fontSmall = nullptr; // -12

static std::wstring g_srcDir;
static std::wstring g_rootDir;   // ② mirror: source of truth if the edit round-trips fail
static std::vector<Target> g_targets;

static int    g_hotAction = A_NONE;   // painted button under cursor
static int    g_pressAction = A_NONE;
static int    g_hotRow = -1;          // hovered target row index
static bool   g_sbDragList = false, g_sbDragLog = false;
static int    g_listScroll = 0;       // px
static int    g_logScroll = 0;        // px from top of virtual content
static bool   g_logStick = true;      // auto-follow tail
static bool   g_fullscreen = false;
static RECT   g_restoreRect = {};

static std::vector<std::wstring> g_lines;      // log lines (capped)
static const size_t kMaxLogLines = 400;

// Sidebar pages: 0 quick-install, 1 installed management, 2 history + log
static int    g_page = 0;
static int    g_hotRail = -1;
static std::vector<Target>        g_installed;    // remembered installs (page 1)
static std::vector<std::wstring>  g_histLines;    // audit trail (page 2)
static int    g_instScroll = 0;
static int    g_histScroll = 0;
static void ReloadInstalled();    // defined after ClassifyTarget below
static std::vector<std::wstring> LoadHistoryLines();   // defined after AppendHistory
static std::vector<std::wstring> LoadInstalledList();  // defined in the prefs section
static void ReloadHistory() {
    g_histLines = LoadHistoryLines();
    g_histScroll = 0;
}

static COLORREF StatusColor(int st) {
    switch (st) {
    case TS_INSTALLED: return g_skin.ok;
    case TS_PARTIAL:
    case TS_PROXYONLY: return g_skin.warn;
    }
    return g_skin.sub;
}

// Brushes for the native EDIT controls (everything else is painted).
static HBRUSH g_brField = nullptr;
static void BuildSkinBrushes() {
    if (g_brField) { DeleteObject(g_brField); g_brField = nullptr; }
    g_brField = CreateSolidBrush(g_skin.field);
}
static void DestroySkinBrushes() {
    if (g_brField) { DeleteObject(g_brField); g_brField = nullptr; }
}

static HWND g_pop = nullptr, g_popEdit = nullptr;
static HWND g_editSrc = nullptr, g_editRoot = nullptr;

static void LogLine(const std::wstring& s);   // defined with the log buffer below
static void ClampScrolls();                   // defined in the layout section below

// ---------------------------------------------------------------------------
// Localized strings
// ---------------------------------------------------------------------------
struct MS {
    const wchar_t *sub, *cap1, *cap2, *cap3, *browse, *scan,
                  *all, *none, *install, *uninstall,
                  *toLight, *toDark, *logTitle, *popout, *clear, *empty,
                  *readyAuto, *readyManual, *scanNone, *errRoot, *noSel,
                  *page1, *page2, *refresh, *instEmpty, *histEmpty,
                  *page3, *dbgEmpty,
                  *dbgReload, *dbgFolder, *lblScale, *lblMode, *lblSharp,
                  *tProxy, *tHotkeys, *tUi, *tTransfer, *tColor,
                  *tKeys, *kProxy, *kMode, *kUp, *kDown, *kUi, *capHint,
                  *tCtrlAlt, *lblMatched, *lblBilinear, *dbgSaved,
                  *tAnamorphic, *tScaleX, *tScaleY, *tDepthAware, *tVrnr,
                  *tNRSet, *tCustomNR, *styleBal, *styleSharp, *styleCine,
                  *lblIntensity, *lblLocalStruct, *lblLocalTone, *lblSkin, *tAutoMask;
    const wchar_t* cntFmt;
};
static const MS kM[LANG_COUNT] = {
    { L"游戏内面板代理 · 安装 / 卸载工具",
      L"代理文件包（与代理 DLL 同目录时自动识别）",
      L"选择游戏文件夹或游戏库根目录，然后扫描",
      L"勾选要操作的游戏目录（双击可在资源管理器中打开）",
      L"浏览...", L"扫描", L"全选", L"全不选",
      L"一键安装", L"一键卸载",
      L"☀ 浅色", L"☾ 深色", L"日志", L"分离 ↗", L"清空",
      L"暂无目标——点击「扫描」以发现游戏目录",
      L"[就绪] 已自动识别源目录（本管理器所在文件夹）。接下来在②中选择游戏目录并扫描。按 F11 可切换全屏。",
      L"[就绪] 未在管理器所在文件夹找到代理 nvngx_dlssnr.dll——请在①中手动选择发布包文件夹；要安装的游戏目录请在②中选择。按 F11 可切换全屏。",
      L"[扫描] 未发现包含 nvngx_dlssnr 的文件夹。若该游戏本未自带原版 DLL，请先把 NVIDIA 的 nvngx_dlssnr.dll 手动放入游戏目录，再重新扫描。",
      L"[错误] 请先在②中选择有效的游戏文件夹或根目录。",
      L"[提示] 没有勾选任何目标——请先在③的列表中勾选要操作的文件夹。",
      L"已安装 NR CostScaler 管理（勾选后卸载，双击打开目录）",
      L"操作记录（安装 / 卸载历史）",
      L"刷新",
      L"暂无已安装记录——先在「快速安装」页完成安装",
      L"暂无操作记录",
      L"面板调试（读写 nvngx_dlssnr.ini）",
      L"请先在「快速安装」页的①中选择代理包文件夹，再回到本页打开调试面板",
      L"重载 ini", L"打开文件夹", L"分辨率缩放", L"重建模式", L"锐化强度",
      L"代理注入", L"快捷键", L"游戏内面板", L"传递强度", L"色彩强度",
      L"快捷键", L"切换代理", L"切换模式", L"提高缩放", L"降低缩放", L"呼出面板", L"按下任意键…",
      L"Ctrl+Alt 组合", L"匹配残差", L"双线性", L"已保存 ✓",
      L"各向异性缩放（实验）", L"水平缩放", L"垂直缩放", L"深度感知轮廓", L"隔帧推理 · 实验",
      L"NVIDIA NR 参数", L"自定义 NR 参数（覆盖调用方）", L"平衡", L"锐利", L"电影",
      L"强度", L"局部结构", L"局部色调", L"皮肤结构", L"自动遮罩",
      L"发现 %d 个目标 · 已勾选 %d 项" },
    { L"In-game panel proxy · Install / Uninstall",
      L"Proxy package (auto-detected next to this manager)",
      L"Pick a game folder or library root, then Scan",
      L"Tick the game folders to operate on (double-click to open)",
      L"Browse...", L"Scan", L"Select all", L"Clear",
      L"Install selected", L"Uninstall selected",
      L"☀ Light", L"☾ Dark", L"Log", L"Pop out ↗", L"Clear",
      L"No targets yet — press Scan to discover game folders",
      L"[Ready] Source folder auto-detected (this manager's folder). Now pick a game dir in ② and Scan. F11 toggles fullscreen.",
      L"[Ready] Proxy nvngx_dlssnr.dll not found next to this manager — pick the release folder in ①; game folders go in ②. F11 toggles fullscreen.",
      L"[Scan] No folder containing nvngx_dlssnr was found. If the game ships no stock DLL, place NVIDIA's nvngx_dlssnr.dll into the game dir first, then rescan.",
      L"[Error] Please pick a valid game folder or root in ② first.",
      L"[Hint] Nothing ticked — tick target folders in the ③ list first.",
      L"Installed NR CostScaler (tick to uninstall, double-click to open)",
      L"Operation history (install / uninstall records)",
      L"Refresh",
      L"No installed records yet — install from the Quick Install page first",
      L"No operations recorded yet",
      L"Panel debug (edits nvngx_dlssnr.ini)",
      L"Pick the proxy package folder in ① (Quick Install) first, then reopen this page",
      L"Reload ini", L"Open folder", L"Resolution scale", L"Resolve mode", L"Sharpness",
      L"Proxy injection", L"Hotkeys", L"In-game panel", L"Transfer strength", L"Color strength",
      L"Hotkeys", L"Toggle proxy", L"Toggle mode", L"Scale up", L"Scale down", L"Show panel", L"Press any key…",
      L"Ctrl+Alt combo", L"Matched Residual", L"Bilinear Direct", L"Saved ✓",
      L"Anamorphic scaling (experimental)", L"Horizontal scale", L"Vertical scale", L"Depth-aware resolve", L"Alternating frames · exp",
      L"NVIDIA NR settings", L"Custom NR params (override caller)", L"Balanced", L"Sharp", L"Cinematic",
      L"Intensity", L"Local structure", L"Local tone", L"Skin structure", L"Auto mask",
      L"%d targets found · %d ticked" },
    { L"Прокси внутриигровой панели · Установка / удаление",
      L"Пакет прокси (определяется автоматически рядом с менеджером)",
      L"Выберите папку игры или корень библиотеки и нажмите «Сканировать»",
      L"Отметьте папки игр (двойной клик — открыть в проводнике)",
      L"Обзор...", L"Сканировать", L"Выбрать все", L"Снять все",
      L"Установить выбранные", L"Удалить выбранные",
      L"☀ Светлая", L"☾ Тёмная", L"Журнал", L"Отделить ↗", L"Очистить",
      L"Пока нет целей — нажмите «Сканировать»",
      L"[Готово] Папка источника определена автоматически. Выберите папку игры в ② и нажмите «Сканировать». F11 — полный экран.",
      L"[Готово] Прокси nvngx_dlssnr.dll не найден рядом с менеджером — укажите папку релиза в ①; папки игр — в ②. F11 — полный экран.",
      L"[Сканирование] Папки с nvngx_dlssnr не найдены. Если игра не содержит оригинальный DLL, сначала положите NVIDIA nvngx_dlssnr.dll в папку игры.",
      L"[Ошибка] Сначала выберите действительную папку игры в ②.",
      L"[Подсказка] Ничего не отмечено — отметьте папки в списке ③.",
      L"Установленные NR CostScaler (отметьте для удаления, двойной клик — открыть)",
      L"История операций (установка / удаление)",
      L"Обновить",
      L"Записей об установке пока нет — установите на странице «Быстрая установка»",
      L"Операций пока не было",
      L"Отладка панели (правит nvngx_dlssnr.ini)",
      L"Сначала выберите папку пакета прокси в ① («Быстрая установка»), затем вернитесь сюда",
      L"Перезагрузить ini", L"Открыть папку", L"Масштаб", L"Режим", L"Резкость",
      L"Внедрение прокси", L"Горячие клавиши", L"Панель в игре", L"Сила переноса", L"Сила цвета",
      L"Горячие клавиши", L"Сменить прокси", L"Сменить режим", L"Увеличить", L"Уменьшить", L"Показать панель", L"Нажмите любую клавишу…",
      L"Комбо Ctrl+Alt", L"Согласованный остаток", L"Билинейный прямой", L"Сохранено ✓",
      L"Анизотропный масштаб (эксп.)", L"Гориз. масштаб", L"Верт. масштаб", L"Учёт глубины", L"Черезкадрово · эксп.",
      L"Параметры NVIDIA NR", L"Свои параметры NR (замещают вызывавшего)", L"Баланс", L"Резкость", L"Кино",
      L"Интенсивность", L"Лок. структура", L"Лок. тон", L"Структура кожи", L"Автомаска",
      L"Найдено %d · отмечено %d" },
    { L"게임 내 패널 프록시 · 설치 / 제거",
      L"프록시 패키지 (이 관리자와 같은 폴더면 자동 인식)",
      L"게임 폴더 또는 라이브러리 루트를 선택하고 스캔",
      L"작업할 게임 폴더를 선택 (더블클릭으로 열기)",
      L"찾아보기...", L"스캔", L"모두 선택", L"선택 해제",
      L"선택 항목 설치", L"선택 항목 제거",
      L"☀ 라이트", L"☾ 다크", L"로그", L"분리 ↗", L"지우기",
      L"대상 없음 — 스캔을 눌러 게임 폴더를 찾으세요",
      L"[준비] 소스 폴더를 자동 인식했습니다. ②에서 게임 폴더를 선택하고 스캔하세요. F11로 전체 화면 전환.",
      L"[준비] 관리자 옆에서 프록시 nvngx_dlssnr.dll을 찾지 못했습니다 — ①에서 릴리스 폴더를, 게임 폴더는 ②에서 선택하세요. F11로 전체 화면 전환.",
      L"[스캔] nvngx_dlssnr이 포함된 폴더를 찾지 못했습니다. 게임에 원본 DLL이 없다면 NVIDIA nvngx_dlssnr.dll을 게임 폴더에 넣고 다시 스캔하세요.",
      L"[오류] ②에서 유효한 게임 폴더를 먼저 선택하세요.",
      L"[안내] 선택된 항목이 없습니다 — ③ 목록에서 폴더를 먼저 선택하세요.",
      L"설치된 NR CostScaler 관리 (체크 후 제거, 더블클릭으로 열기)",
      L"작업 기록 (설치 / 제거 이력)",
      L"새로 고침",
      L"설치 기록이 없습니다 — 먼저 '빠른 설치' 페이지에서 설치하세요",
      L"아직 기록된 작업이 없습니다",
      L"패널 디버그 (nvngx_dlssnr.ini 편집)",
      L"먼저 '빠른 설치' 페이지 ①에서 프록시 패키지 폴더를 선택한 뒤 이 페이지로 돌아오세요",
      L"ini 다시 로드", L"폴더 열기", L"해상도 배율", L"처리 모드", L"샤픈 강도",
      L"프록시 주입", L"단축키", L"게임 내 패널", L"전송 강도", L"색상 강도",
      L"단축키", L"프록시 전환", L"모드 전환", L"배율 올리기", L"배율 내리기", L"패널 표시", L"아무 키나 누르세요…",
      L"Ctrl+Alt 조합", L"매칭 재차", L"바이리디어 직접", L"저장됨 ✓",
      L"아니소트로픽 배율 (실험)", L"가로 배율", L"세로 배율", L"깊이 인식", L"교대 프레임 · 실험",
      L"NVIDIA NR 설정", L"NR 파라미터 사용 (호출부 대체)", L"밸런스", L"샤프", L"시네마틱",
      L"강도", L"로컬 구조", L"로컬 톤", L"피부 구조", L"자동 마스크",
      L"%d개 발견 · %d개 선택" },
};

// ---------------------------------------------------------------------------
// Filesystem / PE-version helpers
// ---------------------------------------------------------------------------
static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool DirExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static long long FileSize(const std::wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return -1;
    return ((long long)d.nFileSizeHigh << 32) | (long long)d.nFileSizeLow;
}
static std::wstring FileCompany(const std::wstring& p) {
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(p.c_str(), &handle);
    if (!size) return L"";
    std::vector<BYTE> buf(size);
    if (!GetFileVersionInfoW(p.c_str(), 0, size, buf.data())) return L"";
    struct LC { WORD lang; WORD code; }* tr = nullptr; UINT tl = 0;
    if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&tr, &tl) ||
        tl < sizeof(LC)) return L"";
    wchar_t sub[256];
    swprintf_s(sub, L"\\StringFileInfo\\%04x%04x\\CompanyName", tr[0].lang, tr[0].code);
    wchar_t* val = nullptr; UINT vl = 0;
    if (VerQueryValueW(buf.data(), sub, (LPVOID*)&val, &vl) && val) return val;
    return L"";
}
static bool LooksLikeRealDll(const std::wstring& p) {
    if (!FileExists(p)) return false;
    std::wstring c = FileCompany(p);
    if (c.find(L"NVIDIA") != std::wstring::npos) return true;
    return FileSize(p) > 8 * 1024 * 1024;
}
static bool LooksLikeProxyDll(const std::wstring& p) {
    return FileExists(p) && !LooksLikeRealDll(p);
}
static void ClassifyTarget(Target& t) {
    std::wstring dll  = t.dir + L"\\" + kDll;
    std::wstring real = t.dir + L"\\" + kReal;
    bool hasDll  = FileExists(dll);
    bool hasReal = FileExists(real);
    bool dllIsReal  = hasDll  && LooksLikeRealDll(dll);
    bool dllIsProxy = hasDll  && !dllIsReal;
    if (hasReal && dllIsProxy)           t.state = TS_INSTALLED;
    else if (!hasReal && dllIsReal)      t.state = TS_ORIGINAL;
    else if (hasReal || dllIsReal)       t.state = TS_PARTIAL;
    else if (dllIsProxy)                 t.state = TS_PROXYONLY;
    else                                 t.state = TS_ORIGINAL;
    t.check = (t.state == TS_ORIGINAL || t.state == TS_INSTALLED);
}
static void ReloadInstalled() {
    g_installed.clear();
    for (const std::wstring& d : LoadInstalledList()) {
        if (!DirExists(d)) continue;
        Target t;
        t.dir = d;
        ClassifyTarget(t);
        t.check = false;
        g_installed.push_back(t);
    }
    g_instScroll = 0;
}
static std::wstring WinErr(DWORD e) {
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, e, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   (LPWSTR)&buf, 0, nullptr);
    std::wstring s = buf ? buf : L"unknown error";
    if (buf) LocalFree(buf);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back();
    wchar_t code[40];
    swprintf_s(code, L"  (0x%08X)", e);
    return s + code;
}
static bool SameDir(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}
static std::wstring GetEditText(HWND e) {
    wchar_t buf[MAX_PATH * 2];
    GetWindowTextW(e, buf, MAX_PATH * 2);
    std::wstring s = buf;
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/' || s.back() == L' ')) s.pop_back();
    return s;
}
static std::wstring PickFolder(HWND owner) {
    std::wstring result;
    IFileDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))))
        return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                result = p;
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

// ---------------------------------------------------------------------------
// Preferences persistence
// ---------------------------------------------------------------------------
static std::wstring PrefsPath() {
    wchar_t exe[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    std::wstring dir(exe);
    size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.resize(slash);
    return dir + L"\\dlssnr_manager.ini";
}
static void LoadPrefs() {
    std::wstring ini = PrefsPath();
    int lang = GetPrivateProfileIntW(L"UI", L"Language", LANG_ZH, ini.c_str());
    if (lang < 0 || lang >= LANG_COUNT) lang = LANG_ZH;
    g_lang = lang;
    g_dark = GetPrivateProfileIntW(L"UI", L"Dark", 1, ini.c_str()) != 0;
    g_skin = MakeSkin(g_dark);
}
static void SavePrefs() {
    std::wstring ini = PrefsPath();
    wchar_t v[8];
    swprintf_s(v, L"%d", g_lang);
    WritePrivateProfileStringW(L"UI", L"Language", v, ini.c_str());
    swprintf_s(v, L"%d", g_dark ? 1 : 0);
    WritePrivateProfileStringW(L"UI", L"Dark", v, ini.c_str());
}

// ---------------------------------------------------------------------------
// Install history — remember which games' NR we installed / uninstalled.
//   dlssnr_manager.ini  [Installed]  dir=1   → currently installed (record)
//   dlssnr_manager_history.log            → full append-only audit trail
// ---------------------------------------------------------------------------
static std::wstring HistoryPath() {
    std::wstring ini = PrefsPath();
    size_t slash = ini.find_last_of(L"\\/");
    return ini.substr(0, slash + 1) + L"dlssnr_manager_history.log";
}

static std::vector<std::wstring> LoadInstalledList() {
    std::wstring ini = PrefsPath();
    std::vector<wchar_t> buf(32768);
    GetPrivateProfileStringW(L"Installed", nullptr, L"", buf.data(), (DWORD)buf.size(), ini.c_str());
    std::vector<std::wstring> out;
    const wchar_t* p = buf.data();
    while (*p) {
        out.push_back(p);
        p += wcslen(p) + 1;
    }
    return out;
}
static void MarkInstalled(const std::wstring& dir) {
    WritePrivateProfileStringW(L"Installed", dir.c_str(), L"1", PrefsPath().c_str());
}
static void MarkUninstalled(const std::wstring& dir) {
    WritePrivateProfileStringW(L"Installed", dir.c_str(), nullptr, PrefsPath().c_str());
}
static void AppendHistory(const wchar_t* action, const std::wstring& dir) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t ts[48];
    swprintf_s(ts, L"[%04u-%02u-%02u %02u:%02u:%02u] ",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    FILE* f = nullptr;
    if (_wfopen_s(&f, HistoryPath().c_str(), L"a, ccs=UTF-8") == 0 && f) {
        fwprintf(f, L"%s%s %s\n", ts, action, dir.c_str());
        fclose(f);
    }
}
static std::vector<std::wstring> LoadHistoryLines() {
    std::vector<std::wstring> out;
    FILE* f = nullptr;
    if (_wfopen_s(&f, HistoryPath().c_str(), L"r, ccs=UTF-8") == 0 && f) {
        wchar_t line[1024];
        while (fgetws(line, 1024, f)) {
            size_t n = wcslen(line);
            while (n && (line[n - 1] == L'\n' || line[n - 1] == L'\r')) line[--n] = 0;
            if (n) out.push_back(line);
        }
        fclose(f);
    }
    return out;
}

// ---------------------------------------------------------------------------
// GDI+ bootstrap + AA helpers
// ---------------------------------------------------------------------------
static ULONG_PTR g_gdipToken = 0;
static void EnsureGdiplus() {
    if (g_gdipToken) return;
    Gdiplus::GdiplusStartupInput si;
    ULONG_PTR tok = 0;
    if (GdiplusStartup(&tok, &si, nullptr) == Gdiplus::Ok) g_gdipToken = tok;
}
static DWORD ToArgb(COLORREF c) {
    return 0xFF000000u | (DWORD(GetRValue(c)) << 16) | (DWORD(GetGValue(c)) << 8) | DWORD(GetBValue(c));
}
static void AddRoundRectPath(GpPath* path, float x, float y, float w, float h, float r) {
    float m = (w < h ? w : h) * 0.5f;
    if (r > m) r = m;
    if (r < 0.5f) r = 0.5f;
    GdipAddPathArc(path, x, y, r * 2, r * 2, 180.0f, 90.0f);
    GdipAddPathArc(path, x + w - r * 2, y, r * 2, r * 2, 270.0f, 90.0f);
    GdipAddPathArc(path, x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0.0f, 90.0f);
    GdipAddPathArc(path, x, y + h - r * 2, r * 2, r * 2, 90.0f, 90.0f);
    GdipClosePathFigure(path);
}
static void GpRoundFill(HDC dc, int x, int y, int w, int h, int rad, COLORREF c) {
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
static void GpRoundFrame(HDC dc, int x, int y, int w, int h, int rad, COLORREF c, float width = 1.0f) {
    if (w <= 0 || h <= 0) return;
    EnsureGdiplus();
    GpPath* path = nullptr;
    if (GdipCreatePath(Gdiplus::FillModeAlternate, &path) != Gdiplus::Ok) return;
    AddRoundRectPath(path, (float)x + width * 0.5f, (float)y + width * 0.5f,
                     (float)w - width, (float)h - width, (float)rad);
    GpGraphics* gfx = nullptr;
    if (GdipCreateFromHDC(dc, &gfx) == Gdiplus::Ok) {
        GdipSetSmoothingMode(gfx, Gdiplus::SmoothingModeAntiAlias);
        GpPen* pen = nullptr;
        if (GdipCreatePen1(ToArgb(c), width, Gdiplus::UnitPixel, &pen) == Gdiplus::Ok) {
            GdipDrawPath(gfx, pen, path);
            GdipDeletePen(pen);
        }
        GdipDeleteGraphics(gfx);
    }
    GdipDeletePath(path);
}
static HRGN RoundRgn(int x, int y, int w, int h, int rad) {
    EnsureGdiplus();
    GpPath* path = nullptr;
    HRGN rgn = nullptr;
    if (GdipCreatePath(Gdiplus::FillModeAlternate, &path) == Gdiplus::Ok) {
        AddRoundRectPath(path, (float)x, (float)y, (float)w, (float)h, (float)rad);
        GpRegion* reg = nullptr;
        if (GdipCreateRegionPath(path, &reg) == Gdiplus::Ok) {
            GdipGetRegionHRgn(reg, nullptr, &rgn);
            GdipDeleteRegion(reg);
        }
        GdipDeletePath(path);
    }
    return rgn;
}
static COLORREF Lighten(COLORREF c, int amt = 18) {
    int r = GetRValue(c) + amt, g = GetGValue(c) + amt, b = GetBValue(c) + amt;
    return RGB(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}
static COLORREF Darken(COLORREF c, int amt = 24) {
    int r = GetRValue(c) - amt, g = GetGValue(c) - amt, b = GetBValue(c) - amt;
    return RGB(r < 0 ? 0 : r, g < 0 ? 0 : g, b < 0 ? 0 : b);
}

// ---------------------------------------------------------------------------
// Scan
// ---------------------------------------------------------------------------
static bool AlreadyListed(const std::wstring& dir) {
    for (const Target& t : g_targets)
        if (SameDir(t.dir, dir)) return true;
    return false;
}
static void ScanTargets(const std::wstring& root) {
    g_targets.clear();
    g_listScroll = 0;
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    auto TryAdd = [&](const std::wstring& dir) {
        if (SameDir(dir, g_srcDir)) return;
        if (FileExists(dir + L"\\" + kDll) || FileExists(dir + L"\\" + kReal)) {
            Target t;
            t.dir = dir;
            ClassifyTarget(t);
            if (!AlreadyListed(dir)) g_targets.push_back(t);
        }
    };
    TryAdd(root);
    std::vector<std::pair<std::wstring, int>> stack;
    stack.push_back({ root, 0 });
    size_t visited = 0;
    while (!stack.empty() && visited < 80000 && g_targets.size() < 400) {
        std::wstring dir = stack.back().first;
        int depth = stack.back().second;
        stack.pop_back();
        ++visited;
        TryAdd(dir);
        if (depth >= 6) continue;
        WIN32_FIND_DATAW fd;
        HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (find == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == L'.') continue;
            if (_wcsicmp(fd.cFileName, L"$RECYCLE.BIN") == 0) continue;
            if (_wcsicmp(fd.cFileName, L"System Volume Information") == 0) continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            stack.push_back({ dir + L"\\" + fd.cFileName, depth + 1 });
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    // Union with remembered installs — a game outside the scanned tree that
    // we installed earlier must still show up in the list.
    for (const std::wstring& d : LoadInstalledList()) {
        if (DirExists(d)) TryAdd(d);
    }
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

// ---------------------------------------------------------------------------
// Install / uninstall
// ---------------------------------------------------------------------------
static bool InstallInto(const Target& t) {
    std::wstring srcDll = g_srcDir + L"\\" + kDll;
    std::wstring srcIni = g_srcDir + L"\\" + kIni;
    if (!LooksLikeProxyDll(srcDll)) {
        LogLine(L"  [!] ① source folder is not the proxy package. / ①不是代理发布包目录。");
        return false;
    }
    std::wstring tgtDll  = t.dir + L"\\" + kDll;
    std::wstring tgtReal = t.dir + L"\\" + kReal;
    bool hasDll  = FileExists(tgtDll);
    bool hasReal = FileExists(tgtReal);
    if (hasDll && LooksLikeRealDll(tgtDll)) {
        if (hasReal) {
            LogLine(L"  [!] Stock and _real both exist — resolve manually. / 原版与 real 并存，请手动处理。");
            return false;
        }
        if (!MoveFileExW(tgtDll.c_str(), tgtReal.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            LogLine(L"  [!] Rename failed: " + WinErr(GetLastError()));
            return false;
        }
        LogLine(L"  nvngx_dlssnr.dll → nvngx_dlssnr_real.dll ✔");
    } else if (!hasReal) {
        LogLine(L"  [!] No stock NVIDIA nvngx_dlssnr.dll — skipped. / 缺少原版 DLL，已跳过。");
        return false;
    }
    if (!CopyFileW(srcDll.c_str(), tgtDll.c_str(), FALSE)) {
        LogLine(L"  [!] Copy failed: " + WinErr(GetLastError()) + L" (close the game; system dirs need admin)");
        return false;
    }
    if (FileExists(srcIni))
        CopyFileW(srcIni.c_str(), (t.dir + L"\\" + kIni).c_str(), FALSE);
    LogLine(L"  Proxy DLL + INI copied ✔");
    return true;
}
static bool UninstallFrom(const Target& t) {
    std::wstring tgtDll  = t.dir + L"\\" + kDll;
    std::wstring tgtReal = t.dir + L"\\" + kReal;
    bool hasDll  = FileExists(tgtDll);
    bool hasReal = FileExists(tgtReal);
    if (!hasDll && !hasReal) { LogLine(L"  -"); return true; }
    if (!hasReal && LooksLikeRealDll(tgtDll)) { LogLine(L"  -"); return true; }
    bool ok = true;
    if (hasDll) {
        if (!DeleteFileW(tgtDll.c_str())) {
            LogLine(L"  [!] Delete failed: " + WinErr(GetLastError()));
            ok = false;
        } else {
            LogLine(L"  Proxy DLL removed ✔");
        }
    }
    if (ok && hasReal) {
        if (!MoveFileExW(tgtReal.c_str(), tgtDll.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            LogLine(L"  [!] Restore failed: " + WinErr(GetLastError()));
            ok = false;
        } else {
            LogLine(L"  Stock DLL restored ✔");
        }
    }
    if (ok) LogLine(L"  (nvngx_dlssnr.ini kept)");
    return ok;
}
static void RunBatch(bool install) {
    if (g_srcDir.empty() || !LooksLikeProxyDll(g_srcDir + L"\\" + kDll)) {
        LogLine(L"[!] Proxy package not found — run this manager from the release folder (①). / 未找到代理文件包，请在①选择发布包目录。");
        return;
    }
    int done = 0, failed = 0;
    for (const Target& t : g_targets) {
        if (!t.check) continue;
        LogLine((install ? L"[Install] " : L"[Uninstall] ") + t.dir);
        bool ok = install ? InstallInto(t) : UninstallFrom(t);
        if (ok) {
            ++done;
            // remember it (only fully successful operations are recorded)
            if (install) {
                MarkInstalled(t.dir);
                AppendHistory(L"INSTALL", t.dir);
            } else {
                MarkUninstalled(t.dir);
                AppendHistory(L"UNINSTALL", t.dir);
            }
        } else {
            ++failed;
        }
    }
    if (!done && !failed)
        LogLine(kM[g_lang].noSel);
    else {
        static const wchar_t* fin[LANG_COUNT] = {
            L"[完成] 成功 %d 个，失败 %d 个。",
            L"[Done] %d succeeded, %d failed.",
            L"[Готово] Успешно: %d, ошибок: %d.",
            L"[완료] 성공 %d, 실패 %d.",
        };
        wchar_t buf[96];
        swprintf_s(buf, fin[g_lang], done, failed);
        LogLine(buf);
    }
    for (Target& t : g_targets) {
        bool keep = t.check;
        ClassifyTarget(t);
        t.check = keep;
    }
}

// ---------------------------------------------------------------------------
// Pop-out log window
// ---------------------------------------------------------------------------
static LRESULT CALLBACK PopWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_popEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                    ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                    0, 0, 620, 340, h, nullptr, nullptr, nullptr);
        SendMessageW(g_popEdit, WM_SETFONT, (WPARAM)g_font, TRUE);
        return 0;
    case WM_SIZE:
        if (g_popEdit) MoveWindow(g_popEdit, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    case WM_DESTROY:
        g_pop = nullptr;
        g_popEdit = nullptr;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
static void TogglePopLog(HWND h) {
    if (!g_pop) {
        g_pop = CreateWindowExW(0, L"DLSSNR_Manager_PopLog", L"DLSSNR log · 日志",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 380,
                                h, nullptr, (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE), nullptr);
        // replay current lines
        std::wstring all;
        for (size_t i = 0; i < g_lines.size(); ++i) {
            if (i) all += L"\r\n";
            all += g_lines[i];
        }
        SetWindowTextW(g_popEdit, all.c_str());
        ShowWindow(g_pop, SW_SHOW);
    } else if (IsWindowVisible(g_pop)) {
        ShowWindow(g_pop, SW_HIDE);
    } else {
        ShowWindow(g_pop, SW_SHOW);
        SetForegroundWindow(g_pop);
    }
}

// ---------------------------------------------------------------------------
// Log buffer
// ---------------------------------------------------------------------------
static void LogLine(const std::wstring& s) {
    g_lines.push_back(s);
    if (g_lines.size() > kMaxLogLines) g_lines.erase(g_lines.begin());
    g_logStick = true;
    ClampScrolls();
    if (g_popEdit) {
        // cheap rebuild — log is capped so this stays small
        std::wstring all;
        for (size_t i = 0; i < g_lines.size(); ++i) {
            if (i) all += L"\r\n";
            all += g_lines[i];
        }
        SetWindowTextW(g_popEdit, all.c_str());
        SendMessageW(g_popEdit, EM_SETSEL, (WPARAM)GetWindowTextLengthW(g_popEdit), (LPARAM)-1);
        SendMessageW(g_popEdit, EM_SCROLLCARET, 0, 0);
    }
    InvalidateRect(g_hwnd, nullptr, FALSE);
}
static void ClearLog() {
    g_lines.clear();
    g_logScroll = 0;
    if (g_popEdit) SetWindowTextW(g_popEdit, L"");
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
struct Btn { int id; RECT rc; };
struct Layout {
    RECT card[4];        // page cards (meaning depends on the active page)
    RECT srcEdit, rootEdit;
    RECT srcWell, rootWell;
    RECT badge[3];
    RECT listArea;       // rows viewport (quick-install list)
    RECT instArea, instSb;   // installed-management page
    RECT histArea, histSb;   // history card on the log page
    RECT dbgArea;            // embedded debug-panel host area (page 4)
    RECT dbgInfo;            // BlueMiku info card, left column (page 4)
    RECT logArea;        // lines viewport
    RECT sbList, sbLog;  // scrollbar tracks
    RECT countText;
    RECT rail;           // sidebar navigation rail
    RECT railBtn[4];     // page icon buttons
    std::vector<Btn> btns;
};
static Layout g_L;

static void ComputeLayout(int W, int H) {
    const int m = 16, gap = 12;
    g_L.btns.clear();

    // ---- horizontal nav bar (tabs) ----
    g_L.rail = { m, 10, m + 198, 58 };                    // 10 + 4*40 + 3*8 + 10 pad
    for (int i = 0; i < 4; ++i) {
        int x0 = g_L.rail.left + 10 + i * 48;
        g_L.railBtn[i] = { x0, 16, x0 + 40, 52 };
    }
    const int cx = m;        // content spans from the left margin now
    const int topY = 92;     // content starts below bar + subtitle

    // ---- page 0: quick install ----
    if (g_page == 0) {
        int actH = 42;
        int actY = H - 16 - actH;
        int c3h  = actY - gap - 270;
        if (c3h < 160) c3h = 160;

        g_L.card[0] = { cx, topY,      W - m, topY + 78 };    // 92..170
        g_L.card[1] = { cx, topY + 88, W - m, topY + 166 };   // 180..258
        g_L.card[2] = { cx, topY + 176, W - m, actY - gap };

        // wells / edits (row 2 of each card); right side reserves room for
        // the widest buttons ("Сканировать" needs ~110px at the base font)
        int rowY1 = topY + 36, rowY2 = topY + 124;
        g_L.srcWell  = { cx + 16, rowY1, W - m - 118, rowY1 + 26 };
        g_L.srcEdit  = { cx + 22, rowY1 + 3, W - m - 124, rowY1 + 23 };   // inset: r10 corners show
        g_L.rootWell = { cx + 16, rowY2, W - m - 240, rowY2 + 26 };
        g_L.rootEdit = { cx + 22, rowY2 + 3, W - m - 246, rowY2 + 23 };

        // Buttons share the wells' vertical centre (well h=26 → button h=30, y-2)
        g_L.btns.push_back({ A_SRC_BROWSE,  { W - m - 106, rowY1 - 2, W - m - 16, rowY1 + 28 } });
        g_L.btns.push_back({ A_ROOT_BROWSE, { W - m - 232, rowY2 - 2, W - m - 140, rowY2 + 28 } });
        g_L.btns.push_back({ A_SCAN,        { W - m - 128, rowY2 - 2, W - m - 16,  rowY2 + 28 } });

        int card3Top = topY + 176;
        int listTop = card3Top + 40;
        int listBot = card3Top + c3h - 50;
        g_L.listArea = { cx + 8, listTop, W - m - 8, listBot };
        g_L.sbList   = { W - m - 22, listTop + 4, W - m - 12, listBot - 4 };

        int footY = card3Top + c3h - 44;
        const int bw = 124;   // wide enough for RU "Выбрать все"
        g_L.btns.push_back({ A_ALL,     { cx + 16, footY, cx + 16 + bw, footY + 30 } });
        g_L.btns.push_back({ A_SELNONE, { cx + 16 + bw + 8, footY, cx + 16 + 2 * bw + 8, footY + 30 } });
        g_L.countText = { cx + 16 + 2 * bw + 16, footY + 3, W - m - 16, footY + 27 };

        g_L.btns.push_back({ A_INSTALL,   { cx, actY, cx + 226, actY + actH } });
        g_L.btns.push_back({ A_UNINSTALL, { cx + 234, actY, cx + 460, actY + actH } });

        const int cardY[3] = { topY, topY + 88, topY + 176 };
        for (int i = 0; i < 3; ++i)
            g_L.badge[i] = { cx + 14, cardY[i] + 12, cx + 40, cardY[i] + 38 };
    }
    // ---- page 1: installed management ----
    else if (g_page == 1) {
        g_L.card[0] = { cx, topY, W - m, H - 16 };
        int listTop = topY + 40;
        int footY   = H - 16 - 44;
        int listBot = footY - 8;
        g_L.instArea = { cx + 8, listTop, W - m - 8, listBot };
        g_L.instSb   = { W - m - 22, listTop + 4, W - m - 12, listBot - 4 };
        g_L.btns.push_back({ A_ALL,       { cx + 16, footY, cx + 140, footY + 30 } });
        g_L.btns.push_back({ A_SELNONE,   { cx + 148, footY, cx + 272, footY + 30 } });
        g_L.btns.push_back({ A_REFRESH,   { cx + 280, footY, cx + 396, footY + 30 } });
        g_L.btns.push_back({ A_UNINSTALL, { W - m - 226, footY, W - m, footY + 30 } });
        g_L.countText = { cx + 404, footY + 3, W - m - 236, footY + 27 };
    }
    // ---- page 2: history + session log ----
    else if (g_page == 2) {
        int histH = (H - 16 - topY - gap) * 45 / 100;
        if (histH < 120) histH = 120;
        g_L.card[0] = { cx, topY, W - m, topY + histH };
        g_L.card[1] = { cx, topY + histH + gap, W - m, H - 16 };
        g_L.histArea = { cx + 14, topY + 36, W - m - 26, topY + histH - 10 };
        g_L.histSb   = { W - m - 24, topY + 40, W - m - 14, topY + histH - 14 };
        int logY = topY + histH + gap;
        g_L.logArea = { cx + 14, logY + 36, W - m - 26, H - 16 - 10 };
        g_L.sbLog   = { W - m - 24, logY + 40, W - m - 14, H - 16 - 14 };
        g_L.btns.push_back({ A_POPOUT, { W - m - 156, logY + 7, W - m - 16, logY + 31 } });
        g_L.btns.push_back({ A_CLEAR,  { W - m - 264, logY + 7, W - m - 172, logY + 31 } });
    }
    // ---- page 3: native BlueMiku debug page (full-width control list) ----
    else {
        g_L.card[0] = { cx, topY, W - m, H - 16 };
        g_L.dbgArea = { cx + 20, topY + 56, W - m - 20, H - 16 - 20 };
        // action buttons live in the caption row, right-aligned
        g_L.btns.push_back({ A_DBG_RELOAD, { W - m - 20 - 124 - 8 - 124, topY + 12, W - m - 20 - 124 - 8, topY + 44 } });
        g_L.btns.push_back({ A_DBG_FOLDER, { W - m - 20 - 124, topY + 12, W - m - 20,      topY + 44 } });
    }

    // header icon buttons (wide enough for "☀ Light mode" / RU strings)
    g_L.btns.push_back({ A_LANG,  { W - m - 150 - 8 - 56, 14, W - m - 150 - 8, 54 } });
    g_L.btns.push_back({ A_THEME, { W - m - 150, 14, W - m, 54 } });
}

// ---------------------------------------------------------------------------
// Scroll helpers
// ---------------------------------------------------------------------------
static const int kListItemH = 46;
static const int kLogLineH = 20;

static void PlaceEdits(HWND h) {
    MoveWindow(g_editSrc, g_L.srcEdit.left, g_L.srcEdit.top,
               g_L.srcEdit.right - g_L.srcEdit.left, g_L.srcEdit.bottom - g_L.srcEdit.top, TRUE);
    MoveWindow(g_editRoot, g_L.rootEdit.left, g_L.rootEdit.top,
               g_L.rootEdit.right - g_L.rootEdit.left, g_L.rootEdit.bottom - g_L.rootEdit.top, TRUE);
    ShowWindow(g_editSrc,  g_page == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_editRoot, g_page == 0 ? SW_SHOW : SW_HIDE);
}

static int ListMaxScroll() {
    int content = (int)g_targets.size() * kListItemH;
    int vh = g_L.listArea.bottom - g_L.listArea.top;
    return content > vh ? content - vh : 0;
}
static int LogMaxScroll() {
    int content = (int)g_lines.size() * kLogLineH;
    int vh = g_L.logArea.bottom - g_L.logArea.top;
    return content > vh ? content - vh : 0;
}
static int InstMaxScroll() {
    int content = (int)g_installed.size() * kListItemH;
    int vis = g_L.instArea.bottom - g_L.instArea.top;
    if (vis < 1) vis = 1;
    int m = content - vis;
    return m > 0 ? m : 0;
}
static int HistMaxScroll() {
    int content = (int)g_histLines.size() * kLogLineH;
    int vis = g_L.histArea.bottom - g_L.histArea.top;
    if (vis < 1) vis = 1;
    int m = content - vis;
    return m > 0 ? m : 0;
}
static int DbgMaxScroll();   // defined in the debug-page module below
static int g_dbgScroll = 0;  // defined in the debug-page module below
static void ClampScrolls() {
    int lm = ListMaxScroll();
    if (g_listScroll > lm) g_listScroll = lm;
    if (g_listScroll < 0) g_listScroll = 0;
    int lg = LogMaxScroll();
    if (g_logStick) g_logScroll = lg;
    if (g_logScroll > lg) g_logScroll = lg;
    if (g_logScroll < 0) g_logScroll = 0;
    int im = InstMaxScroll();
    if (g_instScroll > im) g_instScroll = im;
    if (g_instScroll < 0) g_instScroll = 0;
    int hm = HistMaxScroll();
    if (g_histScroll > hm) g_histScroll = hm;
    if (g_histScroll < 0) g_histScroll = 0;
    int dm = DbgMaxScroll();
    if (g_dbgScroll > dm) g_dbgScroll = dm;
    if (g_dbgScroll < 0) g_dbgScroll = 0;
}

// ---------------------------------------------------------------------------
// Debug page (page 3) — native BlueMiku (creeper-qt) control set that edits
// the nvngx_dlssnr.ini inside the ① proxy package folder. Fully painted with
// the manager's own GDI+ widgets: no game-panel UI embedded.
// ---------------------------------------------------------------------------
struct DebugCfg {
    bool enableProxy = true, enableHotkeys = true, enableUi = true, requireCtrlAlt = true;
    int  mode = 1;
    float scale = 0.75f, transfer = 1.0f, color = 1.0f, sharpness = 0.20f;
    int  keyToggleProxy = VK_SPACE, keyToggleMode = VK_END,
         keyScaleUp = VK_PRIOR, keyScaleDown = VK_NEXT, keyToggleUi = VK_F11;
    // v1.0.5 upstream additions
    bool enableAnamorphic = false;
    float scaleX = 0.65f, scaleY = 0.85f;
    bool enableDepthAware = true;
    bool enableVrnr = false;
    bool useCustomNR = false;
    unsigned nrStyle = 0;   // 0 Balanced / 1 Sharp / 2 Cinematic
    float nrIntensity = 1.0f, nrLocalStructure = 1.0f, nrLocalTone = 1.0f, nrSkin = -1.0f;
    bool nrAutoMask = false;
};
static DebugCfg s_pcfg;
static wchar_t  s_pini[MAX_PATH] = { 0 };
static int      g_dbgHot = -1, g_dbgDrag = -1, s_capRow = -1;
static ULONGLONG s_pdirty = 0, s_savedAt = 0;
static const ULONGLONG kDbgTimerId = 7;      // commit-debounce timer

static void ClampPanel() {
    if (s_pcfg.scale < 0.25f) s_pcfg.scale = 0.25f;
    if (s_pcfg.scale > 2.00f) s_pcfg.scale = 2.00f;   // v1.0.5: super-sampling
    s_pcfg.scale = (float)((int)(s_pcfg.scale * 100.0f + 0.5f)) / 100.0f;
    if (s_pcfg.mode != 0 && s_pcfg.mode != 1) s_pcfg.mode = 1;
    if (s_pcfg.scaleX < 0.25f) s_pcfg.scaleX = 0.25f;
    if (s_pcfg.scaleX > 2.00f) s_pcfg.scaleX = 2.00f;
    if (s_pcfg.scaleY < 0.25f) s_pcfg.scaleY = 0.25f;
    if (s_pcfg.scaleY > 2.00f) s_pcfg.scaleY = 2.00f;
    if (s_pcfg.nrIntensity < 0.0f) s_pcfg.nrIntensity = 0.0f;
    if (s_pcfg.nrIntensity > 2.0f) s_pcfg.nrIntensity = 2.0f;
    if (s_pcfg.nrLocalStructure < 0.0f) s_pcfg.nrLocalStructure = 0.0f;
    if (s_pcfg.nrLocalStructure > 2.0f) s_pcfg.nrLocalStructure = 2.0f;
    if (s_pcfg.nrLocalTone < 0.0f) s_pcfg.nrLocalTone = 0.0f;
    if (s_pcfg.nrLocalTone > 2.0f) s_pcfg.nrLocalTone = 2.0f;
    if (s_pcfg.nrSkin < -1.0f) s_pcfg.nrSkin = -1.0f;
    if (s_pcfg.nrSkin > 2.0f) s_pcfg.nrSkin = 2.0f;
    if (s_pcfg.nrStyle > 2) s_pcfg.nrStyle = 0;
    if (s_pcfg.transfer < 0.0f) s_pcfg.transfer = 0.0f;
    if (s_pcfg.transfer > 2.0f) s_pcfg.transfer = 2.0f;
    if (s_pcfg.color < 0.0f) s_pcfg.color = 0.0f;
    if (s_pcfg.color > 1.0f) s_pcfg.color = 1.0f;
    if (s_pcfg.sharpness < 0.0f) s_pcfg.sharpness = 0.0f;
    if (s_pcfg.sharpness > 1.0f) s_pcfg.sharpness = 1.0f;
}
static void LoadPanelIni() {
    wchar_t buf[64];
    s_pcfg.enableProxy   = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableProxy", 1, s_pini) != 0;
    s_pcfg.enableHotkeys = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableHotkeys", 1, s_pini) != 0;
    s_pcfg.enableUi      = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableUi", 1, s_pini) != 0;
    s_pcfg.mode          = (int)GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnlargementMode", 1, s_pini);
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"ResolutionScale", L"0.75", buf, 64, s_pini);
    s_pcfg.scale = (float)_wtof(buf);
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"TransferStrength", L"1.00", buf, 64, s_pini);
    s_pcfg.transfer = (float)_wtof(buf);
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"Sharpness", L"0.20", buf, 64, s_pini);
    s_pcfg.sharpness = (float)_wtof(buf);
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"ColorStrength", L"1.00", buf, 64, s_pini);
    s_pcfg.color = (float)_wtof(buf);
    s_pcfg.requireCtrlAlt = GetPrivateProfileIntW(L"Hotkeys", L"RequireCtrlAlt", 1, s_pini) != 0;
    s_pcfg.keyToggleProxy = (int)GetPrivateProfileIntW(L"Hotkeys", L"KeyToggleProxy", VK_SPACE, s_pini);
    s_pcfg.keyToggleMode  = (int)GetPrivateProfileIntW(L"Hotkeys", L"KeyToggleMode",  VK_END,   s_pini);
    s_pcfg.keyScaleUp     = (int)GetPrivateProfileIntW(L"Hotkeys", L"KeyScaleUp",     VK_PRIOR, s_pini);
    s_pcfg.keyScaleDown   = (int)GetPrivateProfileIntW(L"Hotkeys", L"KeyScaleDown",   VK_NEXT,  s_pini);
    s_pcfg.keyToggleUi    = (int)GetPrivateProfileIntW(L"Hotkeys", L"KeyToggleUI", VK_F11, s_pini);
    // v1.0.5 upstream additions
    s_pcfg.enableAnamorphic  = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableAnamorphic", 0, s_pini) != 0;
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"ResolutionScaleX", L"0.65", buf, 64, s_pini);
    s_pcfg.scaleX = (float)_wtof(buf);
    GetPrivateProfileStringW(L"DLSSNR_Proxy", L"ResolutionScaleY", L"0.85", buf, 64, s_pini);
    s_pcfg.scaleY = (float)_wtof(buf);
    s_pcfg.enableDepthAware = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableDepthAwareResolve", 1, s_pini) != 0;
    s_pcfg.enableVrnr       = GetPrivateProfileIntW(L"DLSSNR_Proxy", L"EnableAlternatingFrames", 0, s_pini) != 0;
    s_pcfg.useCustomNR      = GetPrivateProfileIntW(L"DLSSNR_Settings", L"UseCustomSettings", 0, s_pini) != 0;
    s_pcfg.nrStyle          = (unsigned)GetPrivateProfileIntW(L"DLSSNR_Settings", L"Style", 0, s_pini);
    GetPrivateProfileStringW(L"DLSSNR_Settings", L"Intensity", L"1.00", buf, 64, s_pini);
    s_pcfg.nrIntensity = (float)_wtof(buf);
    GetPrivateProfileStringW(L"DLSSNR_Settings", L"LocalStructureStrength", L"1.00", buf, 64, s_pini);
    s_pcfg.nrLocalStructure = (float)_wtof(buf);
    GetPrivateProfileStringW(L"DLSSNR_Settings", L"LocalToneStrength", L"1.00", buf, 64, s_pini);
    s_pcfg.nrLocalTone = (float)_wtof(buf);
    GetPrivateProfileStringW(L"DLSSNR_Settings", L"SkinStructureStrength", L"-1.00", buf, 64, s_pini);
    s_pcfg.nrSkin = (float)_wtof(buf);
    s_pcfg.nrAutoMask = GetPrivateProfileIntW(L"DLSSNR_Settings", L"UseAutoMask", 0, s_pini) != 0;
    ClampPanel();
}
static void SavePanelIni() {
    wchar_t buf[32];
    WritePrivateProfileStringW(L"DLSSNR_Proxy", L"EnableProxy", s_pcfg.enableProxy ? L"1" : L"0", s_pini);
    swprintf_s(buf, L"%.2f", s_pcfg.scale);        WritePrivateProfileStringW(L"DLSSNR_Proxy", L"ResolutionScale", buf, s_pini);
    swprintf_s(buf, L"%u", (unsigned)s_pcfg.mode); WritePrivateProfileStringW(L"DLSSNR_Proxy", L"EnlargementMode", buf, s_pini);
    swprintf_s(buf, L"%.2f", s_pcfg.transfer);     WritePrivateProfileStringW(L"DLSSNR_Proxy", L"TransferStrength", buf, s_pini);
    swprintf_s(buf, L"%.2f", s_pcfg.color);        WritePrivateProfileStringW(L"DLSSNR_Proxy", L"ColorStrength", buf, s_pini);
    swprintf_s(buf, L"%.2f", s_pcfg.sharpness);    WritePrivateProfileStringW(L"DLSSNR_Proxy", L"Sharpness", buf, s_pini);
    WritePrivateProfileStringW(L"DLSSNR_Proxy", L"EnableHotkeys", s_pcfg.enableHotkeys ? L"1" : L"0", s_pini);
    WritePrivateProfileStringW(L"DLSSNR_Proxy", L"EnableUi", s_pcfg.enableUi ? L"1" : L"0", s_pini);
    WritePrivateProfileStringW(L"Hotkeys", L"RequireCtrlAlt", s_pcfg.requireCtrlAlt ? L"1" : L"0", s_pini);
    swprintf_s(buf, L"%d", s_pcfg.keyToggleProxy); WritePrivateProfileStringW(L"Hotkeys", L"KeyToggleProxy", buf, s_pini);
    swprintf_s(buf, L"%d", s_pcfg.keyToggleMode);  WritePrivateProfileStringW(L"Hotkeys", L"KeyToggleMode", buf, s_pini);
    swprintf_s(buf, L"%d", s_pcfg.keyScaleUp);     WritePrivateProfileStringW(L"Hotkeys", L"KeyScaleUp", buf, s_pini);
    swprintf_s(buf, L"%d", s_pcfg.keyScaleDown);   WritePrivateProfileStringW(L"Hotkeys", L"KeyScaleDown", buf, s_pini);
    swprintf_s(buf, L"%d", s_pcfg.keyToggleUi);    WritePrivateProfileStringW(L"Hotkeys", L"KeyToggleUI", buf, s_pini);
    // v1.0.5 upstream additions
    WritePrivateProfileStringW(L"DLSSNR_Proxy", L"EnableAnamorphic", s_pcfg.enableAnamorphic ? L"1" : L"0", s_pini);
    swprintf_s(buf, L"%.2f", s_pcfg.scaleX); WritePrivateProfileStringW(L"DLSSNR_Proxy", L"ResolutionScaleX", buf, s_pini);
    swprintf_s(buf, L"%.2f", s_pcfg.scaleY); WritePrivateProfileStringW(L"DLSSNR_Proxy", L"ResolutionScaleY", buf, s_pini);
    WritePrivateProfileStringW(L"DLSSNR_Proxy", L"EnableDepthAwareResolve", s_pcfg.enableDepthAware ? L"1" : L"0", s_pini);
    WritePrivateProfileStringW(L"DLSSNR_Proxy", L"EnableAlternatingFrames", s_pcfg.enableVrnr ? L"1" : L"0", s_pini);
    WritePrivateProfileStringW(L"DLSSNR_Settings", L"UseCustomSettings", s_pcfg.useCustomNR ? L"1" : L"0", s_pini);
    swprintf_s(buf, L"%u", s_pcfg.nrStyle); WritePrivateProfileStringW(L"DLSSNR_Settings", L"Style", buf, s_pini);
    swprintf_s(buf, L"%.2f", s_pcfg.nrIntensity); WritePrivateProfileStringW(L"DLSSNR_Settings", L"Intensity", buf, s_pini);
    swprintf_s(buf, L"%.2f", s_pcfg.nrLocalStructure); WritePrivateProfileStringW(L"DLSSNR_Settings", L"LocalStructureStrength", buf, s_pini);
    swprintf_s(buf, L"%.2f", s_pcfg.nrLocalTone); WritePrivateProfileStringW(L"DLSSNR_Settings", L"LocalToneStrength", buf, s_pini);
    swprintf_s(buf, L"%.2f", s_pcfg.nrSkin); WritePrivateProfileStringW(L"DLSSNR_Settings", L"SkinStructureStrength", buf, s_pini);
    WritePrivateProfileStringW(L"DLSSNR_Settings", L"UseAutoMask", s_pcfg.nrAutoMask ? L"1" : L"0", s_pini);
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, s_pini);   // flush
}
static void MarkPdirty() {
    s_pdirty = GetTickCount64();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}
static bool DebugPanelReady() {
    return !g_srcDir.empty() && LooksLikeProxyDll(g_srcDir + L"\\" + kDll);
}
static void ReopenDebugIni(HWND h) {
    if (!DebugPanelReady()) return;
    swprintf_s(s_pini, L"%s\\nvngx_dlssnr.ini", g_srcDir.c_str());
    LoadPanelIni();
    g_dbgScroll = 0;
    s_capRow = -1;
    InvalidateRect(h, nullptr, TRUE);
}

// --- row model -------------------------------------------------------------
enum DbgKind { DBG_TOGGLE, DBG_SEG, DBG_SEG3, DBG_SLIDER, DBG_SECTION, DBG_KEY };
enum DbgField {
    D_PROXY = 0, D_HOTKEYS, D_UI, D_CTRLALT, D_MODE,
    D_SCALE, D_TRANSFER, D_COLOR, D_SHARP,
    D_ANAM, D_SX, D_SY, D_DEPTH, D_VRNR,
    D_CUSTOMNR, D_STYLE, D_INTENSITY, D_LSTRUCT, D_LTONE, D_SKIN, D_AUTOMASK,
    K_PROXY, K_MODE, K_UP, K_DOWN, K_UI,
};
struct DbgRow { DbgKind kind; int f; };
static const DbgRow kDbgRows[] = {
    { DBG_TOGGLE, D_PROXY }, { DBG_TOGGLE, D_HOTKEYS }, { DBG_TOGGLE, D_UI },
    { DBG_SEG,     D_MODE },
    { DBG_SLIDER,  D_SCALE }, { DBG_SLIDER, D_TRANSFER },
    { DBG_SLIDER,  D_COLOR }, { DBG_SLIDER, D_SHARP },
    { DBG_TOGGLE,  D_ANAM },
    { DBG_SLIDER,  D_SX }, { DBG_SLIDER, D_SY },
    { DBG_TOGGLE,  D_DEPTH }, { DBG_TOGGLE, D_VRNR },
    { DBG_SECTION, 1 },
    { DBG_TOGGLE,  D_CUSTOMNR },
    { DBG_SEG3,    D_STYLE },
    { DBG_SLIDER,  D_INTENSITY }, { DBG_SLIDER, D_LSTRUCT },
    { DBG_SLIDER,  D_LTONE }, { DBG_SLIDER, D_SKIN },
    { DBG_TOGGLE,  D_AUTOMASK },
    { DBG_SECTION, 0 },
    { DBG_TOGGLE, D_CTRLALT },
    { DBG_KEY, K_PROXY }, { DBG_KEY, K_MODE }, { DBG_KEY, K_UP },
    { DBG_KEY, K_DOWN },  { DBG_KEY, K_UI },
};
static const int kDbgRowCount = (int)(sizeof(kDbgRows) / sizeof(kDbgRows[0]));

static int DbgRowH(int i) {
    switch (kDbgRows[i].kind) {
    case DBG_TOGGLE:  return 44;
    case DBG_SEG:     return 52;
    case DBG_SEG3:    return 52;
    case DBG_SLIDER:  return 56;
    case DBG_SECTION: return 34;
    case DBG_KEY:     return 40;
    }
    return 40;
}
static int DbgContentH() {
    int h = 24;
    for (int i = 0; i < kDbgRowCount; ++i) h += DbgRowH(i);
    return h;
}
static int DbgMaxScroll() {
    int vis = g_L.dbgArea.bottom - g_L.dbgArea.top;
    int m = DbgContentH() - vis;
    return m > 0 ? m : 0;
}
static int DbgRowY(int idx) {
    int y = g_L.dbgArea.top + 12 - g_dbgScroll;
    for (int i = 0; i < idx; ++i) y += DbgRowH(i);
    return y;
}
static int DbgHitRow(int x, int y) {
    POINT p{ x, y };
    if (!PtInRect(&g_L.dbgArea, p)) return -1;
    int yy = y - (g_L.dbgArea.top + 12) + g_dbgScroll;
    for (int i = 0; i < kDbgRowCount; ++i) {
        int h = DbgRowH(i);
        if (yy < h) return i;
        yy -= h;
    }
    return -1;
}

// --- value accessors --------------------------------------------------------
static bool DbgGetB(int f) {
    switch (f) {
    case D_PROXY:   return s_pcfg.enableProxy;
    case D_HOTKEYS: return s_pcfg.enableHotkeys;
    case D_UI:      return s_pcfg.enableUi;
    case D_CTRLALT: return s_pcfg.requireCtrlAlt;
    case D_ANAM:    return s_pcfg.enableAnamorphic;
    case D_DEPTH:   return s_pcfg.enableDepthAware;
    case D_VRNR:    return s_pcfg.enableVrnr;
    case D_CUSTOMNR:return s_pcfg.useCustomNR;
    case D_AUTOMASK:return s_pcfg.nrAutoMask;
    }
    return false;
}
static void DbgSetB(int f, bool v) {
    switch (f) {
    case D_PROXY:   s_pcfg.enableProxy = v; break;
    case D_HOTKEYS: s_pcfg.enableHotkeys = v; break;
    case D_UI:      s_pcfg.enableUi = v; break;
    case D_CTRLALT: s_pcfg.requireCtrlAlt = v; break;
    case D_ANAM:    s_pcfg.enableAnamorphic = v; break;
    case D_DEPTH:   s_pcfg.enableDepthAware = v; break;
    case D_VRNR:    s_pcfg.enableVrnr = v; break;
    case D_CUSTOMNR:s_pcfg.useCustomNR = v; break;
    case D_AUTOMASK:s_pcfg.nrAutoMask = v; break;
    }
}
static float DbgGetF(int f) {
    switch (f) {
    case D_SCALE:    return s_pcfg.scale;
    case D_TRANSFER: return s_pcfg.transfer;
    case D_COLOR:    return s_pcfg.color;
    case D_SHARP:    return s_pcfg.sharpness;
    case D_SX:       return s_pcfg.scaleX;
    case D_SY:       return s_pcfg.scaleY;
    case D_INTENSITY:return s_pcfg.nrIntensity;
    case D_LSTRUCT:  return s_pcfg.nrLocalStructure;
    case D_LTONE:    return s_pcfg.nrLocalTone;
    case D_SKIN:     return s_pcfg.nrSkin;
    }
    return 0.0f;
}
static void DbgSetF(int f, float v) {
    switch (f) {
    case D_SCALE:    s_pcfg.scale = v; break;
    case D_TRANSFER: s_pcfg.transfer = v; break;
    case D_COLOR:    s_pcfg.color = v; break;
    case D_SHARP:    s_pcfg.sharpness = v; break;
    case D_SX:       s_pcfg.scaleX = v; break;
    case D_SY:       s_pcfg.scaleY = v; break;
    case D_INTENSITY:s_pcfg.nrIntensity = v; break;
    case D_LSTRUCT:  s_pcfg.nrLocalStructure = v; break;
    case D_LTONE:    s_pcfg.nrLocalTone = v; break;
    case D_SKIN:     s_pcfg.nrSkin = v; break;
    }
    ClampPanel();
}
static float DbgFMin(int f) {
    switch (f) {
    case D_SCALE: case D_SX: case D_SY: return 0.25f;
    case D_SKIN: return -1.0f;
    }
    return 0.0f;
}
static float DbgFMax(int f) {
    switch (f) {
    case D_SCALE: case D_SX: case D_SY: return 2.0f;
    case D_TRANSFER: case D_INTENSITY: case D_LSTRUCT: case D_LTONE: return 2.0f;
    }
    return 1.0f;
}
static int* DbgKeyPtr(int f) {
    switch (f) {
    case K_PROXY: return &s_pcfg.keyToggleProxy;
    case K_MODE:  return &s_pcfg.keyToggleMode;
    case K_UP:    return &s_pcfg.keyScaleUp;
    case K_DOWN:  return &s_pcfg.keyScaleDown;
    case K_UI:    return &s_pcfg.keyToggleUi;
    }
    return nullptr;
}
static void DbgLabel(int f, wchar_t* out, size_t cch) {
    const MS& m = kM[g_lang];
    const wchar_t* t = L"";
    switch (f) {
    case D_PROXY:   t = m.tProxy;   break;
    case D_HOTKEYS: t = m.tHotkeys; break;
    case D_UI:      t = m.tUi;      break;
    case D_MODE:    t = m.lblMode;  break;
    case D_SCALE:   t = m.lblScale; break;
    case D_TRANSFER:t = m.tTransfer;break;
    case D_COLOR:   t = m.tColor;   break;
    case D_SHARP:   t = m.lblSharp; break;
    case K_PROXY:   t = m.kProxy;   break;
    case K_MODE:    t = m.kMode;    break;
    case K_UP:      t = m.kUp;      break;
    case K_DOWN:    t = m.kDown;    break;
    case K_UI:      t = m.kUi;      break;
    case D_CTRLALT: t = m.tCtrlAlt; break;
    case D_ANAM:    t = m.tAnamorphic; break;
    case D_SX:      t = m.tScaleX; break;
    case D_SY:      t = m.tScaleY; break;
    case D_DEPTH:   t = m.tDepthAware; break;
    case D_VRNR:    t = m.tVrnr; break;
    case D_CUSTOMNR:t = m.tCustomNR; break;
    case D_INTENSITY:t = m.lblIntensity; break;
    case D_LSTRUCT: t = m.lblLocalStruct; break;
    case D_LTONE:   t = m.lblLocalTone; break;
    case D_SKIN:    t = m.lblSkin; break;
    case D_AUTOMASK:t = m.tAutoMask; break;
    }
    swprintf_s(out, cch, L"%s", t);
}
static void VkName(int vk, wchar_t* out, size_t cch) {
    if (vk >= L'0' && vk <= L'Z') { swprintf_s(out, cch, L"%c", (wchar_t)vk); return; }
    switch (vk) {
    case VK_SPACE: wcscpy_s(out, cch, L"Space"); return;
    case VK_END:   wcscpy_s(out, cch, L"End");   return;
    case VK_PRIOR: wcscpy_s(out, cch, L"PgUp");  return;
    case VK_NEXT:  wcscpy_s(out, cch, L"PgDn");  return;
    default: break;
    }
    if (vk >= VK_F1 && vk <= VK_F24) { swprintf_s(out, cch, L"F%d", vk - VK_F1 + 1); return; }
    UINT sc = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC) << 16;
    if (!GetKeyNameTextW((LONG)sc, out, (int)cch)) swprintf_s(out, cch, L"0x%02X", vk);
}

// ---------------------------------------------------------------------------
// Painted buttons
// ---------------------------------------------------------------------------
static std::wstring BtnLabel(int id) {
    const MS& m = kM[g_lang];
    switch (id) {
    case A_SRC_BROWSE: case A_ROOT_BROWSE: return m.browse;
    case A_SCAN:       return m.scan;
    case A_ALL:        return m.all;
    case A_SELNONE:       return m.none;
    case A_INSTALL:    return m.install;
    case A_UNINSTALL:  return m.uninstall;
    case A_THEME:      return g_dark ? m.toLight : m.toDark;
    case A_LANG:       return kLangShort[g_lang];
    case A_POPOUT:     return m.popout;
    case A_CLEAR:      return m.clear;
    case A_REFRESH:    return m.refresh;
    case A_DBG_RELOAD: return m.dbgReload;
    case A_DBG_FOLDER: return m.dbgFolder;
    }
    return L"";
}
static bool BtnOnCard(int id) {
    return id == A_SRC_BROWSE || id == A_ROOT_BROWSE || id == A_SCAN ||
           id == A_ALL || id == A_SELNONE || id == A_REFRESH ||
           id == A_POPOUT || id == A_CLEAR;
}
static void PaintButton(HDC dc, const Btn& b) {
    bool hot = (g_hotAction == b.id);
    bool press = (g_pressAction == b.id && hot);
    bool filled = (b.id == A_INSTALL);
    bool onCard = BtnOnCard(b.id);

    RECT rc = b.rc;
    rc.left += 1; rc.top += 1; rc.right -= 1; rc.bottom -= 1;   // AA edge fits
    COLORREF outside = onCard ? g_skin.card : g_skin.bg;
    HBRUSH bgb = CreateSolidBrush(outside);
    FillRect(dc, &rc, bgb);
    DeleteObject(bgb);

    COLORREF fill = filled ? (press ? Darken(g_skin.primary, 20)
                                    : hot ? Lighten(g_skin.primary, 18) : g_skin.primary)
                           : (hot ? g_skin.cardHi : g_skin.card);
    COLORREF txtc = filled ? g_skin.onPrimary : (hot ? g_skin.accent : g_skin.text);
    const int rad = 10;
    GpRoundFill(dc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, rad, fill);
    if (filled)
        GpRoundFrame(dc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, rad,
                     hot ? Lighten(g_skin.primary, 40) : g_skin.primary);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, txtc);
    std::wstring t = BtnLabel(b.id);
    DrawTextW(dc, t.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
static void DrawCheck(HDC dc, int cx, int cy, bool checked, const Skin& s) {
    int d = 18;
    int x = cx - d / 2, y = cy - d / 2;
    if (checked) {
        GpRoundFill(dc, x, y, d, d, d / 2, s.primary);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, s.onPrimary);
        RECT r{ x, y, x + d, y + d };
        DrawTextW(dc, L"\u2713", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        GpRoundFill(dc, x, y, d, d, d / 2, s.card);
        GpRoundFrame(dc, x, y, d, d, d / 2, s.outline, 1.4f);
    }
}
static int HitBtn(int x, int y) {
    for (int i = 0; i < 4; ++i) {
        POINT p{ x, y };
        if (PtInRect(&g_L.railBtn[i], p)) return A_PG0 + i;
    }
    for (const Btn& b : g_L.btns) {
        POINT p{ x, y };
        if (PtInRect(&b.rc, p)) return b.id;
    }
    return A_NONE;
}
static int HitListRow(int x, int y) {
    POINT p{ x, y };
    if (!PtInRect(&g_L.listArea, p)) return -1;
    int idx = (y - g_L.listArea.top + g_listScroll) / kListItemH;
    if (idx < 0 || idx >= (int)g_targets.size()) return -1;
    return idx;
}
static int HitInstRow(int x, int y) {
    POINT p{ x, y };
    if (!PtInRect(&g_L.instArea, p)) return -1;
    int idx = (y - g_L.instArea.top + g_instScroll) / kListItemH;
    if (idx < 0 || idx >= (int)g_installed.size()) return -1;
    return idx;
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
static void HandleAction(int id) {
    switch (id) {
    case A_THEME:
        g_dark = !g_dark;
        g_skin = MakeSkin(g_dark);
        BuildSkinBrushes();
        SavePrefs();
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    case A_LANG:
        g_lang = (g_lang + 1) % LANG_COUNT;
        SavePrefs();
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    case A_SRC_BROWSE: {
        std::wstring d = PickFolder(g_hwnd);
        if (d.empty()) break;
        if (LooksLikeProxyDll(d + L"\\" + kDll)) {
            g_srcDir = d;
            SetWindowTextW(g_editSrc, d.c_str());
            LogLine(L"[Source] " + d);
        } else if (FileExists(d + L"\\" + kDll) || FileExists(d + L"\\" + kReal)) {
            g_rootDir = d;                       // mirror — scan falls back to this
            SetWindowTextW(g_editRoot, d.c_str());
            LogLine(L"[Hint] Game folder detected → moved to step ②. Press Scan. / 检测到游戏目录，已填入②，请点击扫描。");
        } else {
            LogLine(L"[!] No nvngx_dlssnr.dll in this folder — ① needs the proxy package; game folders go to ②.");
        }
        break;
    }
    case A_ROOT_BROWSE: {
        std::wstring d = PickFolder(g_hwnd);
        if (!d.empty()) {
            g_rootDir = d;
            SetWindowTextW(g_editRoot, d.c_str());
        }
        break;
    }
    case A_SCAN: {
        std::wstring root = GetEditText(g_editRoot);
        if (root.empty()) root = g_rootDir;      // fallback if the edit misbehaves
        // strip surrounding quotes (hand-pasted paths often carry them)
        if (root.size() >= 2 && root.front() == L'"' && root.back() == L'"')
            root = root.substr(1, root.size() - 2);
        if (root.empty() || !DirExists(root)) {
            LogLine(kM[g_lang].errRoot);
            break;
        }
        g_rootDir = root;
        ScanTargets(root);
        if (g_targets.empty()) {
            LogLine(kM[g_lang].scanNone);
        } else {
            wchar_t buf[80];
            swprintf_s(buf, L"[Scan] %d target(s) found. / 发现 %d 个目标。", (int)g_targets.size(), (int)g_targets.size());
            LogLine(buf);
        }
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    }
    case A_PG0:
    case A_PG1:
    case A_PG2:
    case A_PG3: {
        int p = id - A_PG0;
        if (g_page != p) {
            g_page = p;
            if (p == 1) ReloadInstalled();
            if (p == 2) ReloadHistory();
            ClampScrolls();
            RECT rc; GetClientRect(g_hwnd, &rc);
            ComputeLayout(rc.right - rc.left, rc.bottom - rc.top);
            PlaceEdits(g_hwnd);
            if (p == 3) ReopenDebugIni(g_hwnd);
            InvalidateRect(g_hwnd, nullptr, TRUE);
        }
        break;
    }
    case A_REFRESH: {
        ReloadInstalled();
        static const wchar_t* rmsg[LANG_COUNT] = {
            L"[刷新] 已重新检查安装记录。",
            L"[Refresh] Installed list re-checked from disk.",
            L"[Обновление] Список установок проверен заново.",
            L"[새로 고침] 설치 기록을 다시 확인했습니다.",
        };
        LogLine(rmsg[g_lang]);
        InvalidateRect(g_hwnd, nullptr, TRUE);
        break;
    }
    case A_ALL:
    case A_SELNONE: {
        std::vector<Target>& list = (g_page == 1) ? g_installed : g_targets;
        for (Target& t : list) t.check = (id == A_ALL);
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    }
    case A_INSTALL:   RunBatch(true);  InvalidateRect(g_hwnd, nullptr, FALSE); break;
    case A_UNINSTALL:
        if (g_page == 1) {
            int done = 0, failed = 0;
            for (const Target& t : g_installed) {
                if (!t.check) continue;
                LogLine(L"[Uninstall] " + t.dir);
                if (UninstallFrom(t)) {
                    ++done;
                    MarkUninstalled(t.dir);
                    AppendHistory(L"UNINSTALL", t.dir);
                } else {
                    ++failed;
                }
            }
            if (!done && !failed) LogLine(kM[g_lang].noSel);
            ReloadInstalled();
        } else {
            RunBatch(false);
        }
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    case A_POPOUT:    TogglePopLog(g_hwnd); break;
    case A_CLEAR:     ClearLog(); break;
    case A_DBG_RELOAD:
        ReopenDebugIni(g_hwnd);   // re-read the ini from disk
        {
            static const wchar_t* r[LANG_COUNT] = {
                L"[调试] 已重新加载 nvngx_dlssnr.ini。",
                L"[Debug] nvngx_dlssnr.ini reloaded.",
                L"[Отладка] nvngx_dlssnr.ini перезагружен.",
                L"[디버그] nvngx_dlssnr.ini을 다시 불러왔습니다.",
            };
            LogLine(r[g_lang]);
        }
        InvalidateRect(g_hwnd, nullptr, TRUE);
        break;
    case A_DBG_FOLDER:
        if (!g_srcDir.empty())
            ShellExecuteW(g_hwnd, L"open", g_srcDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
}

// ---------------------------------------------------------------------------
// Fullscreen
// ---------------------------------------------------------------------------
static void ToggleFullscreen(HWND h) {
    DWORD style = (DWORD)GetWindowLongPtrW(h, GWL_STYLE);
    if (!g_fullscreen) {
        MONITORINFO mi{ sizeof(mi) };
        if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) {
            g_fullscreen = true;
            GetWindowRect(h, &g_restoreRect);
            SetWindowLongPtrW(h, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
            SetWindowPos(h, HWND_TOP,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        }
    } else {
        g_fullscreen = false;
        SetWindowLongPtrW(h, GWL_STYLE, (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
        SetWindowPos(h, nullptr,
                     g_restoreRect.left, g_restoreRect.top,
                     g_restoreRect.right - g_restoreRect.left,
                     g_restoreRect.bottom - g_restoreRect.top,
                     SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOOWNERZORDER);
    }
}

// ---------------------------------------------------------------------------
// Main window
// ---------------------------------------------------------------------------
static void DrawThumb(HDC dc, const RECT& track, int scroll, int maxScroll, int visUnits, int totalUnits) {
    if (maxScroll <= 0 || totalUnits <= 0) return;
    int trackH = track.bottom - track.top;
    int th = trackH * visUnits / totalUnits;
    if (th < 28) th = 28;
    int ty = track.top + (int)((trackH - th) * ((float)scroll / maxScroll) + 0.5f);
    GpRoundFill(dc, track.left, ty, track.right - track.left, th, 3, g_skin.border);
}
static void GpLine(HDC dc, int x1, int y1, int x2, int y2, float w, COLORREF c) {
    EnsureGdiplus();
    GpGraphics* gfx = nullptr;
    if (GdipCreateFromHDC(dc, &gfx) != Gdiplus::Ok) return;
    GdipSetSmoothingMode(gfx, Gdiplus::SmoothingModeAntiAlias);
    GpPen* pen = nullptr;
    if (GdipCreatePen1(ToArgb(c), w, Gdiplus::UnitPixel, &pen) == Gdiplus::Ok) {
        GdipDrawLineI(gfx, pen, x1, y1, x2, y2);
        GdipDeletePen(pen);
    }
    GdipDeleteGraphics(gfx);
}
static COLORREF MikuLighten(COLORREF c, int amt) {
    auto ch = [&](int v) { int t = v + amt; return t < 0 ? 0 : t > 255 ? 255 : t; };
    return RGB(ch(GetRValue(c)), ch(GetGValue(c)), ch(GetBValue(c)));
}
static void GpPolyFill(HDC dc, const Gdiplus::PointF* pts, int n, COLORREF c) {    EnsureGdiplus();
    GpPath* path = nullptr;
    if (GdipCreatePath(Gdiplus::FillModeAlternate, &path) != Gdiplus::Ok) return;
    GdipAddPathLine2(path, pts, n);
    GdipClosePathFigure(path);
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

static void DrawAll(HDC dc, int W, int H) {
    const Skin& s = g_skin;
    const MS& m = kM[g_lang];
    HBRUSH bgb = CreateSolidBrush(s.bg);
    RECT full{ 0, 0, W, H };
    FillRect(dc, &full, bgb);
    DeleteObject(bgb);
    SetBkMode(dc, TRANSPARENT);

    // header: title vertically centred in the nav bar, right of the tabs;
    // subtitle sits directly below the bar.
    SelectObject(dc, g_fontTitle);
    SetTextColor(dc, s.primary);
    int hx = g_L.rail.right + 14;
    RECT tr{ hx, g_L.rail.top, W - 240, g_L.rail.bottom };
    DrawTextW(dc, L"DLSS5-NR-Boost manager", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, g_font);
    SetTextColor(dc, s.sub);
    RECT sr{ 32, g_L.rail.bottom + 4, W - 240, g_L.rail.bottom + 28 };
    DrawTextW(dc, m.sub, -1, &sr, DT_LEFT | DT_SINGLELINE);   // aligned with step captions

    // ---- horizontal nav bar (QQ-style tab icons) ----
    GpRoundFill(dc, g_L.rail.left, g_L.rail.top, g_L.rail.right - g_L.rail.left,
                g_L.rail.bottom - g_L.rail.top, 18, s.card);
    for (int i = 0; i < 4; ++i) {
        const RECT& r = g_L.railBtn[i];
        bool active = (g_page == i);
        bool hot = (g_hotRail == i);
        if (active || hot)
            GpRoundFill(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 12,
                        active ? s.primContainer : s.cardHi);
        COLORREF ic = active ? s.onPrimContainer : (hot ? s.text : s.sub);
        int cxm = (r.left + r.right) / 2, cym = (r.top + r.bottom) / 2;
        if (i == 0) {                      // lightning bolt — quick install
            Gdiplus::PointF pts[6] = {
                { (float)(cxm + 3),  (float)(cym - 9) }, { (float)(cxm - 6), (float)(cym + 1) },
                { (float)(cxm - 1),  (float)(cym + 1) }, { (float)(cxm - 3), (float)(cym + 9) },
                { (float)(cxm + 6),  (float)(cym - 2) }, { (float)(cxm + 1), (float)(cym - 2) } };
            GpPolyFill(dc, pts, 6, ic);
        } else if (i == 1) {               // grid — installed management
            int q = 7, o = -(7 + 1);
            for (int qx = 0; qx < 2; ++qx)
                for (int qy = 0; qy < 2; ++qy)
                    GpRoundFill(dc, cxm + o + qx * 9, cym + o + qy * 9, q, q, 2, ic);
        } else if (i == 2) {               // clock — operation history
            GpRoundFrame(dc, cxm - 8, cym - 8, 16, 16, 8, ic);
            GpLine(dc, cxm, cym - 4, cxm, cym, 2, ic);
            GpLine(dc, cxm, cym, cxm + 4, cym + 3, 2, ic);
        } else {                            // sliders — panel debug
            for (int k = 0; k < 3; ++k) {
                int ly = cym - 5 + k * 5;
                GpLine(dc, cxm - 8, ly, cxm + 8, ly, 2, ic);
                int kx = (k == 0) ? cxm - 3 : (k == 1) ? cxm + 3 : cxm;
                GpRoundFill(dc, kx - 3, ly - 3, 6, 6, 3, ic);
            }
        }
    }

    // cards (count depends on the page)
    int nCards = (g_page == 0) ? 3 : (g_page == 3 ? 0 : 1);
    for (int i = 0; i < nCards; ++i) {
        RECT r = g_L.card[i];
        GpRoundFill(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, 18, s.card);
    }

    // captions + counters (page-aware)
    SelectObject(dc, g_font);
    SetTextColor(dc, s.text);
    if (g_page == 0) {
        RECT c1{ g_L.card[0].left + 16, g_L.card[0].top + 10, g_L.card[0].right - 16, g_L.card[0].top + 32 };
        DrawTextW(dc, m.cap1, -1, &c1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT c2{ g_L.card[1].left + 16, g_L.card[1].top + 10, g_L.card[1].right - 16, g_L.card[1].top + 32 };
        DrawTextW(dc, m.cap2, -1, &c2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT c3{ g_L.card[2].left + 16, g_L.card[2].top + 10, g_L.card[2].right - 16, g_L.card[2].top + 32 };
        DrawTextW(dc, m.cap3, -1, &c3, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        int checkedN = 0;
        for (const Target& t : g_targets) if (t.check) ++checkedN;
        wchar_t cnt[128];
        swprintf_s(cnt, m.cntFmt, (int)g_targets.size(), checkedN);
        SetTextColor(dc, s.sub);
        RECT cr = g_L.countText;
        DrawTextW(dc, cnt, -1, &cr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    } else if (g_page == 1) {
        RECT c1{ g_L.card[0].left + 16, g_L.card[0].top + 10, g_L.card[0].right - 16, g_L.card[0].top + 32 };
        DrawTextW(dc, m.page1, -1, &c1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        int checkedN = 0;
        for (const Target& t : g_installed) if (t.check) ++checkedN;
        wchar_t cnt[128];
        swprintf_s(cnt, m.cntFmt, (int)g_installed.size(), checkedN);
        SetTextColor(dc, s.sub);
        RECT cr = g_L.countText;
        DrawTextW(dc, cnt, -1, &cr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    } else if (g_page == 2) {
        RECT c1{ g_L.card[0].left + 16, g_L.card[0].top + 8, g_L.card[0].right - 16, g_L.card[0].top + 30 };
        DrawTextW(dc, m.page2, -1, &c1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT c2{ g_L.card[1].left + 16, g_L.card[1].top + 8, g_L.card[1].right - 16, g_L.card[1].top + 30 };
        DrawTextW(dc, m.logTitle, -1, &c2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        // ---- page 3: BlueMiku (creeper-qt) debug page ----
        Miku k = MikuScheme(g_dark);
        RECT c = g_L.card[0];
        int cw = c.right - c.left, ch = c.bottom - c.top;
        (void)cw; (void)ch;

        // FilledCard: surfaceContainerLow, radius 24
        GpRoundFill(dc, c.left, c.top, cw, ch, 24, k.surfLow);

        // caption: primary_container badge + label
        GpRoundFill(dc, c.left + 20, c.top + 16, 36, 36, 12, k.primContainer);
        {   // sliders glyph on the badge
            int bx = c.left + 20 + 18, by = c.top + 16 + 18;
            for (int kk = 0; kk < 3; ++kk) {
                int ly = by - 5 + kk * 5;
                GpLine(dc, bx - 8, ly, bx + 8, ly, 2, k.onPrimContainer);
                int kx = (kk == 0) ? bx - 3 : (kk == 1) ? bx + 3 : bx;
                GpRoundFill(dc, kx - 3, ly - 3, 6, 6, 3, k.onPrimContainer);
            }
        }
        SelectObject(dc, g_fontBold);
        SetTextColor(dc, k.onSurface);
        RECT c1{ c.left + 68, c.top + 14, c.right - 16, c.top + 38 };
        DrawTextW(dc, m.page3, -1, &c1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // ---- right column: native control list on a surfaceContainerLowest
        //      rounded card, scrolled and clipped to the host area ----
        if (!DebugPanelReady()) {
            SelectObject(dc, g_font);
            SetTextColor(dc, k.onVar);
            RECT er = g_L.dbgArea;
            DrawTextW(dc, m.dbgEmpty, -1, &er, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            RECT da = g_L.dbgArea;
            GpRoundFill(dc, da.left, da.top, da.right - da.left, da.bottom - da.top,
                        20, k.surfLowest);
            GpRoundFrame(dc, da.left, da.top, da.right - da.left, da.bottom - da.top,
                         20, k.outlineVar);
            HRGN rgn = RoundRgn(da.left + 1, da.top + 1, da.right - da.left - 2,
                                da.bottom - da.top - 2, 19);
            if (rgn) SelectClipRgn(dc, rgn);
            int rl = da.left + 14, rr = da.right - 14;

            for (int i = 0; i < kDbgRowCount; ++i) {
                const DbgRow& row = kDbgRows[i];
                int y = DbgRowY(i);
                int rh = DbgRowH(i);
                if (y + rh < da.top) continue;
                if (y > da.bottom) break;
                wchar_t lb[96];
                DbgLabel(row.f, lb, 96);

                if (row.kind == DBG_SECTION) {
                    SelectObject(dc, g_fontSmall);
                    SetTextColor(dc, k.onVar);
                    RECT sr2{ rl, y + 8, rr, y + rh };
                    DrawTextW(dc, row.f == 0 ? m.tKeys : m.tNRSet, -1, &sr2,
                              DT_LEFT | DT_SINGLELINE);
                    continue;
                }
                if (g_dbgHot == i) {
                    RECT hr{ rl - 6, y + 2, rr + 6, y + rh - 2 };
                    GpRoundFill(dc, hr.left, hr.top, hr.right - hr.left,
                                hr.bottom - hr.top, 10, k.surfHigh);
                }

                if (row.kind == DBG_TOGGLE) {
                    SelectObject(dc, g_font);
                    SetTextColor(dc, k.onSurface);
                    RECT lr2{ rl, y + rh / 2 - 11, rr - 70, y + rh / 2 + 11 };
                    DrawTextW(dc, lb, -1, &lr2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    bool on = DbgGetB(row.f);
                    int sw = 40, sh = 22, sx = rr - sw, sy = y + rh / 2 - sh / 2;
                    GpRoundFill(dc, sx, sy, sw, sh, sh / 2, on ? k.primary : k.surfHigh);
                    if (!on) GpRoundFrame(dc, sx, sy, sw - 1, sh - 1, sh / 2, k.outlineVar);
                    int th = 16, tx = on ? (sx + sw - th - 3) : (sx + 3);
                    GpRoundFill(dc, tx, sy + 3, th, th, th / 2, on ? k.onPrimary : k.outline);
                } else if (row.kind == DBG_SEG) {
                    SelectObject(dc, g_font);
                    SetTextColor(dc, k.onVar);
                    RECT lr3{ rl, y + 2, rr, y + 20 };
                    DrawTextW(dc, lb, -1, &lr3, DT_LEFT | DT_SINGLELINE);
                    int sy = y + 24, sh2 = 26, gap2 = 8;
                    int bw = (rr - rl - gap2) / 2;
                    GpRoundFill(dc, rl, sy, bw, sh2, sh2 / 2,
                                s_pcfg.mode == 1 ? k.primary : k.surfHigh);
                    if (s_pcfg.mode != 1)
                        GpRoundFrame(dc, rl, sy, bw - 1, sh2 - 1, sh2 / 2, k.outlineVar);
                    SelectObject(dc, g_font);
                    SetTextColor(dc, s_pcfg.mode == 1 ? k.onPrimary : k.onVar);
                    RECT s1{ rl, sy, rl + bw, sy + sh2 };
                    DrawTextW(dc, m.lblMatched, -1, &s1, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    int x2 = rl + bw + gap2;
                    GpRoundFill(dc, x2, sy, bw, sh2, sh2 / 2,
                                s_pcfg.mode == 0 ? k.primary : k.surfHigh);
                    if (s_pcfg.mode != 0)
                        GpRoundFrame(dc, x2, sy, bw - 1, sh2 - 1, sh2 / 2, k.outlineVar);
                    SetTextColor(dc, s_pcfg.mode == 0 ? k.onPrimary : k.onVar);
                    RECT s2{ x2, sy, x2 + bw, sy + sh2 };
                    DrawTextW(dc, m.lblBilinear, -1, &s2, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                } else if (row.kind == DBG_SEG3) {
                    SelectObject(dc, g_font);
                    SetTextColor(dc, k.onVar);
                    RECT lr6{ rl, y + 2, rr, y + 20 };
                    DrawTextW(dc, lb, -1, &lr6, DT_LEFT | DT_SINGLELINE);
                    int sy = y + 24, sh3 = 26, gap3 = 6;
                    int bw = (rr - rl - gap3 * 2) / 3;
                    const wchar_t* names[3] = { m.styleBal, m.styleSharp, m.styleCine };
                    for (int q = 0; q < 3; ++q) {
                        int qx = rl + q * (bw + gap3);
                        bool act = (s_pcfg.nrStyle == (unsigned)q);
                        GpRoundFill(dc, qx, sy, bw, sh3, sh3 / 2,
                                    act ? k.primary : k.surfHigh);
                        if (!act)
                            GpRoundFrame(dc, qx, sy, bw - 1, sh3 - 1, sh3 / 2, k.outlineVar);
                        SelectObject(dc, g_fontSmall);
                        SetTextColor(dc, act ? k.onPrimary : k.onVar);
                        RECT s3r{ qx, sy, qx + bw, sy + sh3 };
                        DrawTextW(dc, names[q], -1, &s3r,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    }
                } else if (row.kind == DBG_SLIDER) {
                    SelectObject(dc, g_fontSmall);
                    SetTextColor(dc, k.onVar);
                    RECT lr4{ rl, y + 2, rr - 80, y + 20 };
                    DrawTextW(dc, lb, -1, &lr4, DT_LEFT | DT_SINGLELINE);
                    float v = DbgGetF(row.f), lo = DbgFMin(row.f), hi = DbgFMax(row.f);
                    wchar_t vv[24];
                    if (row.f == D_SCALE) swprintf_s(vv, L"%d%%", (int)(v * 100.0f + 0.5f));
                    else swprintf_s(vv, L"%.2f", v);
                    SelectObject(dc, g_fontBold);
                    SetTextColor(dc, k.primary);
                    RECT vr{ rr - 78, y + 2, rr, y + 20 };
                    DrawTextW(dc, vv, -1, &vr, DT_RIGHT | DT_SINGLELINE);
                    // M3E track: 12px pill, primary fill, no knob
                    int ty = y + 34, th2 = 12, capr = th2 / 2;
                    int fillW = (int)((float)(rr - rl) * ((v - lo) / (hi - lo)) + 0.5f);
                    if (fillW > 0) GpRoundFill(dc, rl, ty, fillW, th2, capr, k.primary);
                    if (fillW < rr - rl)
                        GpRoundFill(dc, rl + fillW, ty, rr - rl - fillW, th2, capr, k.surfHigh);
                } else if (row.kind == DBG_KEY) {
                    SelectObject(dc, g_font);
                    SetTextColor(dc, k.onVar);
                    RECT lr5{ rl, y + rh / 2 - 11, rr - 110, y + rh / 2 + 11 };
                    DrawTextW(dc, lb, -1, &lr5, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    bool capturing = (s_capRow == i);
                    int kw = 124, kh = 26, kx = rr - kw, ky = y + rh / 2 - kh / 2;
                    GpRoundFill(dc, kx, ky, kw, kh, 8, capturing ? k.primContainer : k.surfHigh);
                    GpRoundFrame(dc, kx, ky, kw - 1, kh - 1, 8, capturing ? k.primary : k.outlineVar);
                    wchar_t kv[48];
                    if (capturing) swprintf_s(kv, 48, L"%s", m.capHint);
                    else if (s_pcfg.requireCtrlAlt) {
                        wchar_t kn[24];
                        VkName(*DbgKeyPtr(row.f), kn, 24);
                        swprintf_s(kv, 48, L"Ctrl+Alt+%s", kn);
                    } else VkName(*DbgKeyPtr(row.f), kv, 48);
                    SelectObject(dc, g_font);
                    SetTextColor(dc, capturing ? k.onPrimContainer : k.onSurface);
                    RECT kr{ kx + 6, ky, kx + kw - 6, ky + kh };
                    DrawTextW(dc, kv, -1, &kr,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                }
            }
            if (rgn) { SelectClipRgn(dc, nullptr); DeleteObject(rgn); }
            DrawThumb(dc, { da.right - 12, da.top + 4, da.right - 4, da.bottom - 4 },
                      g_dbgScroll, DbgMaxScroll(), da.bottom - da.top, DbgContentH());
            // saved flash (top-right of the host card)
            if (s_savedAt && GetTickCount64() - s_savedAt < 1500) {
                SelectObject(dc, g_fontSmall);
                SetTextColor(dc, k.primary);
                RECT sv{ da.left + 14, da.top + 4, da.right - 20, da.top + 22 };
                DrawTextW(dc, m.dbgSaved, -1, &sv, DT_RIGHT | DT_SINGLELINE);
            }
        }
    }   // end page 3

    if (g_page == 0) {
    // input wells
    GpRoundFill(dc, g_L.srcWell.left, g_L.srcWell.top, g_L.srcWell.right - g_L.srcWell.left,
                g_L.srcWell.bottom - g_L.srcWell.top, 10, s.field);
    GpRoundFill(dc, g_L.rootWell.left, g_L.rootWell.top, g_L.rootWell.right - g_L.rootWell.left,
                g_L.rootWell.bottom - g_L.rootWell.top, 10, s.field);

    // ---- target list ----
    RECT la = g_L.listArea;
    GpRoundFill(dc, la.left, la.top, la.right - la.left, la.bottom - la.top, 10, s.field);
    HRGN rgn = RoundRgn(la.left, la.top, la.right - la.left, la.bottom - la.top, 10);
    if (rgn) SelectClipRgn(dc, rgn);
    if (g_targets.empty()) {
        SelectObject(dc, g_font);
        SetTextColor(dc, s.sub);
        RECT er = la;
        DrawTextW(dc, m.empty, -1, &er, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        int first = g_listScroll / kListItemH;
        int yoff = -(g_listScroll % kListItemH);
        for (size_t i = (size_t)first; i < g_targets.size(); ++i) {
            int y = la.top + (int)(i * kListItemH) - g_listScroll;
            if (y + kListItemH < la.top) continue;
            if (y > la.bottom) break;
            if (g_hotRow == (int)i) {
                RECT hr{ la.left + 4, y + 3, la.right - 14, y + kListItemH - 3 };
                GpRoundFill(dc, hr.left, hr.top, hr.right - hr.left, hr.bottom - hr.top, 8, s.cardHi);
            }
            DrawCheck(dc, la.left + 26, y + kListItemH / 2, g_targets[i].check, s);
            SelectObject(dc, g_fontSmall);
            SetTextColor(dc, StatusColor(g_targets[i].state));
            RECT st{ la.left + 52, y + 6, la.left + 52 + 170, y + 24 };
            DrawTextW(dc, StatusText(g_targets[i].state), -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, g_font);
            SetTextColor(dc, s.text);
            RECT fp{ la.left + 52, y + 23, la.right - 24, y + 43 };
            DrawTextW(dc, g_targets[i].dir.c_str(), -1, &fp,
                      DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_PATH_ELLIPSIS);
        }
    }
    if (rgn) { SelectClipRgn(dc, nullptr); DeleteObject(rgn); }
    DrawThumb(dc, { la.right - 14, la.top + 4, la.right - 4, la.bottom - 4 },
              g_listScroll, ListMaxScroll(), la.bottom - la.top, (int)g_targets.size() * kListItemH);
    }   // end page 0 (wells + target list)

    if (g_page == 1) {
        // ---- installed-management list ----
        RECT la = g_L.instArea;
        GpRoundFill(dc, la.left, la.top, la.right - la.left, la.bottom - la.top, 10, s.field);
        HRGN rgn = RoundRgn(la.left, la.top, la.right - la.left, la.bottom - la.top, 10);
        if (rgn) SelectClipRgn(dc, rgn);
        if (g_installed.empty()) {
            SelectObject(dc, g_font);
            SetTextColor(dc, s.sub);
            RECT er = la;
            DrawTextW(dc, m.instEmpty, -1, &er, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            int first = g_instScroll / kListItemH;
            for (size_t i = (size_t)first; i < g_installed.size(); ++i) {
                int y = la.top + (int)(i * kListItemH) - g_instScroll;
                if (y + kListItemH < la.top) continue;
                if (y > la.bottom) break;
                if (g_hotRow == (int)i) {
                    RECT hr{ la.left + 4, y + 3, la.right - 14, y + kListItemH - 3 };
                    GpRoundFill(dc, hr.left, hr.top, hr.right - hr.left, hr.bottom - hr.top, 8, s.cardHi);
                }
                DrawCheck(dc, la.left + 26, y + kListItemH / 2, g_installed[i].check, s);
                SelectObject(dc, g_fontSmall);
                SetTextColor(dc, StatusColor(g_installed[i].state));
                RECT st{ la.left + 52, y + 6, la.left + 52 + 170, y + 24 };
                DrawTextW(dc, StatusText(g_installed[i].state), -1, &st, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(dc, g_font);
                SetTextColor(dc, s.text);
                RECT fp{ la.left + 52, y + 23, la.right - 24, y + 43 };
                DrawTextW(dc, g_installed[i].dir.c_str(), -1, &fp,
                          DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_PATH_ELLIPSIS);
            }
        }
        if (rgn) { SelectClipRgn(dc, nullptr); DeleteObject(rgn); }
        DrawThumb(dc, g_L.instSb, g_instScroll, InstMaxScroll(), la.bottom - la.top,
                  (int)g_installed.size() * kListItemH);
    }   // end page 1

    if (g_page == 2) {
        // ---- history card (top) ----
        RECT ha = g_L.histArea;
        GpRoundFill(dc, ha.left, ha.top, ha.right - ha.left, ha.bottom - ha.top, 10, s.field);
        HRGN rgnH = RoundRgn(ha.left, ha.top, ha.right - ha.left, ha.bottom - ha.top, 10);
        if (rgnH) SelectClipRgn(dc, rgnH);
        SelectObject(dc, g_font);
        if (g_histLines.empty()) {
            SetTextColor(dc, s.sub);
            RECT er = ha;
            DrawTextW(dc, m.histEmpty, -1, &er, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            SetTextColor(dc, s.text);
            int first = g_histScroll / kLogLineH;
            for (size_t i = (size_t)first; i < g_histLines.size(); ++i) {
                int y = ha.top + (int)(i * kLogLineH) - g_histScroll;
                if (y + kLogLineH < ha.top) continue;
                if (y > ha.bottom) break;
                RECT lr{ ha.left + 10, y, ha.right - 8, y + kLogLineH };
                DrawTextW(dc, g_histLines[i].c_str(), -1, &lr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }
        if (rgnH) { SelectClipRgn(dc, nullptr); DeleteObject(rgnH); }
        DrawThumb(dc, g_L.histSb, g_histScroll, HistMaxScroll(), ha.bottom - ha.top,
                  (int)g_histLines.size() * kLogLineH);

        // ---- session log card (bottom) ----
        RECT la2 = g_L.logArea;
        GpRoundFill(dc, la2.left, la2.top, la2.right - la2.left, la2.bottom - la2.top, 10, s.field);
        HRGN rgn2 = RoundRgn(la2.left, la2.top, la2.right - la2.left, la2.bottom - la2.top, 10);
        if (rgn2) SelectClipRgn(dc, rgn2);
        SelectObject(dc, g_font);
        SetTextColor(dc, s.text);
        if (!g_lines.empty()) {
            int first = g_logScroll / kLogLineH;
            for (size_t i = (size_t)first; i < g_lines.size(); ++i) {
                int y = la2.top + (int)(i * kLogLineH) - g_logScroll;
                if (y + kLogLineH < la2.top) continue;
                if (y > la2.bottom) break;
                RECT lr{ la2.left + 10, y, la2.right - 8, y + kLogLineH };
                DrawTextW(dc, g_lines[i].c_str(), -1, &lr, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }
        if (rgn2) { SelectClipRgn(dc, nullptr); DeleteObject(rgn2); }
        DrawThumb(dc, g_L.sbLog, g_logScroll, LogMaxScroll(), la2.bottom - la2.top,
                  (int)g_lines.size() * kLogLineH);
    }   // end page 2

    // ---- buttons on top ----
    for (const Btn& b : g_L.btns) {
        if (b.id == A_DBG_RELOAD || b.id == A_DBG_FOLDER) continue;   // BlueMiku style below
        PaintButton(dc, b);
    }
    if (g_page == 3) {
        Miku k = MikuScheme(g_dark);
        for (const Btn& b : g_L.btns) {
            if (b.id != A_DBG_RELOAD && b.id != A_DBG_FOLDER) continue;
            bool hot = (g_hotAction == b.id);
            bool fillBtn = (b.id == A_DBG_RELOAD);
            COLORREF fill   = fillBtn ? (hot ? MikuLighten(k.primary, 26) : k.primary)
                                      : (hot ? k.surfHigh : k.primContainer);
            COLORREF label  = fillBtn ? k.onPrimary : k.onPrimContainer;
            RECT rc = b.rc;
            rc.left += 1; rc.top += 1; rc.right -= 1; rc.bottom -= 1;
            GpRoundFill(dc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, 15, fill);
            wchar_t txt[64];
            swprintf_s(txt, L"%s", BtnLabel(b.id).c_str());
            SelectObject(dc, g_font);
            SetTextColor(dc, label);
            DrawTextW(dc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

static int SbRatioToScroll(const RECT& track, int y, int maxScroll, int visUnits, int totalUnits) {
    if (maxScroll <= 0 || totalUnits <= 0) return 0;
    int trackH = track.bottom - track.top;
    int th = trackH * visUnits / totalUnits;
    if (th < 28) th = 28;
    int t = trackH - th;
    if (t <= 0) return 0;
    float ratio = (float)(y - track.top - th / 2) / (float)t;
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    return (int)(maxScroll * ratio + 0.5f);
}

static LRESULT CALLBACK MainWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_font      = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                  0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        g_fontBold  = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                  0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        g_fontTitle = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                                  0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        g_fontSmall = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                  0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        BuildSkinBrushes();
        EnsureGdiplus();

        HINSTANCE inst = (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE);
        g_editSrc = CreateWindowExW(0, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_READONLY,
                                    10, 10, 200, 24, h, (HMENU)100, inst, nullptr);
        g_editRoot = CreateWindowExW(0, L"EDIT", L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                     10, 40, 200, 24, h, (HMENU)101, inst, nullptr);
        for (HWND e : { g_editSrc, g_editRoot }) {
            SendMessageW(e, WM_SETFONT, (WPARAM)g_font, TRUE);
            SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(10, 10));
        }

        wchar_t exe[MAX_PATH * 2];
        GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
        std::wstring dir(exe);
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir.resize(slash);
        if (LooksLikeProxyDll(dir + L"\\" + kDll)) {
            g_srcDir = dir;
            SetWindowTextW(g_editSrc, dir.c_str());
            LogLine(kM[g_lang].readyAuto);
        } else {
            LogLine(kM[g_lang].readyManual);
        }
        // restore remembered install targets so they show up without a scan
        for (const std::wstring& d : LoadInstalledList()) {
            if (!DirExists(d)) continue;
            Target t;
            t.dir = d;
            ClassifyTarget(t);
            g_targets.push_back(t);
        }
        if (!g_targets.empty()) {
            wchar_t hb[128];
            swprintf_s(hb, L"[History] %d installed target(s) restored from dlssnr_manager.ini / 已从记录恢复 %d 个安装目标。",
                       (int)g_targets.size(), (int)g_targets.size());
            LogLine(hb);
        }
        RECT rc; GetClientRect(h, &rc);
        ComputeLayout(rc.right - rc.left, rc.bottom - rc.top);
        ClampScrolls();
        PlaceEdits(h);
        SetTimer(h, kDbgTimerId, 100, nullptr);   // debug-page commit debounce
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;   // everything is painted in WM_PAINT (double buffered)
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC wdc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        int W = rc.right - rc.left, H = rc.bottom - rc.top;
        HDC mem = CreateCompatibleDC(wdc);
        HBITMAP bm = CreateCompatibleBitmap(wdc, W, H);
        HGDIOBJ obm = SelectObject(mem, bm);
        DrawAll(mem, W, H);
        BitBlt(wdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
        SelectObject(mem, obm);
        DeleteObject(bm);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, g_skin.text);
        SetBkColor(dc, g_skin.field);
        return (LRESULT)g_brField;
    }
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            ComputeLayout(LOWORD(lp), HIWORD(lp));
            ClampScrolls();
            PlaceEdits(h);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 700;
        mmi->ptMinTrackSize.y = 700;
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hb = HitBtn(pt.x, pt.y);
        if (hb != g_hotAction) { g_hotAction = hb; InvalidateRect(h, nullptr, FALSE); }
        int nr = (hb >= A_PG0 && hb <= A_PG3) ? hb - A_PG0 : -1;
        if (nr != g_hotRail) { g_hotRail = nr; InvalidateRect(h, nullptr, FALSE); }
        int hr = -1;
        if (g_page == 1) hr = HitInstRow(pt.x, pt.y);
        else if (g_page == 3) hr = DbgHitRow(pt.x, pt.y);
        else hr = HitListRow(pt.x, pt.y);
        if (hr != g_hotRow) { g_hotRow = hr; InvalidateRect(h, nullptr, FALSE); }
        SetCursor(LoadCursorW(nullptr, hb != A_NONE ? IDC_HAND : IDC_ARROW));
        if (g_dbgDrag >= 0) {   // debug slider drag
            RECT da = g_L.dbgArea;
            int rl = da.left + 14, rr = da.right - 14;
            float t = (float)(pt.x - rl) / (float)(rr - rl);
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            int f = kDbgRows[g_dbgDrag].f;
            DbgSetF(f, DbgFMin(f) + t * (DbgFMax(f) - DbgFMin(f)));
            MarkPdirty();
        }
        if (g_sbDragList) {
            int target = SbRatioToScroll(g_L.listArea, pt.y, ListMaxScroll(),
                                         g_L.listArea.bottom - g_L.listArea.top,
                                         (int)g_targets.size() * kListItemH);
            // map thumb drag position to scroll
            int trackH = g_L.listArea.bottom - g_L.listArea.top - 8;
            int th = trackH * (g_L.listArea.bottom - g_L.listArea.top) /
                     ((int)g_targets.size() * kListItemH);
            if (th < 28) th = 28;
            int t = trackH - th;
            float ratio = t > 0 ? (float)(pt.y - g_L.listArea.top - 4 - th / 2) / (float)t : 0;
            if (ratio < 0) ratio = 0;
            if (ratio > 1) ratio = 1;
            g_listScroll = (int)(ListMaxScroll() * ratio + 0.5f);
            ClampScrolls();
            InvalidateRect(h, nullptr, FALSE);
        }
        if (g_sbDragLog) {
            int trackH = g_L.sbLog.bottom - g_L.sbLog.top;
            int total = (int)g_lines.size() * kLogLineH;
            int th = total > 0 ? trackH * (g_L.logArea.bottom - g_L.logArea.top) / total : 0;
            if (th < 28) th = 28;
            int t = trackH - th;
            float ratio = t > 0 ? (float)(pt.y - g_L.sbLog.top - th / 2) / (float)t : 0;
            if (ratio < 0) ratio = 0;
            if (ratio > 1) ratio = 1;
            g_logStick = false;
            g_logScroll = (int)(LogMaxScroll() * ratio + 0.5f);
            ClampScrolls();
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int b = HitBtn(pt.x, pt.y);
        if (b != A_NONE) {
            g_pressAction = b;
            SetCapture(h);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        POINT pp = pt;
        RECT lst = { g_L.listArea.right - 14, g_L.listArea.top + 4,
                     g_L.listArea.right - 4, g_L.listArea.bottom - 4 };
        if (PtInRect(&lst, pp) && ListMaxScroll() > 0) {
            g_sbDragList = true;
            SetCapture(h);
            g_listScroll = SbRatioToScroll(lst, pt.y, ListMaxScroll(),
                                           g_L.listArea.bottom - g_L.listArea.top,
                                           (int)g_targets.size() * kListItemH);
            ClampScrolls();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        RECT lsb = g_L.sbLog;
        if (PtInRect(&lsb, pp) && LogMaxScroll() > 0) {
            g_sbDragLog = true;
            g_logStick = false;
            SetCapture(h);
            g_logScroll = SbRatioToScroll(lsb, pt.y, LogMaxScroll(),
                                          g_L.logArea.bottom - g_L.logArea.top,
                                          (int)g_lines.size() * kLogLineH);
            ClampScrolls();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (g_page == 1) {
            RECT isb = g_L.instSb;
            if (PtInRect(&isb, pp) && InstMaxScroll() > 0) {
                SetCapture(h);
                g_instScroll = SbRatioToScroll(isb, pt.y, InstMaxScroll(),
                                               g_L.instArea.bottom - g_L.instArea.top,
                                               (int)g_installed.size() * kListItemH);
                ClampScrolls();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            int row = HitInstRow(pt.x, pt.y);
            if (row >= 0) {
                g_installed[row].check = !g_installed[row].check;
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        if (g_page == 2) {
            if (PtInRect(&g_L.histSb, pp) && HistMaxScroll() > 0) {
                SetCapture(h);
                g_histScroll = SbRatioToScroll(g_L.histSb, pt.y, HistMaxScroll(),
                                               g_L.histArea.bottom - g_L.histArea.top,
                                               (int)g_histLines.size() * kLogLineH);
                ClampScrolls();
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        if (g_page == 3) {
            RECT da = g_L.dbgArea;
            RECT dsb{ da.right - 12, da.top + 4, da.right - 4, da.bottom - 4 };
            if (PtInRect(&dsb, pp) && DbgMaxScroll() > 0) {
                SetCapture(h);
                g_dbgScroll = SbRatioToScroll(dsb, pt.y, DbgMaxScroll(),
                                              da.bottom - da.top, DbgContentH());
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            int row = DbgHitRow(pt.x, pt.y);
            if (row < 0) return 0;
            const DbgRow& r = kDbgRows[row];
            int rh = DbgRowH(row);
            switch (r.kind) {
            case DBG_TOGGLE:
                DbgSetB(r.f, !DbgGetB(r.f));
                MarkPdirty();
                break;
            case DBG_SEG: {
                int mid = da.left + (da.right - da.left) / 2;
                s_pcfg.mode = (pt.x < mid) ? 1 : 0;
                ClampPanel();
                MarkPdirty();
                break;
            }
            case DBG_SEG3: {
                int rl3 = da.left + 14, rr3 = da.right - 14;
                int q = (pt.x - rl3) * 3 / (rr3 - rl3);
                if (q < 0) q = 0;
                if (q > 2) q = 2;
                s_pcfg.nrStyle = (unsigned)q;
                ClampPanel();
                MarkPdirty();
                break;
            }
            case DBG_SLIDER: {
                int rl = da.left + 14, rr = da.right - 14;
                float t = (float)(pt.x - rl) / (float)(rr - rl);
                if (t < 0) t = 0;
                if (t > 1) t = 1;
                DbgSetF(r.f, DbgFMin(r.f) + t * (DbgFMax(r.f) - DbgFMin(r.f)));
                g_dbgDrag = row;
                SetCapture(h);
                MarkPdirty();
                break;
            }
            case DBG_KEY:
                s_capRow = (s_capRow == row) ? -1 : row;   // click again to cancel
                InvalidateRect(h, nullptr, FALSE);
                break;
            default: break;
            }
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        int row = HitListRow(pt.x, pt.y);
        if (row >= 0) {
            g_targets[row].check = !g_targets[row].check;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (g_dbgDrag >= 0) { g_dbgDrag = -1; ReleaseCapture(); }
        if (g_pressAction != A_NONE) {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (HitBtn(pt.x, pt.y) == g_pressAction)
                HandleAction(g_pressAction);
            g_pressAction = A_NONE;
            InvalidateRect(h, nullptr, FALSE);
        }
        if (g_sbDragList || g_sbDragLog) { g_sbDragList = g_sbDragLog = false; ReleaseCapture(); }
        else ReleaseCapture();
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (g_page == 1) {
            int row = HitInstRow(x, y);
            if (row >= 0 && row < (int)g_installed.size())
                ShellExecuteW(h, L"open", g_installed[row].dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else {
            int row = HitListRow(x, y);
            if (row >= 0 && row < (int)g_targets.size())
                ShellExecuteW(h, L"open", g_targets[row].dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(h, &pt);
        int step = (delta > 0 ? -1 : 1) * 60;
        if (g_page == 1) {
            if (PtInRect(&g_L.instArea, pt)) {
                g_instScroll += step;
                ClampScrolls();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            break;
        }
        if (g_page == 2) {
            if (PtInRect(&g_L.histArea, pt)) {
                g_histScroll += step;
                ClampScrolls();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            if (PtInRect(&g_L.logArea, pt)) {
                g_logStick = false;
                g_logScroll += step;
                ClampScrolls();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            break;
        }
        if (g_page == 3) {
            if (PtInRect(&g_L.dbgArea, pt)) {
                g_dbgScroll += step * 2;
                ClampScrolls();
                InvalidateRect(h, nullptr, FALSE);
                return 0;
            }
            break;
        }
        if (PtInRect(&g_L.listArea, pt)) {
            g_listScroll += step;
            ClampScrolls();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
        if (s_capRow >= 0) {   // hotkey capture on the debug page
            bool mod = (wp == VK_SHIFT || wp == VK_CONTROL || wp == VK_MENU ||
                        wp == VK_LWIN || wp == VK_RWIN ||
                        wp == VK_LSHIFT || wp == VK_LCONTROL || wp == VK_LMENU ||
                        wp == VK_RCONTROL || wp == VK_RMENU);
            if (wp != VK_ESCAPE && !mod) {
                int* kp = DbgKeyPtr(kDbgRows[s_capRow].f);
                if (kp) *kp = (int)wp;
                MarkPdirty();
            }
            s_capRow = -1;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (wp == VK_F11) { ToggleFullscreen(h); return 0; }
        break;
    case WM_TIMER:
        if (wp == kDbgTimerId && s_pdirty &&
            GetTickCount64() - s_pdirty > 500) {   // commit debounce
            s_pdirty = 0;
            SavePanelIni();
            s_savedAt = GetTickCount64();
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_DESTROY:
        SavePrefs();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    EnsureGdiplus();
    LoadPrefs();   // restore language + theme before anything is painted

    WNDCLASSW wc{};
    wc.style = CS_DBLCLKS;              // without this, WM_LBUTTONDBLCLK never fires
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DLSSNR_Manager_Class";
    RegisterClassW(&wc);

    WNDCLASSW pc{};
    pc.lpfnWndProc = PopWndProc;    pc.hInstance = hInst;
    pc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    pc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    pc.lpszClassName = L"DLSSNR_Manager_PopLog";
    RegisterClassW(&pc);


    RECT rc{ 0, 0, 700, 740 };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName,
                             L"DLSS5-NR-Boost manager",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;
    ShowWindow(g_hwnd, nCmdShow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(g_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CoUninitialize();
    if (g_font) DeleteObject(g_font);
    if (g_fontBold) DeleteObject(g_fontBold);
    if (g_fontTitle) DeleteObject(g_fontTitle);
    if (g_fontSmall) DeleteObject(g_fontSmall);
    DestroySkinBrushes();
    return 0;
}
