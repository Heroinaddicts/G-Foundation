# CodeEditor 协调回归

本工程链接生产 CodeEditor 模块，通过预先编译的 Foundation 动态库启动真实 TaskApi。
只有测试后端负责记录生成和打开请求，不会启动 Xcode，也不修改用户项目或仓库 Bin。

```sh
cmake -S GameSprout/Tests/CodeEditor -B <测试构建目录> -G Ninja \
  -DCMAKE_PREFIX_PATH=<Qt安装目录> \
  -DCODE_EDITOR_TEST_FOUNDATION_DIRECTORY=<Foundation动态库目录>
cmake --build <测试构建目录> --parallel
ctest --test-dir <测试构建目录> --output-on-failure
```

覆盖范围：

- Assets 内已登记的原生源文件；拒绝绝对路径、越界路径、外逃符号链接、未登记文件、NUL 和错误后缀。
- 打开请求使用作者文件的绝对路径，工程路径固定；后台生成与主线程状态回调分离。
- 同一事件回合连续点击合并，准备过程中的新点击最终打开最新目标。
- 文件内容变化不重建工程结构；新增、改名、删除通过资源事件同步，不在 Update 中轮询。
- 正在准备的打开目标被删除时，错误报告仍不能阻止工程源列表同步。
- 生成失败和 IDE 启动失败各发布一次状态与 Console 错误，并配对 Loading/Unloading。
- 实际运行中的 Task owner 释放只请求取消；后端在 worker 结束后释放，不发生提前析构或迟到回调。

平台后端的真实 Xcode 工程生成与编译校验另见 `Module/CodeEditor/Tests`。
