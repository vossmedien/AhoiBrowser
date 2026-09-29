#!/bin/bash
# usage: cmd-digit-probe-journey.sh <App.app> <outdir>
# Probe for the build-49 finding that ⌘1 did not select the first sidebar
# row: one Workspace with alpha, beta, gamma; ⌘1, ⌘2, ⌘3 and ⌘9 must each
# move to their row, and Ctrl+Tab is the control that stepping works.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9405
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-shortcut-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8823}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
for page in alpha beta gamma delta epsilon; do printf '<title>%s</title>%s' $page $page > $P-site/$page.html; done
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT; SITE=http://127.0.0.1:$SITE_PORT
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
tabs() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(json.dumps(sorted([t["url"] for t in json.load(sys.stdin) if t["type"]=="page"])))'; }
launch() {
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT --enable-logging=stderr \
    --vmodule=browser_view=1,session_bridge_session=1,keyboard_shortcut_registration=1 \
    about:blank >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$P" >> "$OUT/run.txt"
  for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  sleep 4; $AX activate $PID >> "$OUT/steps.txt"
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
# Types into the focused command bar and checks that its text field really
# holds the text; a first keystroke can arrive before the field has focus
# (build 47: a later Return then closed an empty bar).
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
finish() {
  local joined; joined=$(IFS=,; echo "${RESULTS[*]-}")
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
    $AX activate $PID >> "$OUT/steps.txt"; sleep 1; key 17 cmd
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 && { opened=1; break; }
  done
  [ $opened = 1 ] || fail_setup "command bar did not open for $1"
  sleep 1; type_in "$1"; key 36
  waiturl "$1" 20 || fail_setup "did not load $1"; sleep 2
}
switchws() { # <active> <target>
  menu "$1" "$2" || fail_setup "menu to switch to $2 did not open"
  $AX press $PID "$(menuitem "$2")" >> "$OUT/steps.txt"; waitax "$2, Workspace wechseln" 8 || fail_setup "switch to $2 failed"
}
# Title of the page that is visible now (the active tab or pane).
visible() {
  curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;[print(t["id"]) for t in json.load(sys.stdin) if t["type"]=="page"]' | while read -r id; do
    CDP "$id" Runtime.evaluate '{"expression":"document.visibilityState===\"visible\"?(document.title||location.href):\"\"","returnByValue":true}' | python3 -c 'import json,sys;v=json.load(sys.stdin).get("result",{}).get("value","");v and print(v)'
  done | head -1
}
# Every page target with its URL, to explain unexpected MRU targets.
pages() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(" ".join(t["url"] for t in json.load(sys.stdin) if t["type"]=="page"))'; }
waitvisible() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do [ "$(visible)" = "$1" ] && return 0; sleep 1; done; return 1; }
# Closes every about:blank page target: the launch tab is a real temporary
# row, so it has to go before a landing on about:blank can count as a leak.
close_blank() {
  curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;[print(t["id"]) for t in json.load(sys.stdin) if t["type"]=="page" and t["url"]=="about:blank"]' \
    | while read -r id; do curl -s "http://127.0.0.1:$PORT/json/close/$id" >> "$OUT/steps.txt"; echo >> "$OUT/steps.txt"; done
}
# One tab-stepping key inside Workspace Zwei: the expected page must become
# visible and Zwei must stay active. about:blank or an Inbox page is a leak.
step_in_zwei() { # <record name> <expected title> <hidkey args...>
  local name=$1 want=$2; shift 2
  key "$@"; waitvisible "$want" 5; local now; now=$(visible)
  echo "after $name ($*): $now" >> "$OUT/steps.txt"
  [ "$now" = "$want" ] && waitax "Zwei, Workspace wechseln" 3 && record "$name" true || record "$name" false
}
# Evaluates JS in the settings page with a shadow-DOM-piercing finder `q`.
settings_js() {
  local expr="(()=>{const q=(s,r=document)=>{const f=r.querySelector(s);if(f)return f;for(const e of r.querySelectorAll('*')){if(e.shadowRoot){const g=q(s,e.shadowRoot);if(g)return g}}return null};$1})()"
  CDP "chrome://settings" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True}))' "$expr")" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'
}
keys_of() { settings_js "const b=q('.shortcut-keys[data-command-id=\"$1\"]');return b?b.textContent.trim():'missing'"; }
record_key() { # <command id> ; click Change, then the caller presses the key
  settings_js "const b=q('.shortcut-keys[data-command-id=\"$1\"]');if(!b)return 'missing';b.click();b.focus();return 'ok'" >> "$OUT/steps.txt"; sleep 1
}

launch
open_url "$SITE/alpha.html"; open_url "$SITE/beta.html"; open_url "$SITE/gamma.html"
waitvisible gamma 5 || fail_setup "gamma not visible"
close_blank; sleep 1
waitvisible gamma 5 || fail_setup "gamma not visible after closing about:blank"
probe() { # <record> <expected> <hidkey args...>; starts elsewhere
  local name=$1 want=$2; shift 2
  key "$@"; waitvisible "$want" 5; local now; now=$(visible)
  echo "after $name ($*): $now" >> "$OUT/steps.txt"
  [ "$now" = "$want" ] && record "$name" true || record "$name" false
}
probe ctrlTabControl alpha 48 ctrl
probe cmdThree gamma 20 cmd
probe cmdOne alpha 18 cmd
probe cmdTwo beta 19 cmd
probe cmdNine gamma 25 cmd
probe cmdOneAgain alpha 18 cmd
# Keypad 1 is another binding of the same command.
probe ctrlTabBack alpha 48 ctrl
probe cmdTwoAgain beta 19 cmd
# From beta, so a pass needs movement (build 49 passed alpha→alpha).
probe cmdKeypadOne alpha 83 cmd
# Second Workspace (build 49: Cmd+keypad-1 did not reach its first row).
newws Inbox Zwei ""; open_url "$SITE/delta.html"; open_url "$SITE/epsilon.html"
waitvisible epsilon 5 || fail_setup "epsilon not visible"
$AX dump $PID 14 > "$OUT/ax-zwei.txt"
grep -o "AXRow | [^|]*" "$OUT/ax-zwei.txt" >> "$OUT/steps.txt"
probe zweiKeypadOneFromEpsilon delta 83 cmd
probe zweiCtrlTabToDelta delta 48 ctrl
probe zweiCmdTwoFromDelta epsilon 19 cmd
probe zweiCtrlTabBack delta 48 ctrl
probe zweiCmdNineFromDelta epsilon 25 cmd
$AX dump $PID 14 > "$OUT/ax-final.txt"
finish; quit
