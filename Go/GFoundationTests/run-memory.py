#!/usr/bin/env python3
"""Run one API workload plus a passive observer, with repeated in-process rounds."""
import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import selectors
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("stress_runner", ROOT / "run-stress.py")
stress = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stress)
ALL_MODULES = stress.MODULES + ["TcpLifecycleTest"]


def run_module(name, args, out, bins, ports):
    directory = out / name
    directory.mkdir()
    command = [str(bins / "GFoundation"), f"--modules={name},MemoryProbe",
               "--memory_probe=1", f"--memory_module={name}", f"--memory_cycles={args.cycles}",
               f"--memory_idle={args.idle}", f"--memory_output={directory / 'profiles'}",
               f"--memory_final_idle={args.final_idle}",
               f"--stress_seconds={args.seconds}", f"--stress_lifetime_min={args.lifetime_min}",
               f"--stress_lifetime_max={args.lifetime_max}", "--stress_drain_seconds=20",
               f"--stress_output={directory / 'workload'}"]
    if name in ["TcpClientTest", "TcpLifecycleTest"]:
        command += [f"--tcp_client_port={ports['tcp']}"]
    elif name == "TcpServerTest":
        command += [f"--tcp_server_port={stress.free_port()}"]
    elif name == "HttpTest":
        command += [f"--http_url=http://127.0.0.1:{ports['http']}"]
    elif name == "DbTest":
        command += [f"--db_host={args.db_host}", "--db_port=3306", "--db_user=root"]
    elif name == "RedisTest":
        command += ["--redis_stress=1", f"--redis_host={args.redis_host}", "--redis_port=6379",
                    f"--redis_stress_seconds={args.seconds}", "--redis_stress_connect_rate=1000",
                    "--redis_stress_max_active=1000", f"--redis_stress_lifetime_min={args.lifetime_min}",
                    f"--redis_stress_lifetime_max={args.lifetime_max}", "--redis_stress_drain_seconds=20",
                    f"--redis_stress_output={directory / 'workload'}"]
    command += args.module_arg
    assert [v for v in command if v.startswith("--modules=")] == [f"--modules={name},MemoryProbe"]
    env = dict(os.environ)
    env["GODEBUG"] = env.get("GODEBUG", "") + ("," if env.get("GODEBUG") else "") + "memprofilerate=65536"
    metadata = {"module": name, "command": command, "started_utc": stress.utc(),
                "mode": "profile (optimized, symbols retained)", "memory_profile_rate": 65536,
                "isolation": "one API stress workload plus MemoryProbe; sequential modules",
                "credentials": "environment only; not recorded"}
    process = None
    complete = False
    failure = None
    print(f"MEMORY START {name}", flush=True)
    try:
        with (directory / "console.log").open("w") as log, (directory / "resources.jsonl").open("w") as resource:
            process = subprocess.Popen(command, cwd=bins, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
            metadata.update(pid=process.pid, host_started_utc=stress.utc())
            selector = selectors.DefaultSelector()
            selector.register(process.stdout, selectors.EVENT_READ)
            buffer = b""
            started = time.monotonic()
            deadline = started + args.cycles * (args.seconds + args.lifetime_max + 2*args.idle + 45) + args.final_idle + 60
            next_sample = started
            while process.poll() is None and not complete:
                now = time.monotonic()
                if now > deadline:
                    raise TimeoutError("memory workload exceeded deadline")
                if now >= next_sample:
                    ps = subprocess.run(["ps", "-p", str(process.pid), "-o", "pid=,pcpu=,rss="], capture_output=True, text=True)
                    resource.write(json.dumps({"utc":stress.utc(), "elapsed_seconds":now-started,"pid_cpu_percent_rss_kib":ps.stdout.strip()})+"\n")
                    resource.flush()
                    next_sample = now + 3
                for key, _ in selector.select(timeout=.2):
                    data = os.read(key.fd, 65536)
                    if not data:
                        selector.unregister(key.fileobj)
                        continue
                    buffer += data
                    while b"\n" in buffer:
                        line, buffer = buffer.split(b"\n", 1)
                        text = line.decode(errors="replace")
                        log.write(text+"\n")
                        if text.startswith("[MemoryProbe]") or " COMPLETE " in text or "panic:" in text:
                            print(name+" "+text, flush=True)
                        if "panic:" in text or "Initialize faild" in text or "Launch faild" in text:
                            raise RuntimeError(text)
                        if text.startswith(f"[MemoryProbe] COMPLETE module={name} "):
                            complete = True
                    log.flush()
            if buffer:
                log.write(buffer.decode(errors="replace"))
            selector.close()
            if not complete:
                raise RuntimeError("host exited before memory report")
    except Exception as exc:
        failure = str(exc)
    finally:
        stress.stop(process)
        if process:
            metadata.update(host_stopped_utc=stress.utc(), exit_code=process.returncode)
    report_path = directory / "profiles" / "memory-report.json"
    metadata.update(complete=complete, error=failure, report=str(report_path))
    if report_path.exists():
        report = json.loads(report_path.read_text())
        metadata["workload_failed_rounds"] = report["workload_failed_rounds"]
    metadata["workload_reports"] = [str(p) for p in directory.glob("workload/**/report.json")]
    (directory / "metadata.json").write_text(json.dumps(metadata,indent=2)+"\n")
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--modules", default=",".join(stress.MODULES))
    parser.add_argument("--seconds", type=int, default=20)
    parser.add_argument("--cycles", type=int, default=3)
    parser.add_argument("--idle", type=int, default=3)
    parser.add_argument("--final-idle", type=int, default=10)
    parser.add_argument("--lifetime-min", type=int, default=5)
    parser.add_argument("--lifetime-max", type=int, default=10)
    parser.add_argument("--db-host", default="172.16.0.20")
    parser.add_argument("--redis-host", default="172.16.0.20")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--bin-dir", type=Path)
    parser.add_argument("--module-arg", action="append", default=[])
    args = parser.parse_args()
    names = args.modules.split(",")
    if len(names)!=len(set(names)) or any(n not in ALL_MODULES for n in names): parser.error("invalid modules")
    if not 1<=args.cycles<=20 or not 1<=args.seconds<=3600 or not 1<=args.idle<=60: parser.error("invalid durations/cycles")
    if not 1<=args.final_idle<=600:parser.error("invalid final-idle")
    if not 1<=args.lifetime_min<=args.lifetime_max<=3600: parser.error("invalid lifetimes")
    if any(a.startswith(("--modules", "--memory_")) for a in args.module_arg): parser.error("cannot override memory coordination")
    if "DbTest" in names and not os.environ.get("DB_PASSWORD"): parser.error("set DB_PASSWORD")
    out = args.output.resolve()
    bins = (args.bin_dir or out/"bin").resolve()
    if ROOT not in out.parents or ROOT not in bins.parents: parser.error("output/bin-dir must be below GFoundationTests")
    out.mkdir(parents=True,exist_ok=False)
    if not args.skip_build:
        bins.mkdir(parents=True,exist_ok=True)
        stress.build(bins,"profile")
        subprocess.run(["sh",str(ROOT/"MemoryProbe/go.build.sh"),"profile"],env=dict(os.environ,GFOUNDATION_TEST_OUTPUT=str(bins)),check=True)
        if "TcpLifecycleTest" in names:
            subprocess.run(["sh",str(ROOT/"TcpLifecycleTest/go.build.sh"),"profile"],env=dict(os.environ,GFOUNDATION_TEST_OUTPUT=str(bins)),check=True)
    if not all((bins/p).exists() for p in ["GFoundation","StressFixtures","MemoryProbe.so"]+[n+".so" for n in names]): parser.error("incomplete binaries")
    manifest={"started_utc":stress.utc(),"modules":names,"cycles":args.cycles,"seconds":args.seconds,
              "binary_sha256":{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in bins.iterdir() if p.is_file()}}
    (out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    fixture, fixture_log = None, None
    results=[]
    try:
        ports={"http":stress.free_port(),"tcp":stress.free_port()}
        while ports['tcp']==ports['http']: ports['tcp']=stress.free_port()
        if any(n in names for n in ["HttpTest","TcpClientTest","TcpLifecycleTest"]):
            fixture_log=(out/"fixtures.log").open("w")
            fixture=subprocess.Popen([str(bins/"StressFixtures"),"--http",f"127.0.0.1:{ports['http']}","--tcp",f"127.0.0.1:{ports['tcp']}"],stdout=fixture_log,stderr=subprocess.STDOUT)
            deadline=time.monotonic()+5
            while True:
                if fixture.poll() is not None or time.monotonic()>deadline: raise RuntimeError("fixture startup failed")
                try:
                    with socket.create_connection(("127.0.0.1",ports['http']),timeout=.2):break
                except OSError:time.sleep(.05)
        for name in names:
            results.append(run_module(name,args,out,bins,ports))
            (out/"progress.json").write_text(json.dumps(results,indent=2)+"\n")
    finally:
        stress.stop(fixture)
        if fixture_log:fixture_log.close()
        manifest.update(finished_utc=stress.utc(),results=results)
        (out/"summary.json").write_text(json.dumps(manifest,indent=2)+"\n")
    return 0 if len(results)==len(names) and all(r['complete'] and not r['error'] for r in results) else 1


if __name__=="__main__":
    raise SystemExit(main())
