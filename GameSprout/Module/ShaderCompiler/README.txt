ShaderCompiler 模块

入口：Module/Interface/IShaderCompiler.h。
编辑器及离线构建侧模块，使用 DXC、SDL_shadercross、SPIRV-Cross，未引入其他 Shader 前端。
不依赖 AssetsManager、LibraryManager、数据库、TaskApi、IDEWindow 或 GPU Device。
Compile 是同步接口：输入 HLSL 快照、阶段、入口、宏、编译选项和目标列表，
返回代码字节、实际入口、反射、include 路径与内容哈希、诊断信息。
模块不分配 GUID/LocalID、不写 ArtifactDB、不发布产物，也不创建 GPU 对象。

调用顺序：
1. 主线程 Initialize，解析独立安装的工具位置并冻结工具链配置。
2. AssetsPipeline 在 Loading 后提交 TaskApi，并把 IShaderCompiler 指针放入任务快照。
3. 工作线程调用 Compile；IsCancellationRequested 仅在这次调用内使用，不会被保留。
4. Pipeline 校验 include 字节、源文件时间/版本，生成 GUID_LocalID 产物描述。
5. 主线程提交 LibraryManager，完成回调后 Unloading；失败保留上次成功产物。

Compile 可并发使用，同一调用的临时目录和结果互相独立。
Release 不修改编译器只读配置；宿主必须保持模块对象和动态库有效，直到工作任务全部退出。
ShaderCompiler 自己不持有异步任务，也不负责调度、Loading 或数据库线程切换。

资源约定：
Assets 中可编译的 Shader 资源统一使用 .shader，内容为 HLSL。
.meta 的 ImportSettings.Stage 为 Vertex/Fragment/Compute，EntryPoint 默认为 main。
.frag.shader / .ps.shader 默认 Fragment；.compute.shader 默认 Compute；其他默认 Vertex。
一个源文件当前输出一个指定入口/阶段；源文件可保留其他函数。
公共 include 使用 .hlsli；编译器支持宏、条件、嵌套 include 和空格路径。
本格式没有 ShaderLab/Pass 语法，不提供自动关键字组合和变体剔除。

SDK 查找：可执行文件旁 local_tools/shaders/bin，独立安装目录 Bin/ShaderTools/bin，最后 PATH。
工具发现失败只影响 Shader 导入，不阻止编辑器启动。
构建命令和固定版本见 GameSprout/Tools/ShaderTools/README.txt。

编译过程：
DXC 预处理取得实际 include 列表 -> 复制依赖到临时目录 -> 对快照再次预处理并核对依赖图
-> DXC 编译 SPIR-V -> SPIRV-Cross 完整反射 -> SDL_shadercross 绑定反射与目标转换。
优化和调试选项传给 DXC。NAME 宏等于 NAME=1；NAME=value 保留其值；名称不可重复。
最终代码不读取快照外的 include，诊断映射回原始 SourceRoot/SourcePath 并保留行号。
Pipeline 再校验返回依赖的实际字节哈希，主线程提交前继续校验源和依赖时间戳。

目标：
SPIRV + Vulkan：SPIR-V 二进制。
MSL + Apple：SDL3 可接收的 MSL，包含结尾 NUL；它在 SDL 创建 GPU Shader 时由 Metal 编译。
DXIL + Direct3D12：SDL_shadercross 先将 SPIR-V 转为 HLSL，再由 DXC 编译，保留 SDL 资源布局。
Metallib + MacOS/IOS/IOSSimulator：使用相应 Apple SDK 的 metal/metallib 工具，三者分别生成。
请求错误的格式/平台组合、缺失工具或任何目标失败时，返回失败并清空全部代码结果。

Pipeline 当前默认请求 SPIR-V/MSL，GenerateDXIL 可附加 DXIL。
Metallib 通过 IShaderCompiler 的 Targets 显式请求；尚未接入编辑器的项目构建平台设置。
反射中的参数 set/binding、常量块尺寸与字段偏移、输入输出、线程组等存入 ArtifactDB。
最终 Library/Artifact 文件只含代码段，段偏移/大小和实际入口位于 DB 的 Variants 中。
runtime 材质、uniform/storage 绑定、Compute Dispatch 及发布资源索引尚待接入。

验证：ShaderCompilerChecks 单独加载此模块，不启动其他业务模块，覆盖三种格式、并发、取消、
非法目标、Metallib 缺失工具与原始文件诊断；AssetsImportChecks 验证 Pipeline、缓存、依赖、
失败保留和真实 GRHI/Metal Shader 创建；InspectorChecks 验证 .shader 的三个阶段设置。
当前在 macOS arm64 实测 SPIR-V/MSL/DXIL 与 macOS Metallib，Metal Toolchain 安装后的成功路径已通过。
Windows/Linux 的主机部署分支与 iOS 的 Metallib 成功构建尚未真机验证。
