# GameSprout 全量扫描分析报告

> 扫描时间：2026-11-01（Asia/Shanghai）· 范围：`GameSprout/` 全目录（1.7 万文件，含 build 产物）
> 方法：目录盘点 + 全量代码统计 + 6 路并行子代理深挖（构建系统 / Foundation·GRHI / Runtime / Editor·Hub / Module·View / 测试·工具·文档）+ 主代理复核关键证据

---

## 1. 项目概览

| 项 | 结论 |
| --- | --- |
| 定位 | C++/Qt 桌面游戏制作工具：AI 文字生成体素角色 → 本地构建资产 → 通用 Runtime 在视口中展示真实 3D 模型 |
| 仓库 | 是父仓库 `G-Foundation`（`github.com/Heroinaddicts/G-Foundation`）的子目录；兄弟目录还有 `Go/`（未分析）与 git-ignored 的 `Bin/` 输出目录 |
| 技术栈 | C++20 · Qt 6.12.0（Core/Gui/Widgets）· CMake 3.21+ · macOS（Apple Silicon，M4）· 图形经 **SDL3 GPU API**（Metal 后端）· 第三方经 vcpkg（commit 锁定）+ 配置期下载钉版运行时 |
| 代码规模 | 第一方 C++ 约 **9.4 万行**（.cpp/.h/.hpp，不含 build 与 ThirdParty）；全仓 17,196 个文件（大部分为 build 产物） |
| 磁盘 | 源码约 12MB（972 个被跟踪文件）；build 产物 7 个工程共约 1.0GB；`Bin/` 输出 1.7GB（git-ignored） |
| 最近活动 | 最近提交 2026-10-31；构建产物最新时间 2026-11-01 08:55（当天）——项目**活跃开发中** |
| 文档语言 | 中文；模块 README 质量极高（线程模型、所有权、验收状态均有记录） |

**一句话结论**：这是一个工程质量显著高于平均水平、文档意识强、测试意识强的自研引擎/编辑器项目；核心风险集中在「141 个文件的大规模未提交改动 + 壳/插件无版本协商」「测试从未在本机实际运行过」「RHI 文档与实现路线（SDL3 GPU）脱节」三点。

---

## 2. 仓库与工作区状态

- **分支**：`main`，与 `origin/main` 同步；最近提交 `bf9bce7`（按项目记录并恢复上次打开的场景）。
- **未提交改动规模大**：**140 个已修改文件（+3,051 / −1,372）** + 12 项未跟踪（含整目录新增：`Module/AnimationPreview/`、`Module/RuntimeHost/`、`Tests/Shared/`、`View/InspectorUI/Assets/`，以及新文档 `Docs/EditorModuleBoundaries.md`）。
- 未提交改动三条主线（详见 §4.5）：
  1. **GRHI 边界收口**：Editor 壳与所有 Module 公共接口不再暴露 GRHI；Runtime 公共 API 改为 `CreateViewport(NativeWindow)/CreateOffscreenRenderer`，内部自持 GPU Device。
  2. **模块职责拆分**：新增 RuntimeHost（独占 Runtime 生命周期与唯一每帧 Update）与 AnimationPreview（接管 SceneManager 的动画预览会话），SceneManager 回归纯场景文档管理。
  3. **Inspector 动画预览控件改造**：AnimatorClipPreviewEditor 切到 IAnimationPreview，ResourceField→ResourcePicker，大量 .ui 重生成。
- **git 卫生问题**：
  - `Foundation/Tests/` 下 **6 个 .dSYM/DWARF 调试产物被提交进 git**（约 2.8MB）。
  - 根 `.gitignore` 覆盖了各 build 目录与 `*.DS_Store`，但**缺 `__pycache__/`、`*.pyc`**（`GameSprout/__pycache__/` 已出现未跟踪 pyc），且含失效路径 `/Foundation/build`、`/GRHI/Build`。
  - `GameSproutRuntime/.gitignore` 用 `/Build/` 依赖 macOS `core.ignorecase` 才覆盖 `build/`，Linux 上会失效。

---

## 3. 架构总览

```
GameSproutHub（独立 Launcher 进程）
  └─ QProcess::startDetached --ProjectRoot=... ──► GameSproutEditor.app（壳 + 动态插件宿主）
         ├─ QLibrary 扫描加载 Logic/Module/* → IGameSproutModule（21 个业务模块）
         ├─ QLibrary 扫描加载 Logic/View/*   → IGameSproutView（11 个 UI 视图）
         │     运行时按“名字符串”互相查找（GetGameSproutModule<IX>(#name)）
         │
         ├─ RuntimeHost ──► GameSproutRuntime（通用 Runtime，零 Qt 依赖）
         │      └─ dlopen 动态加载 GRHI（SDL3 GPU 后端，私有 SDL 扩展）
         └─ ShaderCompiler（DXC + SDL_shadercross + SPIRV-Cross）
```

| 层 | 目录 | 规模（第一方行数） | 职责 |
| --- | --- | --- | --- |
| 基础库 | Foundation | ~4.0 万行（含少量捆绑第三方） | 任务池/定时器/日志/内存/网络/HTTP/MySQL/Redis/SSH/WS + 容器工具 |
| 图形 RHI | GRHI | 2,491 行 | 公共 RHI 接口（零 SDL/Metal 泄漏）+ SDL3 GPU 后端实现 |
| 通用 Runtime | GameSproutRuntime | 12,513 行 | Entity/Component/Scene、动画、前向渲染、视口、离屏读回 |
| 编辑器壳 | GameSproutEditor | 702 行 | 插件宿主：加载/生命周期/16ms 主循环 |
| 业务模块 | Module | ~1.3 万行 | 资产导入流水线、Library、场景管理、LMDB、Shader 编译等 21 个模块 |
| UI 视图 | View | ~1.2 万行 | Inspector（最大，4,862 行/96 文件）、场景/游戏视口、层级、资产浏览器 |
| 启动器 | GameSproutHub | 843 行 | 项目创建/打开/最近列表 |
| 测试 | Tests + 各 *Tests | ~1.0 万行 | 13 套件 + Runtime/Foundation/GRHI 测试，约 3,100 处 Check 断言 |
| 工具/模板 | Tools / Template / CreateComponent.py | 小 | DXC 工具链工程、插件脚手架模板与生成脚本 |

依赖纪律：Module/View **没有任何文件直接引用 GRHI/SDL**，渲染一律经 RuntimeApi；Runtime 核心零 Qt 依赖；GRHI 公共头无 MTL/Vk/D3D 类型。这条分层边界执行得非常好（且有编译期测试 `RuntimeSdkBoundaryChecks` 强制）。

---

## 4. 子系统逐项分析

### 4.1 Foundation（基础库）—— 成熟但测试缺失

- 11 个组件（Log/Task/System/Memory/Net/TimeWheel/Http/Mysql/Redis/WebSockets/Ssh）+ 公共 ABI 门面 + Utils（对象池、无锁队列、自旋锁、AOI、几何）与捆绑第三方（JsonCpp/Excel/TinyXML）。
- **AGENTS.md 规则合规性：✅ 完全合规**。第一方代码零智能指针（grep 全仓验证 0 命中）；显式 new/delete 均带 try/catch 逆序清理；禁止拷贝；线程所有权与 shutdown 协议文档化（SafeThread 强制 RequestStop→Wait）。
- 亮点：TaskManager 完成回调限定 Update 线程 + 1ms 预算防饿死（有专门测试验证）；Log 先 join 再排空双队列；GRHI 资源登记表拒绝泄漏。
- 问题：
  - **Foundation/Tests 未接入 CMake/CTest**——回归靠 README 手写编译命令，CI 无法拦截（3 个测试仅 287 行，覆盖会话分发/预算/所有权；TimeWheel/Log/Memory/Http/Mysql/Redis/SSH/WebSocket 全部零测试）。
  - **Net 三平台实现约 2,100 行高重复**（Tcper Linux 377 / macOS 347 / Windows 262 行），协议逻辑改一处需三处同步。
  - `SpinLock::ForceUnlock` 是危险逃生门；XPool 构造异常时 chunk 泄漏；XThreadPool 是 49 行空壳；`XEventPool` 双下划线开头标识符（保留字风险）。
  - 第一方代码 TODO/FIXME = **0**，纪律优秀。

### 4.2 GRHI（图形抽象层）—— 路线与文档脱节

- **实现路线与 Docs 冲突**：`Docs/RHIArchitecture.md` 仍写“尚未创建 RHI 实现 / 本阶段只实现 Metal”；实际 GRHI **已完整实现**，且不是自研 Metal 后端，而是**委托 SDL3 GPU API**（`DeviceSDL/CommandBufferSDL/SurfaceSDL` + Cocoa 桥），图形后端由 SDL 按平台自动选择（macOS 只允许 Metal）。
- 依赖一个**仓库外**的私有 SDL 构建：`/Users/max/Documents/GitHub/SDL`（相邻 `../../../SDL/build`），含自研扩展 `SDL_GetGPUAdapters` 等 4 个私有 API，CMake 编译期校验扩展 ABI 版本（防误用官方 SDL）。
- 进度对照 RHIDevelopmentPlan（45 日计划）：D1–D5 契约/四后端映射 ✅；D6–D33 主体功能（设备/选卡、Surface、Buffer/Texture/Sampler、Shader/Pipeline、深度、离屏+读回、提交/取消与资源生命周期）**已在 macOS Metal（Apple M4）真机验证**；D34–D45（回归/压力/SDK 样例）无交付证据；Windows DX12/Vulkan 仅交叉编译，iOS 未验收。
- 质量亮点：命令先记录 CPU 描述，Submit 统一校验后编码，非法输入不触碰底层；资源生命周期纪律为两库最佳（WaitForGPUIdle 后销毁、Release 拒绝遗留资源）；README 记录到“Qt NSView 880×536→1060×656、DPR=2 时 SurfaceFrame 同步为 2120×1312”级别的验证细节。
- 缺口：**无 DeviceCapabilities/上限查询 API**（文档 §4.6 明确要求）；无 MSAA/stencil/compute/barrier 模型（README 已声明为限制）；测试默认 `GRHI_BUILD_TESTS=OFF`。

### 4.3 GameSproutRuntime（通用 Runtime）—— 首版闭环完成，动画大幅超前

- 12,513 行 / 74 源文件：Api（公共 SDK，311 行主头 + 15 个数学头）、Components（Transform/Camera/Mesh/SkinnedMesh/Animator/UI 布局）、Renderer（前向管线/GPU 缓存/拾取/蒙皮/调试绘制/视口）、Animation（采样/混合/Humanoid 重定向/IK/LookAt）、Tests（12 个 ctest，1,900 行）。
- 对照 RuntimeArchitecture.md 模块地图：
  - ✅ 完成：Core/Math、Entity/Component（typeid 工厂）、World/Scene/Transform（TRS 校验+循环检测+动画姿态与编辑 TRS 分离）、Camera/RenderView、Mesh/Texture（预烘焙字节布局严格校验+GPU 上传）、简单前向 Renderer（提取/单 Pass/统计/CPU 拾取）、RHI 桥接（公共接口+运行时动态加载，核心零 Qt）。
  - ✅✅ **大幅超出文档首版范围**：完整动画系统（骨骼采样、分层状态机、遮罩、同步、Humanoid 重定向、IK/LookAt、Root Motion、Clip 直放、通知）——文档列为“后续（F 阶段）”的内容已提前落地。
  - ◐ 部分完成：Platform Services（借宿主能力）、资源系统（同步缓存+修订号失效，无异步/取消）、Material/Shader（内置无光照+贴图 Shader，无参数块）。
  - ❌ 缺失：**Light 组件**（方向光/环境光硬编码在 Preview.hlsl:108-109，与文档“环境光+方向光组件”不符）、Input/Actions、Physics、Audio、Game UI 渲染（Canvas/Image/RectTransform API 齐全但 RenderUI 是空桩 TODO）。
- 代码质量：
  - 内存：手工 new/delete 成对、RAII 保护器（CommandScope/GraphicsApiOwner）、析构兜底清空场景。
  - 线程：单线程宿主模型，无锁；测试显式断言事件同线程；Render 有重入防护。
  - 错误处理：Renderer 全链 Fail/GetLastError，资源校验详尽（蒙皮权重归一化、索引越界、NaN）；**但组件 Update 的异常被静默吞掉**（GameSproutRuntime.cpp:114 `catch(...){}`，无诊断出口）。
  - **死 API**：`OnInitialize`/`FixedUpdate` 在公共接口有声明（GameSproutRuntimeApi.h:48,50），全库无调用点。
- 测试：12 个 ctest 覆盖数学/坐标、动态库故障加载、节点/组件/事件/命令、**真实 GPU 渲染+离屏读回+拾取**（无 GPU 环境跳过码 77）、动画全链路、公共 SDK 编译期边界。**但本机未找到已构建的测试二进制**（见 §4.7）。
- 注：`Renderer/Shaders/GeneratedShaders.h` 5,412 行内嵌 SPIRV/DXIL/MSL 字节（占 Runtime 43%），是 SDL_shadercross 生成的产物，不计入手写代码评价。

### 4.4 插件框架（Editor 壳 + Module/View）—— 结构清晰，契约靠“名字”

- 壳（702 行）：QLibrary 按字母序扫描 `Logic/Module`、`Logic/View`，resolve `GetModule/GetView` 入口；四阶段生命周期 BeforeInitialize→Initialize→Launch→LaunchFinished；Release 顺序：Views → {AnimationPreview, SceneManager, RuntimeHost} → 其余；`PreventUnloadHint` 禁热卸载。
- Module↔View 配对**按名字符串查找 + C 风格强转**（`GetGameSproutModule<I##name>(#name)`，GameSproutEditorApi.h:33），拼错即 UB，无类型校验。模块 4 处依赖视图（IDEWindow/ConsoleUI），视图 3 处依赖模块，编译期仅接口耦合。
- 模块状态：AssetsPipeline（FBX 用 vendored ufbx 单头库、贴图/媒体/Shader 走外部工具进程、指纹缓存+批量进度）与 AssetsManager（4,261 行）最成熟；LocalDatabase 是**未修改的官方 LMDB 0.9.35 源码**（UPSTREAM.txt 记录 commit+SHA256）+ 本地薄封装；ShaderCompiler（359 行，同步接口、并发安全、工具链冻结）；AgentLocal/GameWorld/UIWorld/BuildSetting/Project 5 个空壳桩。
- 质量：Module/View TODO/FIXME = 0；Console 跨线程日志经 `QMetaObject::invokeMethod(AutoConnection)` 切回 UI 线程（`_Dispatch` 为空时静默丢日志）；接口间全裸指针+文档契约，无断言防护；各 DLL 有同名全局 `g_GameSproutModuleApi`。

### 4.5 未提交改动（当前工作区主风险区）

三条主线已完成度分析（基于 diff 与新增文件）：

1. **GRHI 边界收口**：公共 API 层面已彻底（Editor/Module 不再含 GRHI 头，Runtime 内部自持 Device，新增 `NativeWindow`(Cocoa/Win32/X11/Wayland 句柄) 与 `SamplerSettings`）。新增 `Renderer/RuntimeViewport.cpp`（101 行）：dladdr 定位库目录、动态加载 GRHI、进程级共享一个 GRHIApi 实例。
2. **RuntimeHost + AnimationPreview 拆分**：RuntimeHost（64 行）独占 Runtime 创建/销毁/每帧 Update；AnimationPreview（175 行）接管单 Clip 预览与场景播放，经 `SetSimulationPlaying` 请求宿主推进时间；SceneManager 删除 `SceneAnimationPreview.cpp`（−104 行）与 Runtime 所有权。新文档 `Docs/EditorModuleBoundaries.md`（未跟踪）已完整记录新边界。
3. **Inspector 动画预览控件**：.ui 大规模变更（InspectorUI.ui ±597、TransformComponentEditor.ui +832）多为 Designer 重生成 + ResourcePicker 拖放改造。
- **风险**：① 接口横向变更巨大且未提交，壳与插件**无版本协商**，新旧二进制混装会静默崩溃；② Release 顺序硬编码插件名；③ AnimationPreview 迁移是行为平移，需回归预览/Seek/切换场景语义；④ **初始化失败路径存在 Foundation 双重 Release**（GameSproutEditorFinal.cpp:161-168 已调 Release，GetInstance 再 delete 触发析构二次 Release）。

### 4.6 GameSproutHub（Launcher）—— 小而稳，有小瑕疵

- 独立进程，与 Editor 仅命令行交接（`--ProjectRoot`、`--IsNewProject`），无 IPC；历史存 `~/.GameSproutHub/Projects/porjects.json`（**拼写错误**）；QSaveFile 原子写 + `_CanSaveProjects` 防读取失败后覆盖原文件；删除记录不动项目文件。
- macOS 下 `EditorExecutable()` 硬编码 `../../../GameSprout.app/Contents/MacOS/GameSprout`，与 CMake target 名 `GameSproutEditor` 不一致（脆弱约定）。
- `_Projects.Load(error)` 返回值未判断（有兜底）；无防重复启动；无 i18n/qrc。

### 4.7 测试体系 —— 设计用心，但**从未实际运行**

- 13 套件 / 35 cpp + 34 h ≈ 10,307 行，自研极简框架（无 QtTest/gtest）：每文件自定 `Check(bool,msg)` 失败 throw → 非零退出码；全仓约 **3,100 处 Check 断言**（AssetsImport 1,044、Runtime 566、SceneDocument 310、AnimationAssets 283、SceneEditing 216、AnimationIntegration 195……）。
- 深度：真运行回归为主（加载 Bin 下真实 dylib + `QT_QPA_PLATFORM=offscreen` 跑真 UI；SceneDocument/SceneEditing 直接编译 SceneManager 源码）；`RuntimeSdkBoundaryChecks` 为纯编译期边界检查。**无黄金图像/像素对比**，GPU 渲染正确性无自动验证（仅 AnimationIntegration 把 GPU capture 存图供人工查看）。
- **关键发现**：
  - **Tests/ 13 套件本地从未构建运行**：无 build 目录、无 Testing/ 记录。
  - 7 个工程的 `LastTest.log` 全部为空（`enable_testing` 存在但 ctest 空跑，09:27–09:32 当天记录）。
  - Foundation/Tests 未接入 CMake（3 个测试靠 README 手写命令 + ASan/UBSan）；GRHI 测试默认 OFF。
  - 全机唯一构建过的测试二进制是 `Foundation/build/TaskCompletionBudget.check`（10-31 17:25）。
- 盲区：AnimationIntegration 依赖人工 FBX 默认不注册；原生窗口测试需显示环境（跳过码 77）；失败即停无汇总；无 CI。

### 4.8 Shader 工具链与脚手架 —— 完成度高

- `Tools/ShaderTools`：DXC（SDL fork，SHA256 锁定）+ SDL_shadercross + SPIRV-Cross，HLSL → SPIR-V/MSL（+DXIL 可选，Metallib 可选）；macOS arm64 实测 SPIR-V/MSL/DXIL + macOS Metallib 全部通过；iOS/模拟器与 Windows/Linux 主机部署未验证。绑定约定（Vertex space0/1、Fragment space2/3、Compute space0/1/2）在 README 与 ShaderCompiler 模块文档中精确记录。
- `CreateComponent.py`（161 行）：质量良好——名称三重校验（C++ 关键字/保留目标/大小写冲突）、只替换 `{name}` 保留 CMake `${...}`、事务回滚、禁止覆盖；已实际用于生成 RuntimeHost/AnimationPreview。
- `Template/`：Module(5)/View(7) 模板，与脚本配套。

### 4.9 构建系统与第三方依赖

- **无根 CMakeLists / 超级构建**：8 个工程各自独立 configure（`cmake -S <工程> -B <工程>/build`），靠相对路径 SDK cmake + IMPORTED 二进制目标互连，产物统一输出到 `<repo>/Bin`。构建顺序需手工保证：**Foundation → SDL(仓库外，带私有扩展) → GRHI → GameSproutRuntime → Tools/ShaderTools → GameSproutEditor（聚合编译全部 20 个 Module/View 插件）**；GameSproutHub 仅依赖 Foundation+Qt。
- **第三方依赖**：
  - 源码捆绑（git 内）：JsonCpp、Excel、TinyXML2、LMDB 0.9.35、ufbx。
  - vcpkg（commit 锁定 `f87344cac031...`）：libssh2/curl/libmariadb/hiredis/libwebsockets/openssl/zlib/boost-container/boost-interprocess、graphicsmagick、sqlite3[fts5]、**onnxruntime、hnswlib（RAG 向量库）、protobuf**。
  - 配置期下载钉版运行时：Apache Tika + Temurin JRE 21、内嵌 Python 3.12 + 办公 wheel、FFmpeg、**BGE-small-zh-v1.5 ONNX 嵌入模型** —— 这些为 Agent/AI 能力（文档解析、RAG）服务，说明 AgentLocal 虽为空壳，其能力栈已预留。
  - 外部独立构建：SDL3（带 GRHI 显卡枚举私有扩展，位于仓库外的 `/Users/max/Documents/GitHub/SDL`）；ShaderTools 用 FetchContent 锁 SPIRV-Cross/SDL_shadercross（SHA256）。
- **构建状态（证据）**：全部 build 树 Debug / Unix Makefiles / clang++ / Qt 6.12.0；**Editor 主程序与插件 dylib 于 Nov 1 08:53–08:55 刚成功构建**（当天）；libFoundation.dylib 10-31、libGRHI.dylib 10-31、libGameSproutRuntime.dylib 10-31 23:46；Hub 可执行停在 10-25 但源码未变；**测试开关均 OFF**（GRHI_BUILD_TESTS=OFF、GAMESPROUT_RUNTIME_BUILD_TESTS=OFF）。
- 问题：SDL/Qt 为机器绝对路径依赖（换机需重配）；部署靠“复制 dylib 进 .app”而非 rpath，Bin 根与两个 .app 内存在多份同名 dylib 且时间戳已出现不同步（10-31 17:30/17:42）；`Bin/GameSprout.app` 是旧 target 名的孤儿产物；`GameSproutRuntime/Tests/CMakeLists.txt:23` 测试输出目录用中文“加载 测试”；Qt 版本策略不一致（主工程 Qt5/6 兼容、Tests 仅 Qt6）；`VcpkgOverlayPorts` 为空目录但 cmake 仍传 `--overlay-ports`（死配置）；配置期网络依赖重（vcpkg 自举 + 运行时下载），离线不可配置。

---

## 5. 问题清单（按严重度）

### P1 · 高风险（建议立即处理）
1. **未提交大改 + 无版本协商**：141 文件接口横向变更（GRHI 收口 / RuntimeHost·AnimationPreview 拆分）未提交，壳与插件按名加载无版本握手，新旧二进制混装静默崩溃。→ 拆分提交、加版本握手、跑回归。
2. **测试从未运行**：Tests/ 13 套件无构建运行记录、Foundation/Tests 未接入 CMake、GRHI 测试默认 OFF、各工程 ctest 空跑。项目实际处于“测试代码存在但回归从未执行”的状态。
3. **Editor 初始化失败路径双重 Release**（GameSproutEditorFinal.cpp:161-168 + 17-27）：Foundation 可能被二次 delete。

### P2 · 中风险
4. **文档与实现脱节**：RHIArchitecture.md/RHIDevelopmentPlan.md 基于“自研 Metal 后端、尚未实现”的旧假设；实际为 SDL3 GPU 委托且已基本完成 D1–D33。RuntimeArchitecture.md:5 仍写“尚未创建实现模块”。
5. **Runtime 死 API 与静默吞异常**：OnInitialize/FixedUpdate 声明无调用点；组件 Update `catch(...){}` 无诊断（GameSproutRuntime.cpp:114）。
6. **无光照组件**：方向光/环境光硬编码在 Shader（Preview.hlsl:108-109），与文档契约不符；RenderUI 空桩但公共 API 已暴露（半成品接口）。
7. **插件按名字符串强转配对**：拼错即 UB，无类型校验；Release 顺序硬编码插件名；不可热卸载。
8. **Net 三平台 ~2,100 行重复代码**；无 DeviceCapabilities 能力查询 API。
9. **SDL 私有扩展在仓库外**（`/Users/max/Documents/GitHub/SDL`）：换机/CI 需重建本地 SDL，且 `GameSproutRuntime/.gitignore` 的 `/Build/` 依赖 macOS ignorecase 才覆盖 `build/`。
10. **构建编排与部署**：无根 CMakeLists/超级构建，顺序靠人工；SDL/Qt 为机器绝对路径；dylib 靠复制进 .app 部署（Bin 根与两个 .app 多份同名副本，时间戳已现不同步），有 stale 副本风险。

### P3 · 低风险
10. git 卫生：6 个 .dSYM/DWARF 产物被提交（~2.8MB）、缺 `__pycache__/` 忽略规则、根 .gitignore 含失效路径。
11. `porjects.json` 拼写；EditorExecutable 硬编码 GameSprout.app；宏 `GSRegInitliazedCallback`/“Initialize Faild”拼写；孤儿 `Bin/GameSprout.app`；测试输出目录中文路径“加载 测试”；Qt5/6 版本策略不一致；`VcpkgOverlayPorts` 空目录死配置。
12. SpinLock::ForceUnlock 危险逃生门、XPool 构造异常泄漏 chunk、XThreadPool 空壳、5 个空壳 Module 桩。
13. 对象 ID 单调 u64 溢出回 0 复用（GameSproutRuntime.cpp:18-21）；渲染每帧现算世界矩阵无缓存；无视锥剔除/透明排序。
14. Foundation 动态加载无 ABI 版本协商（GRHI 侧有）；ConsoleUI `_Dispatch` 空时静默丢日志。
15. 固定 16ms 主循环无 delta clamp；Hub 无防重复启动、无 i18n。

---

## 6. 改进建议（按优先级）

1. **先把 141 文件改动落地**：拆为“GRHI 收口 / RuntimeHost+AnimationPreview / Inspector UI”三个提交；提交前在本机构建并跑 Tests/AnimationIntegration、SceneDocument、SceneView、GameSproutRuntime 全部测试（历史上从未跑过）。
2. **让测试真正跑起来**：Tests/ 加聚合 CMakeLists（或脚本一键构建全 13 套件 + Runtime/GRHI/Foundation 测试）；Foundation/Tests 接入 CTest；GRHI 测试 CI 开 ON；补一条 CI（GitHub Actions，macOS 主验 + 交叉编译检查）。
3. **加版本握手**：插件导出 ABI 版本号或 manifest，壳加载前比对；Release 顺序改显式依赖声明替代硬编码名单；Release() 幂等化消除双重释放。
4. **文档对齐**：RHIArchitecture.md/RHIDevelopmentPlan.md 更新为 SDL3 GPU 委托路线并标注各步骤真实状态；RuntimeArchitecture.md 更新实现状态（动画已超范围完成、缺光照组件）。
5. **Runtime 修复**：删除或实现 OnInitialize/FixedUpdate；Update 异常写入诊断而非静默；增加 Light 组件或明确降级；RenderUI 与 Canvas 组件 API 二选一。
6. **架构债**：重构 Net 三平台公共层；补 DeviceCapabilities 查询；插件配对加 dynamic_cast 校验；`g_GameSproutModuleApi` 移入实例成员。
7. **卫生**：删除入库 .dSYM、补 `__pycache__/` 忽略、修 porjects.json（兼容读取旧名）、对齐 EditorExecutable 与 target 名、清理孤儿 `Bin/GameSprout.app`、把“加载 测试”目录改 ASCII、统一 Qt5/6 策略。
8. **构建编排**：新增根 CMakeLists/Presets/一键脚本编排 Foundation→GRHI→Runtime→Editor 顺序；把 SDL 获取（路径/commit/补丁）写入文档或脚本；评估 rpath 替代多份 dylib 复制。
9. **增强测试**：抽统一断言库或引入 Catch2；补黄金图像/像素对比验证 GPU 渲染；AnimationIntegration 的 FBX 样例资产入库。

---

## 7. 结论

GameSprout 是一个**进展显著快于文档记录**的自研游戏制作工具：RHI（SDL3 GPU + Metal 真机验证）、通用 Runtime（含超预期的完整动画系统）、编辑器插件框架、资产导入流水线与 Shader 工具链均已成型，Foundation 的 AGENTS.md 工程纪律（禁智能指针、显式所有权、显式线程协议）在全仓执行得相当彻底（第一方代码零 TODO/FIXME、零智能指针违规）。

当前最值得警惕的不是代码能力，而是**工程交付链的断点**：大规模接口重构悬在未提交状态且无二进制版本保护；近万行、3,100 处断言的测试代码从未被执行过；关键架构文档停留在“尚未实现”的旧状态。补齐这三块，项目即可进入以回归验证支撑快速迭代的稳定节奏。
