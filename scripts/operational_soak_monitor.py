#!/usr/bin/env python3
"""Read-only long-term monitor for a running sx1255-multimode service."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path


def iso_now() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")


def iso_from_epoch(value: float) -> str:
    return datetime.fromtimestamp(value, timezone.utc).astimezone().isoformat(
        timespec="seconds"
    )


def get_json(base_url: str, path: str, timeout: float = 3.0):
    with urllib.request.urlopen(base_url.rstrip("/") + path, timeout=timeout) as response:
        return json.load(response)


def systemctl_show(unit: str) -> dict:
    try:
        result = subprocess.run(
            [
                "systemctl", "show", unit,
                "-p", "ActiveState",
                "-p", "MainPID",
                "-p", "NRestarts",
                "-p", "ExecMainStartTimestamp",
            ],
            check=False,
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (OSError, subprocess.SubprocessError):
        return {}
    values = {}
    for line in result.stdout.splitlines():
        key, sep, value = line.partition("=")
        if sep:
            values[key] = value
    return values


def read_text(path: str, default: str = "") -> str:
    try:
        return Path(path).read_text(encoding="utf-8").strip()
    except OSError:
        return default


def read_meminfo() -> dict:
    result = {}
    for line in read_text("/proc/meminfo").splitlines():
        key, _, rest = line.partition(":")
        if not rest:
            continue
        value = rest.strip().split()
        if value and value[0].isdigit():
            result[key] = int(value[0])
    return result


def read_temperature_c() -> float | None:
    raw = read_text("/sys/class/thermal/thermal_zone0/temp")
    try:
        return int(raw) / 1000.0
    except ValueError:
        return None


def selected_plugin_telemetry(plugins: list, name: str) -> dict:
    for plugin in plugins:
        if plugin.get("name") == name:
            return plugin.get("telemetry", {})
    return {}


def collect_sample(base_url: str, unit: str) -> tuple[dict, list[str]]:
    stat = os.statvfs("/")
    sample = {
        "timestamp": iso_now(),
        "epoch": time.time(),
        "boot_id": read_text("/proc/sys/kernel/random/boot_id"),
        "load": read_text("/proc/loadavg"),
        "temperature_c": read_temperature_c(),
        "meminfo": read_meminfo(),
        "disk_total_bytes": stat.f_blocks * stat.f_frsize,
        "disk_free_bytes": stat.f_bavail * stat.f_frsize,
        "service": systemctl_show(unit),
    }
    errors: list[str] = []
    endpoints = {
        "status": "/api/status",
        "health": "/api/health",
        "system": "/api/system",
        "plugins": "/api/plugins",
        "spectrum": "/api/spectrum",
    }
    data = {}
    for key, path in endpoints.items():
        try:
            data[key] = get_json(base_url, path)
        except (OSError, ValueError, urllib.error.URLError) as exc:
            errors.append(f"{path}: {exc}")
            data[key] = {}

    status = data["status"]
    health = data["health"]
    system = data["system"]
    spectrum = data["spectrum"]
    plugins = data["plugins"] if isinstance(data["plugins"], list) else []
    sample.update({
        "state": status.get("state"),
        "mode": status.get("mode"),
        "network_mode": status.get("network_mode"),
        "rssi": status.get("rssi"),
        "power_save": status.get("power_save"),
        "app_uptime_s": status.get("uptime"),
        "stats": status.get("stats", {}),
        "healthy": health.get("healthy"),
        "health": health.get("components", []),
        "memory_rss_kb": system.get("memory_rss_kb"),
        "cpu_seconds": system.get("cpu_seconds"),
        "threads": system.get("threads"),
        "voluntary_ctx_switches": system.get("voluntary_ctx_switches"),
        "frontend_state": system.get("frontend_state"),
        "spectrum_frame": spectrum.get("frame"),
        "spectrum_dropped_samples": spectrum.get("dropped_samples"),
        "noise_floor_db": spectrum.get("noise_floor_db"),
        "fm": selected_plugin_telemetry(plugins, "FM"),
        "dstar": selected_plugin_telemetry(plugins, "DSTAR"),
        "m17": selected_plugin_telemetry(plugins, "M17"),
        "errors": errors,
    })
    return sample, errors


def load_or_create_session(output_dir: Path, duration: int, interval: int):
    output_dir.mkdir(parents=True, exist_ok=True)
    state_path = output_dir / "session.json"
    if state_path.exists():
        state = json.loads(state_path.read_text(encoding="utf-8"))
        state["duration_seconds"] = duration
        state["interval_seconds"] = interval
        return state
    now = time.time()
    state = {
        "started_epoch": now,
        "started_at": iso_from_epoch(now),
        "ends_epoch": now + duration,
        "ends_at": iso_from_epoch(now + duration),
        "duration_seconds": duration,
        "interval_seconds": interval,
        "samples": 0,
        "endpoint_error_samples": 0,
        "unhealthy_samples": 0,
        "app_restarts": 0,
        "boot_changes": 0,
        "last_pid": None,
        "last_boot_id": None,
        "max_threads": 0,
        "max_rss_kb": 0,
        "first_rss_kb": None,
        "last_rss_kb": None,
        "max_temperature_c": None,
        "states": {},
        "modes": {},
        "completed": False,
    }
    state_path.write_text(json.dumps(state, indent=2, sort_keys=True) + "\n",
                          encoding="utf-8")
    return state


def merge_sample(state: dict, sample: dict, errors: list[str]):
    state["samples"] += 1
    if errors:
        state["endpoint_error_samples"] += 1
    if sample.get("healthy") is not True:
        state["unhealthy_samples"] += 1

    pid = sample.get("service", {}).get("MainPID")
    boot_id = sample.get("boot_id")
    previous_pid = state.get("last_pid")
    if previous_pid not in (None, "", "0") and pid not in (None, "", previous_pid):
        state["app_restarts"] += 1
    if state.get("last_boot_id") and boot_id != state["last_boot_id"]:
        state["boot_changes"] += 1
    state["last_pid"] = pid
    state["last_boot_id"] = boot_id

    threads = sample.get("threads")
    if isinstance(threads, int):
        state["max_threads"] = max(state["max_threads"], threads)
    rss = sample.get("memory_rss_kb")
    if isinstance(rss, int):
        state["max_rss_kb"] = max(state["max_rss_kb"], rss)
        if state["first_rss_kb"] is None:
            state["first_rss_kb"] = rss
        state["last_rss_kb"] = rss
    temp = sample.get("temperature_c")
    if isinstance(temp, (int, float)):
        state["max_temperature_c"] = max(state.get("max_temperature_c") or temp, temp)

    state_name = str(sample.get("state") or "UNKNOWN")
    mode_name = str(sample.get("mode") or "NONE")
    state["states"][state_name] = state["states"].get(state_name, 0) + 1
    state["modes"][mode_name] = state["modes"].get(mode_name, 0) + 1


def journal_summary(started_epoch: float) -> dict:
    since = datetime.fromtimestamp(started_epoch).strftime("%Y-%m-%d %H:%M:%S")
    try:
        result = subprocess.run(
            ["journalctl", "-u", "sx1255-multimode", "--since", since,
             "--no-pager", "-o", "cat"],
            check=False,
            capture_output=True,
            text=True,
            timeout=30,
        )
    except (OSError, subprocess.SubprocessError):
        return {"available": False, "lines": 0}
    patterns = {
        "errors": ("error", "failed", "segfault", "killed"),
        "health_problems": ("health problem",),
        "playback_recoveries": ("playback recover",),
        "playback_failures": ("playback write failed",),
        "m17_frame_rejects": ("frame not usable",),
        "m17_lsf_failures": ("lsf crc fail",),
        "fm_digital_rejects": ("fm reject",),
    }
    counts = Counter()
    lines = result.stdout.splitlines()
    for line in lines:
        lower = line.lower()
        for key, needles in patterns.items():
            if any(needle in lower for needle in needles):
                counts[key] += 1
    return {"available": True, "lines": len(lines), **counts}


def write_reports(output_dir: Path, state: dict, latest: dict | None):
    state["elapsed_seconds"] = max(0, min(
        state["duration_seconds"], int(time.time() - state["started_epoch"])
    ))
    state["remaining_seconds"] = max(
        0, state["duration_seconds"] - state["elapsed_seconds"]
    )
    if latest:
        state["last_sample"] = latest
    serialized = json.dumps(state, indent=2, sort_keys=True) + "\n"
    (output_dir / "session.json").write_text(serialized, encoding="utf-8")
    (output_dir / "summary.json").write_text(
        serialized, encoding="utf-8"
    )
    report = [
        "# Operational soak",
        "",
        f"- start: `{state['started_at']}`",
        f"- end: `{state['ends_at']}`",
        f"- duration: {state['duration_seconds']} s "
        f"({state['duration_seconds'] / 3600:.0f} h)",
        f"- elapsed: {state['elapsed_seconds']} s",
        f"- remaining: {state['remaining_seconds']} s",
        f"- samples: {state['samples']}",
        f"- endpoint-error samples: {state['endpoint_error_samples']}",
        f"- unhealthy samples: {state['unhealthy_samples']}",
        f"- application restarts: {state['app_restarts']}",
        f"- boot changes: {state['boot_changes']}",
        f"- max RSS: {state['max_rss_kb']} kB",
        f"- RSS first/last: {state['first_rss_kb']} / {state['last_rss_kb']} kB",
        f"- max threads: {state['max_threads']}",
        f"- max temperature: {state['max_temperature_c']} C",
        f"- completed: {state['completed']}",
        "",
    ]
    if latest:
        report.extend([
            "## Last sample",
            "",
            f"- state: `{latest.get('state')}`",
            f"- mode: `{latest.get('mode')}`",
            f"- network: `{latest.get('network_mode')}`",
            f"- RSSI: {latest.get('rssi')} dBFS",
            f"- power save: {latest.get('power_save')}",
            f"- healthy: {latest.get('healthy')}",
            f"- endpoint errors: {latest.get('errors')}",
            "",
        ])
    (output_dir / "report.md").write_text("\n".join(report), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default="http://127.0.0.1:8080")
    parser.add_argument("--unit", default="sx1255-multimode.service")
    parser.add_argument("--output-dir", default="/var/lib/sx1255-operational-soak")
    parser.add_argument("--duration-seconds", type=int, default=7 * 24 * 3600)
    parser.add_argument("--interval-seconds", type=int, default=30)
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--reset", action="store_true")
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    if args.reset:
        for name in ("session.json", "samples.jsonl", "summary.json", "report.md",
                     "last_sample.json"):
            try:
                (output_dir / name).unlink()
            except FileNotFoundError:
                pass

    state = load_or_create_session(
        output_dir, max(1, args.duration_seconds), max(1, args.interval_seconds)
    )
    if state.get("completed") and not args.once:
        print(f"operational soak already completed: {output_dir / 'report.md'}")
        return 0

    latest = None
    while True:
        sample, errors = collect_sample(args.base_url, args.unit)
        merge_sample(state, sample, errors)
        with (output_dir / "samples.jsonl").open("a", encoding="utf-8") as out:
            out.write(json.dumps(sample, sort_keys=True, separators=(",", ":")) + "\n")
            out.flush()
            os.fsync(out.fileno())
        (output_dir / "last_sample.json").write_text(
            json.dumps(sample, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
        latest = sample

        if time.time() >= state["ends_epoch"]:
            state["completed"] = True
            state["journal"] = journal_summary(state["started_epoch"])
            state["finished_at"] = iso_now()
        write_reports(output_dir, state, latest)
        if state["completed"] or args.once:
            return 0
        time.sleep(max(1, state["interval_seconds"]))


if __name__ == "__main__":
    sys.exit(main())
