# GameLogic 外部代码工程

Assets 中双击原生源文件（包括 `.hpp`），或双击脚本组件的源文件路径，
由 ScriptUI 的打开扩展调用 `ICodeEditor::OpenSource`。资源浏览器不直接启动 IDE。

macOS 使用 Xcode，工程固定为：

```text
<Project>/Library/ScriptBuild/Xcode/GameLogic.xcodeproj
```

工程引用 `<Project>/Assets` 中的真实文件，按目录分组；修改会正常触发资源监听。
Runtime SDK 提供 C++20 索引与类型补全。缓存 `Sources` 不作为用户编辑的文件。
源文件新增、移动和删除由资源事件同步工程清单，内容修改不重写工程文件。
首次打开和显式打开会验证当前工具链；重复打开复用工程。

Xcode 的 `GameLogic` Scheme 调用 Editor 随附的
`Tools/GameSproutScriptBuild --ProjectRoot=<项目绝对目录>`，复用 ScriptCompiler
已有的 Clang 反射、字段桥接、构造桥接和原生动态库编译。
`GameLogicIndex` 仅提供索引，不参与默认构建。
工程只提供当前 ScriptCompiler Kit 的配置，避免显示 Release 而实际仍编 Debug。

```text
ScriptBuild/
  Sources/                  编译用源码快照及生成桥接
  Build/                    原有增量编译树
    Output/libGameLogic.dylib
  Loaded/SlotA、SlotB/       Editor 加载区
  Xcode/GameLogic.xcodeproj  外部编辑工程
```

Xcode 与 Editor 共用编译锁、缓存和同一个输出，正常并发构建可取消地等待最多 60 秒。
Xcode 不改 Editor 加载槽位；Editor 在允许重载时发布代码，进入运行前也会验证最新代码。
CLI 只读现有源 `.meta` 与 `Info/GameLogic.json` 的身份，缺失时要求先在 Editor 导入，
不会为了构建另造 GUID。Xcode 构建无需启动 Editor 窗口。

CodeEditor Module 管源清单、任务与错误；私有 CodeEditorBackend 抽象负责平台工程生成和打开。
目前工厂仅接入 macOS Xcode，其他平台可以增加后端，不改 ScriptUI、Runtime 或 GRHI。
生成和打开通过 TaskApi 在工作线程执行；主线程配对 Loading/Unloading 并推送状态。
每项任务在工作线程内创建、销毁后端，取消注册不依赖阻塞等待，也不借用释放中的后端对象。

回归涵盖真实 Xcode 构建和增量缓存、真实类型/字段桥接、路径及目录树、无变化复用、
增删移动、编译错误、共享锁与取消、资源打开路由和异步模块生命周期。
