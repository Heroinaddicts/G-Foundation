# API 压测模块

所有负载通过公开的 `GFoundationApi` 接口发起。框架 API 调用、回调校验、统计和定时器操作都在主线程；Task 的任务体在框架工作线程执行，不从任务体调用其他 API。CSV 写盘线程不调用框架 API。

| 模块 | 负载和检查 | 默认每秒投放 / 上限 |
| --- | --- | --- |
| TimerTest | 随机生命周期、多目标定时器；Start/Beat/End、Pause/Resume、自停止、计数和调度延迟 | 2000 个定时器 / 20000 个活跃目标 |
| TaskTest | 有序 mask、无序任务、任务组、成功与失败的分组汇总；计算结果、回调顺序和完整性 | 10000 次投放 / 4000 个未完成记录 |
| TcpClientTest | 连接反复建立关闭；随机存活、定期发包；拆包与粘包、二进制内容、顺序、回显 RTT、重复 Close、关闭后 Send | 1000 个连接 / 1000 个 Session |
| TcpServerTest | 使用 INetApi 建立监听和接受连接，并用 INetApi 客户端在同进程施压；校验服务端上下文和 Echo 数据 | 1000 个连接 / 1000 个客户端 Session |
| HttpTest | GET、POST、JSON、流式、同 mask 顺序、排队取消、读流中取消、超时、503；校验参数编码、头、正文、上下文 | 1000 个请求 / 1000 个未完成请求 |
| DbTest | 真实认证建连、随机存活、参数化多行 SELECT、SQL 错误、每次查询校验 CONNECTION_ID 与 FIFO | 100 个连接 / 100 个 Session |
| FindModuleTest | 模块身份、缺失模块、参数读取、六个 API getter；可指定 HttpTest 检查跨插件接口身份 | 100000 次投放 / 每次含四组同步调用 |
| RedisTest | 保留现有功能检查和按 Session 定时 SET/GET/EVAL 的连接轮换负载 | 沿用现有参数；一键脚本默认连接上限 1000 |

TaskTest 的一次“投放”可能是一个普通任务，或一组任务（默认 16 个）；实际 task 数和 group 数分别统计。TimerTest 默认 10% 目标在第二次 Beat 自停止，其余按随机生命周期自然结束；其中一部分还会暂停 20ms 后恢复。

TaskTest 在开始计时前预先计算校验值，投放和完成回调中不重复执行任务的计算循环；每个 worker 仍执行 `task_iterations` 轮计算。报告记录 `expected_values_precomputed=true`，与旧测试的延迟比较时要考虑投放端负载已减少。

HTTP Cancel 按尽力取消处理，不要求运行中的请求一定中断或返回 nil。取消场景分别记录 `cancel_nil_responses`、`cancel_error_responses`、`cancel_completed_responses`；正常完成也是有效结果。typed-nil 接口、重复/缺失回调和错误的请求上下文仍作为失败处理。

TcpServerTest 的上限是客户端数，同进程还有对应的服务端 Session，因此两端 Session 总量约为两倍。服务端 `SERVER_ECHO_ENQUEUE` 衡量接收到调用 Send 返回的耗时，真正的往返耗时看客户端 `ECHO_RTT`。

DbTest 使用新的 `Query(callback, query, args...)` 接口。同一 Session 的多个请求必须串行、按提交顺序回调；报告中的 CONNECT 现在包含物理连接与认证，RELEASE_RETURN 只表示 Release 调用返回，不表示收到断开通知（现有接口没有这个回调）。默认查询不建表、不写业务数据。预检会检查 NULL、数值、日期、二进制与空 BLOB、参数快照、nil 回调、Release 排空及拒绝新请求；还会通过另一条测试连接 KILL 本测试自己创建的连接，验证失效后不会自动重连。

## 一键编译和运行

在仓库根目录执行：

```sh
export DB_PASSWORD='你的测试库密码'
python3 GFoundationTests/run-stress.py \
  --db-host 172.16.0.20 --db-port 3306 --db-user root \
  --redis-host 172.16.0.20 --redis-port 6379
```

默认使用 release，投放持续 30 秒，连接/目标随机存活 5–15 秒，之后排空。没有数据库名也可以执行这些独立于表的 SELECT；需要指定时加 `--db-name`。Redis 认证通过 `REDIS_PASSWORD` 环境变量传入。密码不写入命令参数或报告。

脚本按模块依次运行，避免多个压测互相抢占资源。HTTP 和 TCP 客户端使用脚本启动的独立本地 Fixtures 进程，不访问公网服务。TcpServerTest 自建监听和客户端。脚本只结束自己启动的进程。

产物、日志、原始 CSV、JSON 报告、二进制 SHA256 和进程 CPU/RSS 记录都保存到 `GFoundationTests/.runs/<时间-编号>`，**不覆盖仓库 Bin**。脚本不会抓 pprof，也不改 Redis/MySQL 配置。框架与插件会以同一模式重编译，避免 Go 插件的接口版本不匹配。

只运行部分模块或调整负载：

```sh
python3 GFoundationTests/run-stress.py --modules TimerTest,TaskTest \
  --seconds 30 --rate 10000 --max-active 20000

python3 GFoundationTests/run-stress.py --modules TcpClientTest,TcpServerTest \
  --rate 1000 --max-active 1000 --lifetime-min 5 --lifetime-max 15 \
  --module-arg=--tcp_client_per_tick=10 \
  --module-arg=--tcp_server_per_tick=10

python3 GFoundationTests/run-stress.py --modules DbTest \
  --db-host 172.16.0.20 --db-user root --rate 100 --max-active 100 \
  --module-arg=--db_per_tick=10
```

可以用 `--mode race` 做竞态检查。重复使用已有产物时传 `--skip-build --bin-dir GFoundationTests/.runs/<某次运行>/bin`；复用目录中的主程序和插件必须来自同一次构建。`--output` 可指定新的运行目录，但必须在 `GFoundationTests` 内。

Timer/Task/HTTP 的边界回归独立于压测运行，不访问 Redis/MySQL，也不写入 Bin：

```sh
cd GFoundationTests/Regression
go run -race .
```

覆盖已到期 timer 的恢复、Start/Beat 内暂停、Pause/Resume 回调重入、Task 并发结果完整性与有序回调、Update 批次隔离，以及 HTTP 空响应的接口表示。Timer 用例使用实际时钟，应在压力负载停止后单独执行。

`GFoundationTests/go.build.sh` 已包含 TimerTest、TaskTest，仍接受主构建脚本传入的模式。各模块的 go.build.sh 新增可选环境变量 `GFOUNDATION_TEST_OUTPUT`，未指定时保持原来的 Bin 输出路径。

## 直接运行插件时的参数

通用参数以 `stress_` 开头。各模块可用自己的前缀覆盖，优先级更高：`timer_`、`task_`、`tcp_client_`、`tcp_server_`、`http_`、`db_`、`lookup_`。RedisTest 保留原有参数，详见其 README；一键脚本会转换公共运行参数。

| 参数 | 默认值 | 含义 |
| --- | --- | --- |
| `stress_seconds` | 30 | 投放持续时间 |
| `stress_rate` | 见模块表 | 每秒计划投放，分散为每 10ms 一批 |
| `stress_max_active` | 见模块表 | 活跃或未完成数上限，达到上限就跳过该批剩余投放，保留定时器 |
| `stress_lifetime_min/max` | 5 / 15 | TCP、DB、Timer 目标生命周期，单位秒，按 10ms 随机取值 |
| `stress_period_ms` | 100 | 每个 Session/目标的操作间隔 |
| `stress_per_tick` | 1 | TCP、DB 每次心跳投递的请求数 |
| `stress_payload` | 256 | 二进制正文长度；TCP 会再交替增加 0/8/16 字节检查帧边界 |
| `stress_timeout_ms` | 5000 | 普通 HTTP 请求的执行超时；DB 当前由框架固定为 10 秒 |
| `stress_drain_seconds` | 20 | 超出最长生命周期后的最终排空预算 |
| `stress_seed` | 1792166673896271000 | 可复现的随机种子 |
| `stress_output` | StressResults | 报告根目录 |

额外参数：`tcp_client_host/port/window` 默认 `127.0.0.1/18889/64`；`tcp_server_host/port/window` 默认 `127.0.0.1/18890/64`；`http_url` 默认 `http://127.0.0.1:18888`，需遵守 Fixtures 中的测试端点约定；`db_host/port/user/name/window` 默认 `127.0.0.1/3306/root/空/64`；`task_iterations/group_size/masks` 默认 `2000/16/8`；`lookup_module` 默认 `FindModuleTest`。

## 如何读报告

每次 API 调用至回调的完整耗时写入 samples.csv，包括框架队列和主线程回调等待。同步 API 的 phase 为 `call_to_return`。Timer 的 START 包含请求的 delay，LIFETIME 包含计划生命周期，BEAT_LAG 才是相对计划触发时间的延迟。不能把这些不同口径直接比较。

每项操作都有 issued、completed、success、failed、unresolved、平均、P50/P90/P95/P99/P99.9 分桶上界、真实最大值，以及完整六档分布：`0–10ms`、`10–20ms`、`20–30ms`、`30–40ms`、`40–50ms`、`>50ms`。第一档含 0 和 10ms，其后各档左开右闭。

容量限制跳过与调度漏发分开统计。达到容量上限是正常限流；调度漏发、发送窗口不足、校验错误、缺失/重复回调、排空超时或 CSV 丢行都会使结果失败。CSV 采用有界异步队列，磁盘跟不上时会计数并失败，不能静默丢弃。脚本还会核对唯一请求 ID、操作计数、原始行数和分布总数。COMPLETE 在文件导出完成后打印；一键脚本有任何失败或异常就返回非零。

测试发现现有框架缺陷时会保留明确的失败结果，不把失败改成跳过。这些插件不会修复或修改被测框架。

## 内存与 goroutine 泄露追踪

`run-memory.py` 在每个独立进程中只运行一个 API 压测模块，并加载不产生 API 负载的 MemoryProbe。默认同一进程连续三轮，每轮投放 20 秒、随机生命周期 5–10 秒；先排空、GC 并保留测试统计采样，再清除测试模块引用、GC 并采样。这样可以区分 API 保留、运行时缓存和测试统计缓冲。

```sh
export DB_PASSWORD='你的测试库密码'
python3 GFoundationTests/run-memory.py \
  --output GFoundationTests/.runs/memory-investigation
python3 GFoundationTests/analyze-memory.py \
  GFoundationTests/.runs/memory-investigation --profiles
```

使用优化且保留符号的 profile 构建，进程启动时设置 `GODEBUG=memprofilerate=65536`。各阶段保存 heap、allocs、goroutine profile、完整 goroutine 栈与 MemStats；GC 后采样会执行两次 GC。`load` 阶段不主动 GC，不能直接把负载峰值当作泄露。采集会增加运行时开销，调度指标失败照实保留，这不是纯性能基准。

每个模块的 `profiles/memory-report.json` 包含每轮基线与释放后的存活堆；`analysis/` 包含 pprof 的最终分配归因、轮次差分和测试统计保留差分。`内存数据.md` 与 `memory-facts.json` 是汇总。脚本不自动断言“无泄露”，需要结合持续增长、对象归因、回调排空与 goroutine 栈判断。

`--modules` 可限制 API；`--cycles`、`--seconds`、`--idle`、`--lifetime-min/max` 控制轮次和时间；`--final-idle` 控制最后一次释放后的观察时间，例如设为 110 秒检查 HTTP 空闲连接超时。`--skip-build --bin-dir` 仅适用于已经包含内存生命周期钩子的同次构建产物。

`TcpLifecycleTest` 不包含在默认模块列表中。按当前使用约定，Connected 成功前调用 Close 属于错误用法，不计入正常 API 使用的泄露结论；只在用户明确要求专项复测时运行。默认模式仅提前 Close；`--module-arg=--tcp_lifecycle_close_on_connected=1` 表示只在连接回调中关闭，同时增加 `--module-arg=--tcp_lifecycle_close_before_connected=1` 则表示提前 Close 后，在连接回调中再次 Close，用于验证提前调用是否影响后续正常清理。
