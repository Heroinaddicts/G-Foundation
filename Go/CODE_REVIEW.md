# GFoundation 目录与代码检查

检查日期：2026-10-16。范围：当前仓库全部 Go 源码（约 3,200 行）、9 个 Go module、8 个原有构建脚本、Gitea 工作流、Dockerfile 和 Compose 配置。排除 Git 内部数据与二进制反编译。

## 结论

当前不能编译的直接原因是拆目录后的包名、import、module 依赖没有同步完成，而不是必须先拆 Git 仓库。构建已恢复；运行时仍有重要问题。本次以恢复构建和审计为范围，没有重写网络、HTTP、任务或 Redis 子系统。

建议先稳定接口和构建，再决定拆 Git 仓库。Git 独立不会自动解决 Go 插件的构建兼容性和生命周期问题。

## 当前目录与依赖

```text
GFoundation/                  框架与 main，1 个 module
  Foundation/                 调度、插件、任务、定时器
    Net/ Http/ Database/ Redis/
GFoundationApi/               公共契约，1 个 module
GFoundationUtils/             队列、缓冲、锁、系统函数，1 个 module
GFoundationTests/             实际为 5 个示例插件 + 1 个共享接口 module
Bin/                          可执行文件、插件、日志，已被 Git 跟踪
.gitea/ Dockerfile deploy/    构建部署
```

修复后的直接依赖为：框架 → API、Utils；示例插件 → API；HttpTest 额外依赖 Utils；FindModuleTest 额外依赖共享接口。示例插件不再依赖框架 main module。

## 本次已修复

1. `GFoundationApi/IDbProxyApi.go`、`IHttpApi.go`、`IRedisApi.go` 的 `package Api` 与同目录 `package GFoundationApi` 冲突，统一为后者。
2. DbTest、TcpClientTest、TcpServerTest 使用 API 类型却缺少 import，已补充。
3. 上述三个插件的 go.mod 仍依赖旧 GFoundation module，改为直接依赖和替换 GFoundationApi。
4. FindModuleTest 将共享接口引入为 `Interface`，与被引用 module 的 `GFoundationTests/Interface` 声明不一致，已统一。
5. RedisSession 缺少 IRedisSession 要求的 SetTimeOut，导致作为接口参数传递时无法编译，补充存储 timeout 的方法。**这只补齐接口，Redis 操作依然未实现，不能视为可用功能。**
6. 原脚本由 `sh` 执行，却使用 Bash 数组，Linux 的常见 POSIX shell 无法解析。统一为 POSIX sh，并修复目录引用和输出目录创建。
7. GetCurrentExeDir 原本放在 `_darwin.go` 中，Linux 编译时缺失。移至公共文件；新增 Linux GetThreadID 实现。
8. 修正数据库错误日志的格式占位符；删除示例插件中 panic 后的不可达调用，使 go test 附带的 vet 检查通过。
9. HttpTest 的硬编码 API 密钥引用改为 `DEEPSEEK_API_KEY` 环境变量。旧值仍可能存在于 Git 历史与旧二进制中，建议在服务商后台撤销旧密钥。
10. 新增 `go.check.sh`，统一检查所有 9 个 module。

## 需要优先修复的运行问题

### 1. [P1] TCP 关闭后发送直接 panic（已运行复现）

位置：`GFoundation/Foundation/Net/TcpSession.go:194-210`。

Close 关闭 sendChannel，Send 随后向该通道发送。即使 select 包含 default，向已关闭通道发送仍会 panic。最小复现：创建 session → Close → Send(data, true)，结果为 `send on closed channel`。仅检测 closed 后再发送也有并发竞态。

建议仅关闭生命周期通知通道，由发送循环自行退出；Send 需要明确拒绝关闭后的发送。再为重复关闭、关闭后发送、发送与关闭并发建立回归测试。

### 2. [P1] HTTP 取消向回调传递带 nil 指针的接口（已运行复现）

位置：`GFoundation/Foundation/Http/GHttp.go:69-72,191-194`。

executeRequest 在取消时返回 `(*GHttpResponse)(nil)`，回调参数却是 IHttpResponse 接口。转换后 `response == nil` 为 false，内部指针为 nil，调用 GetError 等方法会 panic。已用 Cancel → Send → Update 的本机程序复现，无需网络。

建议取消时返回明确的带取消错误的响应，或在接口转换之前显式传递 nil。统一约定请求完成回调是否恰好触发一次。

### 3. [P1] Redis 是尚未接通的实现

位置：`GFoundation/Foundation/Redis/RedisSession.go:53-75,130-142`、`Redis/ReidsCli.go:28-70`、`GFoundation/Foundation/GFoundation.go:11-18,68-75`、`GFoundationApi/GFoundationApi.go:3-13`。

Set/Get/Eval 的执行分支为空；创建 session 后没有启动 OptLoop；RedisCli 的 responseQueue 没有 Update 消费方法；框架没有持有、更新或暴露 Redis API。SetDisconnectedCallback 为空，SetTimeOut 仅存储字段。即使连接成功，结果回调也没有正常交付路径。

建议明确标为未完成，或一次完成“连接 → 工作循环 → 操作执行 → 主线程回调 → 超时和关闭”的完整链路，再对业务开放。

### 4. [P1] 数据库有序查询被静默丢弃

位置：`GFoundation/Foundation/Database/GDbSession.go:23-43`。

只有 mask == UnorderQuery 时才执行。其他 mask 不查询、不报错、不回调，业务只能一直等待。

建议实现按 mask 排队，或明确返回不支持错误；不要保留一个表面支持、实际不执行的参数。

### 5. [P1] 插件启动失败后，进程仍进入 Update

位置：`GFoundation/Foundation/GLogic.go:31-75`、`GFoundation/Foundation/GFoundation.go:60-62`、`GFoundation/main.go:27-35`。

Initialize 或 Launch 返回 false 后仅退出 GLogic.Launch。上层没有收到错误，主循环仍启动，已经登记的模块继续被 Update，且没有回滚已初始化模块。symbol 的类型也直接断言，签名不匹配时直接 panic。

建议 Launch 返回 error；启动失败按相反顺序释放已经初始化的模块，主程序以失败状态退出。插件符号使用带 ok 的断言并输出模块名和预期签名。

### 6. [P1] TCP 异步连接与关闭缺少统一状态机

位置：`GFoundation/Foundation/Net/TcpSession.go:52-76,136-144,204-211`。

Dial 尚未完成就调用 Close 时，Close 可能看到 nil 连接并消耗 closeOnce；稍后 Dial 成功又赋值并投递连接事件，后续 Close 已不会关闭新连接。连接字段也缺少跨 goroutine 的同步；拨号无显式超时或取消上下文。

建议使用可取消 DialContext，明确 connecting/connected/closing/closed 状态，以及连接归属；关闭后到达的连接必须立即释放。

### 7. [P1] TCP 接收的时间预算实际上永不生效

位置：`GFoundation/Foundation/Net/TcpSession.go:155-178`。

last 被设为常量 time.Microsecond，再用同一个常量减 last，差值始终为零；预期的超时让出逻辑不会运行，持续流量可能长期占用主循环。

建议以 time.Now/time.Since 计算真实时间，并明确预算的 Duration 单位。

### 8. [P1] Redis 关闭会重复 panic，退出路径可能永久阻塞

位置：`GFoundation/Foundation/Redis/RedisSession.go:62-75,139-142`。

Close 每次都会 close(stopchan)，第二次会 panic。若以后启动 OptLoop，停止分支 range requestchan，但没有对应 close(requestchan)，处理完已排队请求后仍会阻塞。isclosed 也在 goroutine 间无同步读写。

建议关闭幂等、用 context/退出通知协调工作循环，明确关闭后是否排空请求，并关闭底层 redis.Client。

## 其他需要调整的设计

### 9. [P2] 插件路径参数拼写不一致

位置：`GFoundation/Foundation/GLogic.go:36-39`。

检查 modules_dir，却读取 module_dir，设置模块目录不能按预期生效。建议统一参数名，并对空目录返回清晰错误。

### 10. [P2] 模块生命周期顺序不稳定，模块列表缺少验证

位置：`GFoundation/Foundation/GLogic.go:33-34,41-79`。

Initialize 按输入顺序执行，Launch、LaunchFinished、Update 却遍历 map，顺序不稳定。空 modules 会尝试加载 `.so`；空白、重复模块名也没有处理。

建议保留有序模块列表，map 仅用于查找；定义依赖顺序，验证空项和重复项。约定跨模块查找在哪个生命周期阶段可用。

### 11. [P2] Release 定义了但从未调用

位置：`GFoundationApi/GFoundationApi.go:15-20`、`GFoundation/main.go:29-35`。

主循环永久运行，没有信号处理和停止入口；模块、监听器、任务 worker、HTTP client 与数据库缺少统一关闭流程。

建议增加框架 Stop/Close，收到终止信号后停止接单、排空或取消任务，再按逆序释放模块和组件。

### 12. [P2] TCP listener 状态有数据竞争，失败返回还包含 typed nil 风险

位置：`GFoundation/Foundation/Net/TcpServer.go:25-46`、`Net/Neter.go:81-84`。

Close 写 closed，AcceptLoop 无同步读取；监听失败直接调用 err 回调，即使它为 nil。工厂把 `(*TcpServer)(nil)` 返回为 ITcpServer，调用方检查接口不为 nil 后仍可能崩溃。数据库 session/rows 的回调也存在同类 typed nil 转换。

建议工厂显式返回 `(对象, error)`，统一 nil 语义；用关闭监听器与 errors.Is(net.ErrClosed) 控制循环，避免共享裸 bool。

### 13. [P2] HTTP Cancel 没有取消底层请求

位置：`GFoundation/Foundation/Http/GHttpRequest.go:176-178`、`Http/GHttp.go:94-118`。

Cancel 仅设置标志，没有调用正在请求的 context cancel。等待响应头或阻塞读取时，需要等网络返回或超时才退出。

建议请求拥有可取消 context，同时定义取消后流事件和完成事件的交付规则。

### 14. [P2] 主线程回调与异步操作的边界不清楚

位置：`GFoundation/Foundation/Database/GDbProxy.go:45-56`、`Database/GDbSession.go:23-43,66-80`、`GFoundationApi/IDbProxyApi.go:3-6`。

sql.Open 仅创建连接池，没有 Ping，连接成功回调并不能证明数据库可达。Query 初始执行在后台，但 Next/Scan 在主线程回调中调用，读取后续数据库数据仍可能阻塞主线程。IDbRows 缺少 Err，无法区分遍历结束与中途错误，Rows 的回调内生命周期也没有 API 文档。

建议连接检查使用超时上下文；后台读取明确上限的数据或批次，再把结果交回主线程，并暴露遍历错误。

### 15. [P2] 任务并发与排队缺少限制和错误语义

位置：`GFoundation/Foundation/GTask.go:55-68,92-109,115-160`。

无序任务和 TaskGroup 每个任务都开 goroutine；有序任务队列满时 PushTask 阻塞调用线程；mask 除 -1 以外的负值可能产生负数组下标。完成回调和组完成回调没有 nil 检查；任务 panic 会终止整个进程。Start 重复调用或启动后继续添加任务也没有状态约束。

建议为输入、回调和任务组生命周期定义契约，采用有并发上限的 worker，提供排队失败或背压策略。有序 worker 直接循环取任务，避免每项任务完成后再创建新 goroutine。

### 16. [P2] “SPSCQueue” 实际用于多生产者，且队列可无限增长

位置：`GFoundationUtils/SPSCQueue.go:15-34,58-91`、`GFoundationUtils/SpinLock.go:11-14`。

Push 通过自旋锁串行化多个生产者，所以不能仅因为名称叫 SPSC 就认定现有多生产者调用必然数据竞争。但名称掩盖了实际 MPSC 用法；Capacity 是节点块大小，并不限制总长度；TryPush 永远成功；忙等锁在争用时持续消耗 CPU。多个结果队列没有总容量或积压策略。

建议明确单消费者约束，按真实语义命名，测量 Mutex/channel 与当前实现的性能；为积压设上限或可观测指标。

### 17. [P2] SPSCBuffer 零拷贝切片没有安全的生命周期约定

位置：`GFoundationUtils/SPSCBuffer.go:221-230`、`GFoundation/Foundation/Http/GHttpRequest.go:30-36`。

GetDataSlice 返回内部切片，但 Read 返回后就释放 readers 保护；后续消费和压缩会改变底层内存。保存切片或跨 goroutine 使用会有风险。HTTP 流回调消费同一 buffer 后，完成响应 GetBody 只能看到剩余数据，不保证是完整响应体。

建议提供复制接口或借用期限明确的回调接口，并说明流模式下 GetBody 的语义。

### 18. [P2] 定时器用函数代码地址标识 callback，不足以唯一表示闭包

位置：`GFoundation/Foundation/GTimer.go:114-134,137-150`。

不同闭包可能拥有相同代码地址、不同捕获环境；与相同 target 组合时可能互相替换或停止。target 参数接受 any，但实现只接受非 nil 指针，其他值静默忽略。

建议 StartTimer 返回 TimerID/handle，以 handle 暂停、恢复和停止；对非法参数返回错误。时间轮本身可以保留。

### 19. [P2] Go 插件构建产物必须作为一致的一组管理

位置：`go.build.sh`、`GFoundationTests/*/go.build.sh`、`Bin/`。

主程序与插件分别覆盖输出文件，中途构建失败会留下新旧混合产物。检查过程中，在共享包文件变动后加载未同步重建的主程序和插件，实际出现了 `plugin was built with a different version of package GFoundationUtils`；统一完整重建后通过。

建议在临时输出目录完成整组构建和加载检查，再整体发布。固定同一工具链、平台、CGO、构建选项和共享依赖版本。API 独立发版不意味着旧插件能任意搭配新宿主。

### 20. [P2] Docker 镜像没有保证构建平台和 libc 一致

位置：`Dockerfile:1-6`、`.gitea/workflows/main-build-deploy.yaml:17-25`。

当前将 runner 上编出的文件直接复制到 Alpine。若 runner 不是相同架构的 Linux，二进制不能运行；若 CGO/plugin 产物依赖 glibc，Alpine 的 musl 环境可能无法加载。此处是配置风险，没有实际 Docker 运行验证。

建议使用与运行环境一致的多阶段 Docker 构建，在同一阶段编译主程序和插件，并在目标镜像内验证加载。

### 21. [P2] 测试目录没有自动化测试

位置：`GFoundationTests/`，全仓库没有 `_test.go` 文件。

这些是交互式/长期运行的示例插件，DbTest 甚至没有数据库操作。go test 当前显示 no test files，不能证明运行正确。CI 也只构建和部署，没有自动回归或就绪检查。

建议先覆盖关闭与发送、取消、插件失败回滚、查询回调和顺序、定时器边界，再加入无外部服务依赖的集成测试。示例目录可改名 examples，以免误解。

### 22. [P2] 模块可本地联调，但还不具备独立发布条件

位置：所有 go.mod。

module 名称是本地短名，依赖使用 v0.0.0 和相对路径 replace；复制单个目录到另一个仓库后不能直接拉取依赖，replace 也不会作为传递配置自动继承。目前已有 9 个 module，维护成本较高。

建议 API 和框架保留明确 module 边界；发布时使用真实仓库路径和版本，开发时可使用 go.work。Utils 是否独立 module/仓库应取决于外部复用需求；若只是框架内部实现，可以放 internal。共享业务插件接口应放在对应业务的契约包中，避免通用名称 Interface。

### 23. [P3] 产物、日志和系统文件被 Git 跟踪

位置：`Bin/`、`.DS_Store`。

平台相关二进制和运行日志会造成频繁且不可审查的 diff，也会夹带旧配置或旧凭据。本次未删除或取消跟踪现有文件。

建议后续通过 release/CI artifact 管理二进制，日志和系统文件加入忽略规则；已被跟踪的文件需要另行从索引移除，单加 .gitignore 不会生效。

### 24. [P3] 命名、线程约束和默认行为需要整理

位置：`GFoundationApi/ITImerApi.go`、`Redis/ReidsCli.go`、`GFoundation/main.go:15-24`。

文件名存在 ITImer/Reids 拼写问题；接口方法对回调线程、是否可并发调用、参数单位和错误交付缺少一致文档。main 默认启动 pprof、锁定 OS 线程、使用固定循环休眠，缺少配置与性能依据。

建议优先写清“组件 API 由主循环调用、后台结果由 Update 交付”的规则，再调整命名。profiling 与调度参数应可配置，避免一开始进行大范围风格重写。

## 验证结果与边界

- 本机：Go 1.27.1，darwin/arm64，CGO_ENABLED=1。
- `sh ./go.build.sh`：主程序和 5 个插件全部成功。
- 9 个 module 分别执行 `go test ./...`：编译和默认 vet 检查成功；没有测试用例。
- 实际启动 DbTest、HttpTest、FindModuleTest：插件加载成功，跨插件 IHttpTest 调用输出 1000。
- 实际启动 TcpServerTest、TcpClientTest：本机 8888 端口建立连接，随后测试进程已结束。
- 独立临时程序复现 TCP 关闭后发送 panic，以及 HTTP 取消的 typed nil 响应；未改动项目运行逻辑。
- `GOOS=linux GOARCH=amd64 CGO_ENABLED=0 go build ./...`：框架所有包交叉编译成功。**这仅检查 Linux 源码编译，不代表 CGO 插件可用，也不是 Linux 动态插件或容器验收。**
- 未连接 MySQL/Redis、未发送 DeepSeek 请求、未执行真实部署；环境没有 Docker 命令，未验证容器。

## 建议处理顺序

1. 为 TCP 关闭、HTTP 取消、插件启动失败建立回归测试并修复。
2. 完成或明确隔离 Redis；修复数据库有序查询的静默丢弃。
3. 建立统一停止流程、线程契约、任务并发限制和错误语义。
4. 固定整组插件构建和发布环境，在目标容器验证加载。
5. 再整理 examples/internal/公共接口和真实 module 路径，最后决定 Git 拆仓及版本发布策略。

日常检查使用 `sh ./go.check.sh`，整组重建使用 `sh ./go.build.sh`。
