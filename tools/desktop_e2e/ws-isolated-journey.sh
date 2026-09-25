#!/bin/bash
# usage: ws-isolated-journey.sh <App.app> <outdir>
# PID-scoped AX + CDP journey for ADR 0011 level `isolated` (fully separated
# Workspace = own Profile): creation opens its own window (WS-ISO-01), its
# login is invisible to the main Profile (WS-ISO-02), switching hands the
# window over without reloading pages (WS-ISO-17), it stays reachable after a
# relaunch (WS-ISO-14), and deleting it from its own window brings back the
# main window without any Chromium profile UI (WS-ISO-16). Product default
# launch, disposable user data directory.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9346
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-wsiso-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8793}; mkdir -p $P-site
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
# WS-ISO-01: creation opens the Workspace in its own window.
menu Inbox "Neuer Workspace…" || fail_setup "workspace menu did not open"
$AX press $PID "Neuer Workspace…" >> "$OUT/steps.txt"
waitax "AXTextField \\| Workspace-Name" 8 || fail_setup "create dialog did not open"
$AX setvalue $PID "Workspace-Name" "Getrennt" >> "$OUT/steps.txt"; sleep 1
$AX press $PID "AXRadioButton:Vollständig getrennt" >> "$OUT/steps.txt"; sleep 1
$AX press $PID "Erstellen" >> "$OUT/steps.txt"
waitax "Getrennt, Workspace wechseln" 20 && record ownWindowCreated true || record ownWindowCreated false
sleep 3; $AX dump $PID 14 > "$OUT/ax-after-create.txt"
{ ! grep -qi 'Profil auswählen\|Wer verwendet\|Who.s using' "$OUT/ax-after-create.txt"; } \
  && record noProfilePicker true || record noProfilePicker false
[ "$(window_count)" -gt "$BEFORE_WINDOWS" ] && record secondWindow true || record secondWindow false
# WS-ISO-02: log in inside the separated window.
open_url "$SITE/login.html"
[ "$(cookie_of login.html)" = "acct=getrennt" ] && record loginInSeparated true || record loginInSeparated false
ORIGIN=$(origin_of login.html)
# WS-ISO-17: hand over to the main Workspace; the separated window hides.
menu Getrennt "Inbox" || fail_setup "separated window menu has no main Workspaces"
$AX dump $PID 14 > "$OUT/ax-menu-separated.txt"
$AX press $PID "$(menuitem Inbox)" >> "$OUT/steps.txt"
waitax "Inbox, Workspace wechseln" 10 && record handOverToMain true || record handOverToMain false
sleep 2; $AX dump $PID 14 > "$OUT/ax-after-handover.txt"
{ ! grep -q 'Getrennt, Workspace wechseln' "$OUT/ax-after-handover.txt"; } && record separatedHidden true || record separatedHidden false
open_url "$SITE/check.html"
[ -z "$(cookie_of check.html)" ] && record mainNotLoggedIn true || record mainNotLoggedIn false
# Back to the separated Workspace: same page, no reload.
menu Inbox "Getrennt" || fail_setup "main menu has no separated Workspace"
$AX press $PID "$(menuitem Getrennt)" >> "$OUT/steps.txt"
waitax "Getrennt, Workspace wechseln" 10 && record handOverBack true || record handOverBack false
[ -n "$ORIGIN" ] && [ "$(origin_of login.html)" = "$ORIGIN" ] && record noReload true || record noReload false
quit
# WS-ISO-14: after a relaunch the separated Workspace is still reachable.
launch
if waitax "Getrennt, Workspace wechseln" 5; then record reachableAfterRelaunch true; else
  menu Inbox "Getrennt" && $AX press $PID "$(menuitem Getrennt)" >> "$OUT/steps.txt"
  waitax "Getrennt, Workspace wechseln" 15 && record reachableAfterRelaunch true || record reachableAfterRelaunch false
fi
# The login is a persistent cookie of the separated Profile; tab restore of
# that Profile is a separate question, so check the cookie on a fresh page.
open_url "$SITE/check.html?relaunch"
[ "$(cookie_of 'check.html?relaunch')" = "acct=getrennt" ] && record loginKeptAfterRelaunch true || record loginKeptAfterRelaunch false
# WS-ISO-16: delete from the separated window; the main window comes back.
menu Getrennt "Workspace löschen" || fail_setup "delete item missing in separated window"
$AX press $PID "$($AX dump $PID 14 | grep -o 'Workspace löschen[^|]*' | head -1 | sed 's/ *$//')" >> "$OUT/steps.txt"
waitax "AXButton \\| Löschen" 8 || fail_setup "delete dialog did not open"
$AX dump $PID 14 > "$OUT/ax-delete-dialog.txt"
grep -q 'vollständig getrennt' "$OUT/ax-delete-dialog.txt" && record deleteDialogNamesLevel true || record deleteDialogNamesLevel false
$AX press $PID "AXButton:Löschen" >> "$OUT/steps.txt"; sleep 8
$AX dump $PID 14 > "$OUT/ax-after-delete.txt"
{ ! grep -q 'Getrennt, Workspace wechseln' "$OUT/ax-after-delete.txt"; } && grep -q 'Inbox, Workspace wechseln' "$OUT/ax-after-delete.txt" \
  && record mainBackAfterDelete true || record mainBackAfterDelete false
{ ! grep -qi 'Profil auswählen\|Wer verwendet\|Who.s using' "$OUT/ax-after-delete.txt"; } && record noChromiumProfileUi true || record noChromiumProfileUi false
quit
launch; sleep 6; quit
python3 - "$P" > "$OUT/storage.json" <<'PY'
import json,os,sys
root=sys.argv[1]
ls=json.load(open(os.path.join(root,"Local State")))
entries=ls.get("ahoi",{}).get("isolated_profiles",[])
dirs=[d for d in os.listdir(root) if d.startswith("Profile ")]
print(json.dumps({"registry":entries,"profileDirs":dirs}))
PY
python3 -c 'import json,sys;d=json.load(open(sys.argv[1]));sys.exit(0 if not d["registry"] and not d["profileDirs"] else 1)' "$OUT/storage.json" \
  && record profileAndEntryGone true || record profileAndEntryGone false
finish ""
