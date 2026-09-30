#!/bin/bash
# usage: ws-isolated-perf-journey.sh <App.app> <outdir> [counts, default "1 3 5"]
# WS-ISO-11 (ADR 0011): memory and startup cost of 1, 3 and 5 loaded fully
# separated Workspaces (one Chromium Profile each), and whether separated
# Profiles load only on use and unload once no window of theirs is open.
# Per count N, on a fresh user data directory:
#   base      main window only (RSS/process count of the browser tree)
#   loaded    N separated Workspaces created, each with its own window
#   hidden    after handing the last one over to Inbox (its window hides,
#             renderers stay; handoff 016 H4)
#   closed    every separated window closed; `loadedProfiles` must be 0
#   restart   quit and relaunch: startup to DevTools and to the main
#             window, `loadedProfiles` must stay 0 (not opened → not loaded)
# A Profile counts as loaded while the browser holds its History database
# open (lsof). RSS double-counts shared pages; compare like for like only
# (docs/PERFORMANCE_METHODOLOGY.md). Settle 20 s before every sample. One
# run per count and no paired control: a cost record, not a budget verdict;
# adopting it into the methodology belongs to the crest-hardening lane.
# Drives the UI through AX like the desktop journeys: run it only on an idle
# host while holding the desktop e2e.lock.
set -u
APP=$1; OUT=$2; COUNTS=${3:-"1 3 5"}
S=$(cd "$(dirname "$0")" && pwd)
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9348; SETTLE=${AHOI_PERF_SETTLE:-20}
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; SAMPLES="$OUT/samples.jsonl"; : > "$SAMPLES"
now_ms() { python3 -c 'import time;print(int(time.time()*1000))'; }
launch() {
  local t0; t0=$(now_ms)
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT about:blank >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$P" >> "$OUT/run.txt"
  DEVTOOLS_MS=""; WINDOW_MS=""
  for i in $(seq 1 240); do
    if [ -z "$DEVTOOLS_MS" ] && curl -s http://127.0.0.1:$PORT/json/version >/dev/null; then
      DEVTOOLS_MS=$(( $(now_ms) - t0 ))
    fi
    if $AX dump $PID 14 2>/dev/null | grep -q 'Workspace wechseln'; then
      WINDOW_MS=$(( $(now_ms) - t0 )); break
    fi
    sleep 0.25
  done
  sleep 3; $AX activate $PID >> "$OUT/steps.txt"
}
key() {
  for attempt in 1 2 3 4 5; do
    $AX activate $PID >/dev/null; sleep 0.3
    $AX hidkey $PID "$@" >> "$OUT/steps.txt" && return 0
    sleep 1
  done
  return 1
}
quit() { key 12 cmd; for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || return 0; sleep 1; done; kill $PID; sleep 3; }
waitax() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do $AX dump $PID 14 | grep -q -E "$1" && return 0; sleep 1; done; return 1; }
menu() { # <active workspace name> <menu item regex>
  $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
  for attempt in 1 2 3 4; do
    $AX press $PID "$1, Workspace wechseln" AXShowMenu >> "$OUT/steps.txt"
    waitax "AXMenuItem \\| $2" 4 && return 0
    $AX key $PID 53 >> "$OUT/steps.txt"; sleep 2
  done
  return 1
}
loaded_profiles() {
  lsof -p "$PID" 2>/dev/null | grep -E "/Profile [0-9]+/History$" | awk '{print $NF}' | sort -u | wc -l | tr -d ' '
}
sample() { # <count> <phase>
  sleep "$SETTLE"
  python3 - "$PID" "$1" "$2" "$(loaded_profiles)" "${DEVTOOLS_MS:-}" "${WINDOW_MS:-}" >> "$SAMPLES" <<'PY'
import json, subprocess, sys
root, count, phase, loaded, devtools, window = sys.argv[1:]
table = {}
for line in subprocess.run(["ps", "-axo", "pid=,ppid=,rss="], capture_output=True,
                           text=True).stdout.splitlines():
    pid, ppid, rss = (int(v) for v in line.split())
    table[pid] = (ppid, rss)
members, frontier = [], [int(root)]
while frontier:
    pid = frontier.pop()
    if pid in table:
        members.append(pid)
        frontier.extend(c for c, (pp, _) in table.items() if pp == pid)
row = {"separated": int(count), "phase": phase, "loadedProfiles": int(loaded),
       "processes": len(members), "rssKiB": sum(table[p][1] for p in members)}
if phase == "restart":
    row.update(devtoolsReadyMs=int(devtools or -1), mainWindowMs=int(window or -1))
print(json.dumps(row))
PY
  tail -1 "$SAMPLES" >> "$OUT/steps.txt"
}

for N in $COUNTS; do
  P=$(mktemp -d /private/tmp/ahoi-isoperf-profile.XXXXXX)
  launch; sample "$N" base
  ACTIVE=Inbox
  for i in $(seq 1 "$N"); do
    menu "$ACTIVE" "Neuer Workspace…" || { echo "N=$N: menu failed at $i" >> "$OUT/steps.txt"; break; }
    $AX press $PID "Neuer Workspace…" >> "$OUT/steps.txt"
    waitax "AXTextField \\| Workspace-Name" 8 || break
    $AX setvalue $PID "Workspace-Name" "Getrennt $i" >> "$OUT/steps.txt"; sleep 1
    $AX press $PID "AXRadioButton:Vollständig getrennt" >> "$OUT/steps.txt"; sleep 1
    $AX press $PID "Erstellen" >> "$OUT/steps.txt"
    waitax "Getrennt $i, Workspace wechseln" 30 || { echo "N=$N: creation $i failed" >> "$OUT/steps.txt"; break; }
    ACTIVE="Getrennt $i"
  done
  sample "$N" loaded
  if menu "$ACTIVE" "Inbox"; then
    $AX press $PID "Inbox" >> "$OUT/steps.txt"; waitax "Inbox, Workspace wechseln" 15
    sample "$N" hidden
    menu Inbox "$ACTIVE" && $AX press $PID "$ACTIVE – Vollständig getrennt" >> "$OUT/steps.txt"
    waitax "$ACTIVE, Workspace wechseln" 15
  fi
  # Close every separated window (⌘⇧W on the frontmost one); the main
  # window comes back when the last presented one closes.
  for i in $(seq "$N" -1 1); do
    waitax "Getrennt $i, Workspace wechseln" 10 || continue
    key 13 cmd shift; sleep 2
  done
  sample "$N" closed
  quit; launch; sample "$N" restart; quit
  echo "profile for N=$N: $P" >> "$OUT/run.txt"
done
python3 - "$SAMPLES" > "$OUT/summary.json" <<'PY'
import json, sys
rows = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
out = {}
for r in rows:
    out.setdefault(str(r["separated"]), {})[r["phase"]] = r
for n, phases in out.items():
    base = phases.get("base", {}).get("rssKiB")
    for phase in ("loaded", "hidden", "closed", "restart"):
        if base and phase in phases:
            phases[phase]["rssDeltaKiB"] = phases[phase]["rssKiB"] - base
    phases["unloadedWithoutWindow"] = phases.get("closed", {}).get("loadedProfiles") == 0
    phases["notLoadedAtStartup"] = phases.get("restart", {}).get("loadedProfiles") == 0
print(json.dumps(out, indent=1))
PY
cat "$OUT/summary.json"
