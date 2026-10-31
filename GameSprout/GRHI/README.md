# GRHI

GRHI 使用 SDL3 GPU 实现设备层。公共头文件不依赖 SDL，`DeviceSDL` 私有持有
`SDL_GPUDevice`。公共接口只选择物理显卡：`CreateDevice()` 使用默认显卡，
`CreateDevice(adapterId)` 指定显卡。图形后端由 SDL 按平台、驱动和支持的
Shader 格式自动选择，调用方不指定后端。

## 构建与验证

先独立构建带 GRHI 显卡扩展的本地 SDL3，再配置 GRHI。当前目录会自动查找相邻的 `../../../SDL/build`，
也可以通过 `SDL3_DIR` 指定已构建或安装的 SDL CMake 包：

```sh
cmake -S . -B Build -DCMAKE_BUILD_TYPE=Debug -DGRHI_BUILD_TESTS=ON \
    -DSDL3_DIR=/path/to/SDL/build
cmake --build Build
ctest --test-dir Build --output-on-failure
```

输出位于 `../../Bin`。部署时同时复制 GRHI 和 SDL 运行库；macOS 对应
`libGRHI.dylib`、`libSDL3.0.dylib`。消费者通过 `GRHI.cmake` 使用公共 API 和
预构建 GRHI，不引入实现源码。`GRHI::GRHI` 的 `GRHI_RUNTIME_DEPENDENCIES` 属性
提供 SDL 部署路径，GameSprout 已按该属性同步复制运行库。

## 使用

```cpp
GRHI::GRHIApi * api = ExportGRHIApi(runtimeDirectory);
if (api == nullptr) {
    // 动态加载或 SDL 视频初始化失败。
    return;
}

const auto & adapters = api->GetAdapterList();
GRHI::IDevice * device = api->CreateDevice(adapters.front().Id);
if (device == nullptr) {
    // 读取 api->GetLastError() 获取创建失败原因。
} else {
    // 析构自动释放设备；也允许提前调用幂等的 Release()。
    delete device;
}
delete api;
```

API 为主线程使用的单例。重复获取共享同一个实例，由一个统一所有者在所有
设备与借用方结束后删除。GRHI 成对初始化和退出 SDL 视频子系统，不调用全局
`SDL_Quit()`；宿主必须避免在设备存活期间强制退出 SDL。

## 当前范围

- GRHI 不设置 SDL 的后端名称属性，由 SDL 自动选择，实际可用性取决于 SDL
  构建、平台、驱动和 GRHI 声明支持的 Shader 格式。公共接口不暴露后端选择枚举。
- GRHI 在 macOS 上只允许 Metal。非 Metal 的 `SDL_GPU_DRIVER` hint 在调用 SDL
  创建设备之前直接拒绝，包括显式选卡的情况；不加载 Vulkan 或 MoltenVK 来创建设备。
- `Initialize()` 通过本地 SDL 的 `SDL_GetGPUAdapters()` 枚举物理显卡并填充列表，
  不接收或遍历图形后端，不创建逻辑设备。公共 ID 为 `std::string`，直接保存 SDL 的不透明标识；空字符串
  表示默认选择，非空字符串按完整内容精确查找，创建后再次核对实际使用的显卡。
- Metal 使用 registry ID，DXGI 与 Windows Vulkan 使用 LUID；其他 Vulkan 设备使用
  device UUID。SDL 按平台调用一次物理设备枚举提供者：macOS 使用 Metal、Windows
  默认使用 DXGI、其他支持的平台使用 Vulkan。GRHI 不维护后端枚举列表或兼容掩码；
  创建时由选定后端查找该物理标识并验证能力，同名显卡不会仅凭名称合并。
- `DefaultDeviceAdapterID` 保留 SDL 默认选卡策略；显式 ID 无效、显卡被移除或后端
  不可用时返回失败，不静默改用其他显卡。字符串 ID 可以保存到设置文件，但每次
  启动必须重新匹配，不保证跨重启、驱动更新或硬件变化永久有效。
- 设备层接受 Metal 的 MSL/metallib、DX12 的 DXBC/DXIL、Vulkan 的 SPIR-V 格式；
  Shader 资源必须提供匹配的目标产物。已支持纹理/采样器、图形 Shader/Pipeline、
  过程顶点 Draw、顶点/索引缓冲区、Uniform 和带深度测试的 DrawIndexed。
- 最近一次设备创建失败原因通过 `GetLastError()` 返回；创建成功时清空。
- `Release()` 可重复调用；释放后设备拒绝创建 Surface，调用方仍需删除设备包装对象。
- `SurfaceSDL` 包装宿主原生窗口，建立 SDL GPU 呈现连接；支持多个独立 Surface。
  `Draw(texture)` 保留等比例图片显示；底层 AcquireFrame/RenderPass/Submit 可用于三维场景。
  原生窗口仍由宿主管理，Surface 创建/获取画面/销毁须在窗口线程，宿主须晚于 Surface 销毁。
  macOS 请求高像素密度绘制目标，frame.Width/Height 是实际 drawable 像素。
  Foreign NSView 的子视图尺寸变化不会可靠产生 SDL 窗口尺寸事件，因此 GRHI 私有 Cocoa
  桥在每次 AcquireFrame 前同步 SDL 子 Metal 层的 backing 像素尺寸与 contentsScale；
  不修改宿主 NSView 或 NSWindow 几何，不依赖 Qt，也不需要宿主手动通知 resize。
- `GRHI_GPU_DEBUG` 默认关闭，可显式开启。本地 SDL 默认 blit 采样器中未初始化
  的属性字段已修复，macOS 开启验证的运行测试通过。

运行测试覆盖真实显卡枚举、默认与显式选卡、创建后物理标识核对、无效参数、
指定不存在的 SDL 显卡时不回退、重复释放与析构、单例重建；macOS 还覆盖 SDL
指定 Metal hint 时仍可自动创建，以及对非 Metal hint 在创建设备前返回失败。
还验证了 `SDL_GPU_DRIVER` 被设置为无效后端时，物理显卡枚举仍独立成功。
设备工厂独占初始化入口，普通失败和异常路径都会清理设备；拥有 SDL 设备的对象禁止复制。

## 本地 SDL 扩展

SDL 源码改动位于相邻的 `../../../SDL` 仓库，需与 GRHI 一起保留。这是私有扩展，
不是上游标准 SDL API。CMake 会检查扩展头文件和导出符号，避免误用原版运行库。

- `SDL_GetGPUAdapters(&count)`：返回 SDL 分配的统一物理设备信息数组，调用方
  使用 `SDL_free()` 释放；`count == -1` 表示失败。
- `SDL_GetGPUAdapterExtensionVersion()`：查询本地扩展 ABI 版本。统一枚举入口使用
  版本 2，GRHI 配置和初始化时都会校验，需同步重建 SDL 与 GRHI。
- `SDL_PROP_GPU_DEVICE_CREATE_ADAPTER_STRING`：创建设备时传入枚举得到的标识。
- `SDL_PROP_GPU_DEVICE_ADAPTER_STRING`：查询成功创建的设备实际使用的物理标识。

Metal 路径已在本机 Apple M4 验证。DX12 和 Vulkan 后端已交叉编译为 Windows x64
对象文件，尚需 Windows 链接、实际 GPU 运行及双显卡切换验收。GRHI 的 macOS
Vulkan 路径按产品约定返回不支持。
Vulkan 显式枚举要求能取得 device UUID/LUID，缺少相关能力的设备不会伪造标识。

Windows 的 DX12/Vulkan 均在 SDL 内按物理标识选卡，GRHI 统一使用 `DeviceSDL`。
运行时部署 `GRHI.dll` 和 `SDL3.dll` 到同一目录，动态加载器会在该目录查找依赖，
支持 UTF-8 传入的路径。MinGW 与 MSVC 均使用 `GRHI.dll` 这一运行库名称。

## 三维绘制与验证

- `CreateBuffer` 返回 immutable 顶点/索引数据；`Vertex | Index` 允许一次上传预处理
  Mesh 的整块 bytes，再以不同字节 offset 绑定顶点和索引。未提供的数据初始化为零。
- Pipeline 声明 binding/stride 和 Float2/3/4 attribute location。当前是 per-vertex 输入，
  binding/location 小于 16，stride 为 4 的倍数且不超过 2048；索引支持 UInt16/UInt32。
- `ShaderCode::UniformBufferCount` 每阶段最多 4。SetVertexUniform/SetFragmentUniform
  在调用期间复制 std140 数据：长度为 16 字节倍数、每槽最多 16 KiB。切 Pipeline 后
  重新绑定所需输入、uniform、纹理和采样器；Shader 元数据必须与编译产物一致。
- 深度纹理使用 `D32_Float + DepthStencilTarget`，通过独立 DepthTargetView 交给 Pass；
  默认清为 1。Pipeline 的 DepthFormat 必须与 Pass 匹配，DepthTest/Write/Compare 独立设置。
  当前不包含 stencil、MSAA、storage、可变 Buffer 写入、instance-rate 顶点布局或计算管线。
- 命令先记录 CPU 描述，Submit 统一检查资源身份、目标、绑定和索引/顶点范围后编码 SDL。
  Release/delete 后失效的资源不会进入绘制；失败的离屏命令取消。SDL 不允许取消已取得
  的窗口帧，因此此情况仅提交空的获取命令以释放帧，不编码无效绘制。
- Device 必须晚于所有子资源/Surface 销毁。资源至少存活到引用命令提交或取消；接口不接管
  调用方对象。GPU 可能继续执行，原生资源的延后释放由 SDL 处理。
- `ReadTexture` 是显式同步验证入口：提交/取消本设备全部命令后读取普通颜色纹理，返回
  紧密排列 RGBA bytes（BGRA 会转换通道顺序），保留 sRGB 编码；不支持深度或 Surface 帧。
  它等待 GPU fence，不能用于每帧呈现路径。

`GRHI_BUILD_TESTS=ON` 提供真实 Metal 离屏测试：复合 Buffer 上传、UInt16/UInt32 索引、
非索引绘制、矩阵/片元 uniform、深度遮挡、旧图片纹理采样流程及颜色读回；也验证错设备、
失效资源、越界范围、缺少绑定和非法命令状态被拒绝且不会修改目标。测试 Shader 当前提供
Metal 源码，非 Apple 平台将明确跳过 GPU fixture，不能视为已经完成 Vulkan/DX 的真机验收。

Editor 的真实 Qt/NSView 集成另已验证：Scene 子窗由 880×536 变为 1060×656、DPR=2 时，
SurfaceFrame 由 1760×1072 更新为 2120×1312；同时通过真实三角形绘制与宿主提前销毁验证。
