# Editor、Runtime 与图形后端边界

本文件记录当前实现。早期的 RuntimeArchitecture.md 保留为历史讨论稿；实际模块归属以本文和公共接口为准。

## 调用方向

```text
Editor View（Qt 窗口、输入和属性显示）
    ├── SceneManager：场景文档与场景编辑操作
    ├── AnimationPreview：编辑器临时播放会话
    ├── RuntimeAssets：Library 资源提供与源资产编辑服务
    ├── RuntimeHost：资源提供者接入与唯一更新时钟
    └── EditorApi->_Runtime：借用统一的通用 Runtime 实例
                    ↓
             GameSproutRuntime
             对象、组件、动画求值、Renderer、Viewport
                    ↓ 私有实现
                  GRHI
             Device、Surface、GPU 资源与提交
```

游戏的可执行壳可以直接使用相同 Runtime SDK，不依赖 Editor 的 Module/View 注册表或 Qt。

## 模块职责与所有权

| 模块 | 负责 | 不持有的职责 |
| --- | --- | --- |
| Editor 应用组合层 | 创建并持有 Runtime，所有模块退出后销毁一次 | GRHI 工厂和 GPU 对象 |
| EditorApi | 通过公开的 `IGameSproutRuntime* const _Runtime` 提供同一实例的借用指针，基类不负责释放 | Runtime 所有权和更新调度 |
| RuntimeHost | 借用 EditorApi 的 Runtime，接入资源提供者，唯一每帧更新时钟 | Runtime 所有权、场景 JSON、选择状态、预览 UI |
| SceneManager | 打开和保存 `.scene`、活动场景切换、稳定文件 ID、脏状态、场景编辑历史和变换事务 | Runtime 的创建销毁和 Update、动画播放状态、GPU 对象 |
| AnimationPreview | 当前活动场景的 Controller 预览、单对象 Clip 预览、暂停、定位、还原以及预览目标清理 | Runtime 所有权、场景文件保存、窗口和 GPU 设备 |
| RuntimeAssets | 向 Runtime 提供已预处理资源，提供现有动画源文件编辑服务 | View 控件、设备或 Surface |
| SceneViewUI | 原生 Qt 视口、自由编辑相机、输入和编辑工具交互 | 创建 GRHI Device/Surface、读取 FBX/PNG 源文件 |
| GameViewUI | 游戏视口和场景播放按钮 | 单独创建图形设备、拥有 Runtime 或编辑场景文档 |
| InspectorUI | 属性控件、资源选择和编辑动作；控件分别封装为 `.h/.cpp/.ui` | 动画状态机求值、资产预处理、Runtime 生命周期 |

SceneManager 内的历史目前只记录场景编辑，和稳定场景文件 ID、删除子树快照及连续拖拽事务一起工作。跨资源或跨项目的通用历史服务应独立设计，不再向 SceneManager 加入其他业务。

RuntimeHost 和 AnimationPreview 已通过 `CreateComponent.py` 的 `create_component` 生成独立 Module，公共接口位于 `Module/Interface`。新增模块或独立面板 View 同样从该脚本创建；生成模板使用当前 GameSproutEditor API 和 Runtime SDK，不默认引入 GRHI API。

## 渲染接口

View 将平台和原生窗口身份封装为 Runtime 的 `NativeWindow`，调用 `CreateViewport`，得到 `IRuntimeViewport`。窗口所有权仍属于宿主；视口必须在原生窗口销毁或句柄替换前释放，并在新窗口上重建。

Runtime 内部为同一实例共享图形 Device，不同 Scene/Game 视口拥有各自的呈现目标、相机和渲染状态。离屏使用 `CreateOffscreenRenderer`，经 `Resize` 和 `ReadPixels` 完成尺寸调整与验证。

公开 `RenderView` 只描述场景、相机、清屏和调试几何；资源采样使用 Runtime 的 `SamplerSettings`。GRHI 的 Device、Surface、Texture 和 Sampler 类型不出现在 Runtime SDK 的公开接口中。

`GameSproutRuntimeDeploy` 负责将 Runtime 和其私有图形依赖放入应用包。应用包中存在 GRHI 动态库属于部署依赖，不要求 Editor 源码包含 GRHI 头文件或操作 GPU 对象。

## 更新与退出

所有模块 Initialize 完成后才进入 Launch。Editor 入口先创建 Runtime，通过 `GameSproutViewApi(runtime)` 的公开常量指针 `_Runtime` 注入；Module 和 View 都从 EditorApi 借用，不通过 SceneManager 或 RuntimeHost 的二级 Getter 获取。RuntimeHost 在 Initialize 连接该实例，AnimationPreview 在 Launch 注册场景和 Runtime 监听。每帧由 AnimationPreview 更新临时播放策略，再由 RuntimeHost 统一调用一次 Runtime Update，SceneManager 处理场景状态变化。

退出顺序由编辑器组合层明确安排：

1. 先释放各个面板 View，结束原生视口和 UI 订阅，最后释放拥有面板控件的 IDEWindow。
2. AnimationPreview 停止预览并注销监听。
3. SceneManager 清理场景编辑状态、注销监听并销毁其活动场景。
4. RuntimeHost 停止时钟并解除资源提供者，保留 Editor 拥有的 Runtime。
5. 其余模块维持原有退出顺序，保证资源任务、Library 和本地数据库之间的依赖。
6. Editor 入口销毁 Runtime，最后释放 Foundation；EditorApi 基类析构不删除借用指针。

不通过字母顺序反转全部模块来推断依赖；新增服务需要明确退出依赖。

当前编辑器预览仍复用 Runtime 的更新入口；脚本执行器和固定步调度未在本轮增加。后续引入游戏脚本时，应明确区分编辑器动画求值和游戏逻辑 Tick，避免预览推进游戏行为。
