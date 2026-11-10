# 原生用户组件

`ScriptEditor` 负责源码创建、资产事件订阅、TaskApi 调度、类型目录和动态库替换；`ScriptCompiler` 负责源码快照、Clang AST、生成内部绑定、CMake 编译与缓存。界面只通过公开接口与事件协作，不查询功能模块的私有实现。

在 `.h` 中声明继承 `GameSproutRuntime::ScriptBehaviour` 的脚本类，在类声明之后写 `GAMESPROUT_SCRIPT(Test)`。创建模板提供 public 虚析构函数，并在 protected 中用 `override` 声明 `OnCreate`、`OnInitialized`、`EarlyUpdate`、`Update`、`LateUpdate`、`FixedUpdate`、`OnRelease`。生命周期由 Runtime 调用，实现在同名 `.cpp` 中。UUID 与字段注册表由工具生成。

## 游戏模块

`Create → Script` 下提供 `Script` 和 `Module`，两者都生成同名的 `.h` 和 `.cpp`。Module 的 `.h` 声明模块接口、实现类和 `GAMESPROUT_REGIST_MODULE(Type)` 标记。两种源码共用导入和 GameLogic 构建，编译结果分别提供模块和组件脚本的注册表。

模块的名称是实现类的完整 C++ 名称，源文件仍由 `.meta` GUID 标识。例如 `runtime->FindModule<ITestModule>("TestModule")` 返回游戏会话内的借用指针。Runtime 先登记全部模块实例，然后依次调用全部 `Initialize`、`Launch`、`LaunchFinished`，再初始化对象脚本。各帧阶段先调用模块，再调用该阶段的组件。Stop 先释放对象脚本，再逆序释放模块。

- Script 和 Module 在类内授权 `GameSproutRuntime::Internal::GameplayFactory<Type>`，并声明接收 `IGameSproutRuntime*` 的私有构造函数。工厂在创建实例时传入该实例所属的 Runtime。模板将指针保存到私有的 `_Runtime`，供普通业务方法使用。Script 的 GameObject 归属仍由工厂建立。
- 类外注册宏只声明编译期标记。构造函数可绑定 Runtime 和初始化成员。场景引用在 Script 的 `OnInitialized` 中读取，Module 可在 `Initialize` 中查找同一会话的其他模块。
- 游戏代码通过 `_GameObject->AddComponent<T>()`、`GetComponent<T>()`、`RemoveComponent<T>()` 使用脚本。每个对象同类脚本最多一个，重复添加返回 `nullptr`；`OnCreate` 中查询自身返回当前真实实例，也可以查询或创建内置组件。内置组件重复添加仍返回已有组件。
- 实例在 `OnCreate` 前已经登记并恢复作者字段。Runtime 完成本批节点、组件、字段与引用的装配，并提交 `OnCreate` 产生的结构修改后，调用一次 `OnInitialized`，之后才允许 `EarlyUpdate`、`Update`、`LateUpdate` 和 `FixedUpdate`。此时可以访问同批其他组件及其数据，但不保证其他组件已经执行完自己的 `OnInitialized`；后续新增组件不会重新初始化已有组件。宿主通过 `Runtime.AdvanceFrame(deltaSeconds, fixedStepCount, fixedDeltaSeconds)` 推进完整帧：全体 `EarlyUpdate` → 零到多次（全体 `FixedUpdate` → 物理）→ 全体 `Update` → 独立动画求值及相关通知 → 全体 `LateUpdate`。`EarlyUpdate` 是 GameSprout 自定义的脚本前置阶段，用于本帧输入锁存；Unity 的同名 PlayerLoop 阶段并非 MonoBehaviour 回调。`Runtime.Update(deltaSeconds)` 推进不含固定步的完整变量帧；`Runtime.FixedUpdate(fixedDeltaSeconds)` 保留为低层单固定步入口，完整游戏循环优先使用 `AdvanceFrame`。停止执行或移除组件时调用 `OnRelease`，回调中的移除延迟到安全边界处理。
- 每个 `.h` 最多标记一个组件或模块，可以包含其他辅助类。注册身份来自 `.h.meta` 的 GUID，源引用使用该 GUID 加固定 LocalID `1`。保留 `.h.meta` 的 GUID 即可在改名或移动后维持原有引用。`.cpp` 也有自己的资产身份，供源码追踪和构建使用。
- `ScriptBindingsGenerator` 调用本机 Clang JSON AST，识别标记的真实 C++ 类、访问权限和字段类型，再生成 `GameLogic.generated.cpp`。不使用正则猜测 C++ 声明，也不创建用户类实例来读取默认值。
- 支持的 public 实例字段自动序列化；private / protected 使用 `GAMESPROUT_SERIALIZE_FIELD` 标记。字段类型包括 bool、有符号/无符号整数、float/double、枚举、std::string、Vector2/3/4、Quaternion、Color、ResourceReference、`IGameObject*` 和类型明确的组件指针。结构体/普通数据类递归展开；C 数组、std::array、std::vector、std::list 与 std::map/std::unordered_map 保存逻辑元素，不保存容器地址、分配器或内部节点。访问器通过每个声明类授权的友元生成，继承字段由其声明类处理；字段 ID 为 `声明类型::成员名`。字段更名视为新字段。
- 编译器拒绝派生类隐藏基类成员，覆盖 SDK 和用户定义基类，包括未标记的 private/protected 字段、静态字段、类型别名、枚举值以及非虚接口的同名函数。真正覆盖基类虚函数的实现可以使用；生命周期参数、返回值、const/static 等签名不匹配会编译失败，`using Base::Update` 也不能把错误生命周期重载变成有效实现。诊断包含声明位置、冲突名称和基类来源，不依赖等到 Play 执行才发现问题。
- `_GameObject` 是 SDK 已有的所属对象成员；用户预设引用使用 `_Target` 等独立名称。通过 `GetGameObject()` 可以明确取得当前脚本所属节点。声明检查不把所有普通局部变量或辅助函数重载当成错误，也不能静态证明任意运行时指针永远有效；对象引用为空或失效时仍需按运行逻辑处理。
- static、const、位域、任意拥有内存的原生指针和引用、其他未支持类型，以及需要调用函数或读取对象状态的初始化表达式不会自动持久化；显式标记的不支持字段导致编译失败。GameObject/组件指针仅支持空默认值，目标通过 Inspector 配置。未标记的非公开字段保留普通 C++ 运行时语义。
- 编辑态不执行用户构造与生命周期。Component 保存字段值及资源引用；用户实例仅在 Runtime 显式开启脚本执行后创建。Editor 由 PlayMode 协调运行会话、RuntimeHost 推进游戏循环，动画预览不等于游戏执行。
- `AddComponent()` 返回后可以继续配置属性和添加其他组件，不会在返回前调用 `OnInitialized`。脚本回调中新建的对象在本轮回调／遍历结束、结构提交后分批初始化；`OnInitialized` 中新增的对象进入下一批，初始化完成前不更新。非激活或禁用的脚本等待激活／启用后初始化。
- 宿主跨多次调用装配对象时，使用可嵌套的 `BeginConstruction(error)`／`EndConstruction(error)`。最外层提交前禁止模拟与渲染；场景加载、模型实例化和组件恢复已接入此边界。没有显式范围的外部新增组件在下一次正时间步前初始化；`Update(0)` 和仅派发事件不会启动待执行的用户脚本。`BeginExecution` 则主动完成启动初始化，不等待第一帧。
- 源文件事件触发排队编译；编译中发生的新事件提升版本号，旧任务结束后丢弃过期结果再编译。主线程先 Loading，再提交 TaskApi；完成、拒绝与释放均配对 Unloading。
- `.cpp/.cc/.cxx` 分别编译。Clang 从源码编译单元发现注册宏，生成绑定时只包含注册类型所在的 `.h`。每份注册头应能独立被包含。只用到其他类型的指针或引用时可以在头文件前置声明，在实现文件里再包含其头文件。
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
    friend struct GameSproutRuntime::Internal::GameplayFactory<Test>;
    explicit Test(GameSproutRuntime::IGameSproutRuntime* runtime) : _Runtime(runtime) {}
    GameSproutRuntime::IGameSproutRuntime* const _Runtime;

    GAMESPROUT_SERIALIZE_FIELD
    GameSproutRuntime::IGameObject* _Target = nullptr;
protected:
    GAMESPROUT_SERIALIZE_FIELD
    float _Speed = 1.0f;
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

递归字段与组件引用

```cpp
struct MovementOptions {
    float Speed = 1.0f;
    GameSproutRuntime::IRigidbodyComponent* Body = nullptr;
};
class Mover final : public GameSproutRuntime::ScriptBehaviour {
private:
    friend struct GameSproutRuntime::Internal::GameplayFactory<Mover>;
    explicit Mover(GameSproutRuntime::IGameSproutRuntime*) {}

protected:
    GAMESPROUT_SERIALIZE_FIELD
    MovementOptions _Options;
    GAMESPROUT_SERIALIZE_FIELD
    std::vector<MovementOptions> _Targets;
    GAMESPROUT_SERIALIZE_FIELD
    std::map<std::string, MovementOptions> _Presets;
};
GAMESPROUT_SCRIPT(Mover)
```

- `IRigidbodyComponent*` 是 Runtime 持有组件的借用引用；不能写成 `IRigidbodyComponent` 值成员，后者是不可实例化的抽象类。普通数据结构体保存内容，GameObject/Component 保存身份，两者没有混用所有权。
- Dictionary 键支持 bool、整数、string 和枚举；修改键必须保持唯一。固定数组不可改变长度，vector/list 可调整长度、插入、删除与移动；递归编辑只提交当前路径的变化，并以根字段为一次 Undo 事务。
- 组件作者引用保存 `ComponentFileID`。对象和组件共用文档内唯一 ID 空间，实际组件 ID 不依赖类型名称或列表位置。运行中临时组件使用 Scene/Object/Component 的会话内身份，这些值不能写进源文件。
- Scene/Prefab 先创建所有节点与组件，再发布完整组件身份映射。每个 Prefab 实例重新映射整棵值树中的对象/组件引用，包含结构体、数组和字典内部引用。未来 Player 在自己的进程恢复映射，不接收 Editor 内存地址。
- 普通数据类按值保存，不隐式支持拥有任意裸指针、共享所有权或循环对象图；这类声明必须采用单独的显式所有权/对象图协议，不能误当成引擎组件引用。
