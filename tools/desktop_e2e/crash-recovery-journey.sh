#!/bin/bash
# usage: crash-recovery-journey.sh <App.app> <outdir>
# CRASH-01/02/03/04/07/08 on the installed candidate with temporary tabs:
# a crashed renderer leaves the other tab working and its own tab reloads;
# a killed GPU process is replaced while the pages keep working; after a hard
# browser kill with a normal and an incognito window, Chromium's restore
# prompt brings back each normal page exactly once and never the incognito
# window or page. Saved-tab crash recovery needs a sidebar drag and is not
# covered here.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9403
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-crash-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8821}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
for n in alpha beta; do printf '<title>%s</title>%s page' $n $n > $P-site/$n.html; done
printf '<title>incpage</title>incognito page' > $P-site/incpage.html
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT; SITE=http://127.0.0.1:$SITE_PORT
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
pages() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(json.dumps(sorted([t["url"] for t in json.load(sys.stdin) if t["type"]=="page"])))'; }
count_of() { pages | python3 -c 'import json,sys;print(sum(sys.argv[1] in u for u in json.load(sys.stdin)))' "$1"; }
launch() {
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT about:blank >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$P" >> "$OUT/run.txt"
  for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  sleep 4; $AX activate $PID >> "$OUT/steps.txt"
}
quit() { key 12 cmd; for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || return 0; sleep 1; done; echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3; }
# Keys go through the HID event tap like a real keyboard; hidkey refuses
# unless the app is frontmost, so bring it forward and retry.
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
waiturl() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do pages | grep -q "$1" && return 0; sleep 1; done; return 1; }
RESULTS=(); record() { RESULTS+=("\"$1\": $2"); echo "$1 -> $2" >> "$OUT/steps.txt"; }
finish() {
  local joined; joined=$(IFS=,; echo "${RESULTS[*]}")
  local sep=""; [ -n "$joined" ] && sep=", "
  echo "{${joined}${1:+$sep\"setupFailed\": \"$1\"}}" | python3 -c 'import json,sys;d=json.load(sys.stdin);d["pass"]=("setupFailed" not in d) and all(v is True for k,v in d.items() if k!="setupFailed");print(json.dumps(d,indent=1))' > "$OUT/verdict.json"
  cat "$OUT/verdict.json"
}
fail_setup() { $AX dump $PID 14 > "$OUT/ax-setup-failure.txt"; finish "$1"; quit; exit 4; }
eval_in() { # <url substring> <expression>
  CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True}))' "$2")" 2>/dev/null | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))' 2>/dev/null
}
open_url() { # <url> ; ⌘T + type + Return
  local opened=0
  for attempt in 1 2 3; do
    $AX activate $PID >> "$OUT/steps.txt"; sleep 1; key 17 cmd
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 && { opened=1; break; }
  done
  [ $opened = 1 ] || fail_setup "command bar did not open for $1"
  sleep 1; type_in "$1"; sleep 1; key 36
  waiturl "$1" 5 || { echo "info: Return repeated" >> "$OUT/steps.txt"; key 36; }
  waiturl "$1" 20 || fail_setup "did not load $1"; sleep 2
}
press_label() { # <regex of an AXButton title> ; presses the first match
  local title; title=$($AX dump $PID 14 | grep -o -E "AXButton \| ($1)[^|]*" | head -1 | sed -e 's/^AXButton | //' -e 's/ *$//')
  [ -n "$title" ] && $AX press $PID "$title" >> "$OUT/steps.txt"
}
gpu_pid() { ps -axww -o pid=,command= | grep -F -- "--user-data-dir=$P" | grep -F -- "--type=gpu-process" | grep -v grep | awk '{print $1}' | head -1; }

launch
open_url "$SITE/alpha.html"; open_url "$SITE/beta.html"
# CRASH-01: crash alpha's renderer; beta keeps working, alpha reloads.
eval_in alpha.html 'window.mark=1' >/dev/null
CDP alpha.html Page.crash > "$OUT/page-crash.txt" 2>&1; sleep 3
[ "$(eval_in beta.html '6*7')" = 42 ] && record otherTabIntact true || record otherTabIntact false
[ "$(eval_in alpha.html '6*7')" != 42 ] && record crashedRendererGone true || record crashedRendererGone false
kill -0 $PID 2>/dev/null && record browserSurvivesRendererCrash true || record browserSurvivesRendererCrash false
$AX dump $PID 14 > "$OUT/ax-sad-tab.txt"
CDP alpha.html Page.reload '{}' > /dev/null 2>&1; sleep 3
[ "$(eval_in alpha.html '6*7')" = 42 ] && [ "$(eval_in alpha.html 'String(window.mark)')" = undefined ] \
  && record crashedTabReloads true || record crashedTabReloads false
# CRASH-02: kill the GPU process; a new one starts and pages keep working.
G1=$(gpu_pid); echo "gpu before: $G1" >> "$OUT/steps.txt"
if [ -n "$G1" ]; then
  kill -9 "$G1"; sleep 6; G2=$(gpu_pid); echo "gpu after: $G2" >> "$OUT/steps.txt"
  [ -n "$G2" ] && [ "$G2" != "$G1" ] && record gpuProcessReplaced true || record gpuProcessReplaced false
  kill -0 $PID 2>/dev/null && [ "$(eval_in beta.html '6*7')" = 42 ] && $AX dump $PID 14 | grep -q 'AXWindow' \
    && record uiSurvivesGpuCrash true || record uiSurvivesGpuCrash false
else
  record gpuProcessReplaced false
fi
# CRASH-03/04/07/08: hard kill with a normal and an incognito window open.
key 45 cmd shift; sleep 3; open_url "$SITE/incpage.html"
BEFORE=$(pages); echo "before kill $BEFORE" >> "$OUT/pages.txt"
sleep 3; kill -9 $PID; sleep 3
launch; sleep 4
$AX dump $PID 14 > "$OUT/ax-after-relaunch.txt"
grep -q 'AXWindow | .*\(incpage\|Inkognito\)' "$OUT/ax-after-relaunch.txt" && record noIncognitoWindowBack false || record noIncognitoWindowBack true
# The restore button text carries soft hyphens.
RESTORE=$(grep -o 'AXButton | Wieder[^|]*' "$OUT/ax-after-relaunch.txt" | head -1 | sed -e 's/^AXButton | //' -e 's/ *$//')
if [ -n "$RESTORE" ]; then
  record restoreOffered true
  $AX press $PID "$RESTORE" >> "$OUT/steps.txt"
  waiturl alpha.html 15 && waiturl beta.html 10 && record normalSessionRestored true || record normalSessionRestored false
else
  record restoreOffered false
fi
sleep 3; AFTER=$(pages); echo "after restore $AFTER" >> "$OUT/pages.txt"
[ "$(count_of alpha.html)" = 1 ] && [ "$(count_of beta.html)" = 1 ] && record noDuplicatesAfterRestore true || record noDuplicatesAfterRestore false
[ "$(count_of incpage)" = 0 ] && record incognitoNotRestored true || record incognitoNotRestored false
$AX dump $PID 14 > "$OUT/ax-final.txt"
finish; quit
