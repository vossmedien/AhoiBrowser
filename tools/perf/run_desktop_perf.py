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
import datetime as dt
import hashlib
import http.server
import json
import os
import pathlib
import plistlib
import queue
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from typing import Optional

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import cdp  # noqa: E402
import perf_stats  # noqa: E402

SCENARIO_VERSION = 1
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
    return subprocess.run(args, capture_output=True, text=True).stdout.strip()


def hid_idle_seconds() -> int:
    for line in run("ioreg", "-c", "IOHIDSystem").splitlines():
        if "HIDIdleTime" in line:
            return int(line.split()[-1]) // 1_000_000_000
    return 0


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
        "hidIdleSeconds": hid_idle_seconds(),
    }


def preflight(apps: list[pathlib.Path], port: int, min_idle: int, lease: bool) -> dict:
    conditions = host_conditions()
    reasons = []
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
    }


# --------------------------------------------------------------------------- fixtures

START_PAGE = """<!doctype html><title>ahoi perf start</title><h1>start</h1><script>
addEventListener('load', () => setTimeout(() => {
  const n = performance.getEntriesByType('navigation')[0];
  fetch('/mark', {method: 'POST', body: JSON.stringify({
    label: new URLSearchParams(location.search).get('run'),
    loadEventEndEpochMs: performance.timeOrigin + n.loadEventEnd})});
}, 0));
</script>"""


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
            try:
                mark = self.marks.get(timeout=max(0.05, deadline - time.monotonic()))
            except queue.Empty:
                break
            if mark.get("label") == label:
                return mark
        raise cdp.CDPError(f"start page {label} did not report load in {timeout}s")

    def close(self):
        self.httpd.shutdown()


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
        self.process = subprocess.Popen(
            [app["executable"], f"--user-data-dir={profile}",
             f"--remote-debugging-port={port}", *self.flags, url],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        self.devtools_ready_ms = (cdp.wait_for_endpoint(port, 60)
                                  - self.spawn_monotonic) * 1000

    def session(self) -> cdp.CDPSession:
        return cdp.browser_session(self.port)

    def quit(self) -> None:
        try:
            session = self.session()
            session.send("Browser.close")
            session.close()
        except (OSError, cdp.CDPError):
            pass
        try:
            self.process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            os.killpg(self.process.pid, signal.SIGTERM)  # only our own session
            self.process.wait(timeout=10)


# --------------------------------------------------------------------------- scenarios

def scenario_startup(app, fixtures, port, flags, label, workdir) -> dict:
    profile = pathlib.Path(tempfile.mkdtemp(prefix="profile-", dir=workdir))
    samples = {}
    for phase in ("first_launch", "warm"):
        run_label = f"{label}-{phase}"
        browser = Browser(app, profile, port, fixtures.url(f"/start?run={run_label}"), flags)
        mark = fixtures.wait_mark(run_label, 60)
        samples[f"startup_{phase}_ms"] = mark["loadEventEndEpochMs"] - browser.spawn_epoch_ms
        samples[f"devtools_ready_{phase}_ms"] = browser.devtools_ready_ms
        time.sleep(3)
        browser.quit()
        time.sleep(2)
    shutil.rmtree(profile, ignore_errors=True)
    return samples


def open_tabs(browser: Browser, fixtures: FixtureServer, count: int) -> None:
    session = browser.session()
    for index in range(count):
        session.send("Target.createTarget", {"url": fixtures.url(f"/page/{index}")})
    session.close()


def scenario_memory(app, fixtures, port, flags, label, workdir, tabs=(1, 20),
                    settle=20.0) -> dict:
    samples = {}
    for count in tabs:
        profile = pathlib.Path(tempfile.mkdtemp(prefix="profile-", dir=workdir))
        browser = Browser(app, profile, port, fixtures.url("/page/0"), flags)
        open_tabs(browser, fixtures, count - 1)
        time.sleep(settle)
        totals = tree_totals(browser.process.pid)
        samples[f"memory_{count}_tabs_kib"] = totals["rssKiB"]
        samples[f"processes_{count}_tabs"] = totals["processes"]
        browser.quit()
        shutil.rmtree(profile, ignore_errors=True)
    return samples


def scenario_idle(app, fixtures, port, flags, label, workdir, settle=30.0,
                  window=60.0) -> dict:
    profile = pathlib.Path(tempfile.mkdtemp(prefix="profile-", dir=workdir))
    browser = Browser(app, profile, port, fixtures.url("/page/0"), flags)
    time.sleep(settle)
    before = tree_totals(browser.process.pid)
    time.sleep(window)
    after = tree_totals(browser.process.pid)
    browser.quit()
    shutil.rmtree(profile, ignore_errors=True)
    return {"idle_cpu_percent": 100 * (after["cpuSeconds"] - before["cpuSeconds"]) / window}


def scenario_speedometer(app, fixtures, port, flags, label, workdir, timeout=900.0) -> dict:
    profile = pathlib.Path(tempfile.mkdtemp(prefix="profile-", dir=workdir))
    browser = Browser(app, profile, port, SPEEDOMETER_URL, flags)
    target = next(t for t in cdp.http_json(port, "/json") if t["type"] == "page")
    session = cdp.CDPSession(target["webSocketDebuggerUrl"])
    deadline = time.monotonic() + timeout
    score = None
    while time.monotonic() < deadline and score is None:
        time.sleep(10)
        score = session.evaluate(SPEEDOMETER_RESULT)
    session.close()
    browser.quit()
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


def scenario_trace(app, fixtures, port, flags, label, workdir, driver=None,
                   categories=("browser",), names=None) -> dict:
    """Trace Ahoi UI events while an external driver (e.g. an axtool journey) runs.

    `driver` is a command receiving the browser PID and DevTools port as
    AHOI_PERF_PID / AHOI_PERF_PORT. Requires the Ahoi trace events of handoff 002.
    """
    profile = pathlib.Path(tempfile.mkdtemp(prefix="profile-", dir=workdir))
    browser = Browser(app, profile, port, fixtures.url("/page/0"), flags)
    session = browser.session()
    session.send("Tracing.start", {"traceConfig": {"includedCategories": list(categories)},
                                   "transferMode": "ReportEvents"})
    env = {**os.environ, "AHOI_PERF_PID": str(browser.process.pid),
           "AHOI_PERF_PORT": str(port)}
    subprocess.run(driver, shell=True, env=env, check=True, timeout=900)
    session.send("Tracing.end")
    events = []
    while True:
        message = session.wait_any(("Tracing.dataCollected", "Tracing.tracingComplete"), 120)
        if message["method"] == "Tracing.tracingComplete":
            break
        events.extend(message["params"]["value"])
    session.close()
    browser.quit()
    shutil.rmtree(profile, ignore_errors=True)
    return trace_durations_ms(events, names or {})


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
    parser.add_argument("--scenario", action="append", choices=sorted(SCENARIOS),
                        required=True)
    parser.add_argument("--runs", type=int, default=perf_stats.MIN_SAMPLES)
    parser.add_argument("--port", type=int, default=9355)
    parser.add_argument("--flag", action="append", default=[],
                        help="extra browser flag, applied to both apps")
    parser.add_argument("--driver", help="trace scenario: external journey command")
    parser.add_argument("--trace-metric", action="append", default=[],
                        help="trace scenario: EVENT_NAME=metric")
    parser.add_argument("--min-idle", type=int, default=300)
    parser.add_argument("--lease", action="store_true",
                        help="an installed-app lease is confirmed by the desktop owner")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)

    apps = [args.app] + ([args.baseline_app] if args.baseline_app else [])
    identities = [app_identity(app) for app in apps]
    if args.dry_run:
        print(json.dumps({"apps": identities, "scenarios": args.scenario, "runs": args.runs,
                          "host": host_conditions()}, indent=2))
        return 0
    try:
        conditions = preflight(apps, args.port, args.min_idle, args.lease)
    except Refused as refusal:
        print(refusal, file=sys.stderr)
        return 7

    names = dict(item.split("=", 1) for item in args.trace_metric)
    runs = [{"app": identity, "metrics": {}} for identity in identities]
    fixtures = FixtureServer()
    workdir = pathlib.Path(tempfile.mkdtemp(prefix="ahoi-perf-"))
    started = dt.datetime.now(dt.timezone.utc).isoformat()
    try:
        for index in range(args.runs):
            for scenario in args.scenario:
                # Interleave A B A B so slow host drift hits both apps alike.
                for run_index, identity in enumerate(identities):
                    kwargs = {}
                    if scenario == "trace":
                        kwargs = {"driver": args.driver, "names": names}
                    samples = SCENARIOS[scenario](identity, fixtures, args.port,
                                                  tuple(args.flag), f"r{index}a{run_index}",
                                                  workdir, **kwargs)
                    add_samples(runs[run_index]["metrics"], samples)
    finally:
        fixtures.close()
        shutil.rmtree(workdir, ignore_errors=True)

    after = host_conditions()
    args.output.mkdir(parents=True, exist_ok=True)
    for run_data, identity in zip(runs, identities):
        run_data.update({
            "schemaVersion": 1,
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
            },
            "hostBefore": conditions,
            "hostAfter": after,
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
