# GameSprout 通用 Runtime 架构讨论稿

> 本文是早期架构讨论稿。当前已实现的 Editor/Module、Runtime 与 GRHI 分工见 [EditorModuleBoundaries.md](EditorModuleBoundaries.md)。

状态：设计提案。当前仅实现 Metal，公共 RHI 接口面向 Vulkan、Direct3D 12 与桌面 OpenGL 做兼容性设计审查；Qt 在后续通过宿主适配接入。尚未创建实现模块。

## 1. 目标与边界

产品目标：用户通过文字描述，由 AI 调用体素工具制作模型和贴图；本地构建运行资产；GameSprout 集成通用 Runtime，在指定视口中展示真实 3D 模型。

当前工作顺序：先完成与窗口框架无关的通用 RHI 设计和独立测试宿主验证，再逐步构建上层 Runtime。Qt 接入后置，不作为 RHI 公共接口、依赖或当前验收条件。

当前 RHI 范围与跨后端契约见 [RHIArchitecture.md](RHIArchitecture.md)：Metal 是唯一当前实现，其他后端只做设计映射审查，不同时开发。

同一套 Runtime 将来可以集成进独立游戏盒子。复用的是运行代码、资源协议和宿主接口；不同平台仍需各自编译、适配后端并验证资源能力。

首个闭环只要求静态角色、材质、相机观察、基础光照、本地加载和资源更新。骨骼、物理、游戏规则、联网等在本稿中明确归属，按后续里程碑增加。

源资产保留真正的体素数据；Mesh 是运行构建产物。当前 Runtime 可以只加载 Mesh、贴图和材质，无须加载全部编辑数据。需要运行时挖掘、破坏时，再增加体素运行能力。

## 2. 四个产品边界

| 边界 | 职责 | 主要依赖 |
| --- | --- | --- |
| GameSprout 制作应用 | 对话、AI 调度、制作进度、撤销、源资产编辑 | 制作工具、Runtime 宿主接口 |
| 制作与构建工具 | 体素操作、表面提取、UV、贴图处理、资产打包 | 源数据协议、运行资产协议、基础库 |
| 通用 Runtime | 资源加载、World、运行系统、渲染 | 平台服务、图形后端 |
| 宿主适配 | 窗口、输入、渲染目标、帧回调、生命周期转换 | Qt 或其他窗口/平台实现 |

Runtime 核心不依赖 Launcher、Account、制作聊天界面或具体 View/Module 注册表。通用 RHI 的公共接口与原生图形后端不依赖 Qt、QRhi、GLFW 或 SDL；窗口框架只通过外部适配层提供平台 Surface。后续 Qt 适配层可以依赖 Qt Widgets，但该依赖不进入 RHI 核心。

云端构建、发布、审核、账号与游戏商店属于平台或制作服务，不纳入 Runtime 内核。联网游戏的传输和状态同步属于运行扩展，与账号、商店服务分开。

## 3. 模块地图

“首版”表示文字生成静态角色并显示的闭环。“后续”表示本次明确职责，不立即创建空实现。以下是逻辑模块，不要求逐个建立动态库。

| 层 | 模块 | 职责 | 首版范围 |
| --- | --- | --- | --- |
| 基础 | Core / Math | 错误、日志、ID、数学、坐标约定、时间、必要的内存工具与事件队列 | 必需 |
| 基础 | Platform Services | 文件读取、路径、单调时钟、任务执行等平台能力的接入 | 文件、时钟；任务能力优先复用现有基础设施 |
| 协议 | Contracts / Asset Formats | 运行资产类型、版本、依赖、能力声明、资源 ID | 必需，具体载荷按类型扩展 |
| 资源 | Resource System | 读取、解析、校验、依赖、缓存、引用、取消、版本替换 | 模型、材质、贴图 |
| 对象 | Entity / Component | 对象身份、组件数据、增删与查询、失效检测 | 简单实现，不预建复杂 ECS 调度器 |
| 对象 | World / Scene / Transform | 运行世界、场景实例化、父子变换、对象生命周期 | 一个 World、一个场景实例 |
| 运行 | Runtime Orchestrator / Systems | 初始化、更新顺序、固定步进入口、暂停、关闭 | 单线程明确顺序、变量时间更新 |
| 运行 | Input / Actions | 平台事件到运行输入和动作的映射、焦点、捕获 | 相机观察所需输入 |
| 显示 | Camera / RenderView | 视图、投影、裁剪、输出尺寸、绑定渲染目标 | 一个透视相机、一个视口 |
| 显示 | Mesh / Texture Resources | CPU 运行数据及其布局、渲染侧 GPU 实例化 | 静态模型和普通颜色贴图 |
| 显示 | Material / Shader | 材质定义与实例、参数、纹理槽、内置 Shader | 基础无光照与受光材质 |
| 显示 | Lighting | 光源参数、环境光、后续阴影数据与策略 | 环境光与方向光；阴影可在基础显示后增加 |
| 显示 | Renderer / Render Pipeline | 从 World 提取显示数据、可见性、排序、Pass、绘制提交 | 简单前向绘制流程 |
| 显示 | RHI / Graphics Backend | GPU 设备、缓冲、贴图、采样器、绘制管线、命令和同步 | 一个后端；按实际绘制补齐接口 |
| 显示 | Surface / Host Bridge | 原生 Surface、窗口适配、尺寸/DPI、帧调度、设备重建通知 | 独立最小测试宿主；Qt 适配后置 |
| 动作 | Animation | 骨架、动作资源、采样、混合、部件刚性绑定 | 后续 |
| 物理 | Physics / Collision | 碰撞形状、查询、刚体、触发器、固定步进 | 后续；可对接成熟库 |
| 行为 | Gameplay / Rules | 受约束规则、事件、计时器、游戏状态；以后按需接脚本 | 后续 |
| 体验 | Audio | 音频资源、播放、空间声源与监听器 | 后续 |
| 体验 | Game UI | 游戏内 HUD、文本、布局和游戏输入交互 | 后续；与 Qt 编辑器 UI 分离 |
| 体验 | Effects / Particles | 粒子、简单视觉效果、后处理的资源与运行状态 | 后续；绘制阶段由 Renderer 组织 |
| 世界扩展 | Voxel World / Streaming | 运行时体素、区块加载、编辑、重建与碰撞更新 | 后续，静态体素角色不依赖此模块 |
| 游戏扩展 | Navigation / Character | 路径查询、移动控制、角色运动约束 | 有对应玩法需求后增加 |
| 游戏扩展 | Networking / Replication | 会话传输、对象同步、插值、状态权威 | 联机目标确定后设计 |
| 状态扩展 | Save / World Persistence | 保存运行状态、恢复对象引用、格式迁移 | 后续；首版只需重载已有资产 |
| 工程 | Diagnostics / Tests | 日志、资源/绘制统计、CPU/GPU 耗时、加载和生命周期检查 | 从首版开始 |

场景/预制体序列化和运行存档是不同用途：前者描述初始布局，后者记录游玩后的状态。首版可以只实例化一个模型；不必先完成通用场景编辑器或存档系统。

## 4. 依赖方向

下面箭头表示“使用”，不是强制构建成一条线。

```text
GameSprout / 独立查看器 / 未来游戏盒子
    ├── Host Bridge ──> Surface + Input Events
    └── Runtime Facade / Orchestrator
            ├── Resource System ──> Asset Formats + File/Task Services
            ├── World ──> Entity/Component + Transform
            ├── Animation / Physics / Gameplay ... ──> World + 所需服务
            └── Render Extraction ──> World + Resource Handles
                      ↓
                Render Frame Data
                      ↓
                Renderer ──> Material/Shader + GPU Resource Cache
                      ↓
                     RHI ──> Graphics Backend

基础数据、数学和协议由上层使用，不反向依赖业务模块。
```

约束：

- RHI 不认识 Entity、Scene、角色、体素和 AI。
- Resource System 的公共接口使用逻辑资源 ID/句柄，不暴露 Metal 或其他原生图形对象。
- 渲染侧根据 CPU 资源建立 GPU 缓存，资源上传通过 RHI 执行。
- World 保存组件和运行状态；Renderer 消费提取后的数据，不持有可随时失效的组件指针。
- Physics 与 Renderer 分别使用 World；不彼此调用来维护对象。
- 动画、物理和规则对 Transform 的写入有明确阶段与权威来源。
- 当前 Module/View 插件只作集成入口，不成为 Runtime 的基础依赖。

## 5. Entity、Component、System、World 与 Scene

| 概念 | 本设计中的含义 |
| --- | --- |
| Entity | World 内的对象身份，建议采用索引与代数值，删除后旧句柄失效 |
| Component | 与对象关联的数据，例如 Transform、ModelRenderer、Camera、Light |
| System | 处理某类组件的逻辑，例如变换传播、动画、物理和渲染提取 |
| World | 拥有 Entity、Component 和运行系统的容器 |
| Scene Asset | 可序列化的初始对象布局和资源引用 |
| Scene Instance | 将 Scene Asset 实例化进 World 后的一组对象 |

首版可用简单组件存储、直接查询和显式调用；是否采用外部 ECS 库及更复杂存储留到实现评估。先确定职责，不预先实现反射、通用编辑器、复杂 Job Graph 或每组件动态插件。

Transform 的父子层级是组件中的关系，不是另一套对象身份系统。世界变换在统一阶段计算，并检测非法父子循环。

示例：

```text
World
├── Bear Entity
│   ├── TransformComponent
│   └── ModelRendererComponent -> ModelAsset / Material references
├── Camera Entity
│   ├── TransformComponent
│   └── CameraComponent
└── Sun Entity
    ├── TransformComponent
    └── DirectionalLightComponent
```

体素小格子不是 Entity。静态角色可以由一个 Entity 引用包含多个部件的模型；只有需要独立交互或生命周期的部件才额外实例化为 Entity。骨骼也不要求与每个体素或 Entity 一一对应。

## 6. 资源、材质、Shader 与 RHI 的边界

```text
资产包中的 TextureAsset / MeshAsset / MaterialAsset
    ↓ Resource System 加载、校验
CPU 资源对象与逻辑句柄
    ↓ Renderer 解析依赖，安排上传
GPU 缓存与后端句柄
    ↓ RHI
GPU 资源
```

- MeshAsset：顶点布局、顶点/索引、子网格、包围盒与部件变换。
- TextureAsset：尺寸、颜色空间、格式和像素数据或明确支持的编码。
- MaterialAsset：内置材质/Shader 标识、纹理槽、参数、透明策略。
- Material Instance：对资产参数的运行时覆盖，不直接修改共享资产。
- Shader：定义 GPU 程序、资源绑定布局与所需变体；首版使用内置、构建期处理的 Shader。
- Graphics Pipeline：把 Shader、顶点布局和深度/混合/剔除等状态组装为 GPU 绘制对象。
- Renderer：决定 Pass、相机、光源和绘制顺序。
- RHI：执行图形资源和命令操作，不决定宇航服或木头的含义。

材质逻辑定义不依赖具体 RHI 类。Shader 反射、变体生成和目标平台编译属于构建工具；Runtime 加载其兼容产物并创建管线。

运行资源清单至少包含：格式版本、资源 ID、依赖、目标能力/格式和内容标识。逻辑 AssetId、资源版本、GPU 句柄与 EntityId 分别定义，不能混用。

## 7. 窗口、RenderView 与设备所有权

Runtime 支持外部指定输出。RHI 先通过独立最小宿主验证：宿主负责窗口和事件循环，平台 Surface 适配提供可呈现目标；RHI 也支持独立的离屏纹理目标。公共接口不接收 QWidget、GLFWwindow 或 SDL_Window 指针。

```text
独立测试宿主 / 后续 Qt 宿主
    ↓ 平台 Surface 适配
Surface -> RHI SwapChain -> Render Target
                              ↑
                        Renderer / RenderView

离屏模式：Device -> Texture Render Target，不要求显示窗口。
```

RenderView 保存相机选择、像素尺寸、输出目标描述和显示选项。World 与 RenderView 分离，将来可让多个相机观察同一 World；首版只实现一个视口。

需要区分两种图形接入：

1. 宿主借出设备、命令上下文和渲染目标；宿主管理提交和呈现。
2. 后端根据原生 Surface 创建并拥有设备或交换链，管理提交和呈现。

首版采用第二种模式：宿主提供原生 Surface，RHI 后端管理图形设备、提交与呈现。外部设备/命令上下文导入留待真实宿主需求出现后再设计，不作为首版必须实现的能力。宿主拥有窗口，RHI 拥有其创建的图形对象；释放满足后端的 GPU 使用完成要求。

宿主适配负责：逻辑尺寸与物理像素、DPI 变化、隐藏/最小化、零尺寸、窗口重建和关闭通知。RHI 负责对应的交换链/目标变化和资源生命周期。不同设备的 GPU 资源不能直接共享，未来多窗口支持需按设备分别实例化资源。

### 7.1 通用 RHI 的职责划分

| 子模块 | 职责 |
| --- | --- |
| Device / Capabilities | 创建设备、选择后端、查询格式/限制/可选能力，报告不支持的请求 |
| Surface / SwapChain | 从平台 Surface 创建呈现链，获取当前目标、调整尺寸和呈现 |
| Buffer / Texture / Sampler | 资源描述、创建、上传、更新、视图与销毁 |
| Shader / Binding Layout | 加载目标后端 Shader 产物，声明绑定布局；编译工具链与核心分开 |
| Resource Bindings | 绑定缓冲、贴图和采样器；保持跨后端明确语义 |
| Graphics Pipeline | Shader、顶点布局、光栅化、深度/模板、混合和附件格式 |
| Commands / Render Pass | 记录绘制/拷贝、声明附件读写和资源访问，处理所需状态转换与依赖 |
| Queue / Synchronization | 提交、完成通知、帧资源复用和延迟销毁；首版从单图形队列开始 |
| Validation / Diagnostics | 参数、用法、句柄和生命周期检查，以及后端验证信息接入 |

首版只实现 Metal 后端，同时对照 Vulkan、Direct3D 12、桌面 OpenGL 审查公共接口。资源绑定、Pass、访问同步、Shader、能力、坐标及 Surface 契约先确定，避免 Metal 特性泄漏到公共层。Compute、光线追踪、Bindless、异步多队列等能力后续按需求增加，不默认承诺所有后端一致支持。

原生 Surface 是后端可识别、带明确平台类型与生命周期约定的描述，不是可以任意强转的窗口框架对象。平台专用声明限制在 Surface 适配入口，不传播到缓冲、管线和命令等公共接口。

RHI 不负责资产文件解析、模型格式、材质业务、相机、光照、输入或 World。其输入是资源描述和绘制命令，其输出是目标附件及呈现结果。

## 8. 更新顺序与线程

首版明确区分 Update 与 Render。宿主拥有事件循环；每次 World 更新由宿主按实际经过时间调用一次，渲染在视口允许的图形回调中执行。多视口绘制不重复推进同一个 World。

首版顺序：

```text
接收输入及制作结果
    ↓
提交已完成的资源/对象变更
    ↓
更新相机与 World 状态
    ↓
计算父子世界变换
    ↓
提取 Render Frame Data
    ↓
在合法图形回调中上传资源并绘制
```

后续加入运行系统时，扩展为：

```text
输入动作 -> 游戏逻辑
    ↓
固定步进：角色/运动意图 -> 物理模拟 -> 回写物理对象变换
    ↓
动画采样、非物理部件姿态、显示插值
    ↓
世界变换、相机、音频与渲染提取
```

固定步进使用时间累积和每帧最大补步限制；动态刚体的世界位姿由物理负责，运动学对象将目标提交物理。Root Motion、布娃娃等需求出现时再增加明确的动画/物理同步阶段，避免多个系统随意覆盖同一变换。

首版 World 与图形回调在宿主指定线程上工作。文件读取、解码、体素构建可在工作线程执行，结果通过队列提交；工作线程不直接修改 World 或 Qt 控件，也不擅自调用图形 API。

## 9. 资源更新与生命周期

Runtime 生命周期：Create -> AttachHost/Graphics -> Load -> Update/Render -> Stop -> Detach -> Destroy。

设备重建时保留可恢复的 CPU 资产状态，销毁或失效对应 GPU 缓存，再按新上下文创建。设备丢失的自动恢复程度按后端能力逐步实现，首版至少能明确报错并安全关闭。

资源状态建议：Unloaded -> Loading -> Ready 或 Failed。准备替换时建立候选版本，完成依赖与 GPU 上传后，在安全边界切换可见版本。候选失败保持已有版本；过期任务通过请求 ID/版本检查丢弃。

相同资产可以被多个 Entity 引用，但不同对象的 Transform、动画播放状态和材质覆盖各自独立。GPU 资源的释放等待不再被在途帧使用。

关闭时停止新请求、取消/收束后台结果、停止绘制、解除目标绑定并按所有权释放资源。具体 Qt 析构/回调时机在适配实现中验证，不能依赖全局单例退出时碰巧正确。

## 10. 制作侧体素模块

体素算法不并入 RHI。建议把可复用算法组织为独立库：

```text
Voxel Source Types
├── 部件局部网格、体素尺寸、占用和材质标记
├── Fill / Erase / Mirror / Combine
└── Surface Extraction -> Mesh Data
```

制作应用和 AssetBuilder 使用它。以后运行时破坏系统可以复用其中必要算法，而静态模型展示不强制携带整套编辑器。

首版按部件提取表面，不跨可独立运动的部件剔除或合并拓扑。先做隐藏面剔除；以后增加贪心合并时，合并条件包含材质、方向、UV 和所需着色属性，避免破坏贴图。

源模型到贴图必须有确定映射：材质区域/面标识 -> UV 模板 -> AI 贴图 -> 校验与绑定。普通平铺材质和角色脸/衣服的专用贴图分别处理；图像生成不保证自动符合模板，需要真实样例验证。

前面讨论过 0.25m/0.5m 与 Minecraft 常用尺度，两者尚未作为实现标准最终确认。资产协议显式记录单位和 voxelSize；实现前确定一套默认值，并固定坐标系、朝向、原点和颜色空间约定。

## 11. 建议源码组织

以下仅为目录提案，不立即创建全部目录或空类。

```text
GameSprout/
├── Runtime/
│   ├── Core/              # 基础类型、时间、诊断
│   ├── Contracts/         # 资源与公共接口协议
│   ├── Assets/            # 读取、依赖、缓存和资源类型
│   ├── World/             # Entity、Component、Scene、Transform
│   ├── Rendering/         # Camera、Material、Light、提取与绘制
│   ├── RHI/               # 图形接口与后端
│   ├── Systems/           # 按需加入 Input、Animation、Physics 等
│   └── Runtime/           # 对外入口、生命周期与调度
├── RuntimeHosts/
│   ├── NativeSample/      # 先验证通用 RHI 的最小独立宿主
│   └── Qt/                # 后续实现的 Qt 视口与输入适配
├── Shared/
│   └── Voxel/             # 体素源数据和算法
├── Tools/
│   └── AssetBuilder/      # 本地构建，未来可供云端复用
├── Module/                # 现有制作侧业务模块；以后接入 AI 编排
├── View/                  # 现有 Qt UI；以后加入制作界面
└── Samples/
    └── RuntimeViewer/     # 不依赖制作业务的资产查看程序
```

先独立建立 RHI 和最小测试宿主，再按闭环补充 Core/Contracts、Assets、World、Rendering、Runtime，以及 Voxel/AssetBuilder。QtHost 在后续接入时增加。可以适度合并编译目标，但保留代码依赖边界。

优先检查并复用现有 Foundation 的数学、文件、日志和任务能力，避免复制基础设施。Runtime 不整体依赖数据库、账号或完整网络服务集合。

现有 Launcher 的 Module/View 动态插件框架负责制作应用业务，Runtime 不继承 IGameSproutModule/IGameSproutView。后续集成模块通过组合持有 Runtime，再由 QtHost 适配视口；当前 RHI 测试程序不通过 Launcher 启动。

## 12. 分阶段填空与验收

| 阶段 | 填充模块 | 验收 |
| --- | --- | --- |
| A RHI 协议与约定 | 公共描述、能力查询、Surface 和图形资源所有权 | 固定首后端、格式/坐标约定、绑定与同步语义及测试样例 |
| B 独立图形通路 | 最小原生宿主、一个 RHI 后端、Shader、缓冲、纹理、绘制与同步 | 独立程序显示有贴图的方块；支持尺寸变化和安全关闭；离屏目标可读回验证 |
| C 通用模型运行 | Asset Formats、Assets、Entity/Component、Transform、相机、材质与光照 | 同一资源包在独立查看器加载；后续接入 GameSprout 时不改变资源协议 |
| D 体素制作与构建 | Voxel、AssetBuilder、UV/材质映射、资源替换 | 固定体素命令生成角色，重新构建后更新显示，失败保留旧版本 |
| E AI 闭环与应用集成 | 制作协议、模型/图像能力适配、用户进度、QtHost | 文字生成体素模型和贴图，在 GameSprout 指定视口展示，重启后可重载 |
| F 后续能力 | Animation、Physics、Gameplay、Audio、Game UI 等 | 每个功能按实际玩法单独设置目标与验收 |

有针对性的验证包括：体素表面提取、资源缺失/版本不匹配、Entity 旧句柄失效、父子变换、同一资产多实例、过期构建结果、窗口重建/关闭，以及同包双宿主加载。帧率和内存目标需绑定实际硬件、分辨率与样例规模，当前不凭概念图承诺。

## 13. 实现前需要定版的决策

1. 默认单位、角色体素尺寸、地图块尺寸、坐标系与正面方向。
2. Metal 首个验收平台/设备范围；明确面向 Metal、Vulkan、D3D12、桌面 OpenGL 的绑定、同步、Shader、Surface、能力及所有权契约，Qt 接入后置。
3. 源模型的部件/材质标记协议、运行资产 v1 和贴图映射方式。
4. 简单自有组件存储或外部 ECS 库的实现取舍；公共职责保持一致。
5. 第一个验收样例的模型规模、贴图大小与材质能力。

先定协议与最小验证样例，再逐项实现。不需要为尚未出现的物理、网络和脚本需求提前冻结全部 API。

## 14. 自研 RHI 的工作日估算

当前逐步协作排期以 [RHIDevelopmentPlan.md](RHIDevelopmentPlan.md) 为准：macOS / Metal 主计划目标 45 个工作日，包含 6 日余量；iOS 适配与验收另列。下表保留工作包粗估区间。

这是基于当前设计的初步项目估算，不是已测得的开发速度或交付承诺。假设一名熟悉 C++ 与现代图形 API 的负责人配合 AI 持续实现和验证，工具链与测试设备可用；按有效工程工作日计算，不包含设备采购、账号/证书等待和排队审核。AI 的加速已包含在下表中，不再另乘固定折扣。

“健全”限定为：覆盖体素游戏的常规图形需求，有明确资源/同步语义，运行资源可以持续更新，生命周期经过验证，并且在声明支持的平台上完成实测。它不代表具备 Unity 的全部图形功能，也不代表无限未来需求都已实现。

图形能力至少覆盖：顶点/索引与动态参数缓冲、贴图与采样、常用格式和 mip、资源上传/读回、绘制与实例化绘制、离屏/深度目标、多 Pass 及其访问依赖、Shader/绑定布局/管线、提交同步和延迟销毁。这些能力供上层实现阴影、角色动画绘制、体素区块更新、透明物体、HUD 和基础效果；实际动画、光照和 UI 逻辑由上层负责。

自研 RHI 可以使用系统 SDK、现有 Shader 编译工具和必要的基础库，不意味着同时自研 GPU 驱动、Shader 编译器或完整渲染引擎。

### 14.1 单个 Metal 后端

| 工作包 | 工作日 | 交付内容 |
| --- | --- | --- |
| 公共接口、约定与基础构建 | 3–5 | 描述、能力、句柄、所有权、独立构建与测试入口 |
| GPU 资源与上传 | 5–7 | 缓冲、贴图、采样、资源视图、动态更新和读回 |
| Shader、绑定与管线 | 4–6 | 工具链接入、参数绑定、顶点布局及绘制状态 |
| 命令与 Pass | 4–6 | 绘制/实例化/拷贝、颜色/深度目标及多 Pass 依赖 |
| 同步与生命周期 | 5–8 | 在途帧、资源复用、完成通知、延迟销毁和错误路径 |
| Surface 与离屏接入 | 3–5 | 独立原生宿主、呈现、尺寸变化和关闭 |
| 验证、统计与稳定性修复 | 5–8 | 场景样例、反复更新/创建/销毁、长时间运行和问题定位 |
| 基础合计 | 29–45 | 单后端基线能力 |
| 集成返工与风险余量 | 6–10 | 根据实际失败和性能结果修正 |
| 计划预算 | **35–55** | 单个 Metal 后端的健全首版 |

这些是相互依赖的工作包；相同开发配置下不应假设可完全并行。最早几天出现画面并不等于完成同步、资源管理和稳定性验收。

### 14.2 多平台范围

以下是后续扩展范围的历史粗估，不是当前已安排开发内容。当前只实现 Metal，适用单后端预算；iOS/iPadOS 复用 Metal 后端，但需要独立的宿主适配与真机验收。

| 增量范围 | 增量工作日 | 累计工作日 |
| --- | --- | --- |
| Metal 单后端 | 35–55 | 35–55 |
| 增加 Vulkan 与桌面平台验证，修正跨后端公共语义 | 20–30 | 55–85 |
| 增加约定范围的 iOS / Android 真机适配与回归 | 10–15 | **65–100** |

第二个后端用于检验接口通用性，不能只按机械翻译 API 调用估算。上述移动范围须在开工前确定 OS、设备与 GPU 覆盖；广泛机型兼容测试会增加工作量。Direct3D 12、Web、主机、外部设备导入、多异步队列、Bindless、光线追踪等不包含在本次预算，按需求单独评估。

不包含上层 Renderer、资源包加载、体素算法、AI、场景/ECS、动画、物理、游戏规则、Qt 集成和发布平台。RHI 层测试可以使用专用图形样例验证相关底层能力。

当前协作排期在 D5 完成公共契约与映射审查，在 D7 用独立 Metal 图形样例完成第一次校准，重点验证环境、资源接口和 Surface 路线。其他后端的实现和设备覆盖扩展在后续单独立项、校准。
