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

Only reads the bundle; never installs or modifies it. Stops only processes it
started itself.
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


def classify(hosts: dict[str, int], allowlist: dict[str, str]) -> dict[str, list[dict]]:
    result = {"allowed": [], "conditional": [], "denied": [], "unknown": []}
    for host, count in sorted(hosts.items()):
        if host in LOCAL_HOSTS or host.endswith(".localhost"):
            continue
        if host in allowlist:
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


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--app", type=pathlib.Path, required=True)
    parser.add_argument("--idle-seconds", type=int, default=600)
    parser.add_argument("--port", type=int, default=9356)
    parser.add_argument("--lease", action="store_true",
                        help="the owner confirmed a lease for the installed app")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args(argv)

    if args.app.resolve() == perf.INSTALLED_APP and not args.lease:
        print("refusing: the installed app needs a confirmed lease (--lease)", file=sys.stderr)
        return 7
    identity = perf.app_identity(args.app)
    workdir = pathlib.Path(tempfile.mkdtemp(prefix="ahoi-netaudit-"))
    profile = workdir / "profile"
    netlog = workdir / "netlog.json"
    fixtures = perf.FixtureServer()
    started = dt.datetime.now(dt.timezone.utc).isoformat()
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
        session.send("Browser.close")
        session.close()
        process.wait(timeout=60)
    except (OSError, cdp.CDPError, subprocess.TimeoutExpired) as error:
        print(f"audit run failed: {error}", file=sys.stderr)
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        return 2
    finally:
        fixtures.close()

    text = netlog.read_text(errors="replace") if netlog.exists() else ""
    hosts = netlog_hosts(text)
    classified = classify(hosts, load_allowlist())
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
        "method": "Chromium NetLog (Default capture) plus lsof socket polling every 2 s; "
                  "no root, so non-Chromium DNS is not observed",
    }
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "audit.json").write_text(json.dumps(result, indent=2) + "\n")
    shutil.rmtree(workdir, ignore_errors=True)
    for name, value in result["verdicts"].items():
        print(f"{name:24} {value}")
    for group in ("denied", "unknown", "conditional", "allowed"):
        for item in classified[group]:
            print(f"  {group:11} {item['host']} x{item['count']}")
    return 0 if all(v == "PASS" for v in result["verdicts"].values()) else 1


if __name__ == "__main__":
    sys.exit(main())
