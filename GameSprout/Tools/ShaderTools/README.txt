GameSprout SDL Shader 工具链

工具组合：DXC + SDL_shadercross + SPIRV-Cross。SDK 只部署到编辑器/构建主机，不部署到游戏。
这是独立 CMake 工程，Editor 只消费其二进制目录，不包含编译器或 SDL 的源码工程。

macOS（以及没有官方二进制包的主机）先构建 SDL_shadercross 配套的 DXC：
  cmake -P GameSprout/Tools/ShaderTools/BuildDXC.cmake
脚本固定 SDL 官方子模块使用的 DXC fork 和 SPIRV/DirectX-Headers 版本，校验 SHA256，
源码及构建位于 Bin/Deps/DXC-SDL-src、Bin/Deps/DXC-SDL-build。
首次构建包含 LLVM/DXC，耗时明显高于普通模块构建；之后可以增量构建。

然后从仓库根目录构建工具 SDK：
  cmake -S GameSprout/Tools/ShaderTools -B Bin/Deps/ShaderToolsBuild -DCMAKE_BUILD_TYPE=Release
  cmake --build Bin/Deps/ShaderToolsBuild --config Release --parallel 6
  cmake --install Bin/Deps/ShaderToolsBuild --config Release
Windows x64/arm64、Linux x64 默认使用固定版本的官方 DXC 二进制包。
已有 DXC SDK 时可用 -DGAMESPROUT_DXC_ROOT=... 指定（包含 bin/dxc 和 lib/dxcompiler）。
SDL3 须先独立构建，默认查找相邻 SDL/build，可用 -DSDL3_DIR=... 指定。
工具必须使用主机架构构建；Android/iOS 是 Shader 目标，不是此工程的执行主机。

安装结构：
Bin/ShaderTools/bin：dxc、shadercross、spirv-cross。
Bin/ShaderTools/lib：DXC/DXIL/SDL 所需动态库（Windows 动态库位于 bin）。
Bin/ShaderTools/toolchain.json：编译器二进制签名及上游版本。
Bin/ShaderTools/Licenses：所用组件的许可证。
ShaderCompiler 模块每次构建会将 SDK 复制到可执行程序旁 local_tools/shaders。

Assets 规则：
只用 .shader 管理 Shader 源文件，内容为 HLSL，入口默认 main。
阶段保存在 .meta：Vertex / Fragment / Compute。
.frag.shader 和 .ps.shader 默认 Fragment；.compute.shader 默认 Compute。
.hlsli 用于公共代码；改变 include 会使引用它的 Shader 缓存失效。
示例：GameSprout/Examples/Shaders/Texture.vert.shader、Texture.frag.shader、Fill.compute.shader。
ShaderCompiler 模块接口与线程说明见 Module/ShaderCompiler/README.txt。

SDL 绑定约定（同一组内从 0 开始且连续）：
Vertex：纹理 tN/sN space0；uniform bN space1。
Fragment：纹理 tN/sN space2；uniform bN space3。
Compute：只读 tN/sN space0；可写 uN space1；uniform bN space2。
每组先采样纹理，再 storage texture，再 storage buffer；uniform 独立从 b0 开始。
导入时检查 register space、连续槽位、资源上限；暂不支持 descriptor 数组、
push constants、subpass inputs、加速结构。常量块内字段/数组/矩阵布局保存在反射中。

默认产物为 SPIR-V 和 MSL；可通过 Inspector 的 GenerateDXIL 附加 DXIL。
Metallib 可在 IShaderCompiler.Targets 中显式选择 MacOS/IOS/IOSSimulator。
它需要对应 Apple SDK 与 Metal Toolchain，各平台分别编译，不能直接共用同一文件。
本机已构建 DXC 并实测 SPIR-V/MSL/DXIL 和 GRHI/Metal 创建 Shader。
本机已安装 Metal Toolchain，并实测 macOS Metallib 成功编译；Metal 2.1 按目标平台使用
macos-metal2.1 或 ios-metal2.1 标准参数。iOS 与模拟器的成功构建尚未验证。

原始代码字节与反射分离：
Library/Artifact/<Guid>_<LocalID>.gsasset 仅保存代码段，没有 JSON 头。
ArtifactDB 保存 Stage、资源绑定/常量布局/线程组、各代码段格式/入口/偏移/长度。
工具链签名 ToolchainHash 独立于用户设置 SettingsHash。
当前不实现 ShaderLab、自动关键字组合、材质或完整 runtime 资源绑定。

上游与绑定说明：
https://github.com/libsdl-org/SDL_shadercross
https://github.com/libsdl-org/DirectXShaderCompiler
https://github.com/microsoft/DirectXShaderCompiler
https://github.com/KhronosGroup/SPIRV-Cross
https://wiki.libsdl.org/SDL3/SDL_CreateGPUShader
https://wiki.libsdl.org/SDL3/SDL_CreateGPUComputePipeline
