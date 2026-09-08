#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

#define DLSSNR_SHARED_MEM_NAME L"Local\\DLSSNR_Config_Shared_v1"
#define DLSSNR_MAGIC 0x524E5344 // 'DSNR'

#pragma pack(push, 4)
struct DlssnrSharedConfig {
    uint32_t magic;            // DLSSNR_MAGIC
    uint32_t version;          // Monotonically increasing version counter
    uint32_t enableProxy;      // 1 = Active, 0 = Bypassed
    float    resolutionScale;  // 0.25 to 1.00
    uint32_t enlargementMode;  // 1 = Matched Residual, 0 = Bilinear Direct
    float    transferStrength; // 0.0 to 2.0
    float    colorStrength;    // 0.0 to 1.0
    float    sharpness;        // 0.0 to 1.0
    uint32_t enableHotkeys;    // 1 or 0
    uint32_t requireCtrlAlt;   // 1 or 0
    uint32_t keyToggleProxy;
    uint32_t keyToggleMode;
    uint32_t keyScaleUp;
    uint32_t keyScaleDown;
    uint32_t writerSource;     // 1 = Companion UI, 2 = Proxy/Hotkey, 3 = Disk INI
    uint32_t enableUi;         // 1 = overlay panel hotkey enabled (default 1)
    uint32_t keyToggleUi;      // base key for Ctrl+Alt+<key> overlay toggle (default VK_F11)
    uint32_t uiLanguage;       // dlssnr_ui::Lang (0=zh 1=en 2=ru 3=ko)
};
#pragma pack(pop)
