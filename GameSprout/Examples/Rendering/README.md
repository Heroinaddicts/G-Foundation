# 明亮动画风格的环境

`StylizedDaylight.json` 保存本次 FantasyKingdom 场景使用的线性颜色和美术参数。它是示例调色方案，不是 Runtime 自动读取的配置文件。

环境照明、天空渐变和距离雾属于 Scene 的 `EnvironmentLighting`；方向光颜色、强度与阴影属于 LightComponent。独立 `.mat` 的 Lit 参数控制明暗过渡、暗部色调、饱和度和法线强度，继续使用材质属性定义生成的 `.ui` 控件。`Stylization=0` 使用原有 Lit 受光；其他场景和材质不会被全局切换风格。

`WaterMaterial` 是已有 `Water.shader` 的参数。水面使用世界空间波纹和颜色反射，不包含屏幕空间折射或岸线深度泡沫。自定义 Shader 自行决定雾的处理；Runtime 不在其输出上重复施加不透明材质的距离雾。此预设为水面复制场景相同的雾色、起止距离和最大强度；水材质默认雾强度为 0，只有选用该风格时才开启。

风格目标是暖色阳光、偏蓝的暗部、柔和明暗过渡、蓝色远景与清透水面。现有低多边形模型和纹理保留；这些渲染调整不等于替换成《原神》的模型、贴图或原版材质实现。

美术方向参考米哈游的公开讲座 [Genshin Impact: Crafting an Anime Style Open World](https://www.gdcvault.com/play/1027540)。实现使用本项目自己的 HLSL、SDL3/GRHI 渲染和资源流程。
