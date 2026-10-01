#!/bin/bash
# usage: cmd-digit-probe-journey.sh <App.app> <outdir>
# Probe for the build-49 finding that ⌘1 did not select the first sidebar
# row: ⌘1…⌘8 select the N-th sidebar row of the active Workspace, ⌘9 its
# last row, Ctrl+Tab is the control that stepping works. Inbox holds alpha…
# epsilon, Workspace Zwei zeta…kappa behind them in the one tab strip, so a
# strip-order fallback would leave Zwei or land on the wrong row.
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
INBOX_PAGES="alpha beta gamma delta epsilon"
ZWEI_PAGES="zeta eta theta iota kappa"
for page in $INBOX_PAGES $ZWEI_PAGES; do
  printf '<title>%s</title>%s' $page $page > $P-site/$page.html
done
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT; SITE=http://127.0.0.1:$SITE_PORT
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
# macOS can hold ⌘digits system-wide: on this Mac "Switch to Desktop 1–4"
# (symbolic hotkeys 118–121) sit on ⌘1–⌘4. Such a key never reaches Ahoi
# (browser log: no tab activation at all), and ⌘3 even switches the Space,
# so no page is visible (build 58: empty title). The hotkey is live only
# while that Desktop exists, which is why the 29 September runs lost only ⌘1.
# axtool's keypad keys carry no keypad flag, so the digit's character holds
# them too. Every probe on a held digit is skipped and named in the verdict.
HELD_DIGITS=$(python3 - <<'PY'
import plistlib, subprocess
raw = subprocess.run(["defaults", "export", "com.apple.symbolichotkeys", "-"],
                     capture_output=True).stdout
keys = plistlib.loads(raw).get("AppleSymbolicHotKeys", {}) if raw else {}
digit = {18: "1", 19: "2", 20: "3", 21: "4", 23: "5", 22: "6", 26: "7",
         28: "8", 25: "9", 83: "1", 84: "2", 85: "3", 86: "4", 87: "5",
         88: "6", 89: "7", 91: "8", 92: "9"}
held = set()
for v in keys.values():
    p = v.get("value", {}).get("parameters", [0, 0, 0])
    if not v.get("enabled") or len(p) != 3 or p[2] != 1048576:
        continue
    if 49 <= p[0] <= 57:
        held.add(chr(p[0]))
    if p[1] in digit:
        held.add(digit[p[1]])
print(" ".join(sorted(held)))
PY
)
digit_of() { # <HID keycode> -> the digit it types
  case $1 in
    18|83) echo 1;; 19|84) echo 2;; 20|85) echo 3;; 21|86) echo 4;;
    23|87) echo 5;; 22|88) echo 6;; 26|89) echo 7;; 28|91) echo 8;;
    25|92) echo 9;;
  esac
}
held() { # <HID keycode>: is its digit a macOS hotkey here?
  case " $HELD_DIGITS " in *" $(digit_of "$1") "*) return 0;; esac
  return 1
}
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
RESULTS=(); SKIPPED=(); RAN=0
record() { RESULTS+=("\"$1\": $2"); echo "$1 -> $2" >> "$OUT/steps.txt"; }
finish() {
  local joined; joined=$(IFS=,; echo "${RESULTS[*]-}")
  local sep=""; [ -n "$joined" ] && sep=", "
  # "info…" keys carry context (held digits, skipped probes), not results.
  echo "{${joined}${1:+$sep\"setupFailed\": \"$1\"}}" | python3 -c '
import json, sys
d = json.load(sys.stdin)
d["pass"] = "setupFailed" not in d and all(
    v is True for k, v in d.items()
    if k != "setupFailed" and not k.startswith("info"))
print(json.dumps(d, indent=1))' > "$OUT/verdict.json"
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
# Title of the page that is visible now (the active tab or pane).
visible() {
  curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;[print(t["id"]) for t in json.load(sys.stdin) if t["type"]=="page"]' | while read -r id; do
    CDP "$id" Runtime.evaluate '{"expression":"document.visibilityState===\"visible\"?(document.title||location.href):\"\"","returnByValue":true}' | python3 -c 'import json,sys;v=json.load(sys.stdin).get("result",{}).get("value","");v and print(v)'
  done | head -1
}
waitvisible() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do [ "$(visible)" = "$1" ] && return 0; sleep 1; done; return 1; }
# Closes every about:blank page target: the launch tab is a real temporary
# row, so it has to go before a landing on about:blank can count as a leak.
close_blank() {
  curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;[print(t["id"]) for t in json.load(sys.stdin) if t["type"]=="page" and t["url"]=="about:blank"]' \
    | while read -r id; do curl -s "http://127.0.0.1:$PORT/json/close/$id" >> "$OUT/steps.txt"; echo >> "$OUT/steps.txt"; done
}

launch
for page in $INBOX_PAGES; do open_url "$SITE/$page.html"; done
waitvisible epsilon 5 || fail_setup "epsilon not visible"
close_blank; sleep 1
waitvisible epsilon 5 \
  || fail_setup "epsilon not visible after closing about:blank"
echo "info: macOS holds ⌘ ${HELD_DIGITS:-none}" >> "$OUT/steps.txt"
WS=Inbox
# The visible page must be <expected> and Workspace $WS still active.
check() { # <record> <expected> <description>
  local now; now=$(visible); echo "after $1 ($3): $now" >> "$OUT/steps.txt"
  [ "$now" = "$2" ] && waitax "$WS, Workspace wechseln" 3 \
    && record "$1" true || record "$1" false
}
control() { # <record> <expected>; Ctrl+Tab, the stepping control
  key 48 ctrl; waitvisible "$2" 5; check "$1" "$2" "ctrl+tab"
}
# ⌘<keycode> must show <expected>. A probe that would start on its target
# first steps away with Ctrl+Tab, so a pass always needs movement; "stay"
# expects the key to change nothing (no such row).
probe() { # <record> <expected> <keycode> [stay]
  local name=$1 want=$2 code=$3 mode=${4:-move}
  if held "$code"; then
    SKIPPED+=("$name")
    echo "info: $name skipped, macOS holds ⌘$(digit_of "$code")" \
      >> "$OUT/steps.txt"
    return 0
  fi
  [ "$mode" = move ] && [ "$(visible)" = "$want" ] && { key 48 ctrl; sleep 2; }
  local from; from=$(visible)
  key "$code" cmd
  if [ "$mode" = move ]; then waitvisible "$want" 5; else sleep 3; fi
  [ "$code" = 25 ] || [ "$code" = 92 ] || RAN=$((RAN + 1))
  check "$name" "$want" "⌘ key $code from $from"
}
control ctrlTabControl alpha
probe cmdNine epsilon 25
probe cmdOne alpha 18
probe cmdThree gamma 20
probe cmdTwo beta 19
probe cmdFive epsilon 23
probe cmdFour delta 21
# Keypad digits are further bindings of the same commands.
probe cmdKeypadOne alpha 83
probe cmdKeypadFive epsilon 87
# Second Workspace (build 49: ⌘Keypad1 did not reach its first row). Its
# rows sit at strip indices 5…9.
newws Inbox Zwei ""
for page in $ZWEI_PAGES; do open_url "$SITE/$page.html"; done
waitvisible kappa 5 || fail_setup "kappa not visible"
WS=Zwei
$AX dump $PID 14 > "$OUT/ax-zwei.txt"
grep -oE "AXRow \| [^|]*|AXRadioButton \| [^|]*Tab[^|]*" "$OUT/ax-zwei.txt" \
  >> "$OUT/steps.txt"
control zweiCtrlTabControl zeta
probe zweiCmdNine kappa 25
probe zweiCmdOne zeta 18
probe zweiCmdThree theta 20
probe zweiCmdFive kappa 23
probe zweiKeypadOne zeta 83
probe zweiKeypadFive kappa 87
# Five rows: ⌘8 has no row (strip order would pick theta, index 7).
probe zweiCmdEightStays "$(visible)" 28 stay
$AX dump $PID 14 > "$OUT/ax-final.txt"
RESULTS+=("\"infoHeldDigits\": \"${HELD_DIGITS}\"")
RESULTS+=("\"infoSkipped\": \"${SKIPPED[*]-}\"")
if [ "$RAN" = 0 ]; then
  finish "macOS holds every ⌘1–⌘8 probe key; nothing was tested"
else
  finish
fi
quit
