# 通用 RHI 设计基线：先实现 Metal

状态：公共语义与后端兼容性设计稿，尚未创建 RHI 实现。本文记录当前范围与接口审查标准，不代表已支持任何后端。

## 1. 已确定的方向

- 本阶段只实现 Metal 后端，先在 macOS 验证；iOS/iPadOS 复用核心，但须另做宿主适配、编译和真机验收。
- 公共接口同时对照 Metal、Vulkan、Direct3D 12、桌面 OpenGL 设计。用户所说的 DX 暂按 Direct3D 12 作为设计目标；D3D11 不自动包含。
- Qt 接入后置。RHI 不依赖 Qt 或具体窗口框架。
- 平台 Surface 适配与图形后端分离；既可向窗口呈现，也可渲染至离屏目标。
- 后续增加后端的目标是新增实现、Shader 编译目标和平台适配，保持上层 Renderer 与资源协议稳定。
- 在真实后端实现和设备验证之前，只能声明“设计已审查”，不能声明“已支持”或承诺所有接口永远无需修订。

## 2. 避免 Metal API 改名式封装

公共头文件不出现 MTL 对象、Vk 对象、D3D 接口、GL 对象 ID 或平台图形头文件。后端对象由私有实现持有；必要的原生互操作入口单独隔离，不成为日常绘制依赖。

公共抽象表达引擎需要的行为，例如资源用途、访问、附件和提交，而不是复刻 MTLRenderCommandEncoder 的方法集合。

```text
上层 Renderer
      ↓
通用描述、句柄、能力与命令契约
      ↓
Backend 实现接口
├── Metal          本阶段实现
├── Vulkan         只做设计映射审查
├── Direct3D 12    只做设计映射审查
└── OpenGL         只做设计映射审查
```

不能用最低公共能力限制全部后端。设计采用“基础能力档 + 可查询扩展”，上层显式选择受支持的路径。

## 3. 公共模块

| 模块 | 公共语义 |
| --- | --- |
| Instance / Adapter / Device | 枚举或选择设备、创建后端、返回能力与限制；可按需简化首版对象数量 |
| Surface / SwapChain | 类型明确的原生 Surface、获取当前呈现目标、尺寸更新、呈现结果 |
| Buffer / Texture / Views | 类型化资源描述、用途、格式、大小、子资源与视图 |
| Sampler | 过滤、寻址、LOD、比较采样等参数，能力不足时明确报错 |
| Shader / Reflection | 阶段、入口、后端产物及统一资源绑定元数据 |
| Binding Layout / Bind Group | 逻辑资源布局及资源集合，定义数组、可见阶段、范围和动态偏移 |
| Graphics Pipeline | Shader、顶点格式、拓扑、光栅化、深度/模板、混合与附件兼容性 |
| Command Recording | 有序记录绘制、拷贝及资源访问，不要求一定存在原生录制对象 |
| Render Pass | 附件、Load/Store、Clear、Resolve 和明确禁止/可选的反馈访问 |
| Resource Access / Barriers | 资源范围、访问类型、阶段及依赖/转换规则 |
| Queue / Submission | 提交、完成值、帧复用及后续可扩展的等待/通知描述 |
| Validation / Diagnostics | 参数、句柄、布局、访问和生命周期检查，后端错误与统计 |

Compute、Bindless、光线追踪、稀疏资源、异步多队列等作为独立能力扩展，不预先填充无语义的空接口。基础提交描述保留显式同步概念，避免以后只能通过 WaitIdle 扩展。

## 4. 必须先明确的跨后端契约

### 4.1 资源与内存

- 资源逻辑用途与底层分配分开，描述 Vertex、Index、Uniform、Sampled、Storage、Attachment、Copy 等用途组合。
- GPUOnly、Upload、Readback 表达访问意图，不将 Shared、Managed、Coherent 等后端机制直接作为公共要求。
- 不假设 CPU 总能直接访问 GPU 资源；提供上传、拷贝和完成后读回路径。
- 明确写入时数据何时被复制或借用、借用到何时，Map/Unmap 的可用范围及 Flush/Invalidate 由什么层负责。
- 同步和视图以显式的 mip、array layer、aspect、buffer range 表达，不能把整个贴图永远当作单一状态。
- 明确行距、拷贝对齐、压缩块约束、缓冲偏移对齐；通过 Limits/FormatCapabilities 查询。
- 公共句柄必须区分资源类别与所属 Device，旧句柄可检查失效，不依赖原生对象地址。

### 4.2 绑定模型

统一表示逻辑资源，例如 group + binding + resource type + shader stages。纹理与 Sampler 分开描述；组合采样器所需关系通过编译元数据提供。

后端负责映射：

| Metal | Vulkan | Direct3D 12 | OpenGL |
| --- | --- | --- | --- |
| 各阶段资源索引或按能力使用 Argument Buffer | Descriptor Set / Layout | Root Signature 与 Descriptor Table 等 | Uniform/Storage Buffer 绑定点、纹理单元与 Sampler |

这是预期映射方案，不意味着各后端机制完全等价。需要记录数组长度、动态偏移、布局一致性、更新时机、绑定上限与资源存活要求。

禁止把 Metal 的 setVertexBuffer/setFragmentTexture 索引约定直接变成所有后端的 Shader 资产协议。首版可以只支持固定大小布局；运行时数组与 Bindless 作为可选能力。

### 4.3 命令、Pass 与同步

- 绘制命令具有统一的顺序和数据捕获语义，定义资源何时固定、记录后能否修改、何时允许复用。
- 首版从单图形队列和有限在途帧开始，不要求所有后端支持原生多线程录制。
- RenderPassDesc 明确颜色/深度/模板附件、Load/Store、Clear、Resolve、采样数和子资源范围。
- 公共同步模型至少表达访问类型、阶段、子资源范围和依赖边界；Metal 后端即使在某些情况下不需要额外命令，也不能省略这些公共语义。
- 在实现前定版命令内与提交间的状态规则：调用方声明的访问、转换边界、RHI 追踪与验证责任，不能有时自动、有时要求调用方猜测。
- 同步正确性不能依靠每帧 WaitIdle；等待全部 GPU 完成仅用于明确的关闭、诊断或重建路径。
- 某些附件读取、读写反馈或 Shader Storage 用法可以限制为可选能力；不做错误的静默兼容。

OpenGL 后端的设计路径是记录/回放命令到受控 Context，并按公共依赖插入必要同步，而不是要求 GL 提供原生 Metal 式 CommandBuffer。一次资源绑定或绘制不应依赖调用方留下的未知 GL 状态。

### 4.4 Pipeline、坐标与颜色

- 公共 PipelineDesc 描述完整必要状态；后端可创建原生 PSO，也可保存状态并在执行时应用。
- 动态状态集合明确列出，不能默认某个后端允许全部动态修改。
- 固定一套 RHI 裁剪深度、Viewport/Scissor 原点、正面绕序及贴图坐标约定，由后端或 Shader 编译目标适配差异。
- sRGB 与线性格式、附件格式、深度格式、MSAA 和 Resolve 支持通过格式能力查询，不依赖某台 Mac 的默认行为。
- 渲染管线的附件格式、采样数和绑定布局兼容性可检查，不能错误复用缓存。

具体坐标数值与必需格式在公共接口定版时列出；不要将坐标差异分散到角色、材质和相机业务代码中。

### 4.5 Shader 工具链

- 运行接口接收目标后端兼容的产物与统一反射元数据，不把 MSL、SPIR-V、DXIL 或 GLSL 任意一种规定为所有后端的直接输入。
- 统一源语言与编译工具另行评估；允许调用成熟编译/转换工具，不自研编译器。
- 同一源 Shader 在各目标的资源布局、入口、数据结构内存布局和矩阵约定必须可验证。
- Shader 包按目标、版本和能力区分，缓存键包含编译目标及相关配置。
- 增加后端可能需要重新构建运行产物；稳定的是逻辑资产和公共调用语义，不是一个 Shader 二进制跑遍所有后端。

### 4.6 能力与不支持的行为

DeviceCapabilities 包含功能开关、资源/绑定上限、对齐和格式用法支持等。请求超限必须给出明确错误，不静默忽略。

基础能力档按体素游戏需要定义，另列扩展。例如纹理数组、Storage Buffer、Compute、Indirect Draw、Timestamp Query 等分别确认是否为基础或扩展，不能因为某个 Metal 设备支持就默认所有平台必需支持。

“OpenGL 兼容”需要约定桌面 Core 版本/扩展基线；不代表同时覆盖旧式 OpenGL、OpenGL ES 与 WebGL。当前先完成桌面 OpenGL 的设计映射，具体版本在接口定版时明确。

## 5. 后端映射审查表

每个基础接口进入 Metal 实现前，都要记录四项：公共语义、Metal 映射、其余目标映射、限制/测试。

| 公共概念 | Metal | Vulkan | Direct3D 12 | OpenGL 设计路径 |
| --- | --- | --- | --- | --- |
| Device | Metal Device | Logical Device | D3D Device | 设备能力与受控 Context |
| Buffer / Texture | 对应资源 | Resource + Memory | Resource + Heap | Buffer / Texture 对象 |
| Binding Layout | 索引映射/可选 Argument Buffer | Descriptor Layout | Root Signature 等 | 绑定点映射 |
| Graphics Pipeline | Pipeline + 其他必要状态 | Graphics Pipeline | PSO + 必要状态 | Program + 状态对象描述 |
| Render Pass | Encoder / Pass Descriptor | 相应渲染与附件描述 | 附件绑定与操作 | FBO、清除/保存语义及受控绘制 |
| Command Recording | CommandBuffer / Encoder | CommandBuffer | CommandList | 有序回放与状态管理 |
| Resource Dependency | 按使用与模式插入所需同步 | Barrier / Access / Stage 等 | Resource Barrier 等 | 对应依赖的屏障/顺序处理 |
| Submission Complete | 提交完成机制 | Fence / 相应同步机制 | Fence | Sync 与受控回放完成机制 |
| Present | Layer / Drawable | Surface / SwapChain | SwapChain | 平台 Surface / SwapBuffers |

此表是设计审查入口，不是所有命令的一一对应表。例如附件反馈、队列能力和可重用录制的语义仍须逐项限制或补充。

## 6. 这阶段如何验证兼容设计

本阶段不实现 Vulkan、D3D12、OpenGL。采用以下审查与测试减少未来返工：

1. 先写只依赖公共接口的样例，Metal 实现不得要求样例调用 Metal 私有扩展才能完成基础功能。
2. 为每个基础接口完成四后端映射审查，发现无法映射的能力时明确为扩展或调整公共语义。
3. 在公共层提供验证能力；测试代码可使用记录器/Mock 检查命令、生命周期和依赖，不因此提前建立一个声称支持真实 GPU 的假后端。
4. 真实 Metal 测试覆盖贴图模型、深度、实例化、离屏后采样、深度采样、资源动态更新、上传/读回、反复创建销毁及呈现目标重建。
5. 为基础能力建立后端无关的测试样例，未来后端接入时复用，并允许合理的浮点/图像误差；不要求逐像素完全一致。
6. CI 或头文件检查确认公共接口可在不含 Metal/Apple SDK 的编译环境中被解析；实际运行测试只声明 Metal 结果。

只能通过另一个真实后端最终验证跨后端语义。当前验收应明确区分“公共设计已完成映射审查”和“某后端已实现并通过运行测试”。后续发现遗漏时优先以有版本的扩展或局部修订处理，避免上层重写。

## 7. 当前工作顺序

1. 选择基础能力档与目标 API 范围，完成资源、绑定、Pass、同步、Shader、坐标及 Surface 的公共契约。
2. 逐项完成四后端映射表与后端无关样例设计。
3. 建立公共头文件、独立构建和验证入口，再实现 Metal 后端。
4. 用样例验证资源更新和长期生命周期，完成单后端基线。
5. 后续按项目需要增加其他后端，不为其实现提前占用当前开发范围。

RHI 首版工期仍以健全单 Metal 后端的 35–55 个工作日作为粗估；跨后端接口审查属于该阶段的设计与验证工作。iOS/iPadOS 真机范围需要单列验收，本阶段不使用此前多后端的 65–100 日作为当前排期。

按你我协作拆分的基准排期见 [RHIDevelopmentPlan.md](RHIDevelopmentPlan.md)：39 日实施与验证 + 6 日返工余量，目标 45 个工作日；iOS 真机验收单列追加。

## 8. 参考

- [Vulkan 同步指南](https://docs.vulkan.org/guide/latest/synchronization.html)：同步与资源访问依赖需要显式处理。
- [Direct3D 12 资源绑定](https://learn.microsoft.com/en-us/windows/win32/direct3d12/resource-binding)：绑定布局需考虑 Descriptor、Descriptor Table、Heap 与 Root Signature。
- [Sokol 公共图形封装](https://github.com/floooh/sokol)：可参考跨 API 资源、管线和 Pass 的抽象方式；不作为本项目已选依赖。
- [bgfx 公共接口](https://github.com/bkaradzic/bgfx/blob/master/include/bgfx/bgfx.h)：可参考能力查询、句柄、目标和资源描述；不作为本阶段后端实现。
