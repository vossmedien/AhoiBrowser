#!/bin/bash
# usage: ws-isolated-recovery-journey.sh <App.app> <outdir>
# PID-scoped AX + CDP journey for the crash and reopen paths of ADR 0011
# fully separated Workspaces (crest 013):
#  WS-ISO-14: create one, close its window, restart; it is reopened from the
#    main window's Workspace menu with its login kept.
#  WS-ISO-15: the process is killed right after "Erstellen"; after a restart
#    the Workspace either exists completely (listed, opens, registry state
#    active) or not at all (no entry, no Profile directory), never half.
#  WS-ISO-08: the process is killed right after a confirmed deletion; the
#    next start resumes it: no registry entry, no Profile directory after a
#    second start, and no restored page or window of the deleted Profile.
# The kill timing is not deterministic; every check judges the end state.
# Product default launch, disposable user data directory; run only while
# holding the desktop e2e.lock (the queue runner does).
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9350
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-wsrecover-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8796}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
printf '<title>login</title><script>document.cookie="acct=getrennt; max-age=3600; path=/"</script>logged in' > $P-site/login.html
printf '<title>check</title>check' > $P-site/check.html
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT; SITE=http://127.0.0.1:$SITE_PORT
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
tabs() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(json.dumps(sorted([t["url"] for t in json.load(sys.stdin) if t["type"]=="page"])))'; }
target_of() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(" ".join(t["id"] for t in json.load(sys.stdin) if t["type"]=="page" and sys.argv[1] in t["url"]))' "$1"; }
launch() {
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT about:blank >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$P" >> "$OUT/run.txt"
  for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  sleep 5; $AX activate $PID >> "$OUT/steps.txt"
}
quit() { key 12 cmd; for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || return 0; sleep 1; done; echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3; }
key() {
  for attempt in 1 2 3 4 5; do
    $AX activate $PID >/dev/null; sleep 0.3
    $AX hidkey $PID "$@" >> "$OUT/steps.txt" && return 0
    sleep 1
  done
  echo "hidkey gave up: $*" >> "$OUT/steps.txt"; return 1
}
type_in() {
  for attempt in 1 2 3; do
    $AX type $PID "$1" >> "$OUT/steps.txt"; sleep 1
    $AX dump $PID 14 | grep "AXTextField" | grep -F -q -- "| $1" && return 0
    echo "info: typed text missing, retyping" >> "$OUT/steps.txt"
    key 0 cmd; sleep 0.5
  done
  return 1
}
waitax() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do $AX dump $PID 14 | grep -q -E "$1" && return 0; sleep 1; done; return 1; }
waiturl() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do tabs | grep -q "$1" && return 0; sleep 1; done; return 1; }
RESULTS=(); record() { RESULTS+=("\"$1\": $2"); echo "$1 -> $2" >> "$OUT/steps.txt"; }
check() { local name=$1; shift; "$@" && record "$name" true || record "$name" false; }
finish() {
  local joined; joined=$(IFS=,; echo "${RESULTS[*]}")
  local sep=""; [ -n "$joined" ] && sep=", "
  echo "{${joined}${1:+$sep\"setupFailed\": \"$1\"}}" | python3 -c 'import json,sys;d=json.load(sys.stdin);d["pass"]=("setupFailed" not in d) and all(v is True for k,v in d.items() if k!="setupFailed");print(json.dumps(d,indent=1))' > "$OUT/verdict.json"
  cat "$OUT/verdict.json"
}
fail_setup() { $AX dump $PID 14 > "$OUT/ax-setup-failure.txt"; finish "$1"; quit; exit 4; }
menu() { # <active workspace name> <menu item regex>
  $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
  for attempt in 1 2 3 4; do
    $AX press $PID "$1, Workspace wechseln" AXShowMenu >> "$OUT/steps.txt"
    waitax "AXMenuItem \\| $2" 4 && return 0
    $AX key $PID 53 >> "$OUT/steps.txt"; sleep 2
  done
  return 1
}
menuitem() { $AX dump $PID 14 | grep -oE "AXMenuItem \| $1( – [^|]*)? \|" | head -1 | sed -E 's/^AXMenuItem \| //; s/ \|$//'; }
at() { waitax "$1, Workspace wechseln" "${2:-10}"; }
switch_to() { menu "$1" "$2" && $AX press $PID "$(menuitem "$2")" >> "$OUT/steps.txt" && at "$2" 15; }
open_url() {
  local opened=0
  for attempt in 1 2 3; do
    sleep 1; key 17 cmd
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 && { opened=1; break; }
  done
  [ $opened = 1 ] || fail_setup "command bar did not open for $1"
  sleep 1; type_in "$1"; key 36
  waiturl "$1" 20 || fail_setup "did not load $1"; sleep 2
}
eval_in() {
  CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True,"awaitPromise":True}))' "$2")" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'
}
cookie_of() { eval_in "$1" "document.cookie"; }
# Registry entries and separated Profile directories on disk.
state() {
  python3 - "$P" <<'PY'
import json, os, sys
root = sys.argv[1]
try:
    entries = json.load(open(os.path.join(root, "Local State"))).get("ahoi", {}).get("isolated_profiles", [])
except (OSError, ValueError):
    entries = []
dirs = sorted(d for d in os.listdir(root) if d.startswith("Profile "))
print(json.dumps({"entries": [{k: e.get(k) for k in ("name", "state", "profile_dir")} for e in entries], "dirs": dirs}))
PY
}
entries_named() { state | python3 -c 'import json,sys;print(sum(1 for e in json.load(sys.stdin)["entries"] if e["name"]==sys.argv[1]))' "$1"; }
dir_count() { state | python3 -c 'import json,sys;print(len(json.load(sys.stdin)["dirs"]))'; }
create() { # <active> <name> [kill]
  menu "$1" "Neuer Workspace…" || fail_setup "workspace menu did not open"
  $AX press $PID "Neuer Workspace…" >> "$OUT/steps.txt"
  waitax "AXTextField \\| Workspace-Name" 8 || fail_setup "create dialog did not open"
  $AX setvalue $PID "Workspace-Name" "$2" >> "$OUT/steps.txt"; sleep 1
  $AX press $PID "AXRadioButton:Vollständig getrennt" >> "$OUT/steps.txt"; sleep 1
  $AX press $PID "Erstellen" >> "$OUT/steps.txt"
  if [ "${3:-}" = kill ]; then sleep "${AHOI_E2E_KILL_DELAY:-0.3}"; kill -9 $PID; sleep 2; return 0; fi
  at "$2" 20
}
delete_active() { # <active> [kill]
  menu "$1" "Workspace löschen" || fail_setup "delete item missing for $1"
  $AX press $PID "$($AX dump $PID 14 | grep -o 'Workspace löschen[^|]*' | head -1 | sed 's/ *$//')" >> "$OUT/steps.txt"
  waitax "AXButton \\| Löschen" 8 || fail_setup "delete dialog did not open"
  $AX press $PID "AXButton:Löschen" >> "$OUT/steps.txt"
  if [ "${2:-}" = kill ]; then sleep "${AHOI_E2E_KILL_DELAY:-0.3}"; kill -9 $PID; sleep 2; fi
}

launch
# ---- WS-ISO-14: close the separated window, restart, reopen from main.
create Inbox Getrennt || fail_setup "separated Workspace was not created"
open_url "$SITE/login.html"
key 13 cmd shift; sleep 3
check closedWindowLeavesMain eval 'at Inbox 8 && ! at Getrennt 1'
quit; launch
check notReopenedByItself eval 'at Inbox 8 && ! at Getrennt 1'
switch_to Inbox Getrennt
check reopenedFromMainMenu at Getrennt 5
open_url "$SITE/check.html?reopened"
check loginKeptAfterReopen eval '[ "$(cookie_of check.html?reopened)" = "acct=getrennt" ]'
switch_to Getrennt Inbox || fail_setup "hand-over to Inbox failed"

# ---- WS-ISO-15: killed during creation.
create Inbox Absturz kill
echo "after kill during creation: $(state)" >> "$OUT/steps.txt"
launch; sleep 5; quit; launch
echo "after restarts: $(state)" >> "$OUT/steps.txt"
# Profile directories no registry entry names: orphans of a half creation.
orphan_dirs() { state | python3 -c 'import json,sys;s=json.load(sys.stdin);k={e["profile_dir"] for e in s["entries"]};print(" ".join(d for d in s["dirs"] if d not in k))'; }
whole_or_nothing_now() {
  local n; n=$(entries_named Absturz)
  if [ "$n" = 0 ]; then
    # Nothing: no entry and no Profile directory without an entry.
    [ -z "$(orphan_dirs)" ]
  else
    # Whole: IsolatedProfileState::kActive is stored as 1.
    [ "$n" = 1 ] && state | grep -q '"name": "Absturz", "state": 1'
  fi
}
whole_or_nothing() {
  # The startup sweep deletes asynchronously and Local State is written
  # lazily; judge the settled end state, not the first snapshot.
  local end=$(( $(date +%s) + ${AHOI_E2E_SETTLE_SECONDS:-45} ))
  until whole_or_nothing_now; do
    [ $(date +%s) -lt $end ] || { echo "unsettled: $(state)" >> "$OUT/steps.txt"; return 1; }
    sleep 2
  done
  echo "settled: $(state)" >> "$OUT/steps.txt"
  [ "$(entries_named Absturz)" = 0 ] || { switch_to Inbox Absturz && switch_to Absturz Inbox; }
}
check creationCrashWholeOrNothing whole_or_nothing
at Inbox 5 || switch_to Absturz Inbox || switch_to Getrennt Inbox

# ---- WS-ISO-08: killed right after a confirmed deletion of Getrennt.
switch_to Inbox Getrennt || fail_setup "Getrennt not reachable before deletion"
GDIR=$(state | python3 -c 'import json,sys;print(next((e["profile_dir"] for e in json.load(sys.stdin)["entries"] if e["name"]=="Getrennt"),""))')
echo "Getrennt profile dir: $GDIR" >> "$OUT/steps.txt"
delete_active Getrennt kill
echo "after kill during deletion: $(state)" >> "$OUT/steps.txt"
launch; sleep 8
$AX dump $PID 14 > "$OUT/ax-after-deletion-crash.txt"
check noDeletedWindowRestored eval '! grep -q "Getrennt, Workspace wechseln" "$OUT/ax-after-deletion-crash.txt"'
check noDeletedPageRestored eval '! tabs | grep -q -E "login.html|check.html\?reopened"'
check deletedNotListed eval '! menu Inbox "Getrennt"'
$AX key $PID 53 >> "$OUT/steps.txt"
quit; launch; sleep 5; quit
echo "after resume: $(state)" >> "$OUT/steps.txt"
check deletionResumed eval '[ "$(entries_named Getrennt)" = 0 ] && [ -n "$GDIR" ] && [ ! -d "$P/$GDIR" ]'
finish ""
