# DLSSNR-Cost-Scaler

一个用于 NVIDIA DLSS-NR（DirectX 12）的独立代理 DLL，实现**分辨率缩放**与**算力开销控制**。它让神经重建模型以较低的分辨率运行推理，同时借助"高频匹配残差合成"着色器，保留原生 1:1 的几何、精细纹理、文字与边缘细节——也就是说，**在不引入模糊的前提下，把 DLSS-NR 的 GPU 开销从显示分辨率上解耦**。

主要面向配合 RenoDX 插件使用，也适用于任何通过 DirectX 12 调用 `nvngx_dlssnr.dll` 的游戏、引擎或注入器（架构上与具体宿主无关）。

已在 clshortfuse 的 DLSS 插件（`renodx-dlss.addon64`）上实测通过。

> 语言：简体中文 | [English README](README.md)

---

## 功能特性

- **即插即用**的 `nvngx_dlssnr.dll` 独立代理（代理转发给真实 `nvngx_dlssnr_real.dll`）。
- 推理前先用**面积加权盒式滤波器**对画面降采样。
- **高频匹配残差合成**（Resolve）：把神经网络的增量合成回未被改动的原生帧上。
- **HDR 亮度钳制**：防止高光过曝与阴影不稳定。
- 内置 **AMD RCAS**（鲁棒对比度自适应锐化）通道。
- **游戏内热重载**：修改 `nvngx_dlssnr.ini` 后约 1 秒内生效，无需重启游戏。
- **游戏内热键**：实时开关、切换模式、微调缩放。
- **游戏内悬浮面板**：按 `Ctrl+Alt+F10` 呼出**中英双语置顶面板**，全程鼠标操作（拖滑杆、点预设/分段、拨开关），游戏无需中断。
- **独立控制台 EXE**：`dlssnr_console.exe` 可在游戏外编辑同一套设置（INI + 共享内存实时联动）。
- 支持 SDR（B8G8R8A8 / R8G8B8A8）、HDR10 PQ（R10G10B10A2）、scRGB（R16G16B16A16_FLOAT）与三通道 HDR（R11G11B10_FLOAT）。
- **动态子矩形跟踪**：为使用动态分辨率缩放（DRS）的游戏保留视口偏移。

---

## 系统要求

- Windows 10 / 11（64 位）
- NVIDIA RTX 显卡（RTX 20 / 30 / 40 / 50 系）
- 一个通过 DirectX 12 使用 NVIDIA DLSS-NR（`nvngx_dlssnr.dll`）的游戏或插件

---

## 安装方法

1. 进入游戏目录，找到原有的 `nvngx_dlssnr.dll`。
2. 将原文件改名为：
   ```text
   nvngx_dlssnr_real.dll
   ```
3. 把发布包里的代理 `nvngx_dlssnr.dll` 和 `nvngx_dlssnr.ini` 复制到同一目录。
4. *（可选）* 把 `dlssnr_console.exe` 也放到同目录，即可在游戏外调整设置。
5. *（ReShade 用户可选）* 把 `dlssnr-companion.addon64` 复制到游戏目录，可在 ReShade Home 菜单中获得一份实时配置面板。
6. 启动游戏。游戏内按 `Ctrl+Alt+F10`（或运行 `dlssnr_console.exe`）打开配置面板。

---

## 配置说明（`nvngx_dlssnr.ini`）

配置文件在启动时读取，之后每秒自动热重载（检测到改动即生效）：

```ini
[DLSSNR_Proxy]
; 代理总开关
; 1 = 代理启用（应用 ResolutionScale）
; 0 = 代理关闭（100% 原生直通给真实 DLSS-NR）
EnableProxy = 1

; 模型内部分辨率缩放（0.25 ~ 1.00）
; 1.00 = 100% 原生
; 0.85 = 85% 分辨率（神经推理约提速 ~28%）
; 0.80 = 80% 分辨率（约提速 ~35%）
; 0.75 = 75% 分辨率（约提速 ~40%，推荐甜点值）
; 0.67 = 67% 分辨率（DLSS 质量档比例）
; 0.50 = 50% 分辨率（DLSS 性能档比例）
ResolutionScale = 0.75

; 重建（Resolve）算法
; 1 = 匹配残差 Matched Residual（1:1 原生锚点 + 神经细节传递，极致清晰，推荐）
; 0 = 直接神经重建双线性 + RCAS（Bilinear + RCAS）
EnlargementMode = 1

; 神经细节传递强度（0.0 ~ 2.0，默认 1.0）
TransferStrength = 1.00

; 神经色彩/色调传递强度（0.0 ~ 1.0，默认 1.0）
; 1.00 = 完整神经色彩传递
; 0.00 = 仅亮度传递（消除神经网络的偏色/色调偏移，同时保留全部光照与细节）
ColorStrength = 1.00

; 对比度自适应边缘锐化（0.0 ~ 1.0，默认 0.20）
Sharpness = 0.20

; 启用游戏内快捷键
EnableHotkeys = 1

; 启用游戏内悬浮面板热键（Ctrl+Alt+F10）
; 1 = 可在游戏内呼出面板（默认）
; 0 = 关闭面板热键（下方其它热键仍可用）
EnableUi = 1

[Hotkeys]
; 是否需要按住 Ctrl + Alt 修饰键（1 = 需要，0 = 不需要）
RequireCtrlAlt = 1

; 虚拟键码（十进制）：
; Space=32, PageUp=33, PageDown=34, End=35, Home=36, Insert=45, Delete=46
; F1-F12 = 112-123，0-9 = 48-57，A-Z = 65-90
KeyToggleProxy = 32
KeyToggleMode = 35
KeyScaleUp = 33
KeyScaleDown = 34

; 悬浮面板组合键的基础键（Ctrl+Alt+<此键>），默认 F10 = 121
KeyToggleUI = 121
```

各配置项速览：

| 键名 | 取值 | 默认 | 含义 |
|---|---|---|---|
| `EnableProxy` | 0 / 1 | 1 | 代理总开关，0 为原生直通 |
| `ResolutionScale` | 0.25 ~ 1.00 | 0.75 | 模型内部推理分辨率缩放 |
| `EnlargementMode` | 0 / 1 | 1 | 1 = 匹配残差；0 = 双线性 + RCAS |
| `TransferStrength` | 0.0 ~ 2.0 | 1.00 | 神经细节传递强度 |
| `ColorStrength` | 0.0 ~ 1.0 | 1.00 | 神经色彩传递强度（0 = 仅亮度） |
| `Sharpness` | 0.0 ~ 1.0 | 0.20 | RCAS 边缘锐化强度 |
| `EnableHotkeys` | 0 / 1 | 1 | 是否启用游戏内热键 |
| `EnableUi` | 0 / 1 | 1 | 是否启用悬浮面板热键 |
| `RequireCtrlAlt` | 0 / 1 | 1 | 热键是否要求按住 Ctrl+Alt |
| `KeyToggleProxy` | VK 码 | 32 | 开关代理 |
| `KeyToggleMode` | VK 码 | 35 | 切换重建模式 |
| `KeyScaleUp` | VK 码 | 33 | 提高缩放（+5%） |
| `KeyScaleDown` | VK 码 | 34 | 降低缩放（-5%） |
| `KeyToggleUI` | VK 码 | 121 | 面板组合键基础键（F10） |

---

## 游戏内悬浮面板 & 独立控制台

两套界面共用同一份**中英双语布局**（中文主标签 + 英文副标签），且都支持纯鼠标操作：

- **开关（Switch）**——代理 / 热键 / 面板热键。
- **滑杆 + 预设（Slider + Chips）**——分辨率缩放：快捷档位（100 / 85 / 80 / 75 / 67 / 50 %）+ 精细滑杆。
- **分段（Segment）**——重建模式：匹配残差 Matched Residual / 双线性 Bilinear。
- **按键行（Key rows）**——展示热键组合；独立控制台还支持**点击按键行后按下新键来改绑**（Esc 取消）。
- 任何修改都会**自动保存**到 `nvngx_dlssnr.ini`；游戏运行期间还会推送到共享内存，**实时生效**。
- **GitHub 行**——点击面板底部的 `GitHub` 链接即可在浏览器中打开项目主页（<https://github.com/suviland/DLSSNR-Cost-Scaler-CN>）。

### 游戏内（代理 DLL）

- 按 `Ctrl+Alt+F10`（`KeyToggleUI`）显示/隐藏面板。它是一个**无边框置顶工具窗**——显示时会**主动夺取前台焦点**，以便鼠标能在游戏上方正常点击；标题栏可拖到任意位置，点 `×` 再次隐藏（焦点交还游戏）。
- 即使 `EnableHotkeys = 0` 也照常可用；如需彻底关闭，请设 `EnableUi = 0`。

### 独立控制台（`dlssnr_console.exe`）

- 把它放到 `nvngx_dlssnr.ini` 同目录并运行（游戏开不开都可以）。
- 游戏未运行时：修改的是 INI，供下次启动生效；游戏运行时：面板与游戏内悬浮层/热键通过**共享内存实时双向同步**。
- 点击标题栏 `×` 或按 `Esc` 退出。

---

## ReShade Companion 插件（`dlssnr-companion.addon64`）

若使用 ReShade，把 `dlssnr-companion.addon64` 与 ReShade 一起放入游戏目录即可。

- **无侵入**：不挂钩任何图形绘制调用或管线 Pass，仅在 ReShade Home 菜单中作为面板标签存在。
- **滑杆防抖**：内置交互防抖，快速拖动滑杆不会造成卡顿或 GPU 模型抖动。
- **游戏内改键**：可直接在界面中重新绑定快捷键与修饰键要求。

---

## 游戏内热键

当 `EnableHotkeys = 1` 时，默认快捷键为：

| 组合键 | 作用 |
|---|---|
| `Ctrl + Alt + F10` | 开关悬浮面板（独立于 `EnableHotkeys`，由 `EnableUi` 控制） |
| `Ctrl + Alt + Space` | 开关代理（缩放代理 ↔ 原生直通） |
| `Ctrl + Alt + End` | 切换重建模式：匹配残差（1）↔ 双线性（0） |
| `Ctrl + Alt + PageUp` | 分辨率缩放 +5% |
| `Ctrl + Alt + PageDown` | 分辨率缩放 -5% |

---

## 从源码构建

前置条件：
- Visual Studio 2022 或 Build Tools（含 **桌面 C++ 工作负载**）。
- Windows 10/11 SDK，含 `fxc.exe`（DirectX 着色器编译器）。

构建步骤：
1. 打开项目文件夹。
2. 在 x64 开发者命令行或普通命令行中运行 `build.bat`（脚本会自动定位默认安装位置下的 vcvars64 / fxc）。
3. 编译产物生成在项目根目录：`nvngx_dlssnr.dll`（代理 DLL，内含游戏内悬浮面板）与 `dlssnr_console.exe`（独立控制台）。

---

## 致谢

- [Dagherbou / OptiScaler_DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR) —— DLSS-NR 集成的先驱，提出匹配残差合成思路与特性生命周期处理。
- [OptiScaler](https://github.com/optiscaler/OptiScaler) —— 上游超分辨率框架。
- [clshortfuse / RenoDX](https://github.com/clshortfuse/renodx) —— RenoDX 框架与 DLSS ReShade 插件。
- [AMD](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) —— 鲁棒对比度自适应锐化（RCAS）算法。

---

## 许可证

本项目以 [MIT 许可证](LICENSE) 发布。
