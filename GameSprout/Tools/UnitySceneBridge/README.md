# Unity 场景导出桥

把 Unity 当前已加载场景的最终对象快照导出为独立 GameSprout 项目。桥接工具仅在 Unity Editor 中运行；生成的 `.scene`、`.mesh`、`.mat`、PNG、`.meta` 和项目设置供 GameSprout 的资源管线处理，Runtime 不依赖 Unity 程序集。

## 安装与导出

将本目录 `Editor` 下五个文件一起复制到 Unity 项目的一个 `Editor` 目录，例如 `Assets/GameSproutSceneBridge/Editor/`：

- `BridgeExportContext.cs`
- `GameSproutSceneBridge.cs`
- `GameSproutMeshExport.cs`
- `GameSproutMaterialExport.cs`
- `GameSproutShadowExport.cs`

等待 Unity 编译完成，打开要导出的场景并退出 Play 模式，然后使用菜单：

- `Tools → GameSprout → Export Active Scene to New Project...`：选择新项目目录的路径。
- `Tools → GameSprout → Export Active Scene to GameSproutFantasyKingdom`：导出到当前用户 Documents 下的 `GameSproutFantasyKingdom`。例如本机为 `/Users/max/Documents/GameSproutFantasyKingdom`。
- `Tools → GameSprout → Export Active Environment Lighting to...`：单独导出当前环境光，默认选择上述项目的 `Info/UnityEnvironmentLighting.json`。允许目标父目录已存在，但文件本身必须不存在；不会修改已有 `.scene`、资源或 Unity 场景。
- `Tools → GameSprout → Validate Active Environment Lighting`：在系统临时目录生成独立探针 JSON，记录实际环境光以及多方向 SH 对照结果，路径输出到 Unity Console。

完整项目导出的目标目录必须尚不存在，父目录必须已存在。所有输出路径位于 Unity 项目内部时都会拒绝。工具不保存 Unity 场景，不改动模型／贴图导入设置，也不覆盖源资源；如果当前场景存在未保存修改，菜单导出会采集当前内存快照，并在清单记录 `SourceSceneDirty`。

写入先在目标父目录下的独占 `.项目名.export-随机标识` 事务目录内完成；全部成功后整体移动到目标路径。目标已存在、写入失败或导出中出现异常均不会合并／覆盖已有项目。失败时只清理本次创建的事务目录。

生成项目后从 GameSproutHub 打开它。Editor 根据 `UserSettings/EditorState.json` 的 `LastScenePath` 打开导出的场景，AssetsManager／AssetsPipeline 负责生成 `Library/Artifact`。导出目录中的源 `.mesh` 并非已经上传 GPU 的资源。

## 输出与身份

```text
新项目/
  Info/project.json
  Info/UnityExportManifest.json
  ProjectSettings/ProjectSettings.json
  UserSettings/EditorState.json
  Assets/
    Scenes/场景名.scene
    Meshes/*.mesh
    Materials/*.mat
    Textures/*.png
    PhysicsMaterials/*.physicmaterial
```

每份源资源都有当前 `Version=1` 的 GameSprout `.meta`。各 Unity 资产子对象按 Unity GUID、原 localID 和类型派生独立源 GUID，目标主资源 LocalID 固定为 `1`；完整源身份、目标路径和引用映射记录在清单的 `Assets` 中。`.scene` 的节点／组件 ID 为独立文档 ID，清单 `Objects` 记录 Unity GlobalObjectID 到节点 ID 的对应关系。

Unity 的内置 Layer 槽位与 GameSprout 不同。导出时将 Unity 的 Default／Ignore Raycast／UI 映射到 Runtime 的 0／1／2；其余层使用一致的一对一映射。Camera／Light 掩码、节点 Layer 和碰撞矩阵同步转换；具体映射保存在 `LayerMapping`，Tag 定义另行写入项目设置。

Unity 与 Runtime 世界坐标统一为左手系、Y 向上、局部 +Z 向前、米。节点位置／四元数、Mesh 位置／法线／Tangent.xyz 和 Tangent.w、三角形顺序全部原样保留；不再反射 Z 或反转绕序。Mesh 数据取自 Unity 已导入的 Mesh，不会再套一遍 FBX 文件单位或轴转换。场景输出为 SceneDocument Version 2，原生 Mesh 为 GSMESH02 / Version 2；旧坐标合同的源文件不兼容，应重新导出。相机到 GPU 视图空间的变换由 Runtime Renderer 负责，不写入导出的场景数据。

原生 `.mesh` 使用当前 `GSMESH02`、Version=2 的小端二进制格式。工具通过 `MeshUtility.AcquireReadOnlyMeshData` 读取不可读 Mesh，无需修改 `isReadable`。MeshCollider 的几何也走同一原生格式。URP Lit 忽略顶点色，因此桥接快照会省略源顶点色并在清单报告。

## 快照范围与差异

| 内容 | 当前行为 |
| --- | --- |
| 节点与 Prefab | 遍历实际场景树，保留 inactive 节点、最终局部 TRS、材质 Override 和已添加组件；Prefab 展开成普通节点，未保留源 Prefab 连接、实例 Override、Nested／Variant 编辑关系。 |
| 静态 Mesh | 保留位置、法线、Tangent、UV0、三角形和材质槽。源 UV2 的存在会报告；当前没有 Runtime lightmap 绑定。 |
| SkinnedMeshRenderer | 用 `BakeMesh` 导出当前局部姿态的静态 Mesh，并保留节点层级。不会导出骨骼驱动、蒙皮权重、动画状态、Controller 或动画播放能力。 |
| Animator | 不创建 Runtime Animator；报告跳过以及是否缺少 Controller。 |
| 材质 | 将已解析的材质映射到内置 `GameSprout/Lit`，支持基础色／贴图、金属度、粗糙度、法线／强度、自发光常量和双面。URP Smoothness 转为 `1-Smoothness`；法线使用 URP 实际采用的 BaseMap UV 变换。 |
| 颜色与贴图 | 按 Shader 属性的 Color／HDR／Gamma 标记决定颜色转换，保留原值、导出值和策略；原始 PNG 解码后只按 RGBA 字节垂直翻转像素行，再编码为 PNG，匹配 GameSprout 的顶行优先上传；Mesh UV、材质 ST 和法线切线空间不变。法线以 Linear 导入，绿色通道仅按源 Importer 的要求翻转一次。GPU 解码输出同样翻转像素行；路径与精度变化记录在 `TextureConversions`。 |
| 透明／裁切 | 当前内置 Lit 仍按不透明绘制。源透明 Alpha 被保存，但 Premultiply、ZWrite、排序及 Alpha Clip／Cutoff 的效果没有完整对应；会明确报告，玻璃、水和云不保证一致。 |
| 光照 | 映射方向光的启用、颜色、URP 相对强度、影响层及阴影设置；从当前质量档的活动管线读取阴影距离、图集分辨率、级联数、软阴影及实际生效的 bias。bias 保持每级联纹素单位，Runtime 再换算；级联划分、接收端 bias 和 PCF 与 URP 的精确算法仍不同。当前支持的内置不透明几何默认参与主阴影，Renderer 个别投射/接收模式尚未逐一保存，非默认模式会报告。其余 Light 类型及 URP AdditionalLightData 未支持设置明确报告。 |
| 环境漫反射 | 所有 Unity AmbientMode 统一导出 `RenderSettings.ambientProbe` 的最终线性 SH 快照，写入场景顶层 `EnvironmentLighting`；强度固定为 1，系数按世界 Z 反射转换。保留实际三色环境光产生的 SH，不改用简化渐变。 |
| 其他环境效果 | 空间 Light Probe、GI／光照贴图绑定、Reflection Probe、天空盒背景、SSAO、雾、后处理及色调映射尚未复刻。场景级环境漫反射不等于这些功能。清单保留可读取的 Unity 环境设置和 lightmap 引用。 |
| 物理 | 导出 Box／Sphere／Capsule／MeshCollider、Rigidbody 和物理材质。Runtime 只有一个摩擦系数时使用 Unity dynamicFriction，静摩擦差异报告。 |
| 其他组件 | 未支持的脚本、音频、特效等不静默伪装为已实现功能；按类型汇总数量和代表对象。 |

完整场景／资产源数据可被导出与 GameSprout 画面完整复刻是两项验收。材质、光照、透明状态及环境效果的差异应结合清单和实际画面对照确认。

### 当前方向光阴影

方向光 Inspector 的“实时阴影”组用于编辑 `LightSettings.Shadows`，参数通过 LightingAuthoring 事务写入 Scene／Prefab，支持撤销和重做。Runtime API 也可通过 `ILightComponent::GetShadows()`／`SetShadows()` 读取或整体修改；非法值保留原设置，且不发送变更事件。默认关闭阴影，不会因为加载没有显式启用阴影的场景而擅自打开。

当前由最亮且开启阴影的方向光生成主阴影，支持 1／2／4 级联和 256 至 4096 的阴影图集。Soft 开启时使用 3×3 PCF，关闭时单点比较；尚无级联边界混合。阴影距离按各视口相机分别计算，不借用 Scene 编辑相机作为 Game 相机。深度偏移和法线偏移均以每级联阴影纹素为单位，而不是米；Runtime 分别换算为深度比较与世界法线偏移。原生自定义 Shader 的 ShadowCaster、透明／Alpha Clip 阴影和每 Renderer 的投射／接收模式不在本阶段范围内。

## 清单检查

`Info/UnityExportManifest.json` 使用当前 `Version=2`，包含：

- `Counts`：节点、活动节点、Renderer、活动启用 Renderer、Mesh 顶点／三角形、各 Collider、材质、贴图、Skinned 快照及 Animator 跳过数量。
- `Assets`／`Objects`：源资源及节点身份映射。
- `MaterialConversions`：Shader、属性标记、源／目标颜色、Surface／Blend／Alpha Clip、材质参数转换和未映射贴图。
- `TextureConversions`：源 PNG／GPU 解码路径、垂直像素行翻转、UV／ST 保持、独立的法线绿色通道策略、颜色空间、尺寸和 Unity GraphicsFormat；导出会重新编码 PNG，不保留源 PNG 编码字节。像素行翻转只交换 `Color32`，不会额外执行 sRGB／Linear 转换。
- `EnvironmentLighting`／`EnvironmentLightingConversion`：最终环境光、源模式／强度、源及目标系数、颜色空间与强度策略、26 个方向的实际 Unity `SphericalHarmonicsL2.Evaluate` 对照，以及覆盖全部九项的合成探针对照。
- `Warnings`：限制代码、数量和最多八个代表源对象。
- `UnityEnvironment`：环境光、天空盒、雾、反射强度、lightmap 引用及重力等可读取设置。

## 环境光合同与独立验证

场景顶层环境光采用 `Mode="SphericalHarmonics"`、`Intensity=1` 和九个 RGB 系数，基函数依次为 `[1,y,z,x,xy,yz,3*z*z-1,xz,x*x-y*y]`。系数已经是线性、漫反射卷积后的 `E/pi` 多项式系数；Runtime 直接计算，不再乘球谐归一化常数或除以 pi。Unity 与 Runtime 使用相同的世界坐标，全部九项系数按原值保留，不对第 2、5、7 项做符号转换。

依据 Unity 原生 `RenderSettings::UpdateFinalAmbientProbe`、C# `RenderSettings.ambientProbe` 绑定的 `GetFinalAmbientProbe` 及 URP 17.6 的环境光路径：天空盒强度已按 gamma→linear 参与最终 Probe，不能二次乘上源 `ambientIntensity`。Flat／Trilight 的颜色按 sRGB→linear 生成最终 Probe，而不使用天空盒强度；桥接仍保留线性三色作为上下文，但渲染使用实际最终 SH，避免用不同的三色插值近似。导出不会触发 GI 重烘焙；如果当前最终 Probe 全零，保留全黑并报告，绝不从天空颜色伪造兜底。

独立环境导出文件使用 `Version=2`、`CoordinateSystem="LeftHandedYUp"`，包含 `EnvironmentLighting` 和 `EnvironmentLightingConversion`。它仅供明确应用环境光数据，不自动改写项目中的任何场景。也可从 Unity Editor 工具代码调用 `GameSproutSceneBridge.WriteActiveEnvironmentLighting(绝对新文件路径)`，供自动化读取快照。

每次导出都比较 26 个单位方向：Unity 源方向的 `SphericalHarmonicsL2.Evaluate` 与 Runtime 同一方向上的九项多项式；再用包含全部九项的合成系数验证一次。任一方向误差超过相对容差 `1e-4` 就终止导出，防止输出与实际 Unity API 不一致的系数合同。该检查只读取场景设置，不修改 `RenderSettings`。

## 命令与验证

可在另一个未运行的 Unity 项目进程中使用批处理。不要用第二个进程同时打开正由 Editor 占用的相同 Unity 项目：

```sh
/path/to/Unity.app/Contents/MacOS/Unity \
  -batchmode -quit \
  -projectPath /path/to/UnityProject \
  -executeMethod GameSprout.UnitySceneBridge.GameSproutSceneBridge.ExportFromCommandLine \
  -gamesproutScene Assets/PolygonFantasyKingdom/Scenes/Demo_01.unity \
  -gamesproutOutput /path/to/NewGameSproutProject \
  -logFile /tmp/gamesprout-unity-export.log
```

`-gamesproutScene` 可省略，此时导出进程的当前活动场景。指定该参数时，工具在打开场景前拒绝替换已有未保存场景。也可由另一个 Unity Editor 工具直接调用公开入口 `GameSproutSceneBridge.ExportActiveScene(absoluteOutputRoot)`。

GameSprout 端已有原生 Mesh 导入与 Lit 绘制检查。先构建当前 Foundation、GRHI、Runtime 和 Editor 模块，再在仓库根目录运行以下示例命令；Qt 路径按本机调整：

```sh
cmake -S GameSprout/Tests/AssetsImport -B /tmp/gamesprout-bridge-assets-checks \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/path/to/Qt/macos
cmake --build /tmp/gamesprout-bridge-assets-checks --target NativeMeshChecks --parallel
ctest --test-dir /tmp/gamesprout-bridge-assets-checks -R '^Assets.NativeMesh$' --output-on-failure

cmake -S GameSprout/Tests/LightingRendering -B /tmp/gamesprout-bridge-lighting-checks \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/path/to/Qt/macos
cmake --build /tmp/gamesprout-bridge-lighting-checks --parallel
ctest --test-dir /tmp/gamesprout-bridge-lighting-checks --output-on-failure
```

上述两个目标分别检查原生 Mesh 当前格式和 Lit 绘制。

### 导出项目管线检查

`Tests/UnitySceneBridge` 专门验证桥接输出在 GameSprout 端的整条资源路径。默认 CTest 用 `--fixture` 创建隔离小项目，经过真实 Module 的扫描、导入、Library／RuntimeAssets、SceneDocument 装配与 640×360 GPU 读回，检查主资源 LocalID、引用、Loading／Unloading 和中心像素。该 fixture 不启动 Unity Editor，Unity 侧工具仍需要实际导出验证。

```sh
cmake -S GameSprout/Tests/UnitySceneBridge -B /tmp/gamesprout-unity-bridge-checks \
  -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/path/to/Qt/macos
cmake --build /tmp/gamesprout-unity-bridge-checks --parallel
ctest --test-dir /tmp/gamesprout-unity-bridge-checks \
  -R '^UnityBridge.NativeProjectPipeline$' --output-on-failure
```

对已经导出的真实项目，关闭正在使用该项目的 GameSproutEditor 后运行：

```sh
/tmp/gamesprout-unity-bridge-checks/UnitySceneBridgeChecks \
  --project-root=/Users/max/Documents/GameSproutFantasyKingdom

/tmp/gamesprout-unity-bridge-checks/UnitySceneBridgeChecks \
  --project-root=/Users/max/Documents/GameSproutFantasyKingdom --render-all

/tmp/gamesprout-unity-bridge-checks/UnitySceneBridgeChecks \
  --project-root=/Users/max/Documents/GameSproutFantasyKingdom --render-all --compare-environment
```

`--project-root` 会实际扫描和导入目标项目，检查各类资源能否由 RuntimeAssets 解码、场景是否能装配、Mesh／Material 非空引用是否可解析。它会创建或更新该目标项目的 Library 和正常 `.meta` 数据，不会改动原 Unity 项目。

追加 `--render-all` 才进一步按导出场景的 MainCamera 绘制整个场景并读回，在目标项目根目录生成 `BridgePreview.png`。它验证场景产生绘制命令与可读回图像；画面与 Unity 一致的程度还须对照前述透明、环境光和阴影等限制评估。未绑定 Mesh 的源 Renderer 会单独计数，不伪造几何；有效非空资源引用缺失则检查失败。

同时追加 `--compare-environment`，在相同相机和几何下临时关闭环境漫反射，生成 `BridgePreviewWithoutEnvironment.png`，验证两次绘制的 Draw／三角形数量一致，并输出线性读回 RGB 字节总和。比较后恢复内存中的环境设置，不把关闭环境的状态写回 `.scene`。两张预览均在保存 PNG 时由线性读回数据编码为 sRGB。

本机 Unity 6000.6.4f1 的 `Demo_01` 已实际导出到 `/Users/max/Documents/GameSproutFantasyKingdom`。GameSprout 整项目检查通过，节点、Renderer、资源与碰撞体计数均与导出清单一致：14,817 个节点、14,727 个 Renderer（源场景有 12 个未绑定 Mesh）、2,368 份 Mesh 源、32 份材质、35 份贴图、14,248 个 Collider。携带最终环境 SH 的主相机离屏绘制及开关环境对照已通过：两次均为 10,004 次 Draw、2,756,063 个三角形；线性读回 RGB 字节总和从关闭时的 `9.20763e6` 变为开启时的 `1.58871e7`。此次只提交了一个场景导入任务，其余资产命中缓存；两张 PNG 均已生成。该数值是单帧绘制验证，不是帧率承诺；当前空间光照、镜面反射、阴影、透明等差异仍见上表，画面尚未达到 Unity 的最终效果。

贴图方向使用实际 Unity 的 2×2 `Texture2D.SetPixels32`／`EncodeToPNG` 探针核对：Unity 的 `(0,0)` 为红色，而编码 PNG 的顶行为蓝色。桥接 fixture 额外检查翻行后的贴图在 Runtime 中的上下色块，防止 UV 与像素行顺序再次相反。Unity 原 `Demo_01.unity` 导出前后 SHA-256 相同。

环境光探针也已在实际 Unity 中执行：当前最终 Probe 的 26 个方向与导出多项式对照通过，最大绝对误差约 `2.98e-8`；覆盖全部九项的 `SyntheticValidation` 同样通过。最终 SH 已明确应用到 GameSprout 项目的 `Assets/Scenes/Demo_01.scene` 顶层 `EnvironmentLighting`。这证明系数、Z 反射和强度合同已按实际 Unity API 校验，不代表空间 Light Probe、镜面 IBL、天空盒或阴影已完成。
