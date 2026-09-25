#!/bin/bash
# usage: command-bar-focus-probe.sh <App.app> <outdir> [trials]
# Probe for the intermittent "typed URL + Return does nothing" defect: per
# trial, create a Workspace, open the command bar with ⌘T, type a URL, record
# the focused element and windows before and after Return, and press Return a
# second time when the first one did not navigate. Helpers are copied from
# ws-level-deletion-journey.sh.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9347
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-cbprobe-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8794}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
printf '<title>login</title><script>document.cookie="acct=kunde; max-age=3600; path=/"</script>logged in' > $P-site/login.html
printf '<title>check</title>check' > $P-site/check.html
printf '<title>unload</title><script>addEventListener("beforeunload",e=>{e.preventDefault();e.returnValue=""})</script><button id=b style="width:400px;height:300px">tap</button>' > $P-site/unload.html
printf '<title>shared</title>shared page' > $P-site/shared.html
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT; SITE=http://127.0.0.1:$SITE_PORT
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
tabs() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(json.dumps(sorted([t["url"] for t in json.load(sys.stdin) if t["type"]=="page"])))'; }
launch() {
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT about:blank >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$P" >> "$OUT/run.txt"
  for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  sleep 4; $AX activate $PID >> "$OUT/steps.txt"
}
quit() { $AX key $PID 12 cmd >> "$OUT/steps.txt"; for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || return 0; sleep 1; done; echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3; }
waitax() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do $AX dump $PID 14 | grep -q -E "$1" && return 0; sleep 1; done; return 1; }
waiturl() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do tabs | grep -q "$1" && return 0; sleep 1; done; return 1; }
RESULTS=(); record() { RESULTS+=("\"$1\": $2"); echo "$1 -> $2" >> "$OUT/steps.txt"; }
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
    waitax "$2" 4 && return 0
    $AX key $PID 53 >> "$OUT/steps.txt"; sleep 2
  done
  return 1
}
newws() { # <active> <name> <level radio label or "">
  menu "$1" "Neuer Workspace…" || fail_setup "workspace menu did not open for $2"
  $AX press $PID "Neuer Workspace…" >> "$OUT/steps.txt"
  waitax "AXTextField \\| Workspace-Name" 8 || fail_setup "workspace dialog for $2 did not open"
  $AX dump $PID 14 > "$OUT/ax-dialog-$2.txt"
  $AX setvalue $PID "Workspace-Name" "$2" >> "$OUT/steps.txt"; sleep 1
  if [ -n "$3" ]; then $AX press $PID "AXRadioButton:$3" >> "$OUT/steps.txt"; sleep 1; fi
  $AX press $PID "Erstellen" >> "$OUT/steps.txt"
  waitax "$2, Workspace wechseln" 8 || fail_setup "workspace $2 not active"
  # The create dialog must be gone, or it keeps key focus.
  local end=$(( $(date +%s) + 6 ))
  while [ $(date +%s) -lt $end ] && $AX dump $PID 14 | grep -q "AXButton | Erstellen"; do sleep 1; done
  if $AX dump $PID 14 | grep -q "AXButton | Erstellen"; then
    $AX dump $PID 14 > "$OUT/ax-dialog-still-open-$2.txt"; record "dialogClosed_$2" false
  else
    record "dialogClosed_$2" true
  fi
}
open_url() { # <url> ; ⌘T + type + Return
  local opened=0
  for attempt in 1 2 3; do
    $AX activate $PID >> "$OUT/steps.txt"; sleep 1; $AX key $PID 17 cmd >> "$OUT/steps.txt"
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 && { opened=1; break; }
  done
  [ $opened = 1 ] || fail_setup "command bar did not open for $1"
  sleep 1; $AX type $PID "$1" >> "$OUT/steps.txt"; sleep 1; $AX key $PID 36 >> "$OUT/steps.txt"
  waiturl "$1" 20 || fail_setup "did not load $1"; sleep 2
}
switchws() { # <active> <target>
  menu "$1" "$2" || fail_setup "menu to switch to $2 did not open"
  $AX press $PID "$2" >> "$OUT/steps.txt"; waitax "$2, Workspace wechseln" 8 || fail_setup "switch to $2 failed"
}
cookie_of() { CDP "$1" Runtime.evaluate '{"expression":"document.cookie","returnByValue":true}' | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'; }
delete_active() { # <active>
  menu "$1" "Workspace löschen" || fail_setup "delete item missing for $1"
  $AX press $PID "$($AX dump $PID 14 | grep -o 'Workspace löschen[^|]*' | head -1 | sed 's/ *$//')" >> "$OUT/steps.txt"
  waitax "AXButton \\| Löschen" 8 || fail_setup "delete dialog for $1 did not open"
  $AX dump $PID 14 > "$OUT/ax-delete-$1.txt"
  $AX press $PID "AXButton:Löschen" >> "$OUT/steps.txt"
}

TRIALS=${3:-6}
launch
PREV=Inbox
for t in $(seq 1 $TRIALS); do
  newws "$PREV" "Probe$t" ""; PREV="Probe$t"
  { echo "== trial $t after Workspace creation"; $AX focused $PID; } >> "$OUT/focus.txt"
  $AX activate $PID >> "$OUT/steps.txt"; sleep 1
  { echo "== trial $t before cmd-T"; $AX focused $PID; $AX enabled $PID "Neuer Tab"; } >> "$OUT/focus.txt"
  $AX ${AHOI_PROBE_KEY:-key} $PID 17 cmd >> "$OUT/steps.txt"
  if ! waitax "AXWindow \\| Suchen oder URL eingeben" 6; then
    record "barOpened_$t" false
    { echo "== trial $t after unanswered cmd-T"; $AX focused $PID; } >> "$OUT/focus.txt"
    $AX dump $PID 14 > "$OUT/ax-no-bar-$t.txt"
    continue
  fi
  sleep 1; $AX type $PID "$SITE/check.html?t=$t" >> "$OUT/steps.txt"; sleep 1
  { echo "== trial $t before Return"; $AX focused $PID; } >> "$OUT/focus.txt"
  $AX ${AHOI_PROBE_KEY:-key} $PID 36 >> "$OUT/steps.txt"
  if waiturl "check.html?t=$t" 6; then record "firstReturnNavigated_$t" true; continue; fi
  record "firstReturnNavigated_$t" false
  { echo "== trial $t after failed Return"; $AX focused $PID; } >> "$OUT/focus.txt"
  $AX dump $PID 14 > "$OUT/ax-failed-$t.txt"
  $AX key $PID 36 >> "$OUT/steps.txt"
  waiturl "check.html?t=$t" 6 && record "secondReturnNavigated_$t" true || record "secondReturnNavigated_$t" false
  { echo "== trial $t after second Return"; $AX focused $PID; } >> "$OUT/focus.txt"
  $AX key $PID 53 >> "$OUT/steps.txt"
done
quit
finish ""
