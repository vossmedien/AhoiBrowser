#!/usr/bin/env python3
"""Fresh-profile network audit of an installed AhoiBrowser candidate (H5, NET-GCM-01).

Launches the bundle once with a disposable profile and Chromium's own NetLog
(`--log-net-log`, no root needed), polls the process tree's sockets with
`lsof`, stays idle, opens one local page, quits, and then checks the profile's
GCM store. Every contacted host is classified against the endpoint allowlist
(`overlay/.../privacy/config/endpoint_allowlist_v1.json`) and
`docs/NETWORK_SILENCE_CHECKLIST.md`:

  allowed      listed background endpoint
  conditional  allowed only with a user-caused reason (e.g. extension updates)
  denied       named in the checklist as traffic that must not happen
  unknown      anything else; default-deny, so it fails the audit

Phases (--phases, default "idle"):
  idle        NET-GCM-01, PRIV-11/13: 10 min without navigation, one local page
  navigation  PRIV-12: a normal navigation to a loopback page and one public
              HTTPS page (--public-url); the complete endpoint list is
              classified against the same allowlist
  crash       PRIV-16: renderer crash via chrome://crash (and with
              --crash-browser the browser via chrome://inducebrowsercrashforrealz);
              no crash or telemetry upload may follow: no /cr/report request in
              the NetLog, no remote socket of the crash handler, Crashpad
              uploads disabled and no report left pending

Only reads the bundle; never installs or modifies it. Stops only processes it
started itself. The crash phase leaves its local crash report in the default
Crashpad database (~/Library/Application Support/<product>/Crashpad), which
Chromium always uses regardless of --user-data-dir.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import pathlib
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.parse
from typing import Optional

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/perf"))

import cdp  # noqa: E402
import run_desktop_perf as perf  # noqa: E402

ALLOWLIST = ROOT / "overlay/chromium/src/ahoi/browser/privacy/config/endpoint_allowlist_v1.json"
LOCAL_HOSTS = {"127.0.0.1", "localhost", "::1", "[::1]"}
# docs/NETWORK_SILENCE_CHECKLIST.md
DENIED = {
    "android.clients.google.com": "N1 GCM check-in / register3",
    "mtalk.google.com": "N1 GCM MCS",
    "mtalk4.google.com": "N1 GCM MCS",
    "alt1-mtalk.google.com": "N1 GCM MCS",
    "translate.googleapis.com": "N3 translate",
    "optimizationguide-pa.googleapis.com": "N4 optimization guide",
    "accounts.google.com": "Gaia",
    "www.google.com": "N5 NTP promos / One Google Bar",
    "ogs.google.com": "N5 One Google Bar",
    "clients4.google.com": "UMA",
    "content-autofill.googleapis.com": "autofill server",
    "clientservices.googleapis.com": "variations seed",
    "fonts.googleapis.com": "N8 reader fonts",
}
CONDITIONAL = {
    "clients2.google.com": "N11 extension updates only with installed extensions; "
                           "/time and /cr/report are denied (N10)",
}
GCM_CHECKIN_MARKER = b"gservice1-android_id"
# Hosts the navigation phase deliberately visits; they are PRIV-12's
# foreground endpoints, not background traffic.
DEFAULT_PUBLIC_URL = "https://example.com/"
CRASH_UPLOAD_MARKERS = ("/cr/report", "clients2.google.com/cr/",
                        "crash.googleapis.com", "clients2.google.com/cr")
CRASHPAD_SETTINGS_MAGIC = b"sdPC"  # 'CPds' little-endian
CRASHPAD_UPLOADS_ENABLED = 1 << 0

HOST_PATTERNS = (
    re.compile(r'"(?:url|original_url|new_location)"\s*:\s*"(?:https?|wss?)://([^/":?#]+)'),
    re.compile(r'"(?:host|hostname)"\s*:\s*"(?:(?:https?|wss?)://)?([A-Za-z0-9.\-\[\]]+?)(?::\d+)?/?"'),
    re.compile(r'"host_port"\s*:\s*"([A-Za-z0-9.\-]+):\d+"'),
    re.compile(r'"(?:group_name|group_id)"\s*:\s*"(?:ssl/|pm/)?(?:https?://)?([A-Za-z0-9.\-]+):\d+'),
)


def netlog_hosts(text: str) -> dict[str, int]:
    """Host names mentioned by requests, resolutions and connect jobs."""
    hosts: dict[str, int] = {}
    for pattern in HOST_PATTERNS:
        for match in pattern.finditer(text):
            host = match.group(1).strip("[]").lower().rstrip(".")
            if host:
                hosts[host] = hosts.get(host, 0) + 1
    return hosts


def load_allowlist(path: pathlib.Path = ALLOWLIST) -> dict[str, str]:
    data = json.loads(path.read_text())
    return {entry["host"].lower(): entry["id"] for entry in data["allowed_background_endpoints"]}


def classify(hosts: dict[str, int], allowlist: dict[str, str],
             navigated: frozenset[str] = frozenset()) -> dict[str, list[dict]]:
    result = {"allowed": [], "conditional": [], "denied": [], "unknown": [],
              "navigated": []}
    for host, count in sorted(hosts.items()):
        if host in LOCAL_HOSTS or host.endswith(".localhost"):
            continue
        if host in navigated:
            result["navigated"].append({"host": host, "count": count})
        elif host in allowlist:
            result["allowed"].append({"host": host, "count": count, "rule": allowlist[host]})
        elif host in DENIED:
            result["denied"].append({"host": host, "count": count, "reason": DENIED[host]})
        elif host in CONDITIONAL:
            result["conditional"].append({"host": host, "count": count,
                                          "reason": CONDITIONAL[host]})
        else:
            result["unknown"].append({"host": host, "count": count})
    return result


def gcm_checkin_present(profile: pathlib.Path) -> Optional[bool]:
    store = profile / "Default" / "GCM Store"
    if not store.is_dir():
        return None
    for path in store.rglob("*"):
        if path.is_file() and GCM_CHECKIN_MARKER in path.read_bytes():
            return True
    return False


def sockets(pids: list[int]) -> set[str]:
    if not pids:
        return set()
    out = subprocess.run(["lsof", "-nP", "-a", "-i", "-p", ",".join(map(str, pids))],
                         capture_output=True, text=True).stdout
    remotes = set()
    for line in out.splitlines()[1:]:
        if "->" in line:
            remote = line.split("->", 1)[1].split()[0]
            if not remote.startswith(("127.0.0.1", "[::1]", "localhost")):
                remotes.add(remote)
    return remotes


def verdict(classified: dict, gcm_store: Optional[bool]) -> dict:
    gcm_hosts = [d for d in classified["denied"] if d["reason"].startswith("N1")]
    return {
        "NET-GCM-01": "PASS" if not gcm_hosts and gcm_store is not True else "FAIL",
        "fresh-profile-silence": "PASS" if not classified["denied"] and not classified["unknown"]
        else "FAIL",
    }


def crashpad_uploads_enabled(settings: bytes) -> Optional[bool]:
    """Crashpad settings.dat: magic, version, options (bit 0 uploads)."""
    if len(settings) < 12 or settings[:4] != CRASHPAD_SETTINGS_MAGIC:
        return None
    options = int.from_bytes(settings[8:12], "little")
    return bool(options & CRASHPAD_UPLOADS_ENABLED)


def crash_upload_requested(netlog_text: str) -> list[str]:
    return [marker for marker in CRASH_UPLOAD_MARKERS if marker in netlog_text]


def crashpad_snapshot(database: pathlib.Path) -> dict:
    """File names per Crashpad report directory (read-only)."""
    snapshot = {}
    for sub_dir in ("new", "pending", "completed"):
        directory = database / sub_dir
        snapshot[sub_dir] = sorted(p.name for p in directory.iterdir()) if directory.is_dir() else []
    settings = database / "settings.dat"
    snapshot["uploads_enabled"] = (crashpad_uploads_enabled(settings.read_bytes())
                                   if settings.is_file() else None)
    return snapshot


def crash_verdict(before: dict, after: dict, upload_markers: list[str],
                  handler_remotes: set[str], crashed: bool) -> dict:
    new_reports = {k: sorted(set(after.get(k, [])) - set(before.get(k, [])))
                   for k in ("new", "pending", "completed")}
    ok = (crashed and not upload_markers and not handler_remotes
          and after.get("uploads_enabled") is False and not new_reports["pending"])
    return {"PRIV-16": "PASS" if ok else "FAIL", "newReports": new_reports}


def crash_handler_pids(started_after: float) -> list[int]:
    """chrome_crashpad_handler processes started after `started_after`."""
    out = subprocess.run(["ps", "-axo", "pid=,lstart=,command="],
                         capture_output=True, text=True).stdout
    pids = []
    for line in out.splitlines():
        if "chrome_crashpad_handler" not in line or "AhoiBrowser" not in line:
            continue
        parts = line.split(None, 6)
        try:
            started = time.mktime(time.strptime(" ".join(parts[1:6]),
                                                "%a %b %d %H:%M:%S %Y"))
        except (ValueError, IndexError):
            continue
        if started >= started_after - 1:
            pids.append(int(parts[0]))
    return pids


def default_crashpad_database(identity: dict) -> pathlib.Path:
    product = pathlib.Path(identity["path"]).stem
    return pathlib.Path.home() / "Library/Application Support" / product / "Crashpad"


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--app", type=pathlib.Path, required=True)
    parser.add_argument("--idle-seconds", type=int, default=600)
    parser.add_argument("--port", type=int, default=9356)
    parser.add_argument("--lease", action="store_true",
                        help="the owner confirmed a lease for the installed app")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--phases", default="idle",
                        help="comma list of idle,navigation,crash (idle always runs)")
    parser.add_argument("--public-url", default=DEFAULT_PUBLIC_URL)
    parser.add_argument("--crash-browser", action="store_true",
                        help="crash phase: also crash the browser process")
    args = parser.parse_args(argv)
    phases = {p.strip() for p in args.phases.split(",") if p.strip()} | {"idle"}
    if not phases <= {"idle", "navigation", "crash"}:
        parser.error(f"unknown phase in {args.phases}")

    if args.app.resolve() == perf.INSTALLED_APP and not args.lease:
        print("refusing: the installed app needs a confirmed lease (--lease)", file=sys.stderr)
        return 7
    identity = perf.app_identity(args.app)
    workdir = pathlib.Path(tempfile.mkdtemp(prefix="ahoi-netaudit-"))
    profile = workdir / "profile"
    netlog = workdir / "netlog.json"
    fixtures = perf.FixtureServer()
    started = dt.datetime.now(dt.timezone.utc).isoformat()
    launch_time = time.time()
    crashpad_db = default_crashpad_database(identity)
    crashpad_before = crashpad_snapshot(crashpad_db) if "crash" in phases else None
    handler_remotes: set[str] = set()
    navigated: set[str] = set()
    crashed = False
    browser_crashed = False
    process = subprocess.Popen(
        [identity["executable"], f"--user-data-dir={profile}", "--no-first-run",
         "--no-default-browser-check", f"--remote-debugging-port={args.port}",
         f"--log-net-log={netlog}", "--net-log-capture-mode=Default", "about:blank"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
    remotes: set[str] = set()
    try:
        cdp.wait_for_endpoint(args.port, 60)
        deadline = time.monotonic() + args.idle_seconds
        while time.monotonic() < deadline:
            remotes |= sockets(perf.tree(process.pid, perf.process_table()))
            time.sleep(2)
        session = cdp.browser_session(args.port)
        session.send("Target.createTarget", {"url": fixtures.url("/page/0")})
        for _ in range(15):
            remotes |= sockets(perf.tree(process.pid, perf.process_table()))
            time.sleep(2)
        if "navigation" in phases:
            # PRIV-12: a normal navigation to a public HTTPS page.
            navigated.add(urllib.parse.urlsplit(args.public_url).hostname or "")
            session.send("Target.createTarget", {"url": args.public_url})
            for _ in range(10):
                remotes |= sockets(perf.tree(process.pid, perf.process_table()))
                time.sleep(2)
        if "crash" in phases:
            # PRIV-16: a controlled renderer crash, then watch for uploads.
            session.send("Target.createTarget", {"url": "chrome://crash"})
            crashed = True
            for _ in range(15):
                remotes |= sockets(perf.tree(process.pid, perf.process_table()))
                handler_remotes |= sockets(crash_handler_pids(launch_time))
                time.sleep(2)
            if args.crash_browser:
                try:
                    session.send("Target.createTarget",
                                 {"url": "chrome://inducebrowsercrashforrealz"})
                except cdp.CDPError:
                    pass  # the browser dies while answering
                browser_crashed = True
                for _ in range(15):
                    handler_remotes |= sockets(crash_handler_pids(launch_time))
                    time.sleep(2)
        if not browser_crashed:
            session.send("Browser.close")
        try:
            session.close()
        except (OSError, cdp.CDPError):
            if not browser_crashed:
                raise
        process.wait(timeout=60)
    except (OSError, cdp.CDPError, subprocess.TimeoutExpired) as error:
        print(f"audit run failed: {error}", file=sys.stderr)
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            pass
        return 2
    finally:
        fixtures.close()

    text = netlog.read_text(errors="replace") if netlog.exists() else ""
    hosts = netlog_hosts(text)
    classified = classify(hosts, load_allowlist(), frozenset(navigated))
    gcm_store = gcm_checkin_present(profile)
    result = {
        "schemaVersion": 1,
        "kind": "ahoi-fresh-profile-network-audit",
        "app": identity,
        "startedAt": started,
        "idleSeconds": args.idle_seconds,
        "netlogBytes": len(text),
        "hosts": classified,
        "socketRemotes": sorted(remotes),
        "gcmStoreHasCheckin": gcm_store,
        "verdicts": verdict(classified, gcm_store),
        "phases": sorted(phases),
        "method": "Chromium NetLog (Default capture) plus lsof socket polling every 2 s; "
                  "no root, so non-Chromium DNS is not observed",
    }
    if "navigation" in phases:
        result["verdicts"]["PRIV-12-endpoints"] = (
            "PASS" if not classified["denied"] and not classified["unknown"] else "FAIL")
        result["navigatedHosts"] = sorted(navigated)
    if "crash" in phases:
        crashpad_after = crashpad_snapshot(crashpad_db)
        crash = crash_verdict(crashpad_before, crashpad_after,
                              crash_upload_requested(text), handler_remotes, crashed)
        result["verdicts"]["PRIV-16"] = crash["PRIV-16"]
        result["crash"] = {
            "browserCrashed": browser_crashed,
            "crashpadDatabase": str(crashpad_db),
            "uploadsEnabled": crashpad_after.get("uploads_enabled"),
            "newReports": crash["newReports"],
            "uploadMarkersInNetlog": crash_upload_requested(text),
            "crashHandlerRemotes": sorted(handler_remotes),
        }
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "audit.json").write_text(json.dumps(result, indent=2) + "\n")
    shutil.rmtree(workdir, ignore_errors=True)
    for name, value in result["verdicts"].items():
        print(f"{name:24} {value}")
    for group in ("denied", "unknown", "conditional", "allowed", "navigated"):
        for item in classified[group]:
            print(f"  {group:11} {item['host']} x{item['count']}")
    return 0 if all(v == "PASS" for v in result["verdicts"].values()) else 1


if __name__ == "__main__":
    sys.exit(main())
