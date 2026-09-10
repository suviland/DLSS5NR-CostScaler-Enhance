#pragma once
// In-game topmost overlay panel hosted inside the DLSSNR proxy DLL.
//
// 架构（CN fork）：
//   proxy_main.cpp 每帧调用 UiPull()/UiPush()（见 proxy_ui.cpp）在共享内存、
//   INI 与本面板的 IniValue{} 之间同步；OverlaySetHooks 注册的回调由
//   proxy_main.cpp 实现（读写运行时原子变量 + INI 持久化）。
//   热键（默认 Ctrl+Alt+F11）切换面板显隐；面板是独立的 Win32 线程 +
//   分层窗口，绘制全部由 ui_panel.h 提供。
//   ⚠️ 新增设置项的完整改动清单见 ui_panel.h 文件头的「维护速查」。
#include "ui_panel.h"

namespace dlssnr_proxyui {

// Register the config access callbacks (implemented in proxy_main.cpp).
void OverlaySetHooks(const dlssnr_ui::PanelHooks& hk);

// Show / hide the overlay (thread-safe; creates the UI thread lazily).
void OverlayToggle();

// Stop the UI thread and destroy the window. Call on DLL_PROCESS_DETACH.
void OverlayShutdown();

bool OverlayVisible();

} // namespace dlssnr_proxyui
