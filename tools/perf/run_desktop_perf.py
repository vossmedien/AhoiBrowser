#!/usr/bin/env python3
"""Measure AhoiBrowser against unmodified Chromium under matched conditions.

Method: docs/PERFORMANCE_METHODOLOGY.md. The runner launches the given app
bundles itself with disposable profiles, interleaves candidate and baseline
runs (A B A B ...), records host conditions, and writes one run file per app
plus an evaluation against the Master budgets.

It refuses to run while a build or other heavy work is active, while the owner
is using the Mac, on battery, or against /Applications/AhoiBrowser.app without
an explicitly confirmed `installed-app` lease. It only stops processes it
started itself.
"""

from __future__ import annotations

import argparse
import contextlib
import datetime as dt
import hashlib
import http.server
import json
import math
import os
import pathlib
import plistlib
import queue
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
from typing import Optional

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import cdp  # noqa: E402
import build_evidence  # noqa: E402
import perf_stats  # noqa: E402
import owned_process  # noqa: E402
import runtime_guard  # noqa: E402

SCENARIO_VERSION = 2
INSTALLED_APP = pathlib.Path("/Applications/AhoiBrowser.app")
BUSY_PROCESSES = ("ninja", "autoninja", "siso", "clang", "clang++", "ld64.lld",
                  "lld", "swift-frontend", "xcodebuild", "rustc")
AX_CLIENTS = ("VoiceOver", "ahoi-axtool", "Accessibility Inspector")
WINDOW_SIZE = "1440,900"
BASE_FLAGS = ("--no-first-run", "--no-default-browser-check",
              f"--window-size={WINDOW_SIZE}", "--window-position=0,0")
SPEEDOMETER_URL = "https://browserbench.org/Speedometer3.1/?startAutomatically=true"
SPEEDOMETER_RESULT = ("(() => { const e = document.querySelector('#result-number');"
                      " const v = e && parseFloat(e.textContent);"
                      " return Number.isFinite(v) && v > 0 ? v : null; })()")


class Refused(SystemExit):
    pass


# --------------------------------------------------------------------------- host

def run(*args: str) -> str:
    # 2 s timed out for `ps` at load ~100+ (validation runs 1 and ws4, 28 Sep).
    return subprocess.run(args, capture_output=True, text=True, timeout=10, check=True).stdout.strip()


def hid_idle_seconds() -> float:
    # Millisecond resolution: whole seconds plus late timestamps made the
    # guard see phantom input on a loaded host (validation ws8/cb6, 28 Sep).
    for line in run("ioreg", "-c", "IOHIDSystem").splitlines():
        if "HIDIdleTime" in line:
            return int(line.split()[-1]) // 1_000_000 / 1000
    return 0.0


def hid_idle_sample() -> dict:
    """Idle seconds with the clocks read right after it, not after later probes."""
    idle = hid_idle_seconds()
    return {"hidIdleSeconds": idle, "hidIdleSampledAt": time.monotonic(),
            "hidIdleSampledWall": time.time()}


def busy_processes() -> list[str]:
    names = run("ps", "-axco", "comm=").splitlines()
    return sorted({name.strip() for name in names if name.strip() in BUSY_PROCESSES})


def running_ax_clients() -> list[str]:
    names = {name.strip() for name in run("ps", "-axco", "comm=").splitlines()}
    return sorted(name for name in AX_CLIENTS if name in names)


def power_source() -> str:
    first = run("pmset", "-g", "batt").splitlines()[:1]
    if first and "AC Power" in first[0]:
        return "ac"
    return "battery" if first else "unknown"


def port_free(port: int) -> bool:
    with socket.socket() as sock:
        return sock.connect_ex(("127.0.0.1", port)) != 0


def host_conditions() -> dict:
    return {
        "hardwareModel": run("sysctl", "-n", "hw.model"),
        "cpuCount": os.cpu_count(),
        "osBuild": run("sw_vers", "-buildVersion"),
        "powerSource": power_source(),
        "loadAverage": list(os.getloadavg()),
        "thermal": run("pmset", "-g", "therm"),
        "accessibilityClients": running_ax_clients(),
        "busyProcesses": busy_processes(),
        **hid_idle_sample(),
    }


def split_ax_clients(listing: str, owned_groups: set[int]) -> tuple[list[str], list[str]]:
    """(foreign, owned) AX clients from `ps -axco pgid=,comm=` output.

    An AX client in a process group the guard spawned (the trace driver's own
    session, e.g. its `ahoi-axtool`) is the measurement's documented input
    method, not an outside accessibility change; every other client is foreign.
    """
    foreign, owned = set(), set()
    for line in listing.splitlines():
        parts = line.strip().split(None, 1)
        if len(parts) != 2 or not parts[0].isdigit() or parts[1].strip() not in AX_CLIENTS:
            continue
        (owned if int(parts[0]) in owned_groups else foreign).add(parts[1].strip())
    return sorted(foreign), sorted(owned)


def runtime_host_state() -> dict:
    # One process snapshot per poll; do not use system load while the benchmark
    # itself is intentionally busy. Every probe is bounded by run()'s timeout.
    idle = hid_idle_sample()
    listing = run("ps", "-axco", "pgid=,comm=")
    names = {line.strip().split(None, 1)[-1].strip()
             for line in listing.splitlines() if line.strip()}
    guard = runtime_guard.current()
    foreign, owned = split_ax_clients(listing, guard.owned_process_groups() if guard else set())
    return {**idle, "powerSource": power_source(),
            "busyProcesses": sorted(names.intersection(BUSY_PROCESSES)),
            "accessibilityClients": foreign, "ownedAccessibilityClients": owned}


def measurement_sleep(seconds: float) -> None:
    guard = runtime_guard.current()
    if guard:
        guard.wait(seconds)
    else:
        time.sleep(seconds)


def preflight(apps: list[pathlib.Path], port: int, min_idle: int, lease: bool,
              validation_run: bool = False) -> dict:
    """Refuse on an unsuitable host. A validation run (explicit owner approval)
    only proves the harness on a candidate: it skips the quiet-host and idle
    gates, records the conditions and never supports a budget verdict."""
    conditions = host_conditions()
    reasons = []
    if validation_run:
        if not port_free(port):
            reasons.append(f"DevTools port {port} busy")
        for app in apps:
            if app.resolve() == INSTALLED_APP and not lease:
                reasons.append("installed app requires a confirmed installed-app lease (--lease)")
        if reasons:
            raise Refused("refusing to measure: " + "; ".join(reasons))
        return conditions
    if conditions["busyProcesses"]:
        reasons.append("build or compiler activity: " + ", ".join(conditions["busyProcesses"]))
    if conditions["loadAverage"][0] > 0.3 * (conditions["cpuCount"] or 1):
        reasons.append(f"load average {conditions['loadAverage'][0]:.1f} too high")
    if conditions["powerSource"] != "ac":
        reasons.append("not on AC power")
    if conditions["hidIdleSeconds"] < min_idle:
        reasons.append(f"owner active (idle {conditions['hidIdleSeconds']} s < {min_idle} s)")
    if not port_free(port):
        reasons.append(f"DevTools port {port} busy")
    for app in apps:
        if app.resolve() == INSTALLED_APP and not lease:
            reasons.append("installed app requires a confirmed installed-app lease (--lease)")
    if reasons:
        raise Refused("refusing to measure: " + "; ".join(reasons))
    return conditions


# --------------------------------------------------------------------------- app

def app_identity(app: pathlib.Path) -> dict:
    # Chromium's Mac sandbox requires the executable to run from its real
    # bundle path; a symlinked or partially copied bundle crashes the first
    # child launch (DCHECK in SetupCommonSandboxParameters). Callers may pass
    # a symlink so that their own command line does not name the bundle.
    app = app.resolve()
    with (app / "Contents/Info.plist").open("rb") as handle:
        plist = plistlib.load(handle)
    executable = app / "Contents/MacOS" / plist["CFBundleExecutable"]
    digest = hashlib.sha256(executable.read_bytes()).hexdigest()
    return {
        "path": str(app),
        "executable": str(executable),
        "binarySha256": digest,
        "bundleIdentifier": plist.get("CFBundleIdentifier"),
        "chromiumVersion": plist.get("AhoiChromiumVersion")
        or plist.get("CFBundleShortVersionString"),
        "sourceCommit": plist.get("AhoiSourceCommit"),
        "buildProfile": plist.get("AhoiBuildProfile"),
        "chromiumCommit": plist.get("AhoiChromiumCommit"),
        "gnArgsSha256": plist.get("AhoiGNArgsSHA256"),
    }


# --------------------------------------------------------------------------- fixtures

START_PAGE = """<!doctype html><title>ahoi perf start</title><h1>start</h1><script>
// The local page is renderable before this parser-blocking script. Chromium's
// buffered paint entry names the actual first rendered frame, independently of
// load-event/network timing. A missing paint is a failed sample, never a proxy.
let paintChecks = 0;
function reportFirstPaint() {
  const entry = performance.getEntriesByType('paint').find(e => e.name === 'first-paint');
  if (!entry && ++paintChecks < 150) {
    setTimeout(reportFirstPaint, 100);
    return;
  }
  const navigation = performance.getEntriesByType('navigation')[0];
  fetch('/mark', {method: 'POST', body: JSON.stringify({
    label: new URLSearchParams(location.search).get('run'),
    firstPaintEpochMs: entry ? performance.timeOrigin + entry.startTime : null,
    loadEventEndEpochMs: navigation ? performance.timeOrigin + navigation.loadEventEnd : null,
    paintEntryName: entry ? entry.name : null})});
}
addEventListener('load', () => setTimeout(reportFirstPaint, 0));
</script>"""


def first_paint_ms(mark: dict, spawned_epoch_ms: float) -> float:
    """Require the browser's paint entry, not a load/receipt timing proxy."""
    value = mark.get("firstPaintEpochMs")
    received = mark.get("receivedEpochMs")
    if (mark.get("paintEntryName") != "first-paint" or
            type(value) not in (int, float) or
            type(received) not in (int, float) or
            not all(math.isfinite(number) for number in
                    (value, received, spawned_epoch_ms)) or
            not spawned_epoch_ms <= value <= received):
        raise ValueError("startup sample lacks a bounded first-paint entry")
    return value - spawned_epoch_ms


def content_page(index: int) -> str:
    rows = "".join(f"<tr><td>{index}-{row}</td><td>{'lorem ipsum ' * 8}</td></tr>"
                   for row in range(200))
    return (f"<!doctype html><title>ahoi perf page {index}</title>"
            f"<style>td{{padding:4px;border:1px solid #ccc}}</style>"
            f"<h1>page {index}</h1><table>{rows}</table>")


class FixtureServer:
    def __init__(self):
        self.marks: "queue.Queue[dict]" = queue.Queue()
        marks = self.marks

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def _send(self, body: str, status: int = 200):
                data = body.encode()
                self.send_response(status)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def do_GET(self):
                if self.path.startswith("/start"):
                    self._send(START_PAGE)
                elif self.path.startswith("/page/"):
                    self._send(content_page(int(self.path.split("/")[2].split("?")[0] or 0)))
                else:
                    self._send("not found", 404)

            def do_POST(self):
                length = int(self.headers.get("Content-Length", 0))
                marks.put({**json.loads(self.rfile.read(length) or b"{}"),
                           "receivedEpochMs": time.time() * 1000})
                self._send("ok")

        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self.httpd.server_address[1]
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def url(self, path: str) -> str:
        return f"http://127.0.0.1:{self.port}{path}"

    def wait_mark(self, label: str, timeout: float) -> dict:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            guard = runtime_guard.current()
            if guard:
                guard.check()
            try:
                mark = self.marks.get(timeout=min(0.25, max(0.05, deadline - time.monotonic())))
            except queue.Empty:
                continue
            if mark.get("label") == label:
                return mark
        raise cdp.CDPError(f"start page {label} did not report load in {timeout}s")

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()


# --------------------------------------------------------------------------- processes

def parse_cputime(value: str) -> float:
    days = 0
    if "-" in value:
        day_text, value = value.split("-", 1)
        days = int(day_text)
    parts = [float(part) for part in value.split(":")]
    seconds = 0.0
    for part in parts:
        seconds = seconds * 60 + part
    return days * 86400 + seconds


def process_table() -> dict[int, dict]:
    table = {}
    for line in run("ps", "-axo", "pid=,ppid=,rss=,time=").splitlines():
        pid, ppid, rss, cputime = line.split(None, 3)
        table[int(pid)] = {"ppid": int(ppid), "rssKiB": int(rss),
                           "cpuSeconds": parse_cputime(cputime.strip())}
    return table


def tree(root: int, table: dict[int, dict]) -> list[int]:
    members, frontier = [], [root]
    while frontier:
        pid = frontier.pop()
        if pid in table:
            members.append(pid)
            frontier.extend(child for child, info in table.items() if info["ppid"] == pid)
    return members


def tree_totals(root: int) -> dict:
    table = process_table()
    members = tree(root, table)
    return {"processes": len(members),
            "rssKiB": sum(table[pid]["rssKiB"] for pid in members),
            "cpuSeconds": sum(table[pid]["cpuSeconds"] for pid in members)}


class Browser:
    def __init__(self, app: dict, profile: pathlib.Path, port: int, url: str,
                 extra_flags: tuple[str, ...]):
        self.port = port
        self.flags = (*BASE_FLAGS, *extra_flags)
        self.spawn_epoch_ms = time.time() * 1000
        self.spawn_monotonic = time.monotonic()
        self.guard = runtime_guard.current()
        spawn = self.guard.spawn if self.guard else subprocess.Popen
        self.process = spawn(
            [app["executable"], f"--user-data-dir={profile}",
             f"--remote-debugging-port={port}", *self.flags, url],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        try:
            self.devtools_ready_ms = (cdp.wait_for_endpoint(
                port, 60, check_cancel=self.guard.check if self.guard else None)
                                      - self.spawn_monotonic) * 1000
        except BaseException:
            self._stop()
            raise

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.quit()

    def session(self) -> cdp.CDPSession:
        return cdp.browser_session(self.port, check_cancel=self.guard.check if self.guard else None)

    def _stop(self, grace=0):
        guard = getattr(self, "guard", None)
        if guard:
            guard.stop_process(self.process, grace=grace)
        else:
            owned_process.stop(self.process, grace=grace)

    def quit(self) -> None:
        if self.process.poll() is not None:
            self.process.wait()
            return
        if getattr(self, "guard", None) and self.guard.reason:
            self._stop()
            return
        try:
            with contextlib.closing(self.session()) as session:
                session.send("Browser.close")
        except (OSError, cdp.CDPError):
            pass
        finally:
            self._stop(grace=5)


# --------------------------------------------------------------------------- scenarios

def scenario_startup(app, fixtures, port, flags, label, workdir) -> dict:
    profile = pathlib.Path(tempfile.mkdtemp(prefix="profile-", dir=workdir))
    samples = {}
    for phase in ("first_launch", "warm"):
        run_label = f"{label}-{phase}"
        with Browser(app, profile, port, fixtures.url(f"/start?run={run_label}"), flags) as browser:
            mark = fixtures.wait_mark(run_label, 60)
            samples[f"startup_{phase}_ms"] = first_paint_ms(mark, browser.spawn_epoch_ms)
            samples[f"devtools_ready_{phase}_ms"] = browser.devtools_ready_ms
            measurement_sleep(3)
        measurement_sleep(2)
    shutil.rmtree(profile, ignore_errors=True)
    return samples


def open_tabs(browser: Browser, fixtures: FixtureServer, count: int) -> None:
    with contextlib.closing(browser.session()) as session:
        for index in range(count):
            session.send("Target.createTarget", {"url": fixtures.url(f"/page/{index}")})


def scenario_memory(app, fixtures, port, flags, label, workdir, tabs=(1, 20),
                    settle=20.0) -> dict:
    samples = {}
    for count in tabs:
        profile = pathlib.Path(tempfile.mkdtemp(prefix="profile-", dir=workdir))
        with Browser(app, profile, port, fixtures.url("/page/0"), flags) as browser:
            open_tabs(browser, fixtures, count - 1)
            measurement_sleep(settle)
            totals = tree_totals(browser.process.pid)
            samples[f"memory_{count}_tabs_kib"] = totals["rssKiB"]
            samples[f"processes_{count}_tabs"] = totals["processes"]
        shutil.rmtree(profile, ignore_errors=True)
    return samples


def scenario_idle(app, fixtures, port, flags, label, workdir, settle=30.0,
                  window=60.0) -> dict:
    profile = pathlib.Path(tempfile.mkdtemp(prefix="profile-", dir=workdir))
    with Browser(app, profile, port, fixtures.url("/page/0"), flags) as browser:
        measurement_sleep(settle)
        before = tree_totals(browser.process.pid)
        measurement_sleep(window)
        after = tree_totals(browser.process.pid)
    shutil.rmtree(profile, ignore_errors=True)
    return {"idle_cpu_percent": 100 * (after["cpuSeconds"] - before["cpuSeconds"]) / window}


def scenario_speedometer(app, fixtures, port, flags, label, workdir, timeout=900.0) -> dict:
    profile = pathlib.Path(tempfile.mkdtemp(prefix="profile-", dir=workdir))
    score = None
    with Browser(app, profile, port, SPEEDOMETER_URL, flags):
        target = next(t for t in cdp.http_json(port, "/json") if t["type"] == "page")
        guard = runtime_guard.current()
        with contextlib.closing(cdp.CDPSession(
                target["webSocketDebuggerUrl"], check_cancel=guard.check if guard else None)) as session:
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline and score is None:
                measurement_sleep(10)
                score = session.evaluate(SPEEDOMETER_RESULT)
    shutil.rmtree(profile, ignore_errors=True)
    if score is None:
        raise cdp.CDPError("Speedometer produced no score")
    return {"speedometer_score": score}


def trace_durations_ms(events: list[dict], names: dict[str, str]) -> dict[str, list[float]]:
    """Durations of complete ('X') or begin/end ('B'/'E') trace events, by metric."""
    result: dict[str, list[float]] = {metric: [] for metric in names.values()}
    open_events: dict[tuple, float] = {}
    for event in events:
        metric = names.get(event.get("name"))
        if not metric:
            continue
        phase = event.get("ph")
        if phase == "X":
            result[metric].append(event.get("dur", 0) / 1000)
        elif phase == "B":
            open_events[(event["name"], event.get("tid"))] = event["ts"]
        elif phase == "E":
            start = open_events.pop((event["name"], event.get("tid")), None)
            if start is not None:
                result[metric].append((event["ts"] - start) / 1000)
    return result


# cc/metrics/compositor_frame_reporter.cc emits one async "PipelineReporter"
# track per compositor frame (categories "cc,benchmark,..."); the begin event
# carries the frame's final state and its end is the presentation time.
PRESENTED_FRAME_STATES = {"STATE_PRESENTED_ALL", "STATE_PRESENTED_PARTIAL"}
PRESENTED_FRAME_LIMIT_US = 2_000_000  # a switch without a frame in 2 s is a failure


class TraceError(RuntimeError):
    pass


def presented_frames(events: list[dict]) -> dict[int, list[tuple[float, float]]]:
    """(begin, end) microseconds of presented PipelineReporter frames per pid."""
    begins: dict[tuple, tuple[float, str]] = {}
    frames: dict[int, list[tuple[float, float]]] = {}
    for event in events:
        if event.get("name") != "PipelineReporter" or event.get("ph") not in ("b", "e"):
            continue
        identity = event.get("id2") or event.get("id")
        key = (event.get("pid"), json.dumps(identity, sort_keys=True))
        if event["ph"] == "b":
            reporter = (event.get("args") or {}).get("chrome_frame_reporter") or {}
            begins[key] = (event["ts"], str(reporter.get("state", "")))
            continue
        begin = begins.pop(key, None)
        if begin is not None and begin[1] in PRESENTED_FRAME_STATES:
            frames.setdefault(event.get("pid"), []).append((begin[0], event["ts"]))
    for values in frames.values():
        values.sort()
    return frames


def presented_latency_ms(events: list[dict], names: dict[str, str]) -> dict[str, list[float]]:
    """Ahoi event start to the end of the first presented frame begun after it ended.

    A frame whose frame time precedes the end of the Ahoi event cannot contain
    its result. Metric names end in `_presented_ms`. A traced Ahoi event with no
    such frame in the same process within 2 s aborts the scenario, so a missing
    frame can never shorten the statistic.
    """
    frames = presented_frames(events)
    spans: list[tuple[str, int, float, float]] = []
    open_events: dict[tuple, float] = {}
    for event in events:
        metric = names.get(event.get("name"))
        if not metric:
            continue
        phase = event.get("ph")
        if phase == "X":
            spans.append((metric, event.get("pid"), event["ts"], event["ts"] + event.get("dur", 0)))
        elif phase == "B":
            open_events[(event["name"], event.get("pid"), event.get("tid"))] = event["ts"]
        elif phase == "E":
            start = open_events.pop((event["name"], event.get("pid"), event.get("tid")), None)
            if start is not None:
                spans.append((metric, event.get("pid"), start, event["ts"]))
    result: dict[str, list[float]] = {
        metric.removesuffix("_ms") + "_presented_ms": [] for metric in names.values()}
    for metric, pid, start, end in spans:
        frame = next((frame_end for frame_begin, frame_end in frames.get(pid, [])
                      if frame_begin >= end), None)
        if frame is None or frame - start > PRESENTED_FRAME_LIMIT_US:
            raise TraceError(f"no presented frame after {metric} in process {pid}")
        result[metric.removesuffix("_ms") + "_presented_ms"].append((frame - start) / 1000)
    return result


def scenario_trace(app, fixtures, port, flags, label, workdir, driver=None,
                   categories=("browser", "benchmark"), names=None, setup=None) -> dict:
    """Trace Ahoi UI events while an external driver (e.g. an axtool journey) runs.

    `driver` is a command receiving the browser PID and DevTools port as
    AHOI_PERF_PID / AHOI_PERF_PORT. Requires the Ahoi trace events of handoff 002.
    An optional `setup` command (same environment) runs before tracing starts,
    so preparation such as creating a second Workspace yields no samples.
    """
    profile = pathlib.Path(tempfile.mkdtemp(prefix="profile-", dir=workdir))
    events = []
    with Browser(app, profile, port, fixtures.url("/page/0"), flags) as browser:
        with contextlib.closing(browser.session()) as session:
            env = {**os.environ, "AHOI_PERF_PID": str(browser.process.pid),
                   "AHOI_PERF_PORT": str(port)}
            if setup:
                run_driver(setup, env)
            session.send("Tracing.start", {"traceConfig": {"includedCategories": list(categories)},
                                          "transferMode": "ReportEvents"})
            run_driver(driver, env)
            session.send("Tracing.end")
            while True:
                message = session.wait_any(("Tracing.dataCollected", "Tracing.tracingComplete"), 120)
                if message["method"] == "Tracing.tracingComplete":
                    break
                events.extend(message["params"]["value"])
    shutil.rmtree(profile, ignore_errors=True)
    return {**trace_durations_ms(events, names or {}),
            **presented_latency_ms(events, names or {})}


def run_driver(driver: str, env: dict, timeout: float = 900) -> None:
    # Own the shell and its foreground descendants as one process group, so a
    # timeout cannot leave the UI-driving child active after its shell is killed.
    guard = runtime_guard.current()
    spawn = guard.spawn if guard else subprocess.Popen
    if guard:
        if not guard.driver_input_log:
            handle, path = tempfile.mkstemp(prefix="ahoi-perf-driver-input-")
            os.close(handle)  # mkstemp: private 0600 file
            guard.driver_input_log = path
        env = {**env, "AHOI_PERF_DRIVER_INPUT_LOG": guard.driver_input_log}
    process = spawn(driver, shell=True, env=env, start_new_session=True)
    try:
        code = guard.wait_process(process, timeout) if guard else process.wait(timeout=timeout)
        if code:
            raise subprocess.CalledProcessError(code, "trace driver")
    finally:
        if guard:
            guard.stop_process(process)
        else:
            owned_process.stop(process)


SCENARIOS = {
    "startup": scenario_startup,
    "memory": scenario_memory,
    "idle": scenario_idle,
    "speedometer": scenario_speedometer,
    "trace": scenario_trace,
}


# --------------------------------------------------------------------------- main

def add_samples(metrics: dict, samples: dict) -> None:
    for metric, value in samples.items():
        if isinstance(value, list):
            metrics.setdefault(metric, []).extend(value)
        else:
            metrics.setdefault(metric, []).append(value)


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--app", type=pathlib.Path, required=True, help="candidate bundle")
    parser.add_argument("--baseline-app", type=pathlib.Path,
                        help="unmodified Chromium of the same revision")
    parser.add_argument("--build-receipt", type=pathlib.Path,
                        help="candidate build receipt bound to the exact bundle")
    parser.add_argument("--baseline-build-receipt", type=pathlib.Path,
                        help="unmodified Chromium release build receipt")
    parser.add_argument("--scenario", action="append", choices=sorted(SCENARIOS),
                        required=True)
    parser.add_argument("--runs", type=int, default=perf_stats.MIN_SAMPLES)
    parser.add_argument("--port", type=int, default=9355)
    parser.add_argument("--flag", action="append", default=[],
                        help="extra browser flag, applied to both apps")
    parser.add_argument("--driver", help="trace scenario: external journey command")
    parser.add_argument("--driver-setup",
                        help="trace scenario: preparation command run before tracing starts")
    parser.add_argument("--trace-metric", action="append", default=[],
                        help="trace scenario: EVENT_NAME=metric")
    parser.add_argument("--min-idle", type=int, default=300)
    parser.add_argument("--lease", action="store_true",
                        help="an installed-app lease is confirmed by the desktop owner")
    parser.add_argument("--lease-checkpoint", type=pathlib.Path,
                        default=build_evidence.ROOT / "docs/ACTIVE_DESKTOP_CHECKPOINT.md",
                        help="owner checkpoint containing the current Crest-H3-Lease marker")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--validation-run", action="store_true",
                        help="owner-approved harness validation on a busy or attended host; "
                             "results are marked and never count for budgets")
    args = parser.parse_args(argv)
    if args.runs < 1:
        parser.error("--runs must be positive")
    if "trace" in args.scenario and not args.driver:
        parser.error("trace requires an explicit --driver")

    apps = [args.app] + ([args.baseline_app] if args.baseline_app else [])
    identities = [app_identity(app) for app in apps]
    receipt_paths = [args.build_receipt] + ([args.baseline_build_receipt] if args.baseline_app else [])
    try:
        evidence = [build_evidence.verify(identity, path) if path else None
                    for identity, path in zip(identities, receipt_paths)]
    except (OSError, ValueError) as error:
        print(f"refusing to measure: {error}", file=sys.stderr)
        return 7
    runs = [{"app": identity, "buildEvidence": proof, "metrics": {}}
            for identity, proof in zip(identities, evidence)]
    problems = [build_evidence.budget_problem(run, baseline=index > 0)
                for index, run in enumerate(runs)]
    if len(runs) > 1 and not any(problems):
        if evidence[0]["comparison"] != evidence[1]["comparison"]:
            problems.append("candidate and baseline build configurations differ")
    if args.dry_run:
        print(json.dumps({"apps": identities, "scenarios": args.scenario, "runs": args.runs,
                          "buildEvidence": evidence, "budgetProblems": [p for p in problems if p],
                          "host": host_conditions()}, indent=2))
        return 0
    if not args.validation_run and any(problems):
        print("refusing to measure: " + "; ".join(p for p in problems if p), file=sys.stderr)
        return 7
    try:
        conditions = preflight(apps, args.port, args.min_idle, args.lease,
                               args.validation_run)
    except Refused as refusal:
        print(refusal, file=sys.stderr)
        return 7

    names = dict(item.split("=", 1) for item in args.trace_metric)
    bound = [{**identity, "bundleTreeSha256": (proof or {}).get("bundleTreeSha256")}
             for identity, proof in zip(identities, evidence)]
    guard = runtime_guard.LeaseGuard(
        args.lease_checkpoint, bound, conditions, runtime_host_state,
        min_idle=args.min_idle, validation=args.validation_run,
        installed=any(app.resolve() == INSTALLED_APP for app in apps))
    try:
        args.output.mkdir(parents=True, exist_ok=False)
    except OSError:
        print("refusing to measure: output must be a new writable directory", file=sys.stderr)
        return 7
    workdir = pathlib.Path(tempfile.mkdtemp(prefix="ahoi-perf-"))
    started = dt.datetime.now(dt.timezone.utc).isoformat()
    fixtures = None
    failure = None
    try:
        with guard:
            fixtures = FixtureServer()
            for index in range(args.runs):
                for scenario in args.scenario:
                    # Interleave A B A B so slow host drift hits both apps alike.
                    for run_index, identity in enumerate(identities):
                        guard.check()
                        kwargs = {}
                        if scenario == "trace":
                            kwargs = {"driver": args.driver, "names": names,
                                      "setup": args.driver_setup}
                        samples = SCENARIOS[scenario](identity, fixtures, args.port,
                                                      tuple(args.flag), f"r{index}a{run_index}",
                                                      workdir, **kwargs)
                        guard.check()
                        add_samples(runs[run_index]["metrics"], samples)
            after = host_conditions()
    except (Exception, KeyboardInterrupt) as error:
        failure = error
    finally:
        try:
            if fixtures:
                fixtures.close()
        except Exception as error:
            failure = failure or error
        if not isinstance(failure, owned_process.CleanupError):
            shutil.rmtree(workdir, ignore_errors=True)

    if failure is not None:
        # No evaluation.json or ordinary sample evidence for a partial run.
        # Avoid writing arbitrary exception/driver strings, which can hold secrets.
        aborted = {"schemaVersion": 1, "kind": "ahoi-perf-aborted", "pass": False,
                   "startedAt": started, "failureType": type(failure).__name__,
                   "partialRuns": runs, "hostBefore": conditions, "runtimeGuard": guard.summary()}
        if isinstance(failure, owned_process.CleanupError):
            aborted["retainedProfileDirectory"] = str(workdir)
        (args.output / "aborted-run.json").write_text(json.dumps(aborted, indent=2) + "\n")
        print(f"measurement aborted ({type(failure).__name__}); no budget evidence", file=sys.stderr)
        return (130 if isinstance(failure, KeyboardInterrupt) else
                7 if isinstance(failure, runtime_guard.LeaseError) else 1)

    for run_data, identity in zip(runs, identities):
        run_data.update({
            "schemaVersion": 2,
            "kind": "ahoi-perf-run",
            "startedAt": started,
            "conditions": {
                "chromiumVersion": identity["chromiumVersion"],
                "hardwareModel": conditions["hardwareModel"],
                "osBuild": conditions["osBuild"],
                "powerSource": conditions["powerSource"],
                "flags": sorted((*BASE_FLAGS, *args.flag)),
                "windowSize": WINDOW_SIZE,
                "accessibilityClients": conditions["accessibilityClients"],
                "scenarioVersion": SCENARIO_VERSION,
                "validationRun": args.validation_run,
            },
            "hostBefore": conditions,
            "hostAfter": after,
            "runtimeGuard": guard.summary(),
        })
    (args.output / "candidate-run.json").write_text(json.dumps(runs[0], indent=2) + "\n")
    baseline = None
    if len(runs) > 1:
        baseline = runs[1]
        (args.output / "baseline-run.json").write_text(json.dumps(baseline, indent=2) + "\n")
    evaluation = perf_stats.evaluate(runs[0], baseline)
    (args.output / "evaluation.json").write_text(json.dumps(evaluation, indent=2) + "\n")
    for verdict in evaluation["verdicts"]:
        print(f"{verdict['budget']:14} {verdict['verdict']:13} {verdict['text']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
