# Ninja 并行构建

日常仍然只维护 `CMakeLists.txt`，在 Qt Creator 中点击原来的构建按钮。CMake 根据工程配置生成构建规则，Ninja 负责并行执行这些规则；不需要手工阅读或修改生成的 `build.ninja`。

Foundation、GRHI、GameSproutRuntime、GameSproutEditor、GameSproutHub、Module、View 和 Tools/ShaderTools 各自提供 `CMakePresets.json`，统一使用以下预设：

| 配置预设 / 构建预设 | 构建类型 | 中间文件目录 |
| --- | --- | --- |
| `ninja-debug` | Debug | 当前工程的 `build/Ninja_Debug` |
| `ninja-release` | Release | 当前工程的 `build/Ninja_Release` |

预设使用 CMake 3.21 支持的 schema v3；ShaderTools 自身仍要求 CMake 3.24。预设中不保存机器路径、环境变量或固定并发数。

## 并发规则

Ninja 默认并行执行。工程的 `Foundation/ParallelBuild.cmake` 自动查询本机逻辑核心数，将编译池上限和 Qt autogen 并发数设置为该值。构建预设不填写 `jobs`，普通构建无需附加 `--parallel 10`。

只有构建依赖已满足的任务才能同时执行；链接和其他依赖仍由构建图调度。该配置不承诺按核心数等比例缩短耗时。

## Qt Creator

在项目的构建配置中选择对应 Ninja 预设，并使用包含编译器和 Qt 路径的现有 Kit。Ninja 可以使用 Qt 安装目录中已有的工具，不要求另外安装或设置环境变量。

已经打开的旧配置不会仅因新增预设自动切换。在当前项目执行 `Build > Reload CMake Presets`，再导入并选择 `Ninja Debug` 或 `Ninja Release` 对应的配置。优先使用预设生成的独立 Kit，保留原 Kit 以及原有 Run/Deploy 参数，避免修改其他项目共用的全局 Kit。

如果预设导入阶段提示找不到 Ninja，可先按下方命令行示例显式指定本机 Ninja 和 Qt 路径完成配置，再通过 `Import Existing Build` 导入生成的 `build/Ninja_Debug` 目录。这样仍无需设置环境变量。修改已有 `.user` 配置文件时，应先正常关闭 Qt Creator 并备份文件，避免运行中的旧配置覆盖修改。

构建目录分别使用 `build/Ninja_Debug` 和 `build/Ninja_Release`。旧的 Unix Makefiles 缓存目录保留，不与 Ninja 共用。切换后检查有效配置中的 `CMAKE_GENERATOR` 为 `Ninja`、`CMAKE_MAKE_PROGRAM` 指向 Ninja。

此后继续使用 Qt Creator 的普通构建按钮即可；无需在每个工程手工填写核心数。

## 命令行

在需要构建的工程目录内运行：

```sh
cmake --preset ninja-debug
cmake --build --preset ninja-debug
```

Release 使用对应预设：

```sh
cmake --preset ninja-release
cmake --build --preset ninja-release
```

如果终端找不到 Ninja，或 Qt 未安装到 CMake 默认搜索路径，配置时显式传入本机路径即可：

```sh
cmake --preset ninja-debug \
    -DCMAKE_MAKE_PROGRAM:FILEPATH=/path/to/ninja \
    -DCMAKE_PREFIX_PATH:PATH=/path/to/Qt/platform
cmake --build --preset ninja-debug
```

显式路径只进入本地构建缓存，不写进公共预设。源文件自动发现、按目录显示工程树以及既有最终产物的 Bin 输出规则保持原有约定。新的目录隔离的是中间文件，不会把最终动态库改成另一套输出路径。

## 工程范围

GameSproutEditor 已经统一构建其 Module 和 View 插件，日常构建 Editor 时无需再单独重复构建 Module、View。它们的预设保留给独立插件开发使用。

Foundation、GRHI、Runtime 和 ShaderTools 继续独立构建，通过公共 SDK 与已有二进制接入 Editor；没有修改的上游不必每次重建。Ninja 不会自动补齐尚未构建的 IMPORTED SDK。单个 Module/View 的独立工程也可以使用 `-G Ninja` 配置，其现有公共并发设置会自动生效。

## 本机验证记录

2026-11-01，在本机 10 核、32 GB 内存环境下进行 Debug 验证：

- Editor 隔离目录从空目录构建，编译 223 个对象文件，约 50.3 秒；Ninja 日志和进程采样均确认峰值 10 个并行编译任务。
- Editor 隔离目录无改动再次构建约 0.8 秒，实际项目的 `build/Ninja_Debug` 无改动构建也通过。
- Runtime 构建成功，11 项 CTest 全部通过。
- Foundation、GRHI、Runtime、Editor、Hub、Module、View 的实际 Ninja Debug 配置均通过，编译池深度自动检测为 10。
- Qt Creator 普通构建按钮已在 Runtime 工程验证，执行 `cmake --build ... --target all`，由 Ninja 执行并成功结束，没有附加 `--parallel` 参数。

这些是当前代码和本机环境下的记录，不代表所有机器的固定耗时，也未据此推算其他生成器的加速倍数。
