# 外部 IDE 的 GameLogic 构建入口

`GameSproutScriptBuild --ProjectRoot=<项目绝对目录>` 调用同一个 ScriptCompiler，
执行源快照、Clang AST、字段桥接、原生编译和缓存验证。它只使用 QtCore 和编译模块，
不启动 Editor，不创建 Runtime，不执行用户脚本。

工具安装在编辑器可执行文件同级的 `Tools/GameSproutScriptBuild`。
Xcode 的 GameLogic 构建目标调用该工具，索引及编辑文件引用项目的真实 `Assets`。
实际构建继续使用固定 `Library/ScriptBuild/Sources` 和 `Build`，
产物为 `Build/Output/libGameLogic.dylib`，不会把现有增量目录改成 Xcode generator。
同目录的 `GameLogic.schema.json` 保存字段值描述与 BuildID，Editor 无须执行脚本构造函数即可读取；
编译结果同时返回 Types、SchemaPath、SchemaHash，缓存命中也返回完整字段描述。

每次构建只读扫描当前 native 源文件及其 `.meta`，读取已有的源 Guid 和
`Info/GameLogic.json` assembly Guid。缺少身份、格式错误或重复 Guid 会明确失败，
由 AssetsManager/ScriptEditor 管理这些文件；工具不生成或修复 `.meta`、Guid。
编译诊断映射回可编辑的 Assets 路径。

Script 和 Module 的注册宏写在 `.h`，注册身份取自该头文件的 `.meta`。
实现文件分别编译，和头文件一起参与增量构建。

Editor 和 IDE 共享 `.compile.lock`；工具最多等待 60 秒，等待与编译均支持取消。
SIGINT/SIGTERM 经取消回调进入现有子进程树清理流程，清理后才释放构建锁。
工具不发布或加载候选 DLL，Editor 继续管理 Artifact 发布与双槽加载，
运行期间的代码替换保护仍由 ScriptEditor/PlayMode 负责。

退出码：0 成功或缓存命中，2 参数无效，3 项目或身份数据无效，
4 编译失败/锁等待超时，5 未预期错误，130 已取消。

真实 CLI 回归在 `Tools/Tests`，覆盖原生构建及缓存、中文/空格路径、身份只读、
重复 Guid、Assets 路径诊断、锁等待、等待取消和实际 Clang 子进程取消。
