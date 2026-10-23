# RedisTest

通过 `IRedisApi` / `IRedisSession` 运行一次集成测试；插件只依赖 GFoundationApi。

在仓库根目录构建并启动：

```sh
sh ./go.build.sh
./Bin/GFoundation --modules=RedisTest --redis_host=127.0.0.1 --redis_port=6379 --redis_db=0
```

密码通过环境变量 `REDIS_PASSWORD` 提供。可选参数 `--redis_timeout=3` 设置请求超时秒数，允许 1–60，默认 3。建议连接专用测试数据库。

覆盖连接、二进制 Set/Get、输入参数复制、缺失键、Lua 字符串/二进制/整数/数组、Lua 错误、排队与回调顺序、超时配置、空回调、清理、重复关闭、断开回调及关闭后拒绝 Set/Get/Eval。

超时用例验证配置后的命令可执行，不会通过阻塞 Lua 脚本拖慢服务器；实际截止时间需要受控的慢服务器另行测试。

测试使用随机前缀 `gfoundation:redis-test:` 的键，仅删除本次创建的键，不执行 FLUSHDB/FLUSHALL。创建数据后设置 60 秒过期，正常完成时删除。

每项打印 PASS/FAIL，最后打印 `[RedisTest] COMPLETE passed=13 failed=0`。框架进程继续运行，可手动停止。连接或用例回调超时会报告失败，不会无限等待；Release 会关闭测试会话。

插件通过公共 `GFoundationApi.GetRedisApi()` 获取接口，没有依赖框架源码。

## TimerApi 高连接数与随机生命周期压力测试

这是连接反复创建/销毁的模型，不再使用固定连接池。新会话分散到每 10ms 一批创建；默认每秒 1000 条，即每 10ms 最多 10 条。每个会话随机存活指定秒数，期间每 100ms 分别执行配置数量的 SET、GET、EVAL。

```sh
sh ./go.build.sh
./Bin/GFoundation --modules=RedisTest --redis_host=127.0.0.1 --redis_port=6379 --redis_db=0 \
  --redis_stress=1 --redis_stress_connect_rate=1000 --redis_stress_seconds=30 \
  --redis_stress_lifetime_min=5 --redis_stress_lifetime_max=15 \
  --redis_stress_sets_per_100ms=1 --redis_stress_gets_per_100ms=1 \
  --redis_stress_evals_per_100ms=1 --redis_stress_window=256 \
  --redis_stress_max_active=4000 --redis_stress_max_connecting=10000
```

| 参数 | 默认值 | 含义 |
| --- | --- | --- |
| redis_stress | 0 | 1 开启，先执行原有功能用例，再执行压力用例 |
| redis_stress_connect_rate | 1000 | 每秒计划新连接数，最高 100000；分散到 100 个 10ms 周期，默认每批 10 条；达到连接上限时跳过本批剩余建连 |
| redis_stress_seconds | 30 | 创建新连接的持续秒数；结束后现有连接继续活到各自期限 |
| redis_stress_lifetime_min / max | 5 / 15 | 每个连接随机生命周期的秒数范围；均匀采样，精度 100ms |
| redis_stress_sets_per_100ms | 1 | 每连接每 100ms 的 SET 次数，可为 0 |
| redis_stress_gets_per_100ms | 1 | 每连接每 100ms 的 GET 次数，可为 0 |
| redis_stress_evals_per_100ms | 1 | 每连接每 100ms 的 EVAL 次数，可为 0；三者不能同时为 0 |
| redis_stress_window | 256 | 每会话在途命令上限，最高 1024，避免填满实现中的请求通道 |
| redis_stress_max_active | 4000 | 连接中、活跃、排空中的会话总数上限；达到时 return，保留建连定时器 |
| redis_stress_max_connecting | 10000 | 同时等待 Connect 回调的数量上限 |
| redis_stress_payload | 256 | 值大小，16–65536 字节；前 16 字节是连接编号与递增版本 |
| redis_stress_drain_seconds | 30 | 中止后的排空等待、以及结果导出的超时秒数 |
| redis_stress_seed | 当前时间 | 随机种子，可指定 int64 重现随机生命周期序列 |
| redis_stress_output | 可执行文件目录/RedisTestResults | 每轮创建独立子目录，存储原始样本和汇总 |
| redis_timeout | 3 | 单条 Redis 操作的执行超时秒数，不含客户端排队 |

上一版的 connections/requests/batch/interval 参数已移除，传入会明确报错，避免把固定连接池误当连接创建速率。

### 定时器与生命周期

- `OnTimerConnectRedis` 每 10ms 尝试发起一批 Connect，默认每批 10 条，30 秒共计划 3000 批、30000 条连接。统计计划数、实际调用、成功、失败、容量限流及调度漏发。达到上限直接 return，不停止定时器；后续 Beat 有空位便继续建连，跳过的数量不累积补发。主线程延迟错过的批次也只记录漏发，不集中追赶。配置的创建时长结束后，定时器正常结束。
- `redis_stress_connect_rate` 不是 100 的整数倍时，按累计配额之差分配每批数量，例如 101/s 为 99 批各 1 条、1 批 2 条；低于 100/s 时部分批次为 0，未限流、未漏发时每秒总数仍准确。
- 发起 Connect 前增加 connecting 预占名额；回调成功时 connecting 减 1、open 加 1，失败时只释放 connecting。调用 Close 不减少 open，直到 disconnected 回调才减 1。`open + connecting` 不超过上限，open 包括活跃与排空中的连接。
- 每个会话分别注册 `OnTimerRedisSessionSet`、`OnTimerRedisSessionGet`、`OnTimerRedisSessionEval`。target 就是 `IRedisSession`；context 是该会话的压力测试状态。
- Start 记录生命周期事件；Beat 投递本类型命令；End 记录自然/主动结束。三个已启用定时器都结束后，才排入清理操作并关闭会话。
- 随机存活时间从成功连接回调开始计算，按 100ms 周期取整。实际调度可能延迟，实际生命周期和定时器延迟另行统计；错过的周期不会用追赶突发伪装成正常负载。
- 所有 API 调用在主线程。唯一额外 goroutine 只负责文件写入，不调用 TimerApi/Redis API。

未限流时，稳态活跃连接数大致为 `每秒新建连接数 × 平均存活秒数`。默认总上限为 4000，会暂停建连并等待空位；活跃连接、正在建连和正在排空共同占用这些名额。每活跃连接默认每秒投递 30 条业务命令，实际连接数和吞吐量以报告为准。

### 每次调用的原始记录

每轮输出目录打印在 CHURN BEGIN / CHURN FILES 中：

- `samples.csv`：逐次 CONNECT、SET、GET、EVAL、CLEANUP、CLOSE 的调用/回调记录。
- `report.json`：完整配置、随机种子、连接/定时器/漏发计数、各类命令分开的延迟直方图和结果状态。

CSV 字段为 `request_id, connection_id, operation, phase, call_offset_ns, callback_offset_ns, latency_ns, success, status, error, planned_lifetime_ms`。时间是相对本轮开始的单调时钟纳秒偏移。回调入口立即取时间，再做数据校验；`latency_ns = callback_offset_ns - call_offset_ns`，包含请求排队、网络、Redis 执行以及主循环交付回调的等待。完成顺序可能不同于调用顺序，可用 request_id 对应。

每连接有一条 `phase=seed` 的 SET，确保读请求有初始值，这条 SET 也被测时和记录。周期 SET 每次递增版本；GET/EVAL 检查连接编号、预期版本和全部数据内容。每连接的请求队列保持提交顺序，不同连接分别校验，不会把“读到了任意值”当成功。

SET、GET、EVAL 分别统计样本量、成功/错误/数据错误/未完成数、最小/平均/标准差/最大延迟、P50/P90/P95/P99/P99.9 分桶上界。控制台用中文汇总，每种操作固定输出六档：0–10ms、10–20ms、20–30ms、30–40ms、40–50ms、>50ms，列出次数、占全部已回调请求的百分比及累计百分比，零次数的档位也会显示。第一档包含 0 和 10ms，其余档位左开右闭，恰好 50ms 计入 40–50ms。未完成请求不计入耗时分布。

JSON 的 `latency_bands` 保存相同的六档汇总，同时保留总体、成功和失败各自的精细分布。精细直方图在 0–1ms 内精度 10µs、1–10ms 内 100µs、10–100ms 内 1ms，之后分段扩展到 600s 和溢出桶；分位数继续使用精细桶计算。原始 CSV 支持另算精确百分位。

### 不能掩盖的失败

连接容量限制是正常限流，记录 `arrival_epochs_throttled` 和 `connections_skipped_capacity`，不单独判 FAIL；计划连接数仍与实际调用、容量跳过、调度漏发核对。在途窗口、主线程调度不足造成的业务漏发仍分别统计并判 FAIL。写文件使用有界异步队列，队列溢出或磁盘错误会导致中止/FAIL，不会静默丢掉延迟记录或阻塞主线程来掩盖负载不足。

进度日志中的 open 是成功连接后尚未断开的数量，reserved 是 open + connecting。`connect_failed` 统计建连失败，`command_failed` 只统计 SET/GET/EVAL 失败。报告中的 `arrival_interval_ms` 固定为 10，`arrival_batch_min/max` 记录每批配额范围，`peak_arrival_batch` 记录实际单批发起峰值。`arrival_epochs_*` 以 10ms 批次计数；`connections_skipped_scheduler` 是调度漏发的连接数，计划连接总数与实际调用、容量跳过、调度漏发三者核对。

停止创建连接后，等待已有会话随机到期、命令回调完成、测试键删除、关闭回调完成，再写完 CSV/JSON。超过排空期限仍未收到回调的请求记录为 unresolved：回调时间和延迟留空，不伪造为超时延迟样本。报告包含这些未完成数量，不能宣称全部成功。

只删除本轮随机前缀的键，不执行 FLUSHDB/FLUSHALL。直接 SET 不支持设置 TTL，强行终止整个进程可能留下该前缀的键。连接数也会受系统文件描述符、Redis maxclients 和机器资源影响；本程序报告实际成功连接和失败，不会将 API 调用次数当作已建立的 socket 数量。

## 一键采集压测与 pprof

先结束此前的 GFoundation（6060 只能由一个实例监听）。在仓库根目录执行：

```sh
sh GFoundationTests/RedisTest/profile.sh --host 172.16.0.20
```

脚本需要 Python 3 和 Go，直接使用当前 `Bin/GFoundation`、`Bin/RedisTest.so`，不自动编译。若缺少二进制或最近改过源码，请先执行 `sh ./go.build.sh`。密码继续使用 `REDIS_PASSWORD` 环境变量，不写入命令行和元数据。安装了 redis-cli 时会同时采集远端 Redis 的 INFO 和连接配置；没有 redis-cli 时会提示并跳过这些数据。

脚本默认每 10ms 最多创建 10 个连接（每秒 1000 个），创建持续 30 秒，总名额上限 4000，随机存活 5–15 秒，每连接每 100ms 各执行一次 SET/GET/EVAL。等待 CHURN BEGIN 后预热 3 秒，先保存 goroutine 堆栈，再采 15 秒 CPU，随后保存 goroutine、heap 和 allocs；负载结束再抓一份 heap。不主动触发 GC，不开启 block/mutex 采样，不修改 Redis 配置。CPU 采样与 heap 快照分开，采集时间与提前结束的警告写入 collection.json。

如果 NAS 已配置好 SSH 密钥登录，可以一并采集 Docker 限制和压测期间的容器资源：

```sh
sh GFoundationTests/RedisTest/profile.sh --host 172.16.0.20 \
  --docker-ssh root@172.16.0.20 --docker-container redis
```

SSH 使用 BatchMode，不询问密码或自动信任新主机；连接失败会记录原因，本地采集仍继续。Docker 只执行 inspect、stats 和读取容器 `/proc`，不会修改容器或配置。

结果默认保存到 `~/Desktop/GFoundationProfiles/时间-编号/`，并自动生成同名 `.tar.gz`，包含：

- 当前程序和插件快照及 SHA256、Go 构建信息、Git 版本与状态。
- `console.log`、`workload/` 中的逐次请求 CSV 和最终 report.json。
- CPU、goroutine、heap、allocs 原始样本以及自动解析出的函数排名。
- 客户端 ps 与 Redis INFO 时间序列；可选的 NAS Docker 资源时间序列。
- `collection.json` 中的采集时间、测试成败、警告、缺失文件。

测试结束后脚本只终止自己启动的 GFoundation，保留结果并打包。测试失败或关键采集缺失时退出码非零，仍会保存现有数据。不要在采集过程中手动重新编译或修改本轮快照。

只查看执行计划、或者查询全部参数：

```sh
sh GFoundationTests/RedisTest/profile.sh --dry-run
sh GFoundationTests/RedisTest/profile.sh --help
```

可调整 `--seconds`、`--connect-rate`、`--max-connections`、`--lifetime-min`、`--lifetime-max`、`--cpu-seconds`、`--warmup`、`--port`、`--db`、`--output`。创建时长必须至少为预热时间 + CPU 采样时间 + 2 秒。
