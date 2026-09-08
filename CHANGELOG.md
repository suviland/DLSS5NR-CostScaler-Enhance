# Changelog / 变更日志

All notable changes to **DLSS5-NR-Boost** are documented here.
本项目所有显著变更记录于此。格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)。

---

## [0.6.0] — 2026-09-08

### 新增 Added

- **同步上游 v1.0.5 算法更新**（[xenmods/DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler)，以 `e4d51b6` 为基准三方合并，完整保留本项目的 UI / 面板 / 管理器增强）：
  - **超采样至 200%**：`ResolutionScale` 范围扩展到 0.25 ~ 2.00；
  - **硬件双线性降采样 + LDS 瓦片缓存 + 原位拷贝**（`shaders.hlsl` 更新，着色器头文件由构建重新生成）；
  - **深度感知双边轮廓保持**（`EnableDepthAwareResolve`，默认开）；
  - **各向异性 / 非对称神经缩放**（实验）：`EnableAnamorphic` + `ResolutionScaleX` / `ResolutionScaleY`（0.25 ~ 2.00 独立控制）；
  - **隔帧推理 VRNR**（实验，默认关）：`EnableAlternatingFrames`；
  - **NVIDIA 官方 NR 参数**：默认透传调用方设置；`[DLSSNR_Settings]` `UseCustomSettings=1` 时可覆盖 Style（平衡/锐利/电影）、Intensity、LocalStructureStrength、LocalToneStrength、SkinStructureStrength、UseAutoMask；
  - **SEH 崩溃护盾**与遥测诊断字段（`dlssnr_shared.h` 扩展，同时保留本项目的 `enableUi` / `keyToggleUi` / `uiLanguage` 扩展字段；共享内存两侧已同步重编译）。
- **管理器「面板调试」页新增对应控件**：各向异性开关 + 水平/垂直缩放滑条、深度感知 / 隔帧推理开关、NVIDIA NR 参数区（自定义透传开关、风格三段、强度 / 局部结构 / 局部色调 / 皮肤结构滑条、自动遮罩），缩放滑条上限提升至 200%。

---

## [0.5.0] — 2026-09-08

### 变更 Changed

- **项目更名 DLSS5-NR-Boost**：管理器更名为 **`DLSS5-NR-Boost-manager.exe`**（窗口标题 / 页头同步），`build.bat` 产物目标更新。
- **管理器图标**：新增 `app.ico` / `app.rc`（BlueMiku 渐变圆角方块 + 白色闪电，256/48/32/16 多尺寸 PNG-in-ICO），经 `rc.exe` 链入管理器并注册为窗口图标。
- **README 重写**：`README-CN.md` 按新名字与当前功能全量重写（产物表、管理器四页、面板调试、构建、安装、ini 说明、FAQ）。

### 修复 Fixed

- **调试页卡片四角白边**：页面级 BlueMiku 卡片（24px 圆角）叠在同矩形的全局卡片（18px 圆角）上，角落露出底色——第 4 页不再绘制底层卡片。
- 调试页标题去除「①代理包内的」冗余描述（四语言）；副标题「游戏内面板代理 · 安装 / 卸载工具」改为居中排布。
- **地址栏细节（三轮）**：副标题改为与「代理文件包」等分区标题左对齐；原生 EDIT 控件相对圆角输入井四周内缩——圆角完整露出、不再被方形底色盖住；输入框高度压至一行行高，地址文字在框内垂直居中。

---

## [0.4.2] — 2026-09-08

### 新增 Added

- **管理器推倒重构：全自绘 Material 3 Expressive UI**：不再使用任何原生按钮 / ListView / 滚动条——卡片、10px 圆角矩形按钮（内缩 1px 防抗锯齿裁边）、圆形勾选框、双行目标列表（状态小字 + 完整路径）、圆角输入井、列表与日志各自的细圆角滚动条、步进徽章，全部由 GDI+ 抗锯齿绘制，彻底告别原生控件毛边与框线。
  - **交互**：全部在主窗口命中测试——按钮悬浮/按压态、单击行切换勾选、双击行在资源管理器打开目录、滚轮按光标位置分流到列表或日志、日志自动粘底（向上滚动即解除粘附）。
  - **保留能力**：扫描 / 一键安装 / 一键卸载逻辑、四语言（中/EN/RU/한）循环切换、深浅主题自由切换、`dlssnr_manager.ini` 偏好持久化、`F11` 全屏、窗口自由缩放自适应、日志分离弹窗（实时同步）。
- **游戏内面板 M3E 深色化**：`ui_panel.h` 主题迁移至 Material 3 深色令牌——表面 `#141218`、容器 `#26232E`、主色 `#D0BCFF`（onPrimary `#381E72`）；语言芯片、总览胶囊、缩放芯片、模式分段全部改为**药丸全圆角**；开关滑块增加 onPrimary 深色旋钮，保证主色轨道上的对比度。
  - **M3E 滑条**：滑杆重画为 Material 3 Expressive 样式——14px 粗胶囊轨道、左侧主色填充 / 右侧暗色余段、分离式大圆手柄（拖动时出现把手竖条）、右端 stop indicator；滑条行高同步加高。
  - **M3E 滑条（二轮）**：去除白色圆手柄——填充轨道的圆角端部即滑块本体，拖动/悬停时在值位置显示深色竖条把手，造型更贴近 M3 Expressive 官方滑条。
  - **M3E 滑条（三轮）**：彻底移除拖动把手（grip knob）——填充轨道的圆角端部即滑块本体，视觉最简。
  - **管理器抗锯齿 + 边界修复**：卡片与药丸按钮全部改用 GDI+ 抗锯齿绘制（消除直角毛边）；按钮四角的填充色按所在表面（卡片 / 背景）取色，不再出现突兀的方形角。
  - **管理器圆角矩形按钮（四轮）**：按钮由全胶囊改为 **10px 圆角矩形**，且绘制区域相对客户区**内缩 1px**——修复抗锯齿外圈恰好被按钮自身客户区裁掉导致的"下边界显示不全"；输入框高度收紧使文字近似垂直居中；日志控件移除系统滚动条（滚轮与自动滚动保留），界面再无原生控件毛边。
  - **文本与按钮角修复（五轮）**：③分区标题不再被计数文本矩形误限宽度（截断修复）；副标题行高加大消除垂直裁切；日志"清空 / 分离"按钮的角落改按卡片色取色，去除怪异边框。
  - **管理器无边框设计（三轮）**：去除全部控件黑色 `WS_BORDER` 框线——输入框 / 日志改为 GDI+ 圆角"输入井"底色（随主题换肤），列表融入卡片；同时修复 ①②③ 分区标题因行高不足被垂直裁切的问题。
  - **管理器四语言**：新增 中 / EN / RU / 한 循环切换按钮，标题以外的全部界面文本（分区标题、按钮、列表列头、状态列、计数）即时切换。
  - **输入框致命修复（六轮）**：路径输入框样式补上 `WS_CHILD | WS_VISIBLE | WS_TABSTOP`（重写时遗漏导致创建为不可见顶层窗口——文字不可见、无法点击输入、浏览结果不显示）；新增路径内存镜像兜底扫描；浏览/扫描按钮与输入井垂直中心对齐。
  - **双击打开修复（八轮）**：主窗口类注册补上 `CS_DBLCLKS`——此前窗口类未声明双击样式，`WM_LBUTTONDBLCLK` 从不派发，列表双击打开目录完全无效。
  - **QQ 式侧栏导航 + 三页面（八轮）**：左侧图标导航栏（闪电 / 四宫格 / 时钟，全部 GDI+ 矢量自绘）切换三个页面——**快速安装**（原①②③安装流程）、**已安装 NR 管理**（从记录恢复已安装列表，可勾选批量卸载、刷新状态、双击打开目录）、**操作记录与日志**（上半是 history.log 的安装/卸载审计记录，下半是运行日志，支持清空与分离弹窗）。四语言文案同步扩展。
  - **页头与按钮文字裁切修复（九轮）**：页头标题/副标题起点移至侧栏右侧（不再被导航栏盖住左半）；全选/全不选/刷新/扫描/清空按钮按四语言最长文案（俄语）加宽，`DrawTextW` 不再将溢出文字按矩形硬裁。
  - **管理器自由缩放 + 全屏**：窗口可拖拽缩放、最大化，控件随尺寸自适应重排（最小 720×760）；`F11` 切换无边框全屏。
- **语言芯片实心化 + 面板位置记忆**：游戏内面板（与控制台共用绘制）的语言切换芯片由描边空心改为**主色实心圆角矩形 + onPrimary 文字**；面板窗口位置持久化到 `nvngx_dlssnr.ini`（`[DLSSNR_Proxy] PanelX / PanelY`）——游戏内 overlay 每次打开恢复上次拖放位置（钳制到最近显示器工作区内），独立控制台 EXE 同样支持；未保存过时保持右上角默认位置。
- **管理器横排导航 + 内嵌面板调试**：左侧竖排导航栏改为**顶部横排 tab 条**（四个 GDI+ 矢量图标：快速安装 / 已安装管理 / 操作记录 / 面板调试），主标题居 tab 条右侧、副标题移至条下方，内容区全宽利用；`dlssnr_console.exe` 的面板调试功能**集成为第 4 页**——直接内嵌与游戏内/控制台完全相同的面板 UI（无头模式子窗口），读写 ① 代理包内的 `nvngx_dlssnr.ini`，进入页面即加载、拖动滑条/开关即时写回 ini；`dlssnr_console.exe` 保留，专用于游戏内 DLL 的联机调试。
- **面板调试页 creeper-qt (BlueMiku) 重绘**：设计令牌取自 [creeper-qt](https://github.com/creeper5820/creeper-qt)（MIT）的 `kBlueMikuThemePack` 预设——页面整体为 `surfaceContainerLow` 24px 圆角 FilledCard；左侧新增**配置摘要卡**（`surfaceContainerLowest` 20px 圆角）：分辨率缩放大字（primary 高亮）、重建模式、锐化强度、ini 路径，以及「重载 ini」（primary 实心药丸）与「打开文件夹」（primaryContainer tonal 药丸）两个按钮，拖动内嵌面板时摘要实时同步；右侧面板宿主容器带 `outlineVariant` 描边。深浅主题各有一套 BlueMiku 配色。
- **调试页原生控件化**：不再内嵌游戏面板 UI，改为 manager **自绘的 BlueMiku 控件集**直接编辑 ① 代理包内的 `nvngx_dlssnr.ini`——3 个 M3 开关（代理注入 / 快捷键 / 游戏内面板）、Matched·Bilinear 分段药丸、4 条 M3E 无把手滑条（分辨率缩放 25–100%、传递 / 色彩 / 锐化强度）、5 行**可点击重绑的快捷键**（点键帽 → 按下任意键捕获，Esc 取消）；所有改动经 500ms 防抖自动写回 ini 并闪现 "Saved ✓"，右栏圆角卡片内独立滚动，四语言文案齐全。
  - **组合键支持（二轮）**：新增「Ctrl+Alt 组合」开关（对应 ini `RequireCtrlAlt`）；开启后键帽显示 `Ctrl+Alt+键` 组合；捕获时忽略纯修饰键（Shift / Ctrl / Alt / Win），不再误绑单键。分段药丸与"已保存 ✓"提示改用官方四语言文案（匹配残差 / 双线性…）。
  - **布局精简（二轮）**：移除左侧摘要卡，调试列表全宽显示；「重载 ini / 打开文件夹」按钮移至标题行右端。
  - **深色列表头 + 扁平按钮（五轮）**：深色下列表头经 `DarkMode_Explorer` 主题跟随换肤（Win10 1809+）；tonal 按钮取消 1px 描边改为纯扁平色块，去除突兀框线。
  - **日志自绘滚动条 + 分离显示（五轮）**：日志右侧新增 GDI+ 圆角滚动条（可拖拽、随主题换肤、内容不足时隐藏）；新增"分离显示"按钮，可把日志弹出到独立窗口（内容实时同步）。
  - **偏好持久化（五轮）**：语言与主题选择写入管理器同目录的 `dlssnr_manager.ini`，重启后自动恢复。
- **图形化管理器 `dlssnr_manager.exe`**：一键完成代理的多目录安装/卸载，流程与 README 安装方法完全一致。
  - **源目录自动识别**：启动时自动定位安装器所在文件夹（若含代理 `nvngx_dlssnr.dll` 则直接填入），也可手动浏览；会校验所选文件夹内确实是本代理（体积 + 版本资源区分 NVIDIA 原版），防止把原版当代理复制。
  - **递归扫描 + 勾选**：选择游戏库根目录后递归扫描（深度 6 层，跳过 `$RECYCLE.BIN` / `System Volume Information` / 重解析点），列出所有包含 `nvngx_dlssnr.dll` / `nvngx_dlssnr_real.dll` 的子文件夹并标注状态（未安装·原版 / 已安装 / 异常·请手动检查 / 异常·缺原版），由用户勾选要操作的目标；双击条目可在资源管理器中打开该目录。
  - **一键安装**：将目录内 NVIDIA 原版 `nvngx_dlssnr.dll` 改名为 `nvngx_dlssnr_real.dll`（仅在确认其为原版时才改名；原版与 real 并存的异常状态会拒绝并提示手动处理），随后复制代理 DLL 与 `nvngx_dlssnr.ini`（INI 已存在则覆盖）。
  - **一键卸载**：删除代理 DLL，并把 `nvngx_dlssnr_real.dll` 还原为 `nvngx_dlssnr.dll`；保留 INI 供下次安装沿用配置。
  - 文件被占用（游戏运行中）或权限不足（系统目录）时逐项目报错并继续其余目标，全程日志可见。
  - **修复扫描始终失败的严重 bug**：扫描前校验根目录误用了 `FileExists()`（其语义为"存在**且不是目录**"，对文件夹恒为 false），导致无论选什么路径都报"请先选择有效的游戏文件夹"。现改用新增的 `DirExists()` 校验目录。
  - **流程智能化（二轮）**：在①误选游戏目录时不再只报错——自动把该目录转填到②并给出指引；②中选择的根目录**本身**若包含 `nvngx_dlssnr*.dll` 也直接作为目标列出（直接选单个游戏文件夹即可安装，不必依赖子文件夹扫描）；所有提示语明确区分"①=代理发布包 / ②=游戏目录"。
  - **界面重做（二轮）**：标题改为 **DLSSNR-cost-scaler installer**；白底 + 蓝色分步分组框（①②③）、大号标题、加粗"一键安装"主按钮、统一控件尺寸与间距；只读路径框不再是灰色块。

### 变更 Changed

- `build.bat` 新增 `dlssnr_installer.exe` 编译步骤（`comctl32` / `version` / `ole32` / `shell32`）。

---

## [0.4.1] — 2026-09-07

### 修复 Fixed

- **面板鼠标只能生效一次 / 关闭面板后游戏鼠标失焦（需切屏恢复）**：根因是 `RegisterRawInputDevices` 为进程级唯一注册列表，任何一次调用都会整体覆盖——面板注册 `RIDEV_INPUTSINK` 会清掉游戏 / DirectInput 自己的鼠标注册，而游戏只有在下次焦点变化（切屏）重新 Acquire 时才会恢复。彻底改为 **ReShade 式旁观者方案**：完全不碰原始输入注册，面板可见期间安装 **`WH_MOUSE_LL` 低级鼠标钩子（只观察、绝不吞事件，`CallNextHookEx` 全部放行）** 驱动虚拟光标（绝对坐标，钳制在面板内），关闭面板即摘除钩子——游戏输入管道从头到尾零接触，开启期间与关闭之后都保持原样。
- **标题栏拖动抢焦点 / 冻结面板**：旧实现发送 `WM_NCLBUTTONDOWN(HTCAPTION)` 进入系统模态移动循环，会激活覆盖窗口、抢走游戏焦点并阻塞消息泵。现改为**手动拖动**（`SetWindowPos` + `SWP_NOACTIVATE`），覆盖窗口全程不激活、焦点始终留在游戏。
- **虚拟光标模式下 × 关闭按钮失效**：合成鼠标事件路径未检查 `closeHit`，点击 × 无反应。现虚拟光标与真实鼠标两条路径统一走关闭逻辑。
- **钩子事件被窗口过程误吞（面板完全收不到鼠标）**：低级钩子投递的虚拟事件必须走 `WM_APP_VMOUSE / WM_APP_VWHEEL` 应用消息通道——若直接以真实消息 ID（`WM_MOUSEMOVE` 等）投递，会被窗口过程"捕获期间忽略真实鼠标消息"的保护逻辑丢弃。
- **滚轮/按键事件偶发丢失**：虚拟事件原先共享一个待发 `wParam` 变量，会被后续事件覆盖。现每个事件自带坐标与 wParam（滚轮走独立消息 `WM_APP_VWHEEL`），无共享状态可竞争。
- **拖动一次后关闭，再开面板无法拖动 / 关闭后游戏画面冻结闪烁**：`OnLButtonUp` 仅在滑杆拖动（`drag >= 0`）时释放捕获，而**标题栏拖动同样调用了 `SetCapture` 却永远不会被释放**——面板作为后台窗口长期持有系统唯一的鼠标捕获，既抢走游戏的捕获（游戏输入/画面异常），又跨会话残留导致下次拖动状态异常。现钩子模式下**完全不再调用 `SetCapture`**（低级钩子全局投递，无需捕获），并新增 `Panel::ResetInput()` 在每次打开/关闭时清空全部瞬时输入状态（拖动、悬停、按键捕获、待关闭标记）。
- **控制台 EXE 拖动一次后无法再次拖动**：同一捕获泄漏在控制台（真实鼠标路径）的体现——`OnLButtonUp` 的条件释放永远不覆盖标题栏拖动，泄漏的捕获使下一次 `SetCapture` 退化为空操作、拖动状态机失效。现改为：`OnLButtonUp` **无条件 `ReleaseCapture`**（未持有时为无害空操作）；`SetCapture` 前先检查 `GetCapture() != hwnd` 避免冗余重复捕获；`WM_CAPTURECHANGED` 仅响应真正的捕获被夺（`lParam` 非自身），防止自我通知误清进行中的拖动。
- **关闭面板时主动 `SetForegroundWindow` 归还焦点**：面板从不激活、从不持有焦点，"归还"反而可能触发游戏的激活/暂停循环（冻结闪烁）。现关闭路径零干预。

### 变更 Changed

- 低级鼠标钩子仅在面板可见时安装、关闭/隐藏时立即摘除；钩子回调仅做坐标转换与 `PostMessage`，不阻塞系统输入。移动事件增加 8ms 节流 + 同位置去重（游戏鼠标可达 1000Hz，全量投递会灌满 UI 线程拖慢系统输入），按键与滚轮始终即时投递。`EndMouseCapture` 增加防御性 `ReleaseCapture`。

---

## [0.4.0] — 2026-09-07

### 新增 Added

- **四语言界面（zh / en / ru / ko）**：面板由"中英并排双语"改为单选语言，标题栏右上角新增语言切换芯片（`中 → EN → RU → 한` 循环），点击立即生效并持久化。
  - 全部 UI 文案（14 个配置项、3 个分区标题、8 个状态标签）补齐俄语与韩语翻译。
  - `nvngx_dlssnr.ini` 新增 `UiLanguage`（0=zh, 1=en, 2=ru, 3=ko），游戏内切换后自动写回。
  - 共享内存协议（`DlssnrSharedConfig`）新增 `uiLanguage` 字段：游戏内面板、独立控制台、ReShade companion 三端语言实时联动。
- **游戏内鼠标捕获（Raw Input 虚拟光标，与游戏鼠标并行）**：面板自带一套独立指针——`RIDEV_INPUTSINK` 原始输入槽从**硬件层**读取鼠标数据（无视 DirectInput 独占/焦点/光标捕获状态）驱动**虚拟光标**，合成移动/点击/滚轮消息喂给面板，并自绘**软件箭头指针**；**完全不触碰游戏的鼠标输入**（无钩子、无 ClipCursor、无光标干预），游戏鼠标在面板开启期间与关闭之后都保持原样。参照 SkyrimDLSSNR SKSE 插件（Dear ImGui + imgui_impl_win32）与 ReShade 的捕获思路实现。
- **README 四语言**：新增完整俄语（Русский）与韩语（한국어）版本，含 INI 逐键说明与速览表；顶部导航四语锚点互跳。
- 圆角控件改用 GDI+ flat API 抗锯齿绘制（新增 `gdiplus.lib` 链接）。

### 修复 Fixed

- **关闭面板后鼠标不返还给游戏**：最终方案为**并行双鼠标**——彻底移除 `WH_MOUSE_LL` 钩子（此前吞掉全部系统鼠标事件，且 Raw Input 的 `RIDEV_REMOVE` 是进程级操作，会连带拆掉游戏自己的鼠标注册）。现在游戏的鼠标输入从头到尾不被触碰，面板靠自己的 Raw Input 虚拟光标 + 软件箭头独立工作；捕获期间真实鼠标消息在面板窗口被忽略、系统光标隐藏，避免双指针。
- **虚拟光标移动卡顿**：软件箭头绘制在重绘帧内，而旧逻辑仅在悬停状态变化时才重绘，匀速移动（尤其贴边慢移）时箭头一顿一顿跳。现软件光标激活期间每次移动都调度重绘（`InvalidateRect` 由系统合并，双缓冲整帧重绘开销可忽略）。
- **游戏内无法用鼠标操作面板**：旧钩子只在"光标已位于面板矩形内"时拦截，但游戏普遍隐藏系统光标并用 `ClipCursor`/`SetCursorPos`/DirectInput 把鼠标锁在游戏里，光标根本进不了面板区域。现改为 Raw Input 硬件级捕获 + 软件光标（见上），完全不再依赖系统光标状态。
- **文字底部被裁剪 / 垂直不居中**：Microsoft YaHei UI 行高约为字号的 1.8 倍，旧代码按小行高手工计算偏移再顶部对齐（DT_TOP），文字实际渲染位置偏低、分段按钮（匹配残差/双线性）等处下行被裁。现统一改为 `DT_VCENTER` 在完整行高内垂直居中，标签与数值（如滑杆行的名称与百分比）严格同一基线。
- **圆角锯齿**：圆角矩形/圆形由 GDI Region（硬边）改为 GDI+ 抗锯齿路径（`SmoothingModeAntiAlias`）；描边内缩半像素保证 1px 线完整清晰。

### 变更 Changed

- 面板标签绘制接口 `BlitLabel` 增加高度参数（垂直居中语义）。
- 文件头注释与 README 焦点策略描述同步更新（"夺取前台焦点" → "鼠标劫持、键盘焦点保留"）。

---

## [0.3.0] — 2026-09-07

对应发布包 `DLSSNR-Cost-Scaler-panel-0.3.zip`。

### 新增 Added

- **游戏内悬浮配置面板**（`Ctrl+Alt+F11` 呼出）：无边框置顶工具窗，标题栏可拖动，右上角 `×` 关闭。
- **独立控制台** `dlssnr_console.exe`：游戏外运行，通过共享内存与游戏内面板/热键**实时双向同步**；支持点击按键行改绑快捷键（Esc 取消）。
- **可复用 GDI 面板框架** `ui_panel.h`（header-only）：开关 / 滑杆 + 预设档位 / 分段选择 / 按键行 / 页脚状态 / 可点击 GitHub 行。
- **共享内存配置总线** `dlssnr_shared.h`：`version` 递增作为变更信号，`writerSource` 区分写入方（Companion / Proxy / 磁盘 INI）并抑制自身回读。
- **ReShade companion 插件**（`dlssnr-companion.addon64`）：ReShade Home 菜单内的非侵入式配置页，与主配置实时联动。
- 面板底部可点击 `GitHub` 链接行（`ShellExecuteW` 打开项目主页）。

### 修复 Fixed

- **面板操作闪烁**：绘制改为内存 DC 双缓冲，整帧绘制后单次 `BitBlt` 原子呈现，消除拖动滑杆/悬停/实时拉取时的中间帧。
- MSVC 中文源码编译三连坑：`/utf-8` 缺失导致 UTF-8 常量错乱；RPC 头 `small` 宏污染成员名（`Fonts::sub` 改名规避）；`/subsystem:windows` 须用 `wWinMain`。

### 变更 Changed

- 面板标题统一为 `DLSSNR Cost Scaler`。
- 面板默认热键 `Ctrl+Alt+F10` → `Ctrl+Alt+F11`（同步 INI、热键表与全部宿主）。
- README 合并为单文件双语（英文 + 简体中文，锚点互跳），INI 文档与实际配置逐键对齐。

---

## [0.1.0] — 初始 fork

- 基于上游 [xenmods / DLSSNR-Cost-Scaler](https://github.com/xenmods/DLSSNR-Cost-Scaler) 的代理 DLL：
  - 面积加权盒式滤波降采样 + 高频匹配残差合成（Matched Residual）重建。
  - HDR 亮度钳制、AMD RCAS 锐化、INI 热重载、游戏内热键。
  - 支持 SDR / HDR10 PQ / scRGB / R11G11B10，动态子矩形（DRS）跟踪。
- 另致谢 [Dagherbou / OptiScaler_DLSSNR](https://github.com/Dagherbou/OptiScaler_DLSSNR)（匹配残差思路）、[OptiScaler](https://github.com/optiscaler/OptiScaler)、[clshortfuse / RenoDX](https://github.com/clshortfuse/renodx)、[AMD FidelityFX](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK)（RCAS）。

[0.4.0]: https://github.com/suviland/DLSSNR-Cost-Scaler-with-control-panel
[0.3.0]: https://github.com/suviland/DLSSNR-Cost-Scaler-with-control-panel
[0.1.0]: https://github.com/xenmods/DLSSNR-Cost-Scaler
