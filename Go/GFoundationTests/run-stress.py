#!/usr/bin/env python3
"""Build and run isolated API stress plugins without overwriting repository Bin."""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import socket
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parent
MODULES = ["TimerTest", "TaskTest", "TcpClientTest", "TcpServerTest", "HttpTest", "DbTest", "FindModuleTest", "RedisTest"]


def utc():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def stop(process):
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def build(out, mode):
    env = dict(os.environ, GFOUNDATION_TEST_OUTPUT=str(out))
    subprocess.run(["sh", str(ROOT / "go.build.sh"), mode], env=env, check=True)
    # Use the existing mode helper; output is deliberately inside this suite.
    command = '. "$1"; gfoundation_build_init "$2"; gfoundation_build "$3" "$4" exe'
    for source, name in [(REPO / "GFoundation", "GFoundation"), (ROOT / "Fixtures", "StressFixtures")]:
        subprocess.run(["sh", "-c", command, "build-stress", str(REPO / "go.build.common.sh"), mode, str(source), str(out / name)], check=True)


def run_one(name, args, out, bin_dir, fixture_ports):
    directory = out / name
    directory.mkdir()
    common = [f"--stress_seconds={args.seconds}", f"--stress_lifetime_min={args.lifetime_min}",
              f"--stress_lifetime_max={args.lifetime_max}", f"--stress_drain_seconds={args.drain_seconds}",
              f"--stress_output={directory / 'workload'}", f"--stress_payload={args.payload}", f"--stress_seed={args.seed}"]
    if args.rate is not None:
        common.append(f"--stress_rate={args.rate}")
    if args.max_active is not None:
        common.append(f"--stress_max_active={args.max_active}")
    cmd = [str(bin_dir / "GFoundation"), f"--modules={name}"] + common
    if name == "TcpClientTest":
        cmd += [f"--tcp_client_port={fixture_ports['tcp']}"]
    elif name == "TcpServerTest":
        cmd += [f"--tcp_server_port={free_port()}"]
    elif name == "HttpTest":
        cmd += [f"--http_url=http://127.0.0.1:{fixture_ports['http']}"]
    elif name == "DbTest":
        cmd += [f"--db_host={args.db_host}", f"--db_port={args.db_port}", f"--db_user={args.db_user}", f"--db_name={args.db_name}"]
    elif name == "RedisTest":
        cmd += ["--redis_stress=1", f"--redis_host={args.redis_host}", f"--redis_port={args.redis_port}", f"--redis_db={args.redis_db}",
                f"--redis_stress_seconds={args.seconds}", f"--redis_stress_connect_rate={args.rate or 1000}",
                f"--redis_stress_max_active={args.max_active or 1000}", f"--redis_stress_lifetime_min={args.lifetime_min}",
                f"--redis_stress_lifetime_max={args.lifetime_max}", f"--redis_stress_drain_seconds={args.drain_seconds}",
                f"--redis_stress_payload={args.payload}", f"--redis_stress_seed={args.seed}", f"--redis_stress_output={directory / 'workload'}"]
    cmd += args.module_arg
    loaded_modules = [argument for argument in cmd if argument.startswith("--modules=")]
    if loaded_modules != [f"--modules={name}"]:
        raise ValueError("each host process must load exactly its one stress module")
    metadata = {"started_utc": utc(), "command": cmd, "profiling": False,
                "credentials": "DB_PASSWORD / REDIS_PASSWORD inherited from environment; not recorded"}
    (directory / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"\n===== {name} =====", flush=True)
    process = None
    complete = None
    timed_out = False
    failure = None
    try:
        with (directory / "console.log").open("w") as log, (directory / "resources.jsonl").open("w") as resource:
            process = subprocess.Popen(cmd, cwd=bin_dir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
            metadata["pid"] = process.pid
            metadata["host_started_utc"] = utc()
            selector = selectors.DefaultSelector()
            selector.register(process.stdout, selectors.EVENT_READ)
            buffer = b""
            started = time.monotonic()
            next_sample = started
            deadline = started + args.seconds + args.lifetime_max + args.drain_seconds + 45
            while process.poll() is None and complete is None:
                if time.monotonic() >= deadline:
                    timed_out = True
                    break
                if time.monotonic() >= next_sample:
                    sample = subprocess.run(["ps", "-p", str(process.pid), "-o", "pid=,pcpu=,rss="], capture_output=True, text=True)
                    resource.write(json.dumps({"utc": utc(), "elapsed_seconds": time.monotonic()-started, "pid_cpu_percent_rss_kib": sample.stdout.strip()}) + "\n")
                    resource.flush()
                    next_sample = time.monotonic()+2
                for key, _ in selector.select(timeout=.2):
                    data = os.read(key.fd, 65536)
                    if not data:
                        selector.unregister(key.fileobj)
                        continue
                    buffer += data
                    while b"\n" in buffer:
                        line, buffer = buffer.split(b"\n", 1)
                        text = line.decode(errors="replace")
                        log.write(text + "\n")
                        # Individual disconnect logging is part of the existing
                        # host. Preserve it on disk without flooding the UI.
                        if text.startswith(f"[{name}]") or "panic:" in text:
                            print(text, flush=True)
                        if "Initialize faild" in text or "Launch faild" in text:
                            raise RuntimeError(text)
                        match = re.search(r"\[" + re.escape(name) + r"\] COMPLETE passed=(\d+) failed=(\d+)", text)
                        if match:
                            complete = {"passed": int(match[1]), "failed": int(match[2])}
                    log.flush()
            if buffer:
                log.write(buffer.decode(errors="replace"))
            selector.close()
            metadata["elapsed_seconds"] = time.monotonic()-started
            metadata["exit_code_before_cleanup"] = process.poll()
    except Exception as exc:
        failure = f"{type(exc).__name__}: {exc}"
    finally:
        # The framework intentionally has no exit API. Terminate ONLY the host
        # launched here after COMPLETE and report export, never a user's host.
        stop(process)
        if process is not None:
            metadata["host_stopped_utc"] = utc()
            metadata["exit_code_after_cleanup"] = process.returncode
    reports = sorted(directory.glob("workload/**/report.json"))
    status = "passed" if complete and complete["failed"] == 0 and reports and not timed_out and failure is None else "failed"
    validations = []
    for path in reports:
        try:
            validations.append(validate_report(path))
        except Exception as exc:
            validations.append({"path": str(path), "valid": False, "error": str(exc)})
    if any(not v["valid"] for v in validations):
        status = "failed"
    result = {"module": name, "status": status, "complete": complete, "timed_out": timed_out,
              "runner_error": failure, "reports": [str(p) for p in reports], "validation": validations}
    metadata.update(result)
    metadata["finished_utc"] = utc()
    (directory / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    return result


def validate_report(path):
    import csv
    report = json.loads(path.read_text())
    operations = report["operations"]
    expected = sum(op["issued"] for op in operations.values())
    for op in operations.values():
        if op["issued"] != op["completed"] + op["unresolved"]:
            raise ValueError("issued != completed + unresolved")
        if op["completed"] != op["success"] + op["failed"]:
            raise ValueError("completed != success + failed")
        latency = op["all_latency"]
        if sum(b["count"] for b in latency["latency_bands"]) != latency["samples"]:
            raise ValueError("latency bands do not sum to samples")
    seen = set()
    rows = 0
    with (path.parent / "samples.csv").open() as file:
        for row in csv.DictReader(file):
            identifier = row["request_id"]
            if identifier in seen:
                raise ValueError("duplicate request ID")
            seen.add(identifier)
            if row["status"] == "completed" and int(row["latency_ns"]) < 0:
                raise ValueError("negative latency")
            rows += 1
    if rows != expected or rows != report["csv_rows_written"] or report["csv_rows_dropped"]:
        raise ValueError(f"CSV accounting mismatch: expected={expected} rows={rows}")
    return {"path": str(path), "valid": True, "csv_rows": rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--modules", default=",".join(MODULES))
    parser.add_argument("--mode", choices=["release", "profile", "debug", "race"], default="release")
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--rate", type=int)
    parser.add_argument("--max-active", type=int)
    parser.add_argument("--lifetime-min", type=int, default=5)
    parser.add_argument("--lifetime-max", type=int, default=15)
    parser.add_argument("--drain-seconds", type=int, default=20)
    parser.add_argument("--payload", type=int, default=256)
    parser.add_argument("--seed", type=int, default=1792166673896271000)
    parser.add_argument("--redis-host", default="127.0.0.1")
    parser.add_argument("--redis-port", type=int, default=6379)
    parser.add_argument("--redis-db", type=int, default=0)
    parser.add_argument("--db-host", default="127.0.0.1")
    parser.add_argument("--db-port", type=int, default=3306)
    parser.add_argument("--db-user", default="")
    parser.add_argument("--db-name", default="")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--bin-dir", type=Path, help="reuse this suite's build directory")
    parser.add_argument("--module-arg", action="append", default=[], help="extra host argument; e.g. --module-arg=--task_iterations=5000")
    args = parser.parse_args()
    names = args.modules.split(",")
    if len(names) != len(set(names)) or any(n not in MODULES for n in names):
        parser.error("modules must be unique names from " + ",".join(MODULES))
    if not 1 <= args.seconds <= 3600 or not 1 <= args.lifetime_min <= args.lifetime_max <= 3600 or not 1 <= args.drain_seconds <= 3600:
        parser.error("invalid duration/lifetime")
    if args.rate is not None and not 1 <= args.rate <= 100000:
        parser.error("rate must be 1..100000")
    if args.max_active is not None and not 1 <= args.max_active <= 1000000:
        parser.error("max-active must be 1..1000000")
    if not 24 <= args.payload <= 1048576 or not 1 <= args.db_port <= 65535 or not 1 <= args.redis_port <= 65535:
        parser.error("invalid payload/port")
    if "DbTest" in names and not args.db_user:
        parser.error("DbTest requires --db-user (password via DB_PASSWORD); use --modules to omit it")
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
    out = (args.output or ROOT / ".runs" / stamp).resolve()
    # Keep generated build products and reports within the user-authorized tree.
    if ROOT not in out.parents:
        parser.error("output must be below GFoundationTests")
    out.mkdir(parents=True, exist_ok=False)
    bin_dir = (args.bin_dir or out / "bin").resolve()
    if ROOT not in bin_dir.parents:
        parser.error("bin-dir must be below GFoundationTests")
    if args.skip_build:
        if not all((bin_dir / file).is_file() for file in ["GFoundation", "StressFixtures"] + [n+".so" for n in names]):
            parser.error("--skip-build requires a complete --bin-dir")
    else:
        bin_dir.mkdir(parents=True, exist_ok=True)
        build(bin_dir, args.mode)
    manifest = {"started_utc": utc(), "mode": args.mode, "profiling": False, "modules": names,
                "module_isolation": "one stress module per host; wait for process exit before the next module",
                "binary_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in bin_dir.iterdir() if p.is_file()}}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    fixture = None
    fixture_log = None
    results = []
    try:
        ports = {"http": free_port(), "tcp": free_port()}
        while ports["tcp"] == ports["http"]:
            ports["tcp"] = free_port()
        if any(n in names for n in ["HttpTest", "TcpClientTest"]):
            fixture_log = (out / "fixtures.log").open("w")
            fixture = subprocess.Popen([str(bin_dir / "StressFixtures"), "--http", f"127.0.0.1:{ports['http']}", "--tcp", f"127.0.0.1:{ports['tcp']}"], stdout=fixture_log, stderr=subprocess.STDOUT)
            deadline = time.monotonic()+5
            while time.monotonic() < deadline:
                if fixture.poll() is not None:
                    raise RuntimeError("fixture exited; see fixtures.log")
                try:
                    with socket.create_connection(("127.0.0.1", ports["http"]), timeout=.2):
                        break
                except OSError:
                    time.sleep(.05)
            else:
                raise RuntimeError("fixture readiness timed out")
        for name in names:
            results.append(run_one(name, args, out, bin_dir, ports))
    finally:
        stop(fixture)
        if fixture_log:
            fixture_log.close()
        manifest["finished_utc"] = utc()
        manifest["results"] = results
        manifest["status"] = "passed" if len(results) == len(names) and all(r["status"] == "passed" for r in results) else "failed"
        (out / "summary.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print(f"\nSUITE {manifest['status']} {out / 'summary.json'}", flush=True)
    return 0 if manifest["status"] == "passed" else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
