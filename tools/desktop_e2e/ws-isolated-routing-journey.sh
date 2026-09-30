#!/bin/bash
# usage: ws-isolated-routing-journey.sh <App.app> <outdir>
# PID-scoped AX + CDP journey for WS-ISO-06 (ADR 0011, crest 020/050):
# external links with a routing rule to a fully separated Workspace open in
# that Profile, without a navigation in the main Profile first; a rule in
# Quick Window mode opens the Quick Window in the separated Profile, and
# adopting it hands the page over into the separated window as the same
# WebContents; a link without a rule follows the documented default (last
# active Workspace). External links arrive as LaunchServices "open" events
# (open -a), the path Ahoi's routing handles (patch 0057); the journey
# refuses to run while another AhoiBrowser instance could receive them.
# The remembered target ("Ziel merken" in the target chooser) is covered by
# link_routing_editor_unittest.cc, not here. Product default launch,
# disposable user data directory; run only while holding the desktop
# e2e.lock (the queue runner does).
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9349
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
# Matched by the executable path (comm), never by the command line.
other_instances() { ps -axo pid=,comm= | awk -v me="${PID:-0}" '$0 ~ /\/Contents\/MacOS\/AhoiBrowser$/ && $1 != me {print $1}'; }
if [ -n "$(other_instances)" ]; then echo "another AhoiBrowser runs; external links could reach it" >&2; exit 6; fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-wsroute-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8795}; mkdir -p $P-site/iso $P-site/quick
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
printf '<title>login</title><script>document.cookie="acct=getrennt; max-age=3600; path=/"</script>logged in' > $P-site/login.html
printf '<title>routed</title>routed' > $P-site/iso/a.html
printf '<title>quickpage</title><script>window.loads=(+sessionStorage.loads||0)+1;sessionStorage.loads=window.loads</script>quick' > $P-site/quick/q.html
printf '<title>other</title>other' > $P-site/other.html
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
external() { # <url>: a LaunchServices open of the running instance
  [ -z "$(other_instances)" ] || fail_setup "another AhoiBrowser instance appeared"
  open -g -a "$APP" "$1" >> "$OUT/steps.txt" 2>&1
}
db_rows() { # <db file> <sql>, on a copy
  python3 - "$1" "$2" <<'PY'
import os, shutil, sqlite3, sys, tempfile
db, sql = sys.argv[1], sys.argv[2]
if not os.path.exists(db):
    print("<no database>"); sys.exit(0)
t = tempfile.mkdtemp()
for suffix in ("", "-wal", "-shm", "-journal"):
    if os.path.exists(db + suffix):
        shutil.copy(db + suffix, os.path.join(t, "d" + suffix))
for row in sqlite3.connect(os.path.join(t, "d")).execute(sql):
    print("|".join(str(v) for v in row))
shutil.rmtree(t)
PY
}

# Setup: a separated Workspace with a login, then routing rules in the main
# Profile's prefs (written while the browser is quit).
launch
menu Inbox "Neuer Workspace…" || fail_setup "workspace menu did not open"
$AX press $PID "Neuer Workspace…" >> "$OUT/steps.txt"
waitax "AXTextField \\| Workspace-Name" 8 || fail_setup "create dialog did not open"
$AX setvalue $PID "Workspace-Name" "Getrennt" >> "$OUT/steps.txt"; sleep 1
$AX press $PID "AXRadioButton:Vollständig getrennt" >> "$OUT/steps.txt"; sleep 1
$AX press $PID "Erstellen" >> "$OUT/steps.txt"
at Getrennt 20 || fail_setup "separated window did not open"
open_url "$SITE/login.html"
switch_to Getrennt Inbox || fail_setup "hand-over to Inbox failed"
quit
python3 - "$P" > "$OUT/rules.json" <<'PY'
import json, os, sys, uuid
root = sys.argv[1]
entries = json.load(open(os.path.join(root, "Local State")))["ahoi"]["isolated_profiles"]
target = entries[0]["workspace_id"]
def rule(path, mode):
    return {"id": str(uuid.uuid4()), "enabled": True, "host": "127.0.0.1",
            "include_subdomains": False, "path_prefix": path,
            "target_workspace_id": target, "mode": mode}
settings = {"version": 1, "enabled": True,
            "rules": [rule("/iso", "normal_tab"), rule("/quick", "quick_window")],
            "default": {"target": "last_active", "mode": "normal_tab"}}
path = os.path.join(root, "Default", "Preferences")
prefs = json.load(open(path))
prefs.setdefault("ahoi", {}).setdefault("navigation", {})["link_routing"] = settings
json.dump(prefs, open(path, "w"))
print(json.dumps(settings))
PY
[ -s "$OUT/rules.json" ] || fail_setup "routing rules could not be written"
launch
at Inbox 10 || switch_to Getrennt Inbox || fail_setup "main window not presented after relaunch"

# A rule to the separated Workspace: the page opens there, logged in.
external "$SITE/iso/a.html"
waiturl iso/a.html 20 || fail_setup "routed link did not open"
sleep 2
check ruleOpensSeparatedWindow at Getrennt 10
check ruleOpensInSeparatedProfile eval '[ "$(cookie_of iso/a.html)" = "acct=getrennt" ]'

# A Quick Window rule: the Quick Window belongs to the separated Profile,
# and adopting it hands the page into the separated window.
switch_to Getrennt Inbox || fail_setup "hand-over to Inbox failed"
external "$SITE/quick/q.html"
waiturl quick/q.html 20 || fail_setup "quick window link did not open"
sleep 2; $AX dump $PID 14 > "$OUT/ax-quick-window.txt"
check quickWindowOpened waitax "AXWindow \\| Ahoi-Schnellfenster" 5
check quickWindowInSeparatedProfile eval '[ "$(cookie_of quick/q.html)" = "acct=getrennt" ]'
BEFORE_ID=$(target_of quick/q.html)
key 37 cmd
waitax "AXWindow \\| Suchen oder URL eingeben" 4 || key 17 cmd
waitax "AXWindow \\| Suchen oder URL eingeben" 6 || fail_setup "command bar did not open in the quick window"
sleep 1; type_in "In normales Fenster übernehmen"; sleep 2
$AX dump $PID 14 > "$OUT/ax-adopt.txt"; key 36; sleep 4
check adoptionPresentsSeparatedWindow at Getrennt 10
check adoptionKeepsWebContents eval '[ -n "$BEFORE_ID" ] && [ "$(target_of quick/q.html)" = "$BEFORE_ID" ] && [ "$(eval_in quick/q.html window.loads)" = 1 ]'
check adoptedIntoSeparatedSidebar waitax 'quickpage — ' 6

# No rule: the last active Workspace (the documented default) takes it.
switch_to Getrennt Inbox || fail_setup "hand-over to Inbox failed"
external "$SITE/other.html"
waiturl other.html 20 || fail_setup "unruled link did not open"
sleep 2
check defaultRouteLastActive eval 'at Inbox 5 && [ -z "$(cookie_of other.html)" ]'
quit

# No navigation in the main Profile first (crest 020): its history has
# neither routed page, the separated Profile's has both.
ISO=$(ls -d "$P"/Profile\ * 2>/dev/null | head -1)
H_MAIN=$(db_rows "$P/Default/History" "select url from urls")
H_ISO=$(db_rows "$ISO/History" "select url from urls")
printf 'main:\n%s\nseparated:\n%s\n' "$H_MAIN" "$H_ISO" > "$OUT/history.txt"
check mainHistoryUntouched eval '! echo "$H_MAIN" | grep -q -E "/iso/a.html|/quick/q.html"'
check separatedHistoryHasRoutedPages eval 'echo "$H_ISO" | grep -q "/iso/a.html" && echo "$H_ISO" | grep -q "/quick/q.html"'
finish ""
