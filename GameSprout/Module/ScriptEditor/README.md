# 原生用户组件

`ScriptEditor` 负责源码创建、资产事件订阅、TaskApi 调度、类型目录和动态库替换；`ScriptCompiler` 负责源码快照、Clang AST、生成内部绑定、CMake 编译与缓存。界面只通过公开接口与事件协作，不查询功能模块的私有实现。

用户源码只需要继承 `GameSproutRuntime::ScriptBehaviour`，按需实现 `OnCreate`、`OnInitialized`、`Update`、`FixedUpdate`、`OnRelease`，最后写一行 `GAMESPROUT_SCRIPT(Test)`。创建模板直接给出这五个生命周期，不要求构造函数、构造继承、UUID 或字段注册表。

- `GAMESPROUT_SCRIPT(Type)` 只生成编译期类型标记，不执行注册和用户构造。默认构造时的 GameObject 归属由 SDK 内部注入。
- 游戏代码通过 `_GameObject->AddComponent<T>()`、`GetComponent<T>()`、`RemoveComponent<T>()` 使用脚本。每个对象同类脚本最多一个，重复添加返回 `nullptr`；`OnCreate` 中查询自身返回当前真实实例，也可以查询或创建内置组件。内置组件重复添加仍返回已有组件。
- 实例在 `OnCreate` 前已经登记并恢复作者字段。Runtime 完成本批节点、组件、字段与引用的装配，并提交 `OnCreate` 产生的结构修改后，调用一次 `OnInitialized`，之后才允许 `Update` 和 `FixedUpdate`。此时可以访问同批其他组件及其数据，但不保证其他组件已经执行完自己的 `OnInitialized`；后续新增组件不会重新初始化已有组件。可变帧调用 `Runtime.Update(deltaSeconds)`；固定步调用 `Runtime.FixedUpdate(fixedDeltaSeconds)`，两者独立，由宿主决定调度。停止执行或移除组件时调用 `OnRelease`，回调中的移除延迟到安全边界处理。
- 每个 `.hpp` 资源最多标记一个组件，可以包含其他辅助类。隐藏 TypeID 直接使用该源资源 `.meta` GUID；类、文件、目录改名不会改变身份。复制资源时由 AssetsManager 创建新 `.meta` GUID。源引用是同一 GUID 加固定 LocalID `1`。
- `ScriptBindingsGenerator` 调用本机 Clang JSON AST，识别标记的真实 C++ 类、访问权限和字段类型，再生成 `GameLogic.generated.cpp`。不使用正则猜测 C++ 声明，也不创建用户类实例来读取默认值。
- 支持的 public 实例字段自动序列化；private / protected 使用 `GAMESPROUT_SERIALIZE_FIELD` 标记。字段类型包括 bool、有符号整数、float/double、std::string、Vector3、ResourceReference 和 `IGameObject*` 场景对象引用。访问器通过每个声明类授权的友元生成，继承字段由其声明类处理；字段 ID 为 `声明类型::成员名`。字段更名视为新字段。
- 编译器拒绝派生类隐藏基类成员，覆盖 SDK 和用户定义基类，包括未标记的 private/protected 字段、静态字段、类型别名、枚举值以及非虚接口的同名函数。真正覆盖基类虚函数的实现可以使用；生命周期参数、返回值、const/static 等签名不匹配会编译失败，`using Base::Update` 也不能把错误生命周期重载变成有效实现。诊断包含声明位置、冲突名称和基类来源，不依赖等到 Play 执行才发现问题。
- `_GameObject` 是 SDK 已有的所属对象成员；用户预设引用使用 `_Target` 等独立名称。通过 `GetGameObject()` 可以明确取得当前脚本所属节点。声明检查不把所有普通局部变量或辅助函数重载当成错误，也不能静态证明任意运行时指针永远有效；对象引用为空或失效时仍需按运行逻辑处理。
- static、const、位域、任意原生指针和引用、其他未支持类型，以及需要调用函数或读取对象状态的初始化表达式不会自动持久化；Console 会解释跳过原因。`IGameObject*` 仅支持空默认值，指向的节点通过 Inspector 配置。未标记的非公开字段保留普通 C++ 运行时语义。
- 编辑态不执行用户构造与生命周期。Component 保存字段值及资源引用；用户实例仅在 Runtime 显式开启脚本执行后创建。Editor 由 PlayMode 协调运行会话、RuntimeHost 推进游戏循环，动画预览不等于游戏执行。
- `AddComponent()` 返回后可以继续配置属性和添加其他组件，不会在返回前调用 `OnInitialized`。脚本回调中新建的对象在本轮回调／遍历结束、结构提交后分批初始化；`OnInitialized` 中新增的对象进入下一批，初始化完成前不更新。非激活或禁用的脚本等待激活／启用后初始化。
- 宿主跨多次调用装配对象时，使用可嵌套的 `BeginConstruction(error)`／`EndConstruction(error)`。最外层提交前禁止模拟与渲染；场景加载、模型实例化和组件恢复已接入此边界。没有显式范围的外部新增组件在下一次正时间步前初始化；`Update(0)` 和仅派发事件不会启动待执行的用户脚本。`BeginExecution` 则主动完成启动初始化，不等待第一帧。
- 源文件事件触发排队编译；编译中发生的新事件提升版本号，旧任务结束后丢弃过期结果再编译。主线程先 Loading，再提交 TaskApi；完成、拒绝与释放均配对 Unloading。
- `.hpp` 入口聚合为一个 TU，每份 `.hpp` 应能独立 include。`.cpp/.cc/.cxx` 为辅助 TU；`.h/.hxx/.inl` 只复制并参与指纹，由入口按需 include。组件标记放在 `.hpp`，不放在 `.cpp`。
- 缓存指纹包含源路径、源 GUID、内容、SDK、绑定生成格式和工具链设置，只写进固定构建记录。相同输入且候选库校验通过时直接复用；源码或 SDK 改变时保留 CMake 对象文件，工具链改变时重建当前构建树。
- `Library/ScriptBuild/Sources/` 固定保存本次源码快照、反射探针和生成代码。内容相同不重写文件；删除或改名的源会同步移除旧快照。`Build/` 是固定 CMake 增量构建目录，候选库在 `Build/Output/`，成功记录在 `Build/ScriptBuildState.json`。
- `Info/GameLogic.json` 保存 assembly GUID；最终动态库仍平铺为 `Library/Artifact/<assemblyGuid>_1.dylib`（Windows `.dll`、Linux `.so`），没有额外 JSON 头。
- 加载区只有 `ScriptBuild/Loaded/SlotA/` 和 `SlotB/` 两个固定槽位。候选写入非当前槽，校验候选库内容、导出入口、ABI、类型与源 GUID 后，Runtime 原子替换类型及字段；旧库确认卸载后槽位才能复用。候选失败保留当前代码、作者字段和产物，卸载失败时保留句柄并拒绝覆盖占用槽位。游戏执行期间拒绝替换。
- 固定的 `Sources/GameLogic.buildstamp.cpp` 单独编译本次输入标识，加载器验证实际映射代码的标识，防止操作系统返回同路径的旧版本。修改辅助源仅额外编译这个小文件，不会为更新标识重编整个绑定文件。SDK 的构造上下文遵循 Runtime 宿主线程规则，不使用会让 macOS 模块驻留的原生 TLS；若系统仍保留旧映像，加载器明确拒绝发布并提示重启 Editor。
- 编译树和加载区分别通过项目内锁文件防止其他 Editor 进程同时写入；加载时还校验编译结果的库哈希，拒绝编译完成后被其他任务覆盖的候选文件。
- 删除源或编译失败不会静默删除场景作者数据；缺失类型继续保留组件身份和字段，直到资源恢复或用户明确移除。

当前自动字段生成需要具有 JSON AST 能力的 Clang/AppleClang GCC 风格驱动；GCC、MSVC、clang-cl 会明确诊断不支持，不把缺失反射当作成功。当前实现编译开发机器的本机库，不自动交叉编译。部署 Editor 和移动端代码装载、签名需要后续独立工具链支持。

编译器、CMake、构建工具、SDK、架构、Debug/Release、目标版本等由 CMake 配置提供，不依赖环境变量。宿主与游戏库必须使用同 SDK、兼容编译器/标准库/CRT 与配置。固定导出入口是 SDK 的 `GetGameSproutGameLogic`，格式由当前 `ScriptModuleABIVersion` 控制。

Unity 参考：`Editor/Mono/ProjectWindow/ProjectWindowUtil.cs` 将模板预处理与资源导入分开。这里由 ScriptEditor 创建源、AssetsManager 管理 `.meta`，原生编译和自动绑定由 ScriptCompiler 完成。

脚本字段与对象引用

```cpp
class Test final : public GameSproutRuntime::ScriptBehaviour {
private:
    GAMESPROUT_SERIALIZE_FIELD
    GameSproutRuntime::IGameObject* _Target = nullptr;
protected:
    GAMESPROUT_SERIALIZE_FIELD
    float _Speed = 1.0f;
public:
    void OnInitialized(GameSproutRuntime::IGameSproutRuntime* const runtime) override {
        if (_Target) _Target->GetTransform()->SetLocalPosition({0, _Speed, 0});
    }
};
GAMESPROUT_SCRIPT(Test)
```

- Compiler 同时产出无执行代码的 Schema 和本地字段桥接，`ScriptCompileResult.Types` 可直接供编辑器使用。加载器校验 Schema 文件哈希、构建标识和本地桥接字段一致性；子进程 Play 以后只需加载同一构建的动态库并消费值描述，不传函数地址或成员指针。
- `ScriptEditor` 的字段入口只接收 `ScriptFieldTarget`、字段 ID 和 `ScriptValue`，提供选择项、拖入校验、定位和字段快照。字段 Target 包含 SessionID、WorldID、ObjectID、ScriptInstanceID；Hierarchy 在开始拖动时通过 `ReadObjectTarget` 取得带 SessionID、WorldID、ObjectID 的源身份，接收方同时校验源与目标作用域。场景/Play 世界切换使旧命令与旧拖拽失效，即便新的世界重用了相同数值 ID。
- ScriptUI 根据初始选择读取一次字段快照，后续响应模块事件；选择列表只在用户打开选择器时遍历。View 不直接保存 `.scene`，字段修改经 ScriptEditor 调用 SceneManager 的编辑事务，支持 Undo/Redo。Play 期间修改只影响当前运行世界，不覆盖编辑场景。
- `.scene` 的 `SceneObject` 值是 `null` 或 `{ "ObjectFileID": n }`；FileID 0 代表场景根，不能与空引用混同。源文档不保存地址、对象名称或 Runtime InstanceID。首次只允许同场景引用；运行时临时对象没有可保存的 FileID，不能被拖进作者引用。
- SceneDocument 先创建完整树并把稳定 ID 交给 Runtime，再结束 construction。ScriptComponent 保存引用描述；Runtime 恢复本进程内的原生字段指针。目标删除时保留作者 FileID、界面显示 Missing，撤销重建原 FileID 后重新解析；显式清空才删除引用。登记字段之外的普通裸指针副本不获得自动失效保证。
- 当前不启动 Player 子进程。字段 Schema、作者引用以及命令/事件都采用值数据，为后续传输预留边界；进程内接口的 C++ 指针与 std::function 不作为 IPC 数据。
