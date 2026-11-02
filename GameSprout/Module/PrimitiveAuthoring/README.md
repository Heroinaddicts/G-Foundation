# 内置基础物体

PrimitiveAuthoring 负责基础物体作者命令，PrimitiveUI 通过 Hierarchy 的公开扩展注册 `.ui` 定义的 `3D Object` 菜单。创建、父级、命名、组件和选择提交在模块内完成，使用 SceneManager 的通用文档事务，因此一条创建可 Undo/Redo，未保存场景和 Prefab Stage 都遵循同一内容根与文档身份规则。Play、模式切换、Host 帧回调、失效场景/父级和非所属线程拒绝创建。

Cube 为 1 米立方体，Sphere 直径 1 米，Capsule 和 Cylinder 高 2 米、直径 1 米，Plane 为 10×10 米水平面，Quad 为 1×1 米竖直面。全部包含 Mesh、MeshRenderer，采用 Runtime 默认白色材质；Cube、Sphere、Capsule 同时附带对应的 Box、Sphere、Capsule Collider。Cylinder、Plane、Quad 的 MeshCollider 待对应组件实现后接入，不添加 Rigidbody。

RuntimeBuiltinResources.h 集中声明保留 GUID `00000000000000000000000000000001` 和固定 LocalID 1–6。项目 GuidCreator 强制设置 UUID variant 位，不会生成这一 GUID。场景、Prefab 和预处理文档仍保存普通 ResourceReference；内置引用不产生项目源文件依赖。Runtime 自行生成已布局的顶点/法线/UV/索引字节，Renderer 通过正常缓存上传，既不读取用户目录，也不依赖 Qt 或 Editor。保留命名空间中的未知 LocalID 会明确失败。

资源字段与选择器显示内置 Mesh 名称；因为没有用户源文件，定位源资源操作不可用。回归在 `Tests/Primitives`，覆盖几何绕序、实际 GPU 绘制、菜单路由、父级与命名、事务/撤销、场景保存恢复和运行时 Prefab 实例化。

Unity 参考：`Editor/Mono/Commands/GOCreationCommands.cs` 的 CreateAndPlacePrimitive 与 `Editor/Mono/ObjectFactory.bindings.cs` 的 CreatePrimitive：菜单只发创建命令，内置网格、渲染器与碰撞组件分别承担职责。
