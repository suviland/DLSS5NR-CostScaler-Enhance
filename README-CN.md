# DLSS5-NR-Boost

<p align="center">
  <b>简体中文</b> ｜ <a href="README.md">English</a>
</p>

一个用于 NVIDIA DLSS-NR（DirectX 12）的独立代理 DLL 与配套管理工具集，实现**分辨率缩放**与**算力开销控制**。它让神经重建模型以较低的分辨率运行推理，同时借助"高频匹配残差合成"着色器保留原生 1:1 的几何、精细纹理、文字与边缘细节——**在不引入模糊的前提下，把 DLSS-NR 的 GPU 开销从显示分辨率上解耦**。

主要面向配合 RenoDX 插件使用，也适用于任何通过 DirectX 12 调用 `nvngx_dlssnr.dll` 的游戏、引擎或注入器（架构上与具体宿主无关）。已在 clshortfuse 的 DLSS 插件（`renodx-dlss.addon64`）上实测通过。

---

## 产物一览

| 文件 | 用途 |
| --- | --- |
| `nvngx_dlssnr.dll` | 代理本体（放进游戏目录，转发到真实 `nvngx_dlssnr_real.dll`） |
| `DLSS5-NR-Boost-manager.exe` | 图形化管理器：快速安装 / 已安装管理 / 操作记录 / **面板调试** |
| `dlssnr_console.exe` | 独立调试控制台（游戏进程外与代理联机同步，保留用于 DLL 联调） |
| `nvngx_dlssnr.ini` | 配置文件（游戏内每秒热重载，改动即生效） |

管理器基于 **Material 3 Expressive** 视觉（深浅双主题，设计令牌参考 [creeper-qt](https://github.com/creeper5820/creeper-qt) 的 BlueMiku 主题包），支持 **中 / EN / RU / 한** 四语言即时切换；纯 GDI+ 抗锯齿自绘 UI，零外部依赖。

---

## 功能特性

### 代理 DLL（核心算法，已同步上游 v1.0.5）

- **即插即用**：原版改名 `nvngx_dlssnr_real.dll` 后代理即接管。
- 推理前先用降采样着色器压缩画面（**硬件双线性 + LDS 瓦片缓存 + 原位拷贝**，GPU 开销极低）。
- **高频匹配残差合成**（Resolve）：把神经网络的增量合成回未被改动的原生帧上。
- **超采样至 200%**：`ResolutionScale` 范围 0.25 ~ 2.00（DLDSR / 摄影模式 4x 采样密度）。
- **深度感知双边轮廓保持**：利用原生深度缓冲，避免低分辨率神经辐射跨前景边缘渗色。
- **各向异性 / 非对称神经缩放**（实验）：横纵分辨率独立控制（`EnableAnamorphic` + X/Y）。
- **隔帧推理 VRNR**（实验，默认关闭）：每 2 帧评估一次 DLSS-NR，换取更高平均帧率。
- **NVIDIA 官方 NR 参数**：默认**透传调用方**（OptiScaler / RenoDX / 引擎）的 NR 设置；也可通过 `[DLSSNR_Settings]` 自行覆盖（风格：平衡 / 锐利 / 电影，强度、局部结构、局部色调、皮肤结构、自动遮罩）。
- **HDR 亮度钳制** + 内置 **AMD RCAS** 锐化。
- **游戏内热重载**：修改 ini 约 1 秒生效，无需重启游戏。
- **SEH 崩溃护盾**：着色器编译等异常不会直接带走游戏进程。
- 支持 SDR（B8G8R8A8 / R8G8B8A8）、HDR10 PQ（R10G10B10A2）、scRGB（R16G16B16A16）与三通道 HDR（R11G11B10）。
- **动态子矩形跟踪**：为使用动态分辨率缩放（DRS）的游戏保留视口偏移。

### 游戏内悬浮面板

- `Ctrl+Alt+F11` 呼出**置顶面板**：M3E 滑条、Matched/Bilinear 分段、开关，全程鼠标操作，游戏无需中断；
- 面板不抢焦点、不触碰游戏输入管道（低级鼠标钩子旁观方案），关闭后游戏输入立即恢复；
- 面板位置自动记忆；界面语言中 / EN / RU / 한 循环切换。

### DLSS5-NR-Boost manager（图形化管理器）

1. **快速安装**：自动识别代理发布包 → 扫描游戏库（递归、跳过回收站 / 符号链接）→ 勾选目标 → **一键安装**（原版自动改名 `nvngx_dlssnr_real.dll` + 复制代理与 ini）/**一键卸载**（完整还原）；
2. **已安装管理**：从记录恢复所有已安装游戏，批量卸载、状态刷新、双击打开目录；
3. **操作记录**：完整的安装 / 卸载审计历史（`dlssnr_manager_history.log`）与运行日志，支持分离弹窗；
4. **面板调试**：用 manager 原生 BlueMiku 控件直接编辑代理包内的 `nvngx_dlssnr.ini`——全部开关、重建模式分段、滑条（含超采样 25–200%）、各向异性 X/Y、深度感知、隔帧推理、**NVIDIA NR 参数区**、**支持 Ctrl+Alt 组合的快捷键重绑**；改动 500ms 防抖自动写回。

其他：F11 全屏、窗口自由缩放自适应、语言与主题持久化、双击列表打开目录。

---

## 系统要求

- Windows 10 / 11（64 位）
- NVIDIA RTX 显卡（RTX 20 / 30 / 40 / 50 系）
- 一个通过 DirectX 12 使用 NVIDIA DLSS-NR（`nvngx_dlssnr.dll`）的游戏或插件

---

## 构建（x64，MSVC）

```text
build.bat
```

一键产出：`nvngx_dlssnr.dll`（代理）、`dlssnr_console.exe`（调试控制台）、`DLSS5-NR-Boost-manager.exe`（管理器，内嵌图标资源 `app.ico` / `app.rc`）。

---

## 安装方法（推荐使用管理器）

1. 把 `DLSS5-NR-Boost-manager.exe` 与代理 `nvngx_dlssnr.dll`、`nvngx_dlssnr.ini` 放在同一文件夹（管理器自动识别）；
2. 打开管理器 →「快速安装」→ ② 选择游戏目录或游戏库根目录 → 扫描 → 勾选 → **一键安装**；
3. 启动游戏，按 `Ctrl+Alt+F11` 呼出面板；或在管理器「面板调试」页直接调整配置（改完 ini 会自动热重载进游戏）。

<details>
<summary>手动安装（等价流程）</summary>

1. 进入游戏目录，把原有 `nvngx_dlssnr.dll` 改名为 `nvngx_dlssnr_real.dll`；
2. 把代理 `nvngx_dlssnr.dll` 和 `nvngx_dlssnr.ini` 复制到同一目录；
3. *（可选）* 把 `dlssnr_console.exe` 也放进去，即可在游戏外调试；
4. *（ReShade 用户可选）* 把 `dlssnr-companion.addon64` 复制到游戏目录，在 ReShade Home 菜单获得实时配置面板；
5. 启动游戏，按 `Ctrl+Alt+F11` 打开配置面板。

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
EnableHotkeys = 1        ; 游戏内热键总开关
EnableUi = 1             ; 游戏内面板总开关
UiLanguage = 0           ; 面板语言：0 中 / 1 EN / 2 RU / 3 한
PanelX = 1500            ; 面板窗口位置 X（像素；隐藏 / 退出时自动保存）
PanelY = 120             ; 面板窗口位置 Y（像素）
PanelW = 396             ; 面板窗口宽度（自由缩放后自动保存；独立控制台同样读写）
PanelH = 640             ; 面板窗口高度（自由缩放后自动保存；控制台默认 640）

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

> 以上全部配置都可以在 `DLSS5-NR-Boost-manager.exe` 的「面板调试」页可视化修改，改动自动保存。

---

## 常见问题

- **游戏目录在 Program Files？** 请以管理员身份运行管理器，或手动完成安装。
- **面板收不到鼠标？** 面板采用低级鼠标钩子旁观方案，不与游戏抢输入；若被反作弊拦截请反馈。
- **闪退日志出现 `DXGI_ERROR_DEVICE_REMOVED`？** 这是 GPU 驱动复位（TDR），与面板无关（面板不触碰游戏 D3D 设备），可检查超频 / 驱动版本。
- **隔帧推理（VRNR）值得开吗？** 相机平移会有 30Hz 锯齿感、光追游戏中可能闪烁——保持默认关闭可获得最平滑的体验。

---

## 致谢与许可

- 上游算法：[xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler)（本仓库为其分支的中文增强版）
- 设计令牌参考：[creeper-qt](https://github.com/creeper5820/creeper-qt)（MIT）
