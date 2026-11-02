# Prefab 第一版

PrefabManager 管作者源文件、实例来源、覆盖、Apply/Revert 与源事件传播。SceneManager 只提供通用文档捕获/子树事务和 annotations 持久化，PrefabUI 注册既有 Project/Hierarchy 的通用扩展与 Inspector 区段，不创建独立 Dock。ObjectDocument 是 Runtime/Editor 共用的纯 C++ 对象图 codec，不依赖 Qt 或 Editor；Runtime 接受已导入的 BinaryValueLE1 模板并完成装配，GRHI 不认识 Prefab。

## 使用

将 Hierarchy 对象拖入 Project 目录，按对象名生成唯一的 `.prefab`；Hierarchy 右键入口可指定保存位置与名称，指定的目标已存在会失败。创建操作保存后等待 AssetsManager 登记身份和导入完成，再将原场景对象连接到新资产，不销毁重建原对象；等待期间的编辑保留为实例覆盖。AssetsManager 创建/恢复 `.meta` 和 GUID，AssetsPipeline 用 TaskApi 预处理到 `Library/Artifact/<guid>_<mainLocalID>.gsasset`。源节点和组件 ID 是模板内部身份，不各自产生一个 Artifact。

将 Project 的 Prefab 拖入 Hierarchy 创建实例，也可使用明确的右键实例化操作。来源与选择源资产入口位于对象 Inspector，实例根的 Overrides 按钮展开覆盖列表与 Apply All/Revert All；不会替换原有组件编辑页，锁定 Inspector 后操作仍针对锁定对象。实例化必须等待当前产物导入完成。普通字段编辑、组件/子节点变化形成覆盖；来源更新合并未覆盖部分，尽量复用 Runtime 对象。根名称、Transform 位置/旋转和 RectTransform AnchoredPosition 是实例摆放值，不进入 Apply All，也不会被 Revert 覆盖。

双击源资产或 Inspector 的 Open 进入 PrefabStage 独立编辑；来源保存由独立 Stage Module 处理，不向当前场景添加实例。SavePrefab 是纯导出语义，面向普通创建交互的 CreatePrefab/CreatePrefabInDirectory 才采用保存并连接语义。

Apply 原子写回源文件、保留 GUID 和既有源对象/组件 ID，将新增实例对象提升为新的源 ID，并保持它在当前场景中的身份；其他实例经相同合并路径更新。无法合并的实例保留原内容并报告冲突，不丢弃作者覆盖。源暂时缺失保留连接和基线；恢复后继续更新。

## 当前格式与身份

源格式为 `{Version:1, Format:"GameSproutPrefab", Base:null, NestedInstances:[], Data:{...对象文档...}}`。

Data 是单根对象图，GameObject 和 Component 均有非零稳定 ID，共用文档 ID 分配高水位。脚本 InstanceID 仍是该节点内的脚本挂载身份，与组件文件 ID 区别。资源使用 GUID+LocalID；脚本对象引用只允许指向模板内部。复制实例时只重映射对象/组件及其内部引用，Mesh/Texture 等资源继续共享身份。

源本地 ID 使用低 63 位；实例新加对象在覆盖计算时使用带实例标记的临时身份，Apply 后获得单调分配的源 ID。删除后不复用源身份。场景 `Annotations.PrefabInstances` 保存 Source、RootFileID、Mappings、Baseline、SourceHash、Overrides；运行对象数据仍由同一 Runtime 对象树捕获。场景管线导入先读取最新 Prefab 源并合并覆盖、记录源依赖，再剥离 annotations；作者场景文件不会被导入任务改写，Player 不运行来源追踪和 Apply/Revert。

保存子树时，只在导出的源副本中清空指向子树外的场景对象引用（含场景根）：脚本对象字段为 null，Animator/蒙皮对象绑定为 0；内部引用继续重映射，GUID+LocalID 资源引用保持。原场景对象的引用不变。创建并连接后，原有外部场景引用保留为实例覆盖，其他新实例使用源中的空引用；Revert 会恢复为空。这种外部引用不能 Apply 回 Prefab，拒绝时不修改源文件。

Nested 与 Variant 的数据边界已预留，本版不解释非空 Base/NestedInstances，也禁止把 Prefab 插入另一个实例内部。另存现有实例会保存当前完整对象图为独立模板，不生成 Variant。没有旧格式/旧目录迁移分支。

## 事件与生命周期

只在明确操作、选择、场景状态、资源及 Play 事件后处理。没有 Update/定时器遍历查询刷新 UI。事务回调仅传身份和值；原生脚本在完整引用装配之后初始化。Play 期间拒绝作者事务，停止恢复编辑场景后重新检查资源。释放时先取消派发、注销监听与扩展，再释放控件和服务借用。

Runtime 的同步 InstantiatePrefab/InstantiatePrefabData 与现有 Model 装配一样，只接受宿主安全点调用，不允许在 Runtime 生命周期/遍历回调中重入。本版没有增加游戏脚本回调内的异步 Prefab 生成队列。
