# 方向光、环境漫反射与内置 Lit 材质

这一版接通场景方向光、场景级环境漫反射、金属度／粗糙度 Lit 材质及 Scene／Game 绘制。
方向光组件可随 Scene／Prefab 保存；环境照明属于场景，保存于 `.scene`，不随 Prefab 保存。
绘制由 Runtime 通过 GRHI 调用 SDL3 GPU；平台后端固定为 macOS／iOS 的 Metal、
Windows 的 Direct3D 12、Android／Linux 的 Vulkan。

## 在 Editor 中使用

1. 在 Hierarchy 的创建菜单中创建方向光。光源 Inspector 可修改启用状态、颜色、强度和影响层。
2. 在 Project 创建材质时选择 `GameSprout/Lit`，或在独立材质 Inspector 中选择该内置 Shader。
3. 将材质分配给 Mesh Renderer 的材质槽。Lit 也支持现有的 Skinned Mesh Renderer。
4. 调整基础颜色、基础贴图及 UV 变换、金属度、粗糙度、法线贴图和自发光。
5. 转动方向光，Scene 和 Game 在下一次绘制时反映变化；保存场景及材质后重开恢复。
6. 打开 **窗口 → 环境照明**，为当前场景选择关闭、单色、三色渐变或 SH9 模式，并调整强度。
   颜色数值使用线性 RGB，颜色选择器以 sRGB 显示；SH9 系数可展开查看，由导入或 Runtime API 提供。

环境面板通过事件同步当前场景，不依赖逐帧查询。编辑模式下的修改支持撤销／重做，随场景保存；
Play／Pause 下修改当前运行场景，停止后恢复编辑态，不写回源场景。
Prefab Stage 不编辑场景环境，Prefab 保存、实例化和 Apply 不携带或覆盖所属场景的环境照明。

方向光沿对象的局部 **-Z** 方向发出光线。位置和缩放不改变照射方向或强度，父节点的旋转会改变方向。
影响层与对象的 Layer 比较，相机的 CullingMask 单独决定对象是否可见。
当前每个对象使用与其层匹配的最多四盏方向光，按线性亮度从高到低选取，同亮度按层级顺序。

## 颜色和材质合同

- Runtime 的 `Color`、LightSettings 的颜色、Lit 的基础颜色和自发光颜色均为线性 RGB。
  Lit 颜色选择器在显示 sRGB 与存储线性颜色之间转换，数值输入显示存储的线性值。
- 颜色贴图可按 sRGB 导入，由 GPU 采样时解码一次。法线贴图必须按 **Linear** 导入；
  错误的 sRGB 法线贴图会报错，不会静默产生错误光照。
- 金属度和感知粗糙度范围为 0 到 1；金属度 0 对应电介质，1 对应金属。
  法线强度非负，0 关闭法线扰动；自发光是线性 HDR 颜色，不产生照亮其他物体的光源。
- `.mat` 的 Shader 为 `GameSprout/Lit`。属性为 `BaseColor`、`BaseColorTexture`、
  `BaseColorTextureST`、`Metallic`、`Roughness`、`NormalTexture`、`NormalTextureST`、
  `NormalScale`、`Emission` 和 `DoubleSided`。贴图继续使用 GUID + LocalID；
  作者字段 `Emission` 读取为 Runtime 的 `MaterialRenderData::EmissionColor`。
- 法线采用 RGB 切线空间编码，绿色正方向对应正切线 Y。现有 Mesh 未提供独立切线，
  本版通过世界位置和 UV 的屏幕导数构造切线框架；退化 UV 回退到顶点法线。

光照采用 GGX 分布、Smith 可见性、Schlick Fresnel 和金属工作流，并叠加场景环境漫反射和自发光。
环境模式 `None` 关闭间接漫反射；`Color` 使用单色，`Gradient` 使用天空／地平线／地面三色；
`SphericalHarmonics` 使用九项世界空间 RGB 系数，并按当前表面法线求值。
SH 已是漫反射卷积后的 `E/pi` 多项式系数，基函数为 `[1,y,z,x,xy,yz,3*z*z-1,xz,x*x-y*y]`，
不再乘球谐归一化常数或除以 pi。环境设置中的颜色为非负线性 HDR，SH 系数允许有符号值。

没有方向光、自发光或其他照明来源时，Lit 物体为黑色。当前没有阴影、镜面 IBL、实时 GI、
天空盒背景、空间 Light Probe／光照贴图绑定、色调映射、点光源或聚光灯。
这里的环境漫反射不包含镜面 IBL／Reflection Probe，也不是实时 GI；高于 1 的输出在普通 UNorm 目标中截断。
内置 Preview 保留原预览行为，自定义 `.shader` 仍按自己的程序绘制。

## API 与模块边界

```cpp
auto* light = object->AddComponent<GameSproutRuntime::ILightComponent>();
auto settings = light->GetSettings();
settings.Color = {1.0f, 0.8f, 0.6f, 1.0f}; // 线性颜色。
settings.Intensity = 2.0f;
settings.CullingMask = 1u << 3;              // 照亮 Layer 3 的对象。
light->SetSettings(settings);               // 无效值失败且保留原设置。

// scene 为当前 Runtime 的 IScene*。环境状态不属于某个 GameObject 组件。
auto environment = scene->GetEnvironmentLighting();
environment.Mode = GameSproutRuntime::AmbientLightMode::Color;
environment.AmbientColor = {0.15f, 0.18f, 0.22f, 1.0f}; // 线性 RGB。
environment.Intensity = 1.0f;
scene->SetEnvironmentLighting(environment); // 参数无效时整体保留旧状态。
```

上述接口在 `RuntimeLightingApi.h` 中定义；直接调用适用于 Runtime 逻辑。Editor 编辑通过
LightingAuthoring 的公开事务接口，保留撤销及 Play 隔离。
LightingAuthoring 负责 Editor 创建、属性及场景环境事务；LightingUI 负责 `.ui` 控件、菜单、组件扩展和环境面板。
Runtime 持有 LightComponent、场景环境、光源选择和 Shader 数据；GRHI 不认识光源组件或材质业务。
保存、撤销、Prefab Override 与 Play 的临时修改复用现有公开文档／对象编辑接口。

## 验证

- `Runtime.LightingAndDocuments`：参数与事件、Scene 往返、Prefab 与 Override。
- `Runtime.EnvironmentLightingAndDocuments`：环境参数、Scene 文档、Prefab 隔离及事件。
- `Runtime.LightingGPU`：真实 GPU 光照、层过滤、缩放与旋转、材质、法线、颜色空间和蒙皮。
- `Runtime.EnvironmentPacking`／`Runtime.EnvironmentGPU`：环境数据打包与真实 GPU 求值。
- `Lighting.PreparedMaterialGPU`：`.mat` 导入到 Artifact、资源读取、真实 GPU 绘制，保存后的更新与双视口。
- `Lighting.AuthoringTransactionsAndInspector`／`Lighting.EnvironmentDesignerWindow`：编辑事务与 `.ui` 环境窗口。
- `GRHI.PlatformBackendPolicy`：固定平台后端以及拒绝跨后端覆盖。

实际 Unity `Demo_01` 的最终环境 SH 已通过 26 个方向及合成九项探针对照；
GameSprout 的 14,817 节点场景也完成真实 GPU 环境开关比较。
相同主相机下两次均为 10,004 次 Draw、2,756,063 个三角形，环境漫反射改变颜色而不改变几何提交。
这验证了当前环境漫反射链路，不代表已经实现镜面 IBL、光照贴图、阴影或 Unity 的最终画面。
完整命令和预览文件说明见 `Tools/UnitySceneBridge/README.md`。
