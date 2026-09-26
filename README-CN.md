# DLSS5NR-CostScaler

<p align="center">
  <b>简体中文</b> ｜ <a href="README.md">English</a> ｜ <a href="README-RU.md">Русский</a> ｜ <a href="README-KO.md">한국어</a>
</p>

> **实测通过**：ReShade（含 RenoDX 系插件）·《上古卷轴》社区着色器 [Community Shaders](https://github.com/doodlegabe/CommunityShaders) · clshortfuse DLSS 插件（`renodx-dlss.addon64`）

---

## 本分支的优势（对比上游）

本仓库是 [xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler) 的增强分支（上游 v1.0.5 已完整同步）。上游是一个"手改 ini + 重启游戏"的纯算法代理，本分支把它升级为**所见即所得的实时调参工具链**，并深度改造了跳帧降噪：

| 能力 | 上游 | 本分支 |
| --- | :-: | :-: |
| 调参方式 | 手改 ini + 重启游戏 | **游戏内悬浮面板 / 控制台 / 管理器，改了立刻生效** |
| 多端实时联动 | 无 | 面板 ⇄ 控制台 ⇄ 共享内存 ⇄ 代理，毫秒级同步 |
| 图形化安装 / 卸载 | 手动改名复制 | **管理器递归扫描游戏库，一键装 / 卸 / 还原** |
| VRNR 跳帧 | 固定隔帧 | **可视化开关 + 全链路防闪烁**（0.6.3 基础三重 → 0.7.1 权重下限 / MV 重投影 / 剪影保护 → 0.7.3 帧时自适应） |
| FPS Governor | 无 | **自动升降分辨率档位维持目标帧率**，支持 FG 帧生成倍率（0.7.0 同步上游） |
| 界面 | — | M3E 风格、深浅双主题、中 / EN / RU / 한 四语言 |
| 安全面 | — | SEH 崩溃护盾，着色器异常不带走游戏 |

## 新特性

### 三端实时联动的调参体系

- **游戏内悬浮面板**（`Ctrl+Alt+F11`）：置顶、可拖动、自由缩放；不抢焦点、不触碰游戏输入管道（低级鼠标钩子旁观方案），关闭后游戏输入立即恢复；
- **独立控制台 `dlssnr_console.exe`**：游戏进程外运行，经共享内存与代理实时双向同步；
- **ReShade 伴侣插件**（`dlssnr-companion.addon64`）：ReShade Home 菜单内的配置页。

任何一处修改（含 ini 热重载），其余各端即时跟随。

### 图形化管理器 `DLSS5NR-CostScaler-Manager.exe`

纯 Win32 + GDI+ 自绘 Material 3 Expressive 界面（零运行时依赖），四页面：**快速安装**（递归扫描游戏库、一键装 / 卸 / 还原）· **已安装管理** · **操作记录** · **面板调试**（直接读写代理 ini，500ms 防抖自动写回）。

### FPS Governor 帧率调节（0.7.0，同步上游）

动态分辨率档位状态机：EWMA 平滑帧时（约 30 帧窗口）→ 与目标帧率比较 → 持续低于目标 95% 达 1.0s 降 5% 档、持续高于 115% 达 3.0s 升 5% 档，每次调整进入冷却驻留（默认 2.0s）。档位切换利用多槽位缓存实现零等待瞬时切换；**帧生成（FG）模式**下目标帧率按「显示帧率 = 基础 × 倍率」换算，适配 DLSS 3 / FSR 3 / Lossless Scaling 2x/3x/4x。

### 跳帧防闪烁体系（0.6.3 → 0.7.1 → 0.7.3，本分支独有）

开启隔帧推理（VRNR）后，推理帧（满强度神经重建）与跳过帧（陈旧 edit × 衰减权重）交替，帧率越低、单帧位移越大，人眼敏感频段的明暗脉动越明显。本分支的累计修复（总开关 `VrnrAntiFlicker`，默认开）：

- **时间坡道（0.6.3，0.7.1 修正）**：连续跳帧 ≥2 时残差强度按 100% → 90% → 72% → 55% 缓降；交替模式（每张 skip 帧连续计数恒为 1）不再误打折，消除 10% 周期脉动；
- **空间平滑（0.6.3）**：淡出权重改用 3×3 邻域平均亮度差计算，原生噪点不再逐帧驱动权重抖动；
- **高光守恒对称化（0.6.3）**：跳帧同样执行 HDR 高光钳制，高光区两条链路亮度一致；
- **权重下限（0.7.1，实验）**：skip 帧 edit 权重不再低于 `VrnrWeightFloor`（默认 0.60），运动区不整段归零，恢复帧不突变；
- **MV 重投影（0.7.1，实验）**：skip 帧用游戏运动矢量把 2 帧前的陈旧神经输入对齐到本帧再算亮度差，消除「错位画面被误判为运动」导致的整屏淡出脉冲；对齐与原始采样取亮度差较小者，MV 符号约定无关；
- **深度剪影保护（0.7.1，实验）**：skip 帧在深度不连续处（人物 / 物体轮廓）把 edit 权重压到 ≤0.10，陈旧 edit 不越过轮廓产生鬼影；
- **帧时自适应强度（0.7.3，实验）**：按 EWMA 帧率自适应衰减**推理帧**的 edit 强度（`VrnrAdapt`，默认开），低帧率下缩小推理帧与 skip 帧的视觉差——纯 CPU 侧实现，零新增 GPU 资源。

### 算法层（同步上游 v1.0.5）

硬件双线性降采样（LDS 瓦片缓存）→ 低分辨率 NR 推理 → **高频匹配残差合成**回原生帧；**25%–200% 超采样**、**各向异性缩放**（实验）、**深度感知双边轮廓保持**、HDR 亮度钳制 + RCAS 锐化、DRS 动态子矩形跟踪、SDR / HDR10 PQ / scRGB / R11G11B10。

---

## 这是什么

一个用于 NVIDIA DLSS-NR（DirectX 12）的独立代理 DLL 与配套工具集：让神经重建模型以较低分辨率运行推理，同时借助「高频匹配残差合成」着色器保留原生 1:1 的几何、精细纹理、文字与边缘细节——**在不引入模糊的前提下，把 DLSS-NR 的 GPU 开销从显示分辨率上解耦**。架构上与具体宿主无关，任何通过 DirectX 12 调用 `nvngx_dlssnr.dll` 的游戏、引擎或注入器均可使用。

**工作原理**：原版 DLL 被改名为 `nvngx_dlssnr_real.dll`，代理拦截 NGX 调用——先把原生画面降采样（开销极低），交给 DLSS-NR 推理，再把神经网络的增量以「匹配残差」方式合成回未被改动的原生帧上。

---

## 产物一览

构建产物输出到 `build\<版本号>\`（当前 `build/0.7.3/`），文件属性内嵌版本信息：

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

## 配置速查（`nvngx_dlssnr.ini`）

启动时读取，之后每秒自动热重载。下面是速查表，**逐项的作用与原理见下一节「配置详解」**。

```ini
[DLSSNR_Proxy]
EnableProxy = 1              ; 1 = 代理启用；0 = 原生直通
ResolutionScale = 0.75       ; 推理分辨率缩放（0.25 ~ 2.00）
EnableAnamorphic = 0         ; 各向异性缩放（实验）
ResolutionScaleX = 0.65      ; 水平缩放（0.25 ~ 2.00）
ResolutionScaleY = 0.85      ; 垂直缩放（0.25 ~ 2.00）
EnlargementMode = 1          ; 1 = 匹配残差；0 = 双线性
TransferStrength = 1.00      ; 残差合成强度（0 ~ 2）
ColorStrength = 1.00         ; 色彩强度（0 ~ 1）
Sharpness = 0.20             ; RCAS 锐化（0 ~ 1）
EnableDepthAwareResolve = 1  ; 深度感知轮廓保持
EnableAlternatingFrames = 0  ; 隔帧推理 VRNR（实验，默认关）
VrnrAntiFlicker = 1          ; 跳帧防闪烁总开关（默认开）
VrnrWeightFloor = 0.60       ; skip 帧 edit 权重下限（实验，0 ~ 1）
VrnrReproject = 1            ; MV 重投影对齐（实验）
VrnrAdapt = 1                ; 帧时自适应强度（实验，0.7.3）
VrnrAdaptAmount = 0.60       ; 自适应最大减光比例（0 ~ 1）
VrnrAdaptFpsHi = 45          ; 自适应起始帧率（低于此开始减光）
VrnrAdaptFpsLo = 25          ; 自适应满减光帧率
EnableHotkeys = 1            ; 游戏内热键总开关
EnableUi = 1                 ; 游戏内面板总开关
UiLanguage = 0               ; 面板语言：0 中 / 1 EN / 2 RU / 3 한
PanelX/Y/W/H = ...           ; 面板窗口位置 / 尺寸（自动保存）

[DLSSNR_Settings]
UseCustomSettings = 0        ; 0 = 透传调用方的 NR 参数；1 = 用下面的值覆盖
Style = 0                    ; 0 = 平衡 / 1 = 锐利 / 2 = 电影
Intensity = 1.00             ; 重建强度（0 ~ 2）
LocalStructureStrength = 1.00  ; 局部结构保持（0 ~ 2）
LocalToneStrength = 1.00       ; 局部色调（0 ~ 2）
SkinStructureStrength = -1.00  ; 皮肤结构（-1 = 自动，0 ~ 2）
UseAutoMask = 0              ; 快速运动/细小元素自动遮罩

[Hotkeys]
RequireCtrlAlt = 1           ; 热键是否需要 Ctrl+Alt 前缀
KeyToggleProxy = 32          ; 切换代理（VK 值，32 = Space）
KeyToggleMode = 35           ; 切换模式（35 = End）
KeyScaleUp = 33              ; 提高缩放（33 = PageUp）
KeyScaleDown = 34            ; 降低缩放（34 = PageDown）
KeyToggleUI = 122            ; 呼出面板（122 = F11）

[Governor]
EnableGovernor = 0           ; 动态分辨率 Governor（默认关）
TargetFps = 60.0             ; 目标帧率（30 ~ 240）
MinScale = 0.50              ; 最低缩放档位
MaxScale = 1.00              ; 最高缩放档位
HysteresisSec = 2.0          ; 调档冷却驻留（秒）
EnableFgMode = 0             ; 帧生成目标模式
FgMultiplier = 2.0           ; FG 倍率（2/3/4）
```

---

## 配置详解：每个开关的作用与原理

### 代理与缩放（`[DLSSNR_Proxy]`）

| 键 | 作用 | 原理 |
| --- | --- | --- |
| `EnableProxy` | 代理总开关。0 时所有 NGX 调用原样转发，行为与原版 DLL 完全一致 | 热键 / 面板可实时切换；切换只是改写转发路径上的缩放决策，不重建任何 D3D 资源 |
| `ResolutionScale` | 神经推理的分辨率比例。0.75 ≈ 省 40% 推理开销（推荐甜点）；0.50 = DLSS Performance 比例；2.00 = 200% 超采样（DLDSR / 截图用） | 神经推理开销与像素数成正比。缩放只作用于**喂给模型的内部缓冲**，最终画面仍由残差合成回全分辨率原生帧，所以降采样不等于降画质 |
| `EnableAnamorphic` + `ResolutionScaleX/Y` | （实验）水平 / 垂直独立缩放，替代统一比例 | 宽银幕场景垂直细节比水平更敏感，0.65×0.85 组合可再省约 45% 推理负载且帧节奏更稳；两条轴各自降采样，合成时各自升回原生 |
| `EnlargementMode` | 1 = 匹配残差（推荐）；0 = 双线性直出 + RCAS | 匹配残差：以原生 1:1 像素为锚点，只把神经输出的**增量（edit）**叠加回去——几何、文字、UI 天然零损失。双线性：直接放大神经输出，靠 RCAS 挽救锐度，细节损失更大 |
| `TransferStrength` | 残差合成强度（0 ~ 2）。1.0 = 原样搬运神经增量；0 = 纯原生画面 | 残差 = 神经输出 − 降采样输入。乘系数即缩放这个增量：调低画面更"原生"、调高（>1）光影变化更夸张 |
| `ColorStrength` | 色彩强度（0 ~ 1）。0 = 保持原生色相只缩放亮度 | 把神经增量分解为「亮度方向」与「色彩方向」两个分量：1.0 保留完整神经色彩（间接光照反弹 / 材质色），0 只保留亮度增量并按原生像素的色度矢量缩放，消除神经网络的偏色 / 粉化（上游 15f09dd 色相保持重构） |
| `Sharpness` | RCAS 对比度自适应锐化（0 ~ 1） | FidelityFX RCAS 算法：按邻域亮度范围自适应调节锐化权重（平坦区弱、边缘区强），避免过冲振铃。代理多 pass 写同一缓冲时会自动禁用，防止指数级边缘振铃 |
| `EnableDepthAwareResolve` | 深度感知双边轮廓保持 | 采样原生深度缓冲的 4 邻域，检测深度不连续（几何轮廓）；在轮廓处把低分辨率神经增量向原生的混合权重压低（最低 0.25），防止低分辨率光影「渗色」越过前景边缘 |

### VRNR 隔帧推理链路（实验）

| 键 | 作用 | 原理 |
| --- | --- | --- |
| `EnableAlternatingFrames` | 隔帧推理：每 2 帧才跑一次 DLSS-NR 推理，中间帧复用上一帧的神经增量，换取合成平均帧率提升 | 推理帧跑完整链路；skip 帧只执行合成着色器，把**缓存的陈旧 edit** 按亮度差权重衰减后叠加。陈旧增量与当前画面错位越多权重越低（画面回落到原生像素）。代价：帧节奏锯齿与明暗脉动 → 见下面三项防闪烁 |
| `VrnrAntiFlicker` | 防闪烁总开关（0.6.3 三重修复 + 0.7.1/0.7.3 增强项的前置） | 见上方「跳帧防闪烁体系」小节。关闭 = 与 0.6.2 行为完全一致 |
| `VrnrWeightFloor` | （实验）skip 帧 edit 权重下限（默认 0.60，0 = 关闭） | 运动剧烈处亮度差大 → 权重趋 0 → 下一张推理帧突然恢复满强度，形成「归零↔恢复」跳变。设下限后运动区至少保留 60% 残差，恢复帧不再突变。调高更稳、调低残影更少 |
| `VrnrReproject` | （实验）运动矢量重投影对齐（默认开） | 镜头平移时，2 帧前的神经输入与当前帧像素错位，亮度差被误判为「运动」而整屏淡出。改为先用游戏 MVec 把陈旧输入**对齐**到本帧位置再算差；对齐错误时亮度差自然更大，与原始采样取较小者，因此无需关心 MV 符号约定。MV 不可用时自动回退 |
| `VrnrAdapt` | （实验，0.7.3）帧时自适应强度总开关 | 帧率低时单帧位移更大，skip 权重跌得更狠，与推理帧满强度形成人眼敏感频段（15~22Hz）的明暗脉动。本开关在低帧率时**衰减推理帧**的 edit 强度，从两端同时缩小差距。纯 CPU 侧按 EWMA 帧率算出一个 0~1 的减光系数传入着色器，零新增 GPU 资源 |
| `VrnrAdaptAmount` | 最大减光比例（0 ~ 1，默认 0.60）。1.00 = 极低帧率下推理帧神经增量完全抑制 | 与 skip 帧权重的衰减程度对齐用：skip 端由权重链路衰减，推理端由这个系数衰减，两端差距越小脉动越小 |
| `VrnrAdaptFpsHi` / `VrnrAdaptFpsLo` | 减光 smoothstep 窗口（默认 45 / 25） | EWMA 帧率 ≥ Hi 完全不干预；Hi→Lo 之间线性过渡（smoothstep 平滑避免档位感）；≤ Lo 达到 `VrnrAdaptAmount` 满减光 |

### FPS Governor（`[Governor]`）

| 键 | 作用 | 原理 |
| --- | --- | --- |
| `EnableGovernor` | 动态分辨率档位总开关：自动升降 `ResolutionScale` 以维持目标帧率 | 帧时经异常过滤（>80ms 加载画面 / ≤0.5ms 丢弃）后做约 30 帧 EWMA 平滑；有效帧率 < 95% 目标持续 1.0s 降 5% 档，> 115% 目标持续 3.0s 升 5% 档，每次调档进入冷却驻留防抖动 |
| `TargetFps` | 目标帧率预算（30 ~ 240） | Governor 只在 `MinScale`~`MaxScale` 区间内调档，超出边界即停止 |
| `MinScale` / `MaxScale` | 缩放档位上下限（0.25 ~ 2.00） | 档位以 5% 步进；切换利用多槽位管线缓存实现 0ms 瞬时切换（不同缩放比例的中间缓冲预先驻留） |
| `HysteresisSec` | 调档冷却驻留（0.5 ~ 10.0 秒，默认 2.0） | 防止在阈值附近反复升降档造成分辨率呼吸感 |
| `EnableFgMode` | 帧生成目标模式 | 开启帧生成后，「显示帧率 = 基础渲染帧率 × 倍率」。开启本模式后 TargetFps 按**显示帧率**判定，避免帧生成场景下 Governor 误判降档 |
| `FgMultiplier` | FG 倍率（2.0 / 3.0 / 4.0） | 与 DLSS 3 / FSR 3 / Lossless Scaling 2x/3x/4x 对应；有效帧率 = 测量帧率 × 倍率 |

### NVIDIA NR 官方参数（`[DLSSNR_Settings]`）

| 键 | 作用 | 原理 |
| --- | --- | --- |
| `UseCustomSettings` | 0 = 透传调用方（游戏 / OptiScaler / RenoDX）设置的 NR 参数；1 = 用本节覆盖 | 代理默认不干预模型内部参数，保证与宿主行为一致；需要个性化降噪风格时才打开 |
| `Style` | 0 = 平衡（官方默认）/ 1 = 锐利 / 2 = 电影 | 三组官方预设调参，分别偏向微对比、边缘锐度与胶片柔化 |
| `Intensity` | 降噪 / 重建总强度（0 ~ 2） | 直接缩放模型内部降噪强度参数 |
| `LocalStructureStrength` | 局部结构保持（0 ~ 2） | 控制模型对高频几何 / 纹理结构的保留力度 |
| `LocalToneStrength` | 局部色调（0 ~ 2） | 控制局部 HDR 亮度对比与微过渡的保留力度 |
| `SkinStructureStrength` | 皮肤结构（-1 = 自动，0 ~ 2） | -1 时模型按语义启发式自动推断皮肤区域并保护其纹理 |
| `UseAutoMask` | 快速运动 / 细小元素自动遮罩 | 模型内部启发式掩码：运动过快或过细的元素减少降噪介入，降低拖影 |

### 快捷键（`[Hotkeys]`）

| 键 | 作用 | 说明 |
| --- | --- | --- |
| `RequireCtrlAlt` | 是否要求 Ctrl+Alt 组合前缀 | 防止与游戏按键冲突 |
| `KeyToggleProxy` | 切换代理启停（默认 Space） | 即 `EnableProxy` 的运行时切换 |
| `KeyToggleMode` | 切换匹配残差 / 双线性模式（默认 End） | 即 `EnlargementMode` 的运行时切换 |
| `KeyScaleUp` / `KeyScaleDown` | 增 / 减推理分辨率（默认 PageUp / PageDown） | 即 `ResolutionScale` 的运行时调整 |
| `KeyToggleUI` | 呼出游戏内面板（默认 F11，面板内固定要求 Ctrl+Alt） | 面板位置 / 尺寸 / 主题 / 语言均自动保存 |

---

## 系统要求

- Windows 10 / 11（64 位）
- NVIDIA RTX 显卡（RTX 20 / 30 / 40 / 50 系）
- 一个通过 DirectX 12 使用 NVIDIA DLSS-NR（`nvngx_dlssnr.dll`）的游戏或插件

## 构建（x64，MSVC）

```text
build.bat
```

自动完成：HLSL 着色器编译（fxc）→ 版本资源 → 代理 DLL → 控制台 → 管理器。产物统一输出到 `build\<版本>\`，中间文件在 `build\obj\`；版本号在 `build.bat`（`VERSION`）与 `app_version.rc` 中维护。companion 插件另行构建：`companion\build_companion.bat`。

## 常见问题

- **游戏目录在 Program Files？** 请以管理员身份运行管理器，或手动完成安装。
- **面板收不到鼠标？** 面板采用低级鼠标钩子旁观方案，不与游戏抢输入；若被反作弊拦截请反馈。
- **隔帧推理（VRNR）值得开吗？** 开启后请保持防闪烁体系默认开启（`VrnrAntiFlicker` / `VrnrWeightFloor` / `VrnrReproject` / `VrnrAdapt` 全默认开）。低帧率下仍有轻微脉动时，把 `VrnrAdaptAmount` 调高到 0.8~1.0；嫌画面变淡则调低。
- **闪退日志出现 `DXGI_ERROR_DEVICE_REMOVED`？** 这是 GPU 驱动复位（TDR），与面板无关（面板不触碰游戏 D3D 设备），可检查超频 / 驱动版本。

## 致谢与许可

- 上游算法：[xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler)（MIT，本仓库为其增强分支）
- 设计令牌参考：[creeper-qt](https://github.com/creeper5820/creeper-qt)（MIT）BlueMiku 主题包
- 致谢 [Dagherbou/OptiScaler_DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR)、[OptiScaler](https://github.com/optiscaler/OptiScaler)、[clshortfuse/RenoDX](https://github.com/clshortfuse/renodx)、[AMD FidelityFX](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK)（RCAS）、[Community Shaders](https://github.com/doodlegabe/CommunityShaders) 团队

本项目以 MIT 许可开源，详见 [LICENSE](LICENSE)。
