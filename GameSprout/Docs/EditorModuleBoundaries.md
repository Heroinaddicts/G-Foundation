# Editor 功能边界与事件协议

## 当前职责

| 模块 | 负责 |
| --- | --- |
| InspectorUI | 选择与锁定、扩展选派、公共应用/恢复按钮、诊断显示 |
| AssetImportUI | Texture、Model、Audio、Font、Video、Text、Shader 导入设置页面 |
| MaterialUI | Shader 参数控件、内嵌材质查看和提取、独立材质编辑 |
| AnimationUI | 动画片段、Animator、Controller、Avatar、Mask、Rig 的界面与创建菜单 |
| SceneObjectUI | 场景节点和组件分组、Transform/Camera/Mesh/UI 组件编辑 |
| ProjectAssetsUI | 资源浏览、目录导航、选择、通用菜单和打开处理器注册 |
| SceneEditorUI | 场景菜单、创建/打开/保存提示、模型实例化入口 |
| AnimationEditor | Controller/Mask/Rig 源数据创建、校验、保存与重导入协调 |
| AnimationPreview | 临时动画预览与还原、播放状态事件，不保存场景作者数据 |
| RuntimeAssets | Library 产物适配为 Runtime 资源提供者 |
| SceneManager | 场景文档、节点编辑事务、撤销/重做、保存与恢复 |
| RuntimeHost | 唯一 Runtime Update、模拟时钟、帧完成事件 |

动画在 `.scene` 中的序列化和绑定属于场景格式，所以保留于 SceneManager。导入器和资源解码器按文件分类，仍分别归 AssetsPipeline 和 RuntimeAssets；文件数量并不决定是否要新增模块。

## 扩展接口

`IInspectorUI::RegisterExtension()` 注册 `IInspectorExtension`。宿主按优先级选取页面，页面通过 `InspectorPanel` 提供加载、展示信息、草稿判断、校验、应用和恢复。组件编辑器也由扩展工厂提供。增加功能不需要在 InspectorUI 增加资源类型或组件类型分支。

`IProjectAssetsUI` 提供按 owner 注册菜单、资源打开处理器、右键上下文快照和创建后定位。功能 View 在 Launch 注册，在 Release 注销；浏览器不调用功能的私有实现。

Model 的动画扩展控件通过 `IAnimationUI::CreateModelAnimationEditor()` 获得，仅依赖 `IModelAnimationEditor`。共享库 `View/Shared/InspectorControls` 保存资源选择器、通用组件编辑上下文和设置控件基类；扩展注册表只存在于 InspectorUI 实例。

## 刷新与生命周期

1. BeforeInitialize 获得公开接口，Initialize 创建 `.ui` 控件。
2. Launch 先订阅事件、注册扩展，再读取必要初始状态。
3. LaunchFinished 在所有功能注册完毕后建立首次完整视图。
4. 选择、资源、场景或命令结果事件触发刷新；同一批事件可用 Qt 队列合并。空闲 Update 不扫描数据。
5. RuntimeHost 完成 Runtime Update 后发送帧事件。Scene/Game 使用该事件绘制；场景与选择身份来自独立事件缓存。
6. Release 先注销监听与扩展，再销毁控件。宿主注销扩展时也清理使用该扩展的组件编辑器，避免悬空回调。

所有事件在宿主主线程派发，借用数据只在回调期间有效。跨刷新保存 Guid/LocalID 或节点身份，使用时重新验证。


## Runtime 所有权与调用方向

Editor 应用组合层创建并拥有 Runtime，通过 `GameSproutViewApi` 的 `IGameSproutRuntime* const _Runtime` 注入。各 Module/View 都直接借用该公开指针，不经 SceneManager 或 RuntimeHost 的二级 Getter 获取；EditorApi 基类不负责释放。

```text
Editor View（Qt 控件与交互）
    → Module 公共接口和事件（场景、资源、作者编辑、预览）
    → EditorApi->_Runtime（借用统一 Runtime）
    → GameSproutRuntime（对象、组件、动画求值、Renderer、Viewport）
    → GRHI（Runtime 私有图形实现）
```

游戏可执行壳使用相同 Runtime SDK，不依赖 Editor 注册表或 Qt。SceneManager 的历史仅记录场景事务；跨资源或项目的历史服务不能继续塞入场景模块。

## 渲染接口

View 将平台和原生窗口身份封装为 Runtime 的 `NativeWindow`，调用 `CreateViewport`，得到 `IRuntimeViewport`。窗口所有权仍属于宿主；视口必须在原生窗口销毁或句柄替换前释放，并在新窗口上重建。

Runtime 内部为同一实例共享图形 Device，不同 Scene/Game 视口拥有各自的呈现目标、相机和渲染状态。离屏使用 `CreateOffscreenRenderer`，经 `Resize` 和 `ReadPixels` 完成尺寸调整与验证。

公开 `RenderView` 只描述场景、相机、清屏和调试几何；资源采样使用 Runtime 的 `SamplerSettings`。GRHI 的 Device、Surface、Texture 和 Sampler 类型不出现在 Runtime SDK 的公开接口中。

`GameSproutRuntimeDeploy` 负责将 Runtime 和其私有图形依赖放入应用包。应用包中存在 GRHI 动态库属于部署依赖，不要求 Editor 源码包含 GRHI 头文件或操作 GPU 对象。

## 更新与退出

所有模块 Initialize 完成后才进入 Launch；所有功能注册完成后，View 在 LaunchFinished 建立初始快照。Foundation 只由应用组合层推进，Project 不再重复调用 Foundation Update。RuntimeHost 是 Runtime 的唯一更新时钟，完成求值后派发帧事件。AnimationPreview 用控制命令、场景事件和活跃 Clip 的帧订阅维护预览，空闲 Update 不扫描节点。

退出顺序由编辑器组合层明确安排：

1. 先释放各个面板 View，结束原生视口和 UI 订阅，最后释放拥有面板控件的 IDEWindow。
2. AnimationPreview 停止预览并注销监听。
3. SceneManager 清理场景编辑状态、注销监听并销毁其活动场景。
4. RuntimeHost 停止时钟并解除资源提供者，保留 Editor 拥有的 Runtime。
5. 其余模块维持原有退出顺序，保证资源任务、Library 和本地数据库之间的依赖。
6. Editor 入口销毁 Runtime，最后释放 Foundation；EditorApi 基类析构不删除借用指针。

不通过字母顺序反转全部模块来推断依赖；新增服务需要明确退出依赖。

当前编辑器预览仍复用 Runtime 的更新入口；脚本执行器和固定步调度未在本轮增加。后续引入游戏脚本时，应明确区分编辑器动画求值和游戏逻辑 Tick，避免预览推进游戏行为。
