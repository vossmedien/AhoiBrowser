#!/bin/bash
# usage: ws-convert-journey.sh <App.app> <outdir>
# PID-scoped AX + CDP journey for WS-ISO-09 (crest 052): a shared Workspace
# with an open temporary page is converted into a fully separated one. Cancel
# changes nothing; Convert opens the Workspace in its own window at the
# fully separated level, reopens the temporary page there without the
# login (site data does not move), and the main Profile keeps its other
# Workspace. Product default launch, disposable user data directory.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9387
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-wsconv-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8805}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
printf '<title>login</title><script>document.cookie="acct=getrennt; max-age=3600; path=/"</script>logged in' > $P-site/login.html
printf '<title>check</title>check' > $P-site/check.html
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT; SITE=http://127.0.0.1:$SITE_PORT
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
tabs() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(json.dumps(sorted([t["url"] for t in json.load(sys.stdin) if t["type"]=="page"])))'; }
launch() {
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT about:blank >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$P" >> "$OUT/run.txt"
  for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  sleep 5; $AX activate $PID >> "$OUT/steps.txt"
}
quit() { key 12 cmd; for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || return 0; sleep 1; done; echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3; }
# Keys go through the HID event tap like a real keyboard: keys posted to
# the process are intermittently dropped by Chromium (command-bar focus
# probes 4 and 5). hidkey refuses unless the app is frontmost, so bring it
# forward and retry instead of typing into another app.
key() {
  for attempt in 1 2 3 4 5; do
    $AX activate $PID >/dev/null; sleep 0.3
    $AX hidkey $PID "$@" >> "$OUT/steps.txt" && return 0
    sleep 1
  done
  echo "hidkey gave up: $*" >> "$OUT/steps.txt"; return 1
}
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
# Escape before AXShowMenu goes straight to the process: an HID Escape
# arrives asynchronously and would close the menu just opened by AX.
menu() { # <active workspace name> <menu item regex>
  $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
  for attempt in 1 2 3 4; do
    $AX press $PID "$1, Workspace wechseln" AXShowMenu >> "$OUT/steps.txt"
    waitax "AXMenuItem \\| $2" 4 && return 0
    $AX key $PID 53 >> "$OUT/steps.txt"; sleep 2
  done
  return 1
}
# Workspace menu items carry their level in the title ("Kunde – Eigene
# Website-Sitzungen"); resolve a Workspace name to its full item title.
menuitem() { # <workspace name>
  $AX dump $PID 14 | grep -oE "AXMenuItem \| $1( – [^|]*)? \|" | head -1 | sed -E 's/^AXMenuItem \| //; s/ \|$//'
}
open_url() { # <url>
  local opened=0
  for attempt in 1 2 3; do
    sleep 1; key 17 cmd
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 && { opened=1; break; }
  done
  [ $opened = 1 ] || fail_setup "command bar did not open for $1"
  sleep 1; $AX type $PID "$1" >> "$OUT/steps.txt"; sleep 1; key 36
  waiturl "$1" 20 || fail_setup "did not load $1"; sleep 2
}
cookie_of() { CDP "$1" Runtime.evaluate '{"expression":"document.cookie","returnByValue":true}' | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'; }
origin_of() { CDP "$1" Runtime.evaluate '{"expression":"String(performance.timeOrigin)","returnByValue":true}' | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'; }
window_count() { $AX dump $PID 3 | grep -c 'AXWindow'; }
launch
BEFORE_WINDOWS=$(window_count)
menu Inbox "Neuer Workspace…" || fail_setup "workspace menu did not open"
$AX press $PID "Neuer Workspace…" >> "$OUT/steps.txt"
waitax "AXTextField \\| Workspace-Name" 8 || fail_setup "create dialog did not open"
$AX setvalue $PID "Workspace-Name" "Wandel" >> "$OUT/steps.txt"; sleep 1
$AX press $PID "Erstellen" >> "$OUT/steps.txt"
waitax "Wandel, Workspace wechseln" 10 || fail_setup "shared workspace Wandel not active"
sleep 2; open_url "$SITE/login.html"
[ "$(cookie_of login.html)" = "acct=getrennt" ] || fail_setup "login cookie not set in Wandel"
# (a) Cancel changes nothing.
menu Wandel "In vollständig getrennten Workspace umwandeln" || fail_setup "convert item missing"
$AX dump $PID 14 > "$OUT/ax-menu-convert.txt"
$AX press $PID "$($AX dump $PID 14 | grep -o 'In vollständig getrennten Workspace umwandeln[^|]*' | head -1 | sed 's/ *$//')" >> "$OUT/steps.txt"
waitax "AXButton \\| Umwandeln" 8 && record convertDialogShown true || record convertDialogShown false
$AX dump $PID 14 > "$OUT/ax-convert-dialog.txt"
grep -q 'Websitedaten ziehen nicht mit' "$OUT/ax-convert-dialog.txt" && record dialogStatesWhatStays true || record dialogStatesWhatStays false
$AX press $PID "AXButton:Abbrechen" >> "$OUT/steps.txt"; sleep 3
waitax "Wandel, Workspace wechseln" 4 && [ "$(window_count)" = "$BEFORE_WINDOWS" ] && tabs | grep -q login.html \
  && record cancelChangesNothing true || record cancelChangesNothing false
# (b) Convert.
menu Wandel "In vollständig getrennten Workspace umwandeln" || fail_setup "convert item missing (2)"
$AX press $PID "$($AX dump $PID 14 | grep -o 'In vollständig getrennten Workspace umwandeln[^|]*' | head -1 | sed 's/ *$//')" >> "$OUT/steps.txt"
waitax "AXButton \\| Umwandeln" 8 || fail_setup "convert dialog did not open (2)"
$AX press $PID "AXButton:Umwandeln" >> "$OUT/steps.txt"; sleep 12
$AX dump $PID 14 > "$OUT/ax-after-convert.txt"
grep -q 'Wandel, Workspace wechseln' "$OUT/ax-after-convert.txt" && record convertedWindowShown true || record convertedWindowShown false
menu Wandel "Inbox" || menu Inbox "Wandel" || true
$AX dump $PID 14 > "$OUT/ax-menu-after-convert.txt"
grep -q -E 'AXMenuItem \| Wandel – [^|]*getrennt' "$OUT/ax-menu-after-convert.txt" && record levelIsSeparated true || record levelIsSeparated false
grep -q 'AXMenuItem | Inbox' "$OUT/ax-menu-after-convert.txt" && record mainKeepsInbox true || record mainKeepsInbox false
$AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
echo "tabs after convert: $(tabs)" >> "$OUT/steps.txt"
tabs | grep -q login.html && record temporaryPageReopened true || record temporaryPageReopened false
[ -z "$(cookie_of login.html)" ] && record loginDidNotMove true || record loginDidNotMove false
finish; quit
