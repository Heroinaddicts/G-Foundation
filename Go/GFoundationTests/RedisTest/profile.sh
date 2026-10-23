#!/bin/sh
# The shell wrapper keeps this a single runnable file; Python uses only stdlib.
set -eu
exec python3 - "$0" "$@" <<'PY'
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import socket
import subprocess
import sys
import tarfile
import threading
import time
import urllib.request
import urllib.error
import uuid


def utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def main():
    repo = Path(sys.argv[1]).resolve().parents[2]
    parser = argparse.ArgumentParser(description="一键启动 RedisTest 并保存 pprof、压测结果和资源数据；不会自动编译。")
    parser.add_argument("--host", default="172.16.0.20", help="Redis 地址，默认 172.16.0.20")
    parser.add_argument("--port", type=int, default=6379)
    parser.add_argument("--db", type=int, default=0)
    parser.add_argument("--seconds", type=int, default=30, help="新建连接的持续秒数")
    parser.add_argument("--connect-rate", type=int, default=1000)
    parser.add_argument("--max-connections", type=int, default=4000)
    parser.add_argument("--lifetime-min", type=int, default=5)
    parser.add_argument("--lifetime-max", type=int, default=15)
    parser.add_argument("--cpu-seconds", type=int, default=15)
    parser.add_argument("--warmup", type=int, default=3, help="CHURN BEGIN 后等待多少秒开始采样")
    parser.add_argument("--output", type=Path, help="新建结果目录，默认 ~/Desktop/GFoundationProfiles/时间-编号")
    parser.add_argument("--docker-ssh", help="可选：NAS 的 SSH 地址，例如 root@172.16.0.20；需要已有免密登录")
    parser.add_argument("--docker-container", default="redis")
    parser.add_argument("--dry-run", action="store_true", help="只打印计划，不启动程序、不采集数据、不创建文件")
    args = parser.parse_args(sys.argv[2:])
    if not 1 <= args.port <= 65535 or args.db < 0:
        parser.error("port 必须为 1..65535，db 不能为负数")
    if not 1 <= args.seconds <= 86400 or not 1 <= args.connect_rate <= 100000:
        parser.error("seconds 必须为 1..86400，connect-rate 必须为 1..100000")
    if not 1 <= args.max_connections <= 1000000 or not 1 <= args.lifetime_min <= args.lifetime_max <= 3600:
        parser.error("连接上限或生命周期范围不合法")
    if args.cpu_seconds < 1 or args.warmup < 0 or args.warmup + args.cpu_seconds + 2 > args.seconds:
        parser.error("seconds 至少为 warmup + cpu-seconds + 2，确保采样时仍在创建连接")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", args.docker_container):
        parser.error("docker-container 必须为容器名称或 ID")
    if args.docker_ssh and (args.docker_ssh.startswith("-") or not re.fullmatch(r"[A-Za-z0-9_.@:\[\]-]+", args.docker_ssh)):
        parser.error("docker-ssh 必须为 SSH 主机地址，不能包含命令或选项")

    run_id = uuid.uuid4().hex[:8]
    output = (args.output or Path.home() / "Desktop" / "GFoundationProfiles" /
              (datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + run_id)).expanduser().resolve()
    binary = output / "bin" / "GFoundation"
    command = [str(binary), "--modules=RedisTest", "--redis_stress=1",
               "--redis_host=" + args.host, "--redis_port=" + str(args.port), "--redis_db=" + str(args.db),
               "--redis_stress_seconds=" + str(args.seconds), "--redis_stress_connect_rate=" + str(args.connect_rate),
               "--redis_stress_max_active=" + str(args.max_connections),
               "--redis_stress_lifetime_min=" + str(args.lifetime_min),
               "--redis_stress_lifetime_max=" + str(args.lifetime_max),
               "--redis_stress_output=" + str(output / "workload"), "--redis_profile_run_id=" + run_id]
    if args.dry_run:
        print("只预览，不执行：\n" + shlex.join(command))
        print("结果目录：", output)
        print("采样：CHURN BEGIN 后 %ds，CPU %ds，另存 goroutine/heap/allocs" % (args.warmup, args.cpu_seconds))
        print("Docker：", args.docker_ssh or "未启用；不会连接 NAS 的 SSH")
        return 0
    for name in ("go", "ps"):
        if not shutil.which(name):
            parser.error("缺少命令：" + name)
    for name in ("GFoundation", "RedisTest.so"):
        if not (repo / "Bin" / name).is_file():
            parser.error("缺少 Bin/" + name + "；请先在仓库根目录执行 sh ./go.build.sh")
    # Do not attach to an unrelated running instance or kill the user's process.
    with socket.socket() as check:
        try:
            check.bind(("127.0.0.1", 6060))
        except OSError:
            parser.error("127.0.0.1:6060 已占用。请先手动结束此前的 GFoundation，再运行脚本；脚本不会结束已有进程。")
    if args.docker_ssh and not shutil.which("ssh"):
        parser.error("缺少 ssh 命令")
    output.mkdir(parents=True, exist_ok=False)
    (output / "bin").mkdir()
    for name in ("GFoundation", "RedisTest.so"):
        shutil.copy2(repo / "Bin" / name, output / "bin" / name)
    print("结果目录：", output, flush=True)
    print("使用当前已编译的 GFoundation/RedisTest.so；不会重新编译。", flush=True)
    status = {"started_utc": utc(), "command": command, "events": [], "warnings": [], "test_complete": None}
    lock = threading.Lock()
    stop = threading.Event()
    complete = threading.Event()
    process = None
    workers = []
    profiler = None
    redis_cli = shutil.which("redis-cli")
    base = "http://127.0.0.1:6060/debug/pprof/"
    http = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def event(name, **fields):
        with lock:
            status["events"].append(dict(utc=utc(), name=name, **fields))

    def warn(message):
        with lock:
            status["warnings"].append(message)
        print("提示：" + message, flush=True)

    def run_command(cmd, timeout=5, env=None):
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env)
            return {"utc": utc(), "command": cmd, "returncode": result.returncode,
                    "stdout": result.stdout, "stderr": result.stderr}
        except (OSError, subprocess.TimeoutExpired) as exc:
            return {"utc": utc(), "command": cmd, "error": str(exc)}

    def redis_command(*parts):
        env = os.environ.copy()
        if env.get("REDIS_PASSWORD"):
            env["REDISCLI_AUTH"] = env["REDIS_PASSWORD"]
        # Never place passwords on command lines or dump environment variables.
        return run_command([redis_cli, "--raw", "-h", args.host, "-p", str(args.port), *parts], env=env)

    def redis_snapshot(name):
        if redis_cli:
            data = [redis_command("INFO", "all"), redis_command("CONFIG", "GET", "maxclients"),
                    redis_command("CONFIG", "GET", "tcp-backlog")]
            (output / name).write_text(json.dumps(data, ensure_ascii=False, indent=2))

    def ssh_command(parts):
        return run_command(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=3", args.docker_ssh,
                            shlex.join(parts)], timeout=8)

    def docker_snapshot():
        fmt = "Ulimits={{json .HostConfig.Ulimits}} Memory={{.HostConfig.Memory}} NanoCpus={{.HostConfig.NanoCpus}} CpuQuota={{.HostConfig.CpuQuota}} CpuPeriod={{.HostConfig.CpuPeriod}} CpusetCpus={{.HostConfig.CpusetCpus}}"
        data = [ssh_command(["docker", "inspect", "--format", fmt, args.docker_container]),
                ssh_command(["docker", "exec", args.docker_container, "sh", "-c", "cat /proc/1/comm; cat /proc/1/limits; cat /proc/sys/net/core/somaxconn"])]
        (output / "docker.before.json").write_text(json.dumps(data, ensure_ascii=False, indent=2))
        if any(item.get("returncode") != 0 for item in data):
            warn("Docker SSH 信息未全部取得，详见 docker.before.json；本地采集继续。")

    def monitor():
        with (output / "resources.timeline.jsonl").open("w") as stream:
            while not stop.is_set():
                sample = {"utc": utc(), "client": run_command(["ps", "-p", str(process.pid), "-o", "pid=,pcpu=,rss=,vsz=,time=,etime="])}
                if redis_cli:
                    sample["redis"] = redis_command("INFO", "all")
                stream.write(json.dumps(sample, ensure_ascii=False) + "\n")
                stream.flush()
                stop.wait(2)

    def monitor_docker():
        with (output / "docker.timeline.jsonl").open("w") as stream:
            while not stop.is_set():
                stream.write(json.dumps(ssh_command(["docker", "stats", "--no-stream", "--format", "{{json .}}", args.docker_container]), ensure_ascii=False) + "\n")
                stream.flush()
                stop.wait(2)

    def capture(name, endpoint, timeout=10):
        started = utc()
        try:
            with http.open(base + endpoint, timeout=timeout) as response:
                payload = response.read()
            if not payload:
                raise ValueError("返回内容为空")
            (output / name).write_bytes(payload)
            event("capture", file=name, started_utc=started, bytes=len(payload))
            print("已保存：" + name, flush=True)
            return True
        except Exception as exc:
            warn("采集 %s 失败：%s" % (name, exc))
            event("capture_failed", file=name, error=str(exc))
            return False

    def collect_profiles():
        if stop.wait(args.warmup) or complete.is_set():
            warn("压测提前结束，未能开始 CPU 采样。")
            return
        # Keep the heap dump out of the CPU sampling window.
        capture("goroutine.before.txt", "goroutine?debug=2")
        if complete.is_set() or stop.is_set():
            warn("负载已结束，跳过 CPU 采样，避免只采到空闲状态。")
            return
        print("开始 CPU 采样，持续 %d 秒；请等待。" % args.cpu_seconds, flush=True)
        cpu_ok = capture("cpu.pprof", "profile?seconds=" + str(args.cpu_seconds), args.cpu_seconds + 15)
        if complete.is_set() or stop.is_set():
            warn("CPU 采样完成前负载已结束：CPU profile 可能包含空闲尾段，请结合采集时间判断。")
        capture("goroutine.after.txt", "goroutine?debug=2")
        capture("goroutine.pprof", "goroutine")
        capture("heap.load.pprof", "heap")
        capture("allocs.load.pprof", "allocs")
        event("profile_sequence_finished", cpu_ok=cpu_ok)

    result_code = 1
    try:
        metadata = {"utc": utc(), "platform": sys.platform, "python": sys.version,
                    "go": run_command(["go", "version"]),
                    "build": run_command(["go", "version", "-m", str(binary)]),
                    "git_revision": run_command(["git", "-C", str(repo), "rev-parse", "HEAD"]),
                    "git_status": run_command(["git", "-C", str(repo), "status", "--short"]),
                    "sha256": {name: hashlib.sha256((output / "bin" / name).read_bytes()).hexdigest()
                               for name in ("GFoundation", "RedisTest.so")}}
        (output / "metadata.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2))
        if redis_cli:
            redis_snapshot("redis.before.json")
        else:
            warn("没有 redis-cli，跳过 Redis 服务端状态采集；CPU/goroutine/内存照常采集。")
        if args.docker_ssh:
            docker_snapshot()
        with (output / "console.log").open("w") as console, (output / "console.log").open() as reader:
            process = subprocess.Popen(command, cwd=output / "bin", stdout=console, stderr=subprocess.STDOUT)
            status["pid"] = process.pid
            event("framework_started", pid=process.pid)
            # Confirm this endpoint belongs to OUR process before capturing it.
            endpoint_ready = False
            ready_deadline = time.monotonic() + 10
            while time.monotonic() < ready_deadline and process.poll() is None:
                try:
                    with http.open(base + "cmdline", timeout=.5) as response:
                        cmdline = response.read().decode()
                    if str(binary) not in cmdline or "--redis_profile_run_id=" + run_id not in cmdline:
                        raise RuntimeError("6060 的 pprof 不属于本次启动的进程，拒绝采集")
                    endpoint_ready = True
                    break
                except (OSError, urllib.error.URLError):
                    time.sleep(.1)
            if not endpoint_ready:
                raise RuntimeError("本次进程的 pprof 未就绪，请查看 console.log；确认程序已编译 pprof 入口")
            worker = threading.Thread(target=monitor, daemon=True)
            worker.start()
            workers.append(worker)
            if args.docker_ssh:
                worker = threading.Thread(target=monitor_docker, daemon=True)
                worker.start()
                workers.append(worker)
            deadline = time.monotonic() + args.seconds + args.lifetime_max + 90
            partial = ""
            while time.monotonic() < deadline:
                partial += reader.read()
                lines = partial.split("\n")
                partial = lines.pop()
                for line in lines:
                    if any(mark in line for mark in ("[RedisTest]", "panic:", "fatal error:")):
                        print(line, flush=True)
                    if "[RedisTest] CHURN BEGIN" in line and profiler is None:
                        event("churn_begin")
                        profiler = threading.Thread(target=collect_profiles, daemon=True)
                        profiler.start()
                    match = re.search(r"\[RedisTest\] COMPLETE passed=(\d+) failed=(\d+)", line)
                    if match:
                        status["test_complete"] = {"passed": int(match[1]), "failed": int(match[2]), "utc": utc()}
                        complete.set()
                if complete.is_set():
                    result_code = 0 if status["test_complete"]["failed"] == 0 else 1
                    break
                if process.poll() is not None:
                    raise RuntimeError("GFoundation 提前退出，详见 console.log")
                time.sleep(.2)
            else:
                raise RuntimeError("超过整轮采集期限，保留现有证据并停止本次进程")
            if profiler is None:
                warn("未进入 CHURN BEGIN，检查前置功能测试是否失败。")
            else:
                profiler.join(args.cpu_seconds + 65)
                if profiler.is_alive():
                    raise RuntimeError("pprof 采集未能按时完成")
            if profiler is not None:
                capture("heap.after.pprof", "heap")
            if not (output / "cpu.pprof").is_file():
                result_code = 1
                warn("本轮没有有效的 CPU profile，采集结果不完整。")
    except KeyboardInterrupt:
        result_code = 130
        warn("收到中断，只停止脚本启动的进程；已有数据保留。")
    except Exception as exc:
        result_code = 1
        warn(str(exc))
        event("fatal", error=str(exc))
    finally:
        stop.set()
        # Stop the profiling HTTP requests before terminating the owned process.
        if profiler and profiler.is_alive():
            profiler.join(args.cpu_seconds + 65)
        for worker in workers:
            worker.join(10)
        if redis_cli:
            redis_snapshot("redis.after.json")
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        if process:
            status["framework_returncode"] = process.returncode
        status["finished_utc"] = utc()

    print("整理已有 profile 的函数排名……", flush=True)
    for name, flags in (("cpu.top.txt", ["-top"]), ("cpu.cum.txt", ["-top", "-cum"]),
                        ("cpu.callers.txt", ["-peek=net.*Write|checkSyscallConn|runtime.kevent"]),
                        ("heap.top.txt", ["-top", "-inuse_space"])):
        profile = output / ("heap.load.pprof" if name.startswith("heap") else "cpu.pprof")
        if profile.is_file():
            result = run_command(["go", "tool", "pprof", "-symbolize=local", *flags, str(binary), str(profile)], timeout=30)
            (output / name).write_text(result.get("stdout", "") + result.get("stderr", "") + result.get("error", ""))
            if result.get("returncode") != 0:
                warn("解析 %s 失败，原始 profile 已保留。" % name)
                result_code = result_code or 1
    required = ("cpu.pprof", "goroutine.before.txt", "goroutine.after.txt", "goroutine.pprof",
                "heap.load.pprof", "allocs.load.pprof", "heap.after.pprof")
    status["missing_profiles"] = [name for name in required if not (output / name).is_file()]
    if status["missing_profiles"]:
        result_code = result_code or 1
    status["workload_reports"] = [str(path.relative_to(output)) for path in (output / "workload").glob("*/report.json")]
    if not status["workload_reports"]:
        warn("缺少压测 report.json，检查 console.log 是否中途失败或被中断。")
        result_code = result_code or 1
    status["exit_code"] = result_code
    (output / "collection.json").write_text(json.dumps(status, ensure_ascii=False, indent=2))
    (output / "README.txt").write_text(
        "console.log：整轮原始日志；workload/：每次请求的 CSV 和最终 JSON。\n"
        "cpu.pprof / cpu.top.txt / cpu.cum.txt / cpu.callers.txt：CPU 样本、排名和调用关系。\n"
        "goroutine.before.txt / goroutine.after.txt：CPU 采样前后的协程堆栈。\n"
        "heap.load.pprof / heap.after.pprof / allocs.load.pprof：采样后和负载结束的内存快照、分配记录。\n"
        "resources.timeline.jsonl：带 UTC 时间的客户端 ps 和 Redis INFO；ps 列依次为 PID、%CPU、RSS(KiB)、VSZ(KiB)、累计 CPU 时间、运行时长。\n"
        "redis.before.json / redis.after.json：Redis INFO 和连接配置；用 rejected_connections 前后差值判断超限拒绝。\n"
        "docker.*：仅指定 --docker-ssh 时生成，包含 NAS 容器资源时间序列、限制与 somaxconn。\n"
        "metadata.json：Go 版本、构建信息、Git 状态和二进制 SHA256。collection.json：采集事件、时间、警告和测试状态。\n"
        "bin/：本轮实际运行的程序及插件，保留用于符号解析。密码没有写入命令行或元数据。\n"
        "CPU 采样后才抓 heap，减少堆快照对 CPU 样本的干扰。未开启 block/mutex 采样；自定义自旋锁也不能用 mutex profile 直接衡量。\n")
    archive = Path(str(output) + ".tar.gz")
    print("打包原始数据和二进制，可能需要一些时间……", flush=True)
    with tarfile.open(archive, "w:gz", compresslevel=1) as bundle:
        bundle.add(output, arcname=output.name)
    print("采集结束。测试结果和采集警告请看 collection.json。", flush=True)
    print("目录：", output, flush=True)
    print("压缩包：", archive, flush=True)
    return result_code


if __name__ == "__main__":
    sys.exit(main())
PY
