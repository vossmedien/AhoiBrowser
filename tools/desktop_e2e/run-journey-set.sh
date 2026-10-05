#!/bin/bash
# usage: run-journey-set.sh <App.app> <outdir> <journey> [<journey> ...]
# Runs installed-app journeys one after another on the build/test Mac with
# the shared guards the 5 October 2026 runs showed to be necessary:
# - waits for spare CPU (two samples >= AHOI_E2E_MIN_CPU_IDLE, default 30 %)
#   and owner idle (HID idle >= AHOI_E2E_MIN_IDLE for the first journey,
#   20 s afterwards, because the previous journey's own synthetic input
#   resets the idle timer);
# - refuses to start while the console is locked (loginwindow in front or
#   CGSSessionScreenIsLocked): occluded windows are throttled and cannot be
#   activated, so such a run proves nothing;
# - holds `caffeinate -d` for the journey's lifetime so display sleep and the
#   screen lock that follows it (10 min + 300 s on MacbookPro2026) cannot
#   start mid-run; -d does not declare user activity, the HID idle timer and
#   the owner-active guard stay meaningful;
# - holds .work/agent-queue/e2e.lock per journey;
# - records the journey's own verdict: a journey passes only when its
#   verdict.json/results.json says "pass": true, never by exit code alone.
# A journey name maps to tools/desktop_e2e/<name>-journey.sh; "sandbox" maps
# to sandbox-isolation-readback.sh, "webrequest-probe" to
# webrequest-subresource-probe.sh. Summary: <outdir>/summary.json.
set -u
APP=$1; OUT=$2; shift 2
S=$(cd "$(dirname "$0")" && pwd); REPO=$(cd "$S/../.." && pwd)
LOCK=$REPO/.work/agent-queue/e2e.lock
MIN_IDLE=${AHOI_E2E_MIN_IDLE:-300}; MIN_CPU=${AHOI_E2E_MIN_CPU_IDLE:-30}
mkdir -p "$OUT"; : > "$OUT/summary.txt"
idle() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
cpuidle() { top -l 2 -s 3 -n 0 | awk '/CPU usage/ {v=$7} END {sub("%","",v); print int(v)}'; }
locked() {
  ioreg -n Root -d1 | grep -q '"CGSSessionScreenIsLocked"=Yes' && return 0
  lsappinfo info -only name "$(lsappinfo front)" 2>/dev/null | grep -q '"loginwindow"'
}
wait_ready() { # <min idle>; up to six hours
  for i in $(seq 1 480); do
    a=$(cpuidle); b=$(cpuidle); h=$(idle)
    if locked; then state=locked; else state=unlocked; fi
    echo "$(date -u +%FT%TZ) $J cpu=$a,$b hid=$h console=$state" >> "$OUT/wait.log"
    [ "$state" = unlocked ] && [ "$a" -ge "$MIN_CPU" ] && [ "$b" -ge "$MIN_CPU" ] \
      && [ "$h" -ge "$1" ] && ! pgrep -x AhoiBrowser >/dev/null && return 0
    sleep 45
  done
  return 1
}
script_for() {
  case $1 in
    sandbox) echo "$S/sandbox-isolation-readback.sh" ;;
    webrequest-probe) echo "$S/webrequest-subresource-probe.sh" ;;
    *) echo "$S/$1-journey.sh" ;;
  esac
}
verdict_of() { # <journey outdir> -> PASS | FAIL | NO_VERDICT
  python3 - "$1" <<'PY'
import json, pathlib, sys
d = pathlib.Path(sys.argv[1])
for name in ("verdict.json", "results.json"):
    f = d / name
    if f.exists():
        try:
            print("PASS" if json.loads(f.read_text()).get("pass") is True else "FAIL")
        except ValueError:
            print("FAIL")
        sys.exit(0)
print("NO_VERDICT")
PY
}
min=$MIN_IDLE
for J in "$@"; do
  SCRIPT=$(script_for "$J")
  [ -f "$SCRIPT" ] || { echo "$J UNKNOWN" >> "$OUT/summary.txt"; continue; }
  wait_ready "$min" || { echo "$J NOT_STARTED:no-capacity-or-locked" >> "$OUT/summary.txt"; continue; }
  mkdir "$LOCK" 2>/dev/null || { echo "$J NOT_STARTED:e2e-lock-held" >> "$OUT/summary.txt"; continue; }
  echo "run-journey-set $J $(date -u +%FT%TZ)" > "$LOCK/owner"
  AHOI_E2E_MIN_IDLE=$min bash "$SCRIPT" "$APP" "$OUT/$J" > "$OUT/$J.log" 2>&1 &
  JPID=$!
  caffeinate -d -w $JPID &
  wait $JPID; rc=$?
  rm -rf "$LOCK"
  if locked; then after=locked; else after=unlocked; fi
  echo "$J $(verdict_of "$OUT/$J") exit=$rc console-after=$after" >> "$OUT/summary.txt"
  min=20; sleep 5
done
python3 - "$OUT" <<'PY'
import json, pathlib, sys
o = pathlib.Path(sys.argv[1]); rows = {}
for line in (o / "summary.txt").read_text().splitlines():
    name, rest = line.split(" ", 1); rows[name] = rest
(o / "summary.json").write_text(json.dumps(
    {"journeys": rows, "pass": bool(rows) and all(v.startswith("PASS") for v in rows.values())},
    indent=1) + "\n")
print(json.dumps(rows, indent=1))
PY
