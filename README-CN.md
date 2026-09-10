# DLSS5NR-CostScaler

<p align="center">
  <b>简体中文</b> ｜ <a href="README.md">English</a>
</p>

> ✅ **实测通过**：ReShade（含 RenoDX 系插件）·《上古卷轴》社区着色器 [Community Shaders](https://github.com/doodlegabe/CommunityShaders) · clshortfuse DLSS 插件（`renodx-dlss.addon64`）

---

## ✨ 本分支的优势（对比上游）

本仓库是 [xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler) 的增强分支（上游 v1.0.5 已完整同步）。上游是一个"手改 ini + 重启游戏"的纯算法代理，本分支把它升级为**所见即所得的实时调参工具链**，并深度改造了跳帧降噪：

| 能力 | 上游 | 本分支 |
| --- | :-: | :-: |
| 调参方式 | 手改 ini + 重启游戏 | **游戏内悬浮面板 / 控制台 / 管理器，改了立刻生效** |
| 多端实时联动 | 无 | 面板 ⇄ 控制台 ⇄ 共享内存 ⇄ 代理，毫秒级同步 |
| 图形化安装 / 卸载 | 手动改名复制 | **管理器递归扫描游戏库，一键装 / 卸 / 还原** |
| VRNR 跳帧 | 固定隔帧 | **管理器 / 面板可视化开关** |
| 界面 | — | M3E 风格、深浅双主题、中 / EN / RU / 한 四语言 |
| 安全面 | — | SEH 崩溃护盾，着色器异常不带走游戏 |

## 🆕 新特性

### 三端实时联动的调参体系

- **游戏内悬浮面板**（`Ctrl+Alt+F11`）：置顶、可拖动、自由缩放；不抢焦点、不触碰游戏输入管道（低级鼠标钩子旁观方案），关闭后游戏输入立即恢复；
- **独立控制台 `dlssnr_console.exe`**：游戏进程外运行，经共享内存与代理实时双向同步；
- **ReShade 伴侣插件**（`dlssnr-companion.addon64`）：ReShade Home 菜单内的配置页。

任何一处修改（含 ini 热重载），其余各端即时跟随。

### 图形化管理器 `DLSS5NR-CostScaler-Manager.exe`

纯 Win32 + GDI+ 自绘 Material 3 Expressive 界面（零运行时依赖），四页面：**快速安装**（递归扫描游戏库、一键装 / 卸 / 还原）· **已安装管理** · **操作记录** · **面板调试**（直接读写代理 ini，500ms 防抖自动写回）。

### 分页面板 = 完整控制台

**基础 / 高级 / 降噪 NR / 快捷键** 四页覆盖全部配置：缩放 25%–200% 快捷芯片、传递 / 色彩 / RCAS 滑条、深度感知、各向异性 X/Y、**NVIDIA NR 官方参数区**、带 Ctrl+Alt 组合的快捷键可视化重绑；深浅主题、四语言、位置与尺寸记忆。

### 算法层（同步上游 v1.0.5）

硬件双线性降采样（LDS 瓦片缓存）→ 低分辨率 NR 推理 → **高频匹配残差合成**回原生帧；**25%–200% 超采样**、**各向异性缩放**（实验）、**深度感知双边轮廓保持**、HDR 亮度钳制 + RCAS 锐化、DRS 动态子矩形跟踪、SDR / HDR10 PQ / scRGB / R11G11B10。

---

## 这是什么

一个用于 NVIDIA DLSS-NR（DirectX 12）的独立代理 DLL 与配套工具集：让神经重建模型以较低分辨率运行推理，同时借助「高频匹配残差合成」着色器保留原生 1:1 的几何、精细纹理、文字与边缘细节——**在不引入模糊的前提下，把 DLSS-NR 的 GPU 开销从显示分辨率上解耦**。架构上与具体宿主无关，任何通过 DirectX 12 调用 `nvngx_dlssnr.dll` 的游戏、引擎或注入器均可使用。

**工作原理**：原版 DLL 被改名为 `nvngx_dlssnr_real.dll`，代理拦截 NGX 调用——先把原生画面降采样（开销极低），交给 DLSS-NR 推理，再把神经网络的增量以「匹配残差」方式合成回未被改动的原生帧上。

---

## 产物一览

构建产物输出到 `build\<版本号>\`（当前 `build/0.6.3/`），文件属性内嵌版本信息：

| 文件 | 用途 |
| --- | --- |
| `nvngx_dlssnr.dll` | 代理本体（放进游戏目录，转发到真实 `nvngx_dlssnr_real.dll`） |
| `DLSS5NR-CostScaler-Manager.exe` | 图形化管理器（安装 / 卸载 / 已装管理 / 记录 / 面板调试） |
| `dlssnr_console.exe` | 独立调试控制台（游戏外联机同步） |
| `nvngx_dlssnr.ini` | 配置文件（游戏内每秒热重载） |
| `dlssnr-companion.addon64` | ReShade 伴侣插件（可选） |

---

## 快速开始

1. 把管理器与代理 `nvngx_dlssnr.dll`、`nvngx_dlssnr.ini` 放在同一文件夹（自动识别）；
2. 打开管理器 →「快速安装」→ ② 选择游戏目录或游戏库根目录 → 扫描 → 勾选 → **一键安装**；
3. 启动游戏，`Ctrl+Alt+F11` 呼出面板开调；或直接在管理器「面板调试」页改（游戏内 1 秒热重载生效）。

<details>
<summary>手动安装（等价流程）</summary>

1. 进入游戏目录，把原有 `nvngx_dlssnr.dll` 改名为 `nvngx_dlssnr_real.dll`；
2. 把代理 `nvngx_dlssnr.dll` 和 `nvngx_dlssnr.ini` 复制到同一目录；
3. *（可选）* 把 `dlssnr_console.exe` 放进去，在游戏外调试；把 `dlssnr-companion.addon64` 放进去，在 ReShade 菜单调参；
4. 启动游戏，`Ctrl+Alt+F11` 打开面板。

</details>

---

## 配置说明（`nvngx_dlssnr.ini`）

启动时读取，之后每秒自动热重载：

```ini
[DLSSNR_Proxy]
EnableProxy = 1          ; 1 = 代理启用；0 = 原生直通
ResolutionScale = 0.75   ; 推理分辨率缩放（0.25 ~ 2.00；2.00 = 200% 超采样）
EnableAnamorphic = 0     ; 各向异性缩放（实验）：开启后使用下面的 X/Y 独立缩放
ResolutionScaleX = 0.65  ; 水平缩放（0.25 ~ 2.00）
ResolutionScaleY = 0.85  ; 垂直缩放（0.25 ~ 2.00）
EnlargementMode = 1      ; 1 = 匹配残差（Matched）；0 = 双线性（Bilinear）
TransferStrength = 1.00  ; 残差合成强度（0 ~ 2）
ColorStrength = 1.00     ; 色彩强度（0 ~ 1）
Sharpness = 0.20         ; RCAS 锐化（0 ~ 1）
EnableDepthAwareResolve = 1 ; 深度感知轮廓保持
EnableAlternatingFrames = 0 ; 隔帧推理 VRNR（实验，默认关）
VrnrAntiFlicker = 1        ; 跳帧防闪烁（0.6.3，默认开；关闭则与 0.6.2 行为一致）
EnableHotkeys = 1        ; 游戏内热键总开关
EnableUi = 1             ; 游戏内面板总开关
UiLanguage = 0           ; 面板语言：0 中 / 1 EN / 2 RU / 3 한
PanelX = 1500            ; 面板窗口位置 / 尺寸（自动保存，控制台同样读写）
PanelY = 120
PanelW = 396
PanelH = 640

[DLSSNR_Settings]
UseCustomSettings = 0    ; 0 = 透传调用方的 NR 参数；1 = 用下面的值覆盖
Style = 0                ; 0 = 平衡 / 1 = 锐利 / 2 = 电影
Intensity = 1.00         ; 重建强度（0 ~ 2）
LocalStructureStrength = 1.00  ; 局部结构保持（0 ~ 2）
LocalToneStrength = 1.00       ; 局部色调（0 ~ 2）
SkinStructureStrength = -1.00  ; 皮肤结构（-1 = 自动，0 ~ 2）
UseAutoMask = 0          ; 快速运动/细小元素自动遮罩

[Hotkeys]
RequireCtrlAlt = 1       ; 热键是否需要 Ctrl+Alt 前缀
KeyToggleProxy = 32      ; 切换代理（VK 值）
KeyToggleMode = 35       ; 切换模式
KeyScaleUp = 33          ; 提高缩放
KeyScaleDown = 34        ; 降低缩放
KeyToggleUI = 123        ; 呼出面板（默认 F12）
```

> 以上全部配置都可以在管理器「面板调试」页或游戏内面板可视化修改，改动自动保存

---

## 系统要求

- Windows 10 / 11（64 位）
- NVIDIA RTX 显卡（RTX 20 / 30 / 40 / 50 系）
- 一个通过 DirectX 12 使用 NVIDIA DLSS-NR（`nvngx_dlssnr.dll`）的游戏或插件

## 构建（x64，MSVC）

```text
build.bat
```

自动完成：HLSL 着色器编译（fxc）→ 版本资源 → 代理 DLL → 控制台 → 管理器。产物统一输出到 `build\<版本>\`，中间文件在 `build\obj\`；版本号在 `build.bat`（`VERSION`）与 `app_version.rc` 中维护。

## 常见问题

- **游戏目录在 Program Files？** 请以管理员身份运行管理器，或手动完成安装。
- **面板收不到鼠标？** 面板采用低级鼠标钩子旁观方案，不与游戏抢输入；若被反作弊拦截请反馈。
- **隔帧推理（VRNR）值得开吗？** 相机平移会有 30Hz 锯齿感、光追游戏中可能闪烁——保持默认关闭可获得最平滑的体验。
- **闪退日志出现 `DXGI_ERROR_DEVICE_REMOVED`？** 这是 GPU 驱动复位（TDR），与面板无关（面板不触碰游戏 D3D 设备），可检查超频 / 驱动版本。

## 致谢与许可

- 上游算法：[xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler)（MIT，本仓库为其增强分支）
- 设计令牌参考：[creeper-qt](https://github.com/creeper5820/creeper-qt)（MIT）BlueMiku 主题包
- 致谢 [Dagherbou/OptiScaler_DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR)、[OptiScaler](https://github.com/optiscaler/OptiScaler)、[clshortfuse/RenoDX](https://github.com/clshortfuse/renodx)、[AMD FidelityFX](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK)（RCAS）、[Community Shaders](https://github.com/doodlegabe/CommunityShaders) 团队

本项目以 MIT 许可开源，详见 [LICENSE](LICENSE)。
