#!/bin/bash
# usage: ws-isolated-switch-journey.sh <App.app> <outdir>
# PID-scoped AX + CDP journey for the ADR 0011 hand-over between the main
# Profile and a fully separated Workspace (own Profile). Complements
# ws-isolated-journey.sh (creation, data separation, deletion basics).
#  WS-ISO-04: ⌃1/⌃2 and the command bar switch across Profiles; the
#    presented window takes the frame and the sidebar presentation (here
#    floating), so the page viewport keeps its size (no web reflow). The
#    trackpad swipe stays a manual CU step (axtool has no horizontal
#    phased swipe on the sidebar).
#  WS-ISO-18: audio playing in the main Workspace is marked in the
#    separated window's Workspace menu and paused from there, without
#    switching back.
#  WS-ISO-17/19: after a hand-over, quit and relaunch show exactly one
#    window, the last presented one. AHOI_E2E_CPU_LOAD=<n> runs n busy
#    loops during the relaunch (WS-ISO-19).
#  WS-ISO-07: deleting the separated Workspace with a before-unload page:
#    Abbrechen keeps Workspace, Profile and page; Verlassen closes the page
#    and, after a restart, Profile directory and registry entry are gone.
# Product default launch, disposable user data directory. Run it only while
# holding the desktop e2e.lock (the queue runner does).
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9347
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-wsswitch-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8794}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
printf '<title>frame</title>frame' > $P-site/frame.html
printf '<title>tone</title><audio id=a src="tone.wav" loop></audio>tone' > $P-site/tone.html
printf '<title>unload</title><script>addEventListener("beforeunload",e=>{e.preventDefault();e.returnValue=""})</script><button id=b style="width:400px;height:300px">tap</button>' > $P-site/unload.html
# A quiet 440 Hz tone, 2 s, looped by the page.
python3 - $P-site/tone.wav <<'PY'
import math, struct, sys, wave
w = wave.open(sys.argv[1], "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050)
w.writeframes(b"".join(struct.pack("<h", int(3000 * math.sin(2 * math.pi * 440 * i / 22050))) for i in range(44100)))
w.close()
PY
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; LOAD_PIDS=""
trap 'kill $SITE_PID $LOAD_PIDS 2>/dev/null' EXIT; SITE=http://127.0.0.1:$SITE_PORT
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
# Keys go through the HID event tap like a real keyboard (see
# ws-isolated-journey.sh); hidkey refuses unless the app is frontmost.
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
  sleep 1; type_in "$1"; key 36
  waiturl "$1" 20 || fail_setup "did not load $1"; sleep 2
}
eval_in() { # <url substring> <expression> ; with a user gesture
  CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True,"awaitPromise":True,"userGesture":True}))' "$2")" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'
}
origin_of() { eval_in "$1" "String(performance.timeOrigin)"; }
# Window frame and viewport of the window that shows the page.
geometry_of() { eval_in "$1" "[screenX,screenY,outerWidth,outerHeight].join(',')"; }
viewport_of() { eval_in "$1" "innerWidth+'x'+innerHeight"; }
window_count() { $AX dump $PID 3 | grep -c 'AXWindow'; }
iso_dir() { ls -d "$P"/Profile\ * 2>/dev/null | head -1; }
registry_count() {
  python3 -c 'import json,sys;print(len(json.load(open(sys.argv[1])).get("ahoi",{}).get("isolated_profiles",[])))' "$P/Local State" 2>/dev/null || echo "?"
}
at() { waitax "$1, Workspace wechseln" "${2:-10}"; }
switch_key() { # <digit key code> <target name>
  key "$1" ctrl; at "$2" 10
}
switch_command_bar() { # <target name>
  key 17 cmd
  waitax "AXWindow \\| Suchen oder URL eingeben" 6 || return 1
  sleep 1; type_in "$1"; sleep 2
  $AX dump $PID 14 > "$OUT/ax-command-bar-$1.txt"
  key 36; at "$1" 12
}
# The before-unload question of a group close is a native alert
# ("Website verlassen?"); see ws-level-deletion-journey.sh.
unload_prompt() { # <accept|cancel>
  local button=Abbrechen; [ "$1" = accept ] && button=Verlassen
  waitax "AXStaticText \\| Website verlassen" 8 || return 1
  $AX dump $PID 14 > "$OUT/ax-unload-prompt-$1.txt"
  $AX press $PID "$button" >> "$OUT/steps.txt"
}
activate_page() { # <url substring>; sticky activation for before-unload
  for attempt in 1 2 3; do
    $AX activate $PID >/dev/null
    CDP "$1" Page.bringToFront '{}' >/dev/null; sleep 1
    CDP "$1" Input.dispatchMouseEvent '{"type":"mousePressed","x":100,"y":100,"button":"left","clickCount":1}' >/dev/null
    CDP "$1" Input.dispatchMouseEvent '{"type":"mouseReleased","x":100,"y":100,"button":"left","clickCount":1}' >/dev/null
    [ "$(eval_in "$1" 'navigator.userActivation.hasBeenActive')" = True ] && return 0
    [ "$(eval_in "$1" 'String(navigator.userActivation.hasBeenActive)')" = true ] && return 0
    echo "unload page not activated (attempt $attempt)" >> "$OUT/steps.txt"
  done
  return 1
}
delete_separated() {
  menu Getrennt "Workspace löschen" || fail_setup "delete item missing in separated window"
  $AX press $PID "$($AX dump $PID 14 | grep -o 'Workspace löschen[^|]*' | head -1 | sed 's/ *$//')" >> "$OUT/steps.txt"
  waitax "AXButton \\| Löschen" 8 || fail_setup "delete dialog did not open"
  $AX press $PID "AXButton:Löschen" >> "$OUT/steps.txt"
}

launch
menu Inbox "Neuer Workspace…" || fail_setup "workspace menu did not open"
$AX press $PID "Neuer Workspace…" >> "$OUT/steps.txt"
waitax "AXTextField \\| Workspace-Name" 8 || fail_setup "create dialog did not open"
$AX setvalue $PID "Workspace-Name" "Getrennt" >> "$OUT/steps.txt"; sleep 1
$AX press $PID "AXRadioButton:Vollständig getrennt" >> "$OUT/steps.txt"; sleep 1
$AX press $PID "Erstellen" >> "$OUT/steps.txt"
at Getrennt 20 || fail_setup "separated window did not open"
open_url "$SITE/frame.html?iso"

# ---- WS-ISO-04: the separated window floats its sidebar; the hand-over
# carries frame and presentation to the main window.
menu Getrennt "Schwebende Sidebar" || fail_setup "no sidebar presentation item"
$AX press $PID "Schwebende Sidebar" >> "$OUT/steps.txt"; sleep 2
F_ISO=$(geometry_of 'frame.html?iso'); V_ISO=$(viewport_of 'frame.html?iso')
echo "separated frame $F_ISO viewport $V_ISO" >> "$OUT/steps.txt"
check keyboardSwitchToMain switch_key 18 Inbox
open_url "$SITE/frame.html?main"
F_MAIN=$(geometry_of 'frame.html?main'); V_MAIN=$(viewport_of 'frame.html?main')
echo "main frame $F_MAIN viewport $V_MAIN" >> "$OUT/steps.txt"
same_frame() { [ -n "$F_ISO" ] && [ "$F_ISO" = "$F_MAIN" ]; }
check frameCarriedOver same_frame
same_viewport() { [ -n "$V_ISO" ] && [ "$V_ISO" = "$V_MAIN" ]; }
check sidebarPresentationCarriedOver same_viewport
check separatedWindowHidden eval '[ "$(window_count)" = 1 ]'

# ---- WS-ISO-18: audio in the main Workspace, paused from the separated
# window's menu.
open_url "$SITE/tone.html"
eval_in tone.html "(a=>{a.volume=0.3;return a.play().then(()=>'playing')})(document.getElementById('a'))" >> "$OUT/steps.txt"
sleep 3
playing() { [ "$(eval_in tone.html "String(!document.getElementById('a').paused)")" = true ]; }
check audioPlaysInMain playing
check keyboardSwitchToSeparated switch_key 19 Getrennt
V_BACK=$(viewport_of 'frame.html?iso'); echo "separated viewport back $V_BACK" >> "$OUT/steps.txt"
check noReflowOnReturn eval '[ -n "$V_BACK" ] && [ "$V_BACK" = "$V_ISO" ]'
check audioContinuesWhileHidden playing
menu Getrennt "Inbox" || fail_setup "separated window menu has no main Workspaces"
$AX dump $PID 14 > "$OUT/ax-menu-audio.txt"
check playingWorkspaceMarked grep -q 'AXMenuItem | Inbox · spielt Audio' "$OUT/ax-menu-audio.txt"
check pauseOffered grep -q 'AXMenuItem | Wiedergabe in „Inbox“ pausieren' "$OUT/ax-menu-audio.txt"
$AX press $PID "Wiedergabe in „Inbox“ pausieren" >> "$OUT/steps.txt"; sleep 3
paused_elsewhere() { ! playing && at Getrennt 2; }
check pausedWithoutSwitchingBack paused_elsewhere
menu Getrennt "Inbox" || fail_setup "separated window menu did not reopen"
$AX dump $PID 14 > "$OUT/ax-menu-after-pause.txt"; $AX key $PID 53 >> "$OUT/steps.txt"
check markClearedAfterPause eval '! grep -q "spielt Audio" "$OUT/ax-menu-after-pause.txt"'

# ---- WS-ISO-04: the command bar switches across Profiles both ways.
check commandBarSwitchToMain switch_command_bar Inbox
check commandBarSwitchToSeparated switch_command_bar Getrennt
echo "info: trackpad swipe across Profiles is a manual CU step" >> "$OUT/steps.txt"

# ---- WS-ISO-17/19: quit while the separated window is presented.
ORIGIN=$(origin_of 'frame.html?iso')
quit
if [ "${AHOI_E2E_CPU_LOAD:-0}" -gt 0 ]; then
  for i in $(seq 1 "$AHOI_E2E_CPU_LOAD"); do yes > /dev/null & LOAD_PIDS="$LOAD_PIDS $!"; done
  echo "info: cpu load $AHOI_E2E_CPU_LOAD during relaunch" >> "$OUT/steps.txt"
fi
launch; sleep 15
$AX dump $PID 14 > "$OUT/ax-after-relaunch.txt"
[ -n "$LOAD_PIDS" ] && kill $LOAD_PIDS 2>/dev/null; LOAD_PIDS=""
echo "windows after relaunch: $(window_count)" >> "$OUT/steps.txt"
one_window() {
  [ "$(window_count)" = 1 ] && at Getrennt 2 \
    && ! grep -q 'Inbox, Workspace wechseln' "$OUT/ax-after-relaunch.txt"
}
check oneWindowAfterRelaunch one_window
at Getrennt 5 || { menu Inbox "Getrennt" && $AX press $PID "$(menuitem Getrennt)" >> "$OUT/steps.txt"; at Getrennt 15; } \
  || fail_setup "separated Workspace not reachable after relaunch"
check mainReachableAfterRelaunch eval 'menu Getrennt "Inbox"'
$AX key $PID 53 >> "$OUT/steps.txt"
O1=$(origin_of 'frame.html?iso'); sleep 5; O2=$(origin_of 'frame.html?iso')
echo "origin before quit $ORIGIN, after relaunch $O1 / $O2" >> "$OUT/steps.txt"
check noReloadLoop eval '[ -z "$O1" ] || [ "$O1" = "$O2" ]'

# ---- WS-ISO-07: delete with a before-unload page.
open_url "$SITE/unload.html"
activate_page unload.html || echo "info: unload page has no activation" >> "$OUT/steps.txt"
REG_BEFORE=$(registry_count); BEFORE=$(tabs)
delete_separated
check unloadPromptShown unload_prompt cancel
sleep 3
kept() {
  at Getrennt 4 && [ -n "$(iso_dir)" ] && [ "$(registry_count)" = "$REG_BEFORE" ] \
    && [ "$(tabs)" = "$BEFORE" ]
}
check cancelChangesNothing kept
delete_separated
unload_prompt accept || echo "info: no prompt on the confirmed deletion" >> "$OUT/steps.txt"
sleep 8; $AX dump $PID 14 > "$OUT/ax-after-delete.txt"
closed() { ! tabs | grep -q unload.html && at Inbox 4; }
check confirmClosesPages closed
quit; launch; sleep 6; quit
gone() { [ -z "$(iso_dir)" ] && [ "$(registry_count)" = 0 ]; }
check profileAndEntryGoneAfterRestart gone
finish ""
