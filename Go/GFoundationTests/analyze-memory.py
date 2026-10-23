#!/usr/bin/env python3
"""Summarize exact runtime counters and save sampled pprof ownership diffs."""
import argparse
import collections
import json
from pathlib import Path
import subprocess


def goroutine_groups(path):
    result = collections.Counter()
    patterns = {
        "TCP read loops": "(*TcpSession).readLoop", "TCP send loops": "(*TcpSession).sendLoop",
        "Redis session workers": "(*RedisSession).OptLoop", "DB session workers": "(*GDbSession).OptLoop",
        "Task resident workers": "NewGTask.func1", "HTTP resident workers": "Http.NewGHttp.func1",
        "HTTP connection read loops": "(*persistConn).readLoop", "HTTP connection write loops": "(*persistConn).writeLoop",
        "memory collector": "MemoryProbe).capture.func1", "main loop": "main.main()",
        "pprof HTTP server": "main.startProfiler.func1",
    }
    for block in path.read_text().split("\n\n"):
        if not block.startswith("goroutine "):
            continue
        for label, pattern in patterns.items():
            if pattern in block:
                result[label] += 1
                break
        else:
            result[block.splitlines()[1] if len(block.splitlines()) > 1 else "unknown"] += 1
    return dict(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--profiles", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    manifest=json.loads((root/"manifest.json").read_text())
    facts = []
    for module in ["TimerTest", "TaskTest", "TcpClientTest", "TcpServerTest", "HttpTest", "DbTest", "FindModuleTest", "RedisTest", "TcpLifecycleTest"]:
        directory = root/module
        p = directory/"profiles/memory-report.json"
        if not p.exists():
            continue
        report = json.loads(p.read_text())
        snapshots = report["snapshots"]
        baseline = next(s for s in snapshots if s["phase"]=="baseline")
        released = [s for s in snapshots if s["phase"]=="released"]
        drained = [s for s in snapshots if s["phase"]=="drained"]
        final = next(s for s in snapshots if s["phase"]=="final")
        metrics = [json.loads(s) for s in (directory/"profiles/metrics.jsonl").read_text().splitlines()]
        resources = [json.loads(s) for s in (directory/"resources.jsonl").read_text().splitlines()]
        workload_reports = [json.loads(p.read_text()) for p in sorted(directory.glob("workload/**/report.json"))]
        workload_totals = []
        for w in workload_reports:
            ops = w["operations"]
            for op in ops.values():
                assert op["issued"] == op["completed"] + op["unresolved"]
                assert op["completed"] == op["success"] + op["failed"]
            workload_totals.append({"issued":sum(o["issued"] for o in ops.values()),
                                    "failed":sum(o["failed"] for o in ops.values()),
                                    "unresolved":sum(o["unresolved"] for o in ops.values()),
                                    "errors":w.get("errors"), "counters":w["counters"]})
        stat = lambda s: {"cycle":s["cycle"],"heap_bytes":s["stats"]["HeapAlloc"],"objects":s["stats"]["HeapObjects"],"goroutines":s["goroutines"],"fd_count":s.get("fd_count")}
        fact = {"module":module,"baseline":stat(baseline),"released":[stat(s) for s in released],"drained":[stat(s) for s in drained],
                "final":stat(final),"failed_rounds":report["workload_failed_rounds"],"workload_rounds":workload_totals,
                "heap_peak_bytes":max(s["HeapAlloc"] for s in metrics),"heap_inuse_peak_bytes":max(s["HeapInuse"] for s in metrics),
                "goroutine_peak_sampled":max(s["goroutines"] for s in metrics),
                "rss_peak_kib":max(int(s["pid_cpu_percent_rss_kib"].split()[-1]) for s in resources if s["pid_cpu_percent_rss_kib"]),
                "total_allocated_bytes":final["stats"]["TotalAlloc"]-baseline["stats"]["TotalAlloc"]}
        cycle_path = lambda s: directory/"profiles"/f"cycle-{s['cycle']:02d}"/s["phase"]
        fact["goroutine_groups_final"] = goroutine_groups(cycle_path(final)/"goroutine.txt")
        facts.append(fact)
        if args.profiles:
            analysis = directory/"analysis"
            analysis.mkdir(exist_ok=True)
            for name,index,profile,base in [
                ("final-inuse-space","inuse_space",cycle_path(final)/"heap.pprof",None),
                ("final-inuse-objects","inuse_objects",cycle_path(final)/"heap.pprof",None),
                ("released-growth-space","inuse_space",cycle_path(released[-1])/"heap.pprof",cycle_path(released[0])/"heap.pprof"),
                ("test-retained-space","inuse_space",cycle_path(drained[-1])/"heap.pprof",cycle_path(released[-1])/"heap.pprof"),
                ("total-allocations","alloc_space",cycle_path(final)/"allocs.pprof",None),
            ]:
                target=analysis/(name+".txt")
                if target.exists():continue
                cmd=["go","tool","pprof","-top","-nodecount=35",f"-sample_index={index}"]
                if index != "inuse_objects":cmd+=["-unit=bytes"]
                if base:cmd+=["-base",str(base)]
                cmd+=[str(profile)]
                run=subprocess.run(cmd,capture_output=True,text=True,check=True)
                target.write_text(run.stdout+run.stderr)
    (root/"memory-facts.json").write_text(json.dumps(facts,indent=2)+"\n")
    lines=["# 各 API 内存压测数据","", f"每个 API 在独立进程内执行 {manifest['cycles']} 轮、每轮投放 {manifest['seconds']} 秒；每轮停止投放、等待请求与连接排空，双次 GC 后采样，再清除测试模块持有的统计与测试对象引用、双次 GC 后采样。只有一个 API 产生负载，MemoryProbe 仅观察运行时。TcpLifecycleTest 用于验证立即 Close 后是否仍有资源残留，不假定已经断开。", "", "使用优化构建并保留符号（profile 模式），进程启动时设置内存采样率 65536 字节；不是无分析器的延迟基准。存活堆数字来自 runtime.MemStats，pprof 的归因数字是采样估计。堆峰值包含测试记录与采集开销。", "", "| API | 采样堆峰值 MiB | 第一轮释放后 MiB | 第二轮释放后 MiB | 第三轮释放后 MiB | 最终 goroutine / 基线 | 未通过原压测断言的轮数 |", "| --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for f in facts:
        sizes=[f"{s['heap_bytes']/1048576:.3f}" for s in f['released'][:3]]
        sizes += ["—"] * (3-len(sizes))
        lines.append(f"| {f['module']} | {f['heap_peak_bytes']/1048576:.2f} | "+" | ".join(sizes)+f" | {f['final']['goroutines']} / {f['baseline']['goroutines']} | {f['failed_rounds']} |")
    lines += ["", "## 测试数据保留与最终空闲状态", "", "| API | 最后一次结束后（保留测试统计）MiB | 清除测试引用后 MiB | 最终空闲后 MiB | 累计分配 GiB | RSS 峰值 MiB |", "| --- | ---: | ---: | ---: | ---: | ---: |"]
    for f in facts:
        lines.append(f"| {f['module']} | {f['drained'][-1]['heap_bytes']/1048576:.3f} | {f['released'][-1]['heap_bytes']/1048576:.3f} | {f['final']['heap_bytes']/1048576:.3f} | {f['total_allocated_bytes']/1073741824:.3f} | {f['rss_peak_kib']/1024:.2f} |")
    lines += ["", "## goroutine 残留栈分类", ""]
    for f in facts:
        lines += [f"- {f['module']}: "+", ".join(f"{k}={v}" for k,v in f['goroutine_groups_final'].items())]
    lines += ["", "本表不自动判定泄露。第一轮的运行时/连接缓存预热、测试统计缓冲和观察器保存的有限快照，均须通过 analysis/ 下的堆差分归因。性能调度失败与内存是否持续保留分开报告；功能错误或未排空请求须单独检查。", "", "原始阶段快照保存在各模块 profiles/cycle-*/，其中 load 不强制 GC；baseline/drained/released/final 均在两次 GC 后采样。最终空闲时间以 metadata.json 的命令参数为准，默认 10 秒。"]
    (root/"内存数据.md").write_text("\n".join(lines)+"\n")
    print(json.dumps([{k:f[k] for k in ['module','released','final','failed_rounds','goroutine_groups_final']} for f in facts],ensure_ascii=False,indent=2))


if __name__=="__main__":
    main()
