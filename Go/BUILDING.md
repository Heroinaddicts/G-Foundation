# 构建

在仓库根目录执行，模式由根 `go.build.sh` 传到主程序和全部测试插件：

```sh
sh ./go.build.sh release
sh ./go.build.sh profile
sh ./go.build.sh debug
sh ./go.build.sh race
```

不传参数时默认为 `release`。所有模式继续输出到 `Bin/`，切换模式时从根目录整套重编，避免混用主程序和旧插件。

| 模式 | 用途 | 编译参数 |
| --- | --- | --- |
| release | 发布、性能测试 | Go 默认优化，`-ldflags="-s -w"` 删除符号及调试信息 |
| profile | 性能分析 | Go 默认优化，保留符号及调试信息 |
| debug | 断点调试 | `-gcflags="all=-N -l"` 关闭优化和内联 |
| race | 竞态排查 | `-race`，保留优化和调试信息；不用于性能对比 |

`profile` 是构建模式，不会自动发起 pprof 采集。`release` 的符号裁剪不等于进一步提高编译优化级别。

公共参数集中在 `go.build.common.sh`。插件额外使用 `-buildmode=plugin`；macOS 保留 `-extldflags=-Wl,-no_fixup_chains`，与模式对应的链接参数合并，Linux 不使用该平台参数。

主程序和插件应使用同一 Go 工具链及一致的共享依赖、构建标签和相关编译选项。`GOFLAGS` 等 Go 环境设置仍会生效，模式表描述的是脚本设置的参数。

也可直接构建某一部分，并显式指定模式：

```sh
sh GFoundation/go.build.sh release
sh GFoundationTests/go.build.sh release
sh GFoundationTests/RedisTest/go.build.sh release
```

`sh ./go.build.sh --help` 显示用法；未知模式或多余参数会在开始构建前报错。
