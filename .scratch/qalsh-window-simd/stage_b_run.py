#!/usr/bin/env python3
"""Run the frozen Stage-B six-order, 30-population per-process screen."""
from __future__ import annotations

import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import time
from typing import Any

ROOT = pathlib.Path("/home/jonathanhu237/code/qalsh-window-simd")
STAGE = ROOT / "stageb"
DATA = STAGE / "data" / "federalist"
CPU = int(os.environ.get("STAGEB_CPU", "10"))
RUN_TAG = os.environ.get("STAGEB_RUN_TAG", "stageb-20260913")
SIBLINGS = [int(x) for x in pathlib.Path(
    f"/sys/devices/system/cpu/cpu{CPU}/topology/thread_siblings_list"
).read_text().strip().split(",")]
ORDERS = ["ABC", "ACB", "BAC", "BCA", "CAB", "CBA"] * 2
BINS = {
    "A": STAGE / "original-repeat",
    "B": STAGE / "current-baseline-repeat",
    "C": STAGE / "current-candidate-repeat",
}
LABELS = {"A": "audited-original-c", "B": "unmodified-722ecfb", "C": "candidate"}


def stat(cpu: int) -> dict[str, Any]:
    prefix = f"cpu{cpu} "
    with open("/proc/stat") as stream:
        for line in stream:
            if line.startswith(prefix):
                values = list(map(int, line.split()[1:]))
                return {
                    "total": sum(values),
                    "busy": sum(values[:3]) + sum(values[5:]),
                    "fields": values,
                }
    raise RuntimeError(f"missing /proc/stat cpu{cpu}")


def freq(cpu: int) -> str | None:
    for name in ("scaling_cur_freq", "cpuinfo_cur_freq"):
        path = pathlib.Path(f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/{name}")
        if path.exists():
            return path.read_text().strip()
    return None


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def perf_values(path: pathlib.Path) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for line in path.read_text().splitlines():
        fields = line.split(",")
        if len(fields) < 3:
            continue
        event = fields[2].strip()
        value = fields[0].strip()
        if not event or value.startswith("<"):
            result[event] = {"value": value, "raw": line}
            continue
        try:
            number: Any = float(value)
            if number.is_integer():
                number = int(number)
        except ValueError:
            number = value
        result[event] = {"value": number, "unit": fields[1].strip(), "raw": line}
    task = result.get("task-clock", {}).get("value")
    cycles = result.get("cycles", {}).get("value")
    if isinstance(task, (int, float)) and isinstance(cycles, (int, float)) and task > 0:
        result["effective_frequency_hz"] = cycles / (task / 1000.0)
    return result


def app_metrics(stdout: str) -> dict[str, float | int]:
    match = re.search(
        r"open_ms\s+([0-9.e+-]+)\s+warm_ms\s+([0-9.e+-]+)\s+query_ms\s+([0-9.e+-]+)\s+"
        r"query_ms_per_population\s+([0-9.e+-]+)\s+warm_hash\s+(\d+)\s+final_hash\s+(\d+)",
        stdout,
    )
    if not match:
        raise RuntimeError(f"app metrics missing: {stdout!r}")
    return {
        "open_ms": float(match.group(1)),
        "warm_ms": float(match.group(2)),
        "query_ms": float(match.group(3)),
        "query_ms_per_population": float(match.group(4)),
        "warm_hash": int(match.group(5)),
        "final_hash": int(match.group(6)),
    }


def run_one(triplet: int, order: str, code: str, run_index: int, raw_dir: pathlib.Path) -> dict[str, Any]:
    stem = f"triplet-{triplet:02d}-{order}-{code}"
    stdout_path = raw_dir / f"{stem}.stdout"
    perf_path = raw_dir / f"{stem}.perf"
    result_path = raw_dir / f"{stem}.results.tsv"
    command = [
        "taskset", "-c", str(CPU), "perf", "stat", "-x,",
        "-e", "task-clock,cycles,instructions,context-switches,cpu-migrations",
        str(BINS[code]), str(DATA), "1", "ab", "memory", "1", str(result_path), "30",
    ]
    before = {str(cpu): stat(cpu) for cpu in SIBLINGS}
    before_freq = {str(cpu): freq(cpu) for cpu in SIBLINGS}
    started = time.time()
    mono_start = time.perf_counter()
    with stdout_path.open("w") as stdout, perf_path.open("w") as perf:
        completed = subprocess.run(command, stdout=stdout, stderr=perf, check=False)
    elapsed = time.perf_counter() - mono_start
    ended = time.time()
    after = {str(cpu): stat(cpu) for cpu in SIBLINGS}
    after_freq = {str(cpu): freq(cpu) for cpu in SIBLINGS}
    stdout_text = stdout_path.read_text()
    record: dict[str, Any] = {
        "triplet": triplet,
        "order": order,
        "code": code,
        "label": LABELS[code],
        "run_index": run_index,
        "command": command,
        "returncode": completed.returncode,
        "started_unix": started,
        "ended_unix": ended,
        "wrapper_elapsed_s": elapsed,
        "cpu": CPU,
        "siblings": SIBLINGS,
        "before_cpu_stat": before,
        "after_cpu_stat": after,
        "before_freq": before_freq,
        "after_freq": after_freq,
        "app": app_metrics(stdout_text) if completed.returncode == 0 else None,
        "perf": perf_values(perf_path),
        "stdout_file": str(stdout_path),
        "perf_file": str(perf_path),
        "result_file": str(result_path),
        "stdout_sha256": sha256(stdout_path),
        "perf_sha256": sha256(perf_path),
        "result_sha256": sha256(result_path) if result_path.exists() else None,
    }
    return record


def main() -> int:
    raw_dir = STAGE / f"raw-{RUN_TAG}"
    raw_dir.mkdir(parents=True, exist_ok=True)
    manifest = {
        "timestamp": time.time(),
        "host": subprocess.check_output(["hostname"], text=True).strip(),
        "cpu": CPU,
        "siblings": SIBLINGS,
        "orders": ORDERS,
        "variants": {code: {"label": LABELS[code], "binary": str(path), "sha256": sha256(path)} for code, path in BINS.items()},
        "data": str(DATA),
        "repetitions_per_process": 30,
        "triplets": len(ORDERS),
        "harness": str(STAGE / "stage_b_repeat.cc"),
        "harness_sha256": sha256(STAGE / "stage_b_repeat.cc"),
        "runner": str(STAGE / "stage_b_run.py"),
        "run_tag": RUN_TAG,
        "adjustment_reason": os.environ.get("STAGEB_ADJUSTMENT_REASON"),
    }
    (raw_dir / "manifest.json").write_text(json.dumps(manifest, indent=2))
    records: list[dict[str, Any]] = []
    raw_jsonl = raw_dir / "records.jsonl"
    with raw_jsonl.open("w") as stream:
        for triplet, order in enumerate(ORDERS, 1):
            for code in order:
                run_index = len(records) + 1
                print(f"running triplet={triplet} order={order} variant={code}", flush=True)
                record = run_one(triplet, order, code, run_index, raw_dir)
                records.append(record)
                stream.write(json.dumps(record, sort_keys=True) + "\n")
                stream.flush()
                if record["returncode"] != 0:
                    print(json.dumps(record, indent=2), file=sys.stderr)
                    return 1
                print(json.dumps(record["app"], sort_keys=True), flush=True)
    (raw_dir / "records.json").write_text(json.dumps(records, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
