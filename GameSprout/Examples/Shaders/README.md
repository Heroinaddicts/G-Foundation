# 用户 Shader 与材质

第一版支持一个 Forward Pass 的自定义静态网格 Shader。`.shader` 是 HLSL，使用 DXC、SDL_shadercross 和 SPIRV-Cross 离线生成平台程序；Runtime 不读取或编译源代码。

## 在 Editor 中使用

1. 在 Project 的创建菜单中新建 Unlit Shader。
2. 等待 Shader 导入成功，选中它，再创建材质。新 `.mat` 引用该 Shader 的 GUID + LocalID，并填入参数默认值。
3. 将材质分配到对象的 MeshRenderer 材质槽。
4. 在材质 Inspector 修改参数、纹理和 UV 变换并保存。修改 HLSL 或其 include 后，资源流程重编并通知 Runtime 清理对应缓存。

没有选中 Shader 时，新材质使用当前内置预览 Shader。内置预览是现有受支持的路径；用户 Shader 编译或加载失败会报告错误，不会偷偷改用预览程序。

## 源文件合同

源文件有两个合法 HLSL 块注释声明：

- `GameSproutShader`：名称、Pass、顶点/片元入口及绘制状态。
- `GameSproutMaterial`：参数名、类型、显示名、默认值以及分组等编辑信息。

当前必须恰好有一个 `LightMode` 为 `Forward` 的 Pass。支持 Cull、Opaque/Alpha/PremultipliedAlpha 混合、深度测试/写入及比较方式。透明物体按对象距离排序，不能保证相互穿插的透明三角形顺序正确。

VS 的 `b0, space1` 保留给 `GameSproutDraw`，依次包含 `column_major float4x4 Model`、`ViewProjection`、`NormalMatrix`，总共 192 字节。矩阵使用列向量，按 `ViewProjection * Model * position` 相乘。Model 的输入和输出都属于 Unity 风格世界约定（左手、Y 向上、+Z 向前）；View 将世界变换到 -Z 向前的右手相机空间，投影后的深度范围为 0 到 1。自定义 Shader 不得再次镜像世界 Z 或翻转相机角度。

顶点位置、法线、颜色、UV 分别对应位置 0、1、2、3，类型为 float3、float3、float4、float2，HLSL 使用 TEXCOORD0 到 TEXCOORD3。Runtime 为缺少颜色或 UV 的静态网格填入白色或零 UV。

材质常量使用 FS 的 `b0, space3` 起始槽位，VS 材质常量可使用 `b1, space1` 起始槽位。实际偏移和缓冲大小来自编译反射；不要根据 C++ 结构体猜测布局。绑定槽位遵循 SDL 的连续编号规则。

纹理和采样器使用 FS 的 `t0/s0, space2` 起始槽位。Texture2D 的名称必须对应材质的 Texture 属性；其隐含 `<Name>ST` 是 float4，含 XY 缩放和 ZW 偏移。未绑定纹理采用白色，但缺失的非空资源引用仍是错误。

材质中普通浮点参数、Float4 颜色/向量及整型开关会按反射类型传到 GPU。暂不支持的数组、矩阵材质参数、Storage 资源、顶点纹理采样、多 Pass 和自定义蒙皮 Shader 会明确诊断。现有内置蒙皮渲染不受影响。

## 产物与分工

编译后的代码保存在 `Library/Artifact`，文件中没有 JSON 头。Pass、平台、入口、代码区间与绑定布局作为产物描述管理。`.mat` 仍是可读作者文件，参数和资源引用在预处理后交给 Runtime。

ShaderAuthoring 负责源文件创建和校验，ShaderUI/MaterialUI 负责 `.ui` 控件及交互，AssetsPipeline 负责离线任务，RuntimeAssets 适配已生成的资源。Runtime 负责 GPU 程序、材质绑定和 Pipeline 缓存，GRHI 只执行图形命令。Editor 和发布宿主使用相同 Runtime 合同。

内置 `GameSprout/Lit` 已接入方向光和金属度／粗糙度光照，见 [方向光与 Lit 材质](../Lighting/README.md)。
自定义 Shader 仍按自己的程序绘制，当前没有提供自定义程序的场景光源 Uniform 合同。
阴影、GI、ShaderGraph 或用户管线 Pass 注入尚未接入，不能把材质参数出现在 Inspector 当成已实现对应渲染效果。

## 验证入口

`Tests/ShaderRendering` 使用真实 ShaderCompiler、资源管线、Artifact、RuntimeAssets 和 Runtime/GRHI GPU 读回，检查用户 Shader 绘制、数值和纹理/ST 绑定，以及 include 重新导入后的缓存更新。测试建立隔离项目，不修改用户项目。
