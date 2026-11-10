# 原生脚本编译与装载回归

独立测试工程，编译 ScriptCompiler/ScriptEditor 到测试构建目录，不写仓库 `Bin`。临时项目及 Library 位于测试可执行文件旁的 `TestProjects`；成功后移除，失败时保留并输出路径，便于检查候选库、快照和构建记录。需要同 SDK 独立构建的 Runtime。装载事务夹具使用 Clang/GCC 的测试访问开关，生产 Module 的访问权限不变。

```sh
cmake -S GameSprout/Tests/NativeScripts -B /tmp/gamesprout-native-checks -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/Users/max/Qt/6.12.0/macos \
  -DSCRIPT_TEST_RUNTIME_DIRECTORY=/tmp/gamesprout-native-script/runtime
cmake --build /tmp/gamesprout-native-checks --parallel 4
ctest --test-dir /tmp/gamesprout-native-checks --output-on-failure
```

`SCRIPT_TEST_RUNTIME_DIRECTORY` 传动态库所在目录。Windows/Linux 按本机 Qt、工具链及 Runtime 路径配置。

脚本和模块的原生工厂在构造时传入所属 Runtime。新建脚本的 `.h` 使用私有构造函数、对应类型的工厂友元和 `GAMESPROUT_SCRIPT(Test)`。类型身份取自头文件的 `.meta` GUID。Clang AST 自动识别支持的公开实例字段。私有和受保护实例字段使用 `GAMESPROUT_SERIALIZE_FIELD`。字段身份为 `声明类::成员名`。故意损坏的描述和异常桥接仅用于内部测试。

```cpp
class Test final : public GameSproutRuntime::ScriptBehaviour {
private:
    friend struct GameSproutRuntime::Internal::GameplayFactory<Test>;
    explicit Test(GameSproutRuntime::IGameSproutRuntime*) {}

    GAMESPROUT_SERIALIZE_FIELD
    GameSproutRuntime::IGameObject* _Target = nullptr;
protected:
    GAMESPROUT_SERIALIZE_FIELD
    float _Speed = 1.0f;
};
GAMESPROUT_SCRIPT(Test)
```

基类字段也由声明类自己的友元访问器恢复。派生类再次声明与基类同名的成员会在编译阶段报错，包括 public/private/protected 字段、静态字段、类型别名和注入类作用域的枚举值；不因未标记序列化而放行。真正覆盖基类虚函数的实现允许通过，生命周期参数或 const 限定错误、非虚接口同名隐藏均拒绝。未标记的 private/protected 字段、静态成员不参与持久化；显式标记不支持的类型会导致编译反射失败并指出字段。对象引用只接受 `IGameObject*`，默认必须为空，运行时通过所属场景的 FileID 映射解析，不保存地址。

`Build/Output/GameLogic.schema.json` 是独立的工具字段描述，包含 BuildID、字段身份、类型和默认值，不包含函数或对象地址。`ScriptCompileResult.Types` 在编译和缓存命中时都返回该值数据。GameLogic 模块的 BuildID 与描述相同，元数据文件损坏会重建缓存；没有旧格式迁移路径。

`NativeScripts.PrivateProtectedReferences` 使用真实 Clang 生成、真实 GameLogic 动态库和 Runtime，覆盖 private/protected、公有继承、不同声明类的字段身份、int64/LocalID 精度、Schema 缓存修复、初始化前绑定、销毁失效和非法显式标记诊断。该目标及其生成的 GameLogic 不关闭 C++ 访问检查。

`NativeScripts.DeclarationDiagnostics` 检查继承契约：普通及序列化 `_GameObject` 字段、间接基类、静态字段、类型别名、枚举值、嵌套类型、用户基类 private/protected/public 字段、隐藏 `GetGameObject()`、生命周期同名数据、错误生命周期参数/const/static/using 重载均须拒绝，并指出源文件、成员及基类声明。正确的虚函数实现、正常继承、无关辅助函数重载、作用域枚举和无继承关系的辅助记录保持可用。每次拒绝后均验证上一成功动态库与 Schema 没有被覆盖，恢复正确源码后可以重新编译。

固定目录约定：

| 路径 | 用途 |
| --- | --- |
| `Library/ScriptBuild/Sources/` | 当前源镜像、反射探针、自动绑定及小型构建身份 TU；相同内容不重写。 |
| `Library/ScriptBuild/Build/` | 持续复用的 CMake 构建树和对象文件。 |
| `Build/Output/libGameLogic.dylib` | 当前候选库，未映射前可以被下次编译覆盖。Windows/Linux 使用对应名称和后缀。 |
| `Build/ScriptBuildState.json` | 最近成功输入、产物哈希和镜像校验信息。 |
| `Loaded/SlotA/`、`Loaded/SlotB/` | 两个固定加载槽，首次使用 A，版本替换时交替复用。 |
| `Library/Artifact/<assemblyGuid>_1.dylib` | 最后成功发布的平铺产物。 |

测试检查指纹只作为内容身份，不产生逐版本 hash 目录。它通过真实对象文件时间戳、已加载代码的运行结果及额外 QLibrary 租约验证行为，而不只检查返回值。

覆盖的主要情景：

- 成对创建同名 `.h` 和 `.cpp`，拒绝同名源、目录越界和 C++ 关键字。编译真实 GameLogic，验证 Script 和 Module 的私有构造函数均收到当前会话的 Runtime。OnCreate 内自添加返回空、查询自身返回 this。隐藏游戏库可以查询宿主 Mesh、重复添加返回已有 Mesh、执行内置接口 dynamic_cast，并添加、查询和移除 Image。
- 自动发现七个公开字段及默认值、跳过私有字段、报告不支持类型；辅助 `.cpp` 与入口头共同编译，`.inl` 只由类内包含；显式 FixedUpdate 与普通 Update 分开。
- 相同输入命中缓存。改独立辅助 `.cpp` 只重编该对象，不改写聚合绑定或重编无关 TU；改角色头和多轮代码版本时独立对象、未改源镜像的 mtime 保持。删除源及空目录会同步清理镜像。
- 损坏未映射的候选输出后，相同输入自动失效并重新链接，不把坏库当缓存命中，也不重编无关对象。
- A/B/A/B 多轮使用私有代码常量改变实际运行结果，证明复用槽加载了新代码；文件哈希不匹配或已映射 image 的构建身份不匹配都拒绝，当前库、作者字段与 Artifact 保持原值。
- 外部 QLibrary 租约占用旧槽时，连续两次复用仍拒绝，槽文件不被覆盖，外部函数仍可调用；租约释放后同一候选可成功重试。
- 重复字段描述、原生描述与纯 Schema 不一致、执行期间替换、编译错误与取消不会破坏上一成功版本；失败候选释放后槽位可继续复用。热重载保留作者值并初始化新字段，类改名保持源 GUID。
- A 成功后，B 修改字段及代码并在原生后端编译失败，再恢复 A。即使旧成功记录与旧 Output 仍有效，Sources 和生成绑定也必须精确恢复 A，不能以 cache hit 掩盖 B 的残留快照。

普通脚本没有被实例化来推测字段；反射成功也不代替真实编译、链接和动态加载。

## 递归字段与组件引用服务

`NativeScripts.FieldService` 还通过 `ScriptRecursiveFieldsChecks.h` 验证 ScriptEditor 的递归值事务：结构成员路径修改和 Undo/Redo、动态序列增删/改长/重排、固定数组长度约束、字典键判重及值中继续嵌套容器、无符号 64 位枚举精度。引用测试覆盖组件类型过滤、多个匹配时拒绝对象拖拽、明确组件选择后分配作者 FileID、丢失引用保留与清空、Play 引用不分配作者 ID。

测试场景与事务宿主提取在 `ScriptFieldModuleTestServices.h`，供真实 `.ui` 的递归字段集成测试复用。生产 Module 和 View 不依赖测试宿主。
