#pragma once
// In-game topmost overlay panel hosted inside the DLSSNR proxy DLL.
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
