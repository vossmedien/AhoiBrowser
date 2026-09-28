#!/bin/bash
# usage: quick-window-adoption-journey.sh <App.app> <outdir>
# QUICK-03/04 on the installed candidate: ⌥Space opens the Quick Window,
# a page loaded there is adopted into the normal window through the command
# bar ("In normales Fenster übernehmen") as the same WebContents (same
# DevTools target, page state kept, no reload, no clone); the command is not
# offered again once adopted (Crest 146 #6); closing a second Quick Window
# leaves the normal session intact. HID keys, PID-scoped AX, CDP reads.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9391
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-quick-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8811}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
printf '<title>home</title>home page' > $P-site/home.html
printf '<title>quick</title><script>window.loads=(+sessionStorage.loads||0)+1;sessionStorage.loads=window.loads</script>quick page' > $P-site/quick.html
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT; SITE=http://127.0.0.1:$SITE_PORT
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
pages() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(json.dumps(sorted([t["url"] for t in json.load(sys.stdin) if t["type"]=="page"])))'; }
target_of() { # <url substring> ; DevTools target ids of matching pages
  curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(" ".join(t["id"] for t in json.load(sys.stdin) if t["type"]=="page" and sys.argv[1] in t["url"]))' "$1"
}
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
  CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True}))' "$2")" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'
}
open_url() { # <url> ; ⌘T + type + Return in the normal window
  local opened=0
  for attempt in 1 2 3; do
    $AX activate $PID >> "$OUT/steps.txt"; sleep 1; key 17 cmd
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 && { opened=1; break; }
  done
  [ $opened = 1 ] || fail_setup "command bar did not open for $1"
  sleep 1; $AX type $PID "$1" >> "$OUT/steps.txt"; sleep 1; key 36
  waiturl "$1" 20 || fail_setup "did not load $1"; sleep 2
}
ADOPT="In normales Fenster übernehmen"
# Result rows naming the command, not the typed query in the input field.
offered() { $AX dump $PID 14 | grep -v 'AXTextField\|AXComboBox\|AXSearchField' | grep -c "$ADOPT"; }
quick_window() { # ⌥Space; the Quick Window opens on about:blank with its command bar
  local before; before=$(target_of about:blank | wc -w)
  key 49 opt
  local end=$(( $(date +%s) + 10 ))
  while [ $(date +%s) -lt $end ]; do
    [ "$(target_of about:blank | wc -w)" -gt "$before" ] && return 0; sleep 1
  done
  return 1
}

launch
open_url "$SITE/home.html"
BASE=$(pages); echo "base $BASE" >> "$OUT/pages.txt"
# QUICK-03: open the Quick Window and load a page in it.
quick_window && record quickWindowOpened true || { record quickWindowOpened false; fail_setup "quick window did not open"; }
$AX dump $PID 14 > "$OUT/ax-quick-window.txt"
waitax "AXWindow \\| Suchen oder URL eingeben" 6 || key 37 cmd
waitax "AXWindow \\| Suchen oder URL eingeben" 6 || fail_setup "quick window command bar did not open"
sleep 1; $AX type $PID "$SITE/quick.html" >> "$OUT/steps.txt"; sleep 1; key 36
# A Return lost before the bar has key focus leaves the URL typed but
# unsubmitted (build-45 rerun 3); submit once more before giving up.
waiturl quick.html 5 || { echo "info: Return repeated" >> "$OUT/steps.txt"; key 36; }
waiturl quick.html 20 || fail_setup "quick window did not load quick.html"; sleep 2
BEFORE_ID=$(target_of quick.html); eval_in quick.html 'window.adoptMark=42' >/dev/null
echo "quick target before $BEFORE_ID" >> "$OUT/pages.txt"
$AX dump $PID 14 | grep -q 'quick — ' && record notInTreeBeforeAdoption false || record notInTreeBeforeAdoption true
# Adopt through the Quick Window's command bar. The Quick Window is a
# trusted popup without a location bar, so Chromium disables ⌘L
# (IDC_FOCUS_LOCATION) there; ⌘T (IDC_NEW_TAB) stays enabled and opens the
# same command bar. ⌘L is tried first and only logged (build 45: no bar).
key 37 cmd
if waitax "AXWindow \\| Suchen oder URL eingeben" 4; then
  echo "info: cmd-L opens the quick window command bar" >> "$OUT/steps.txt"
else
  echo "info: cmd-L does not open the quick window command bar" >> "$OUT/steps.txt"
  key 17 cmd
fi
waitax "AXWindow \\| Suchen oder URL eingeben" 6 || fail_setup "command bar did not open in the quick window"
sleep 1; $AX type $PID "$ADOPT" >> "$OUT/steps.txt"
sleep 2; $AX dump $PID 14 > "$OUT/ax-adopt-offered.txt"
[ "$(offered)" -gt 0 ] && record adoptOffered true || record adoptOffered false
key 36; sleep 4
AFTER=$(pages); AFTER_ID=$(target_of quick.html); echo "after $AFTER / $AFTER_ID" >> "$OUT/pages.txt"
[ -n "$BEFORE_ID" ] && [ "$AFTER_ID" = "$BEFORE_ID" ] && record sameWebContents true || record sameWebContents false
[ "$(eval_in quick.html 'window.adoptMark')" = 42 ] && [ "$(eval_in quick.html 'window.loads')" = 1 ] \
  && record pageStateKeptNoReload true || record pageStateKeptNoReload false
# The Quick Window is gone: no page other than home and the adopted one.
[ "$AFTER" = "$(python3 -c 'import json,sys;print(json.dumps(sorted([sys.argv[1]+"/home.html",sys.argv[1]+"/quick.html"])))' "$SITE")" ] \
  && record quickWindowClosed true || record quickWindowClosed false
waitax 'quick — ' 6 && record adoptedIntoSidebar true || record adoptedIntoSidebar false
$AX dump $PID 14 > "$OUT/ax-after-adopt.txt"
# Crest 146 #6: a second adoption of the same page is not offered.
key 17 cmd
if waitax "AXWindow \\| Suchen oder URL eingeben" 6; then
  sleep 1; $AX type $PID "$ADOPT" >> "$OUT/steps.txt"; sleep 2
  $AX dump $PID 14 > "$OUT/ax-adopt-again.txt"
  [ "$(offered)" = 0 ] && record adoptNotOfferedAgain true || record adoptNotOfferedAgain false
  key 53; sleep 1
else
  record adoptNotOfferedAgain false
fi
[ "$(target_of quick.html | wc -w | tr -d ' ')" = 1 ] && record singleAdoptedPage true || record singleAdoptedPage false
# QUICK-04: a second Quick Window closes without touching the session.
quick_window || fail_setup "second quick window did not open"
key 53; sleep 1; key 13 cmd; sleep 3
[ "$(pages)" = "$AFTER" ] && kill -0 $PID 2>/dev/null && record closeKeepsSession true || record closeKeepsSession false
$AX dump $PID 14 > "$OUT/ax-final.txt"
finish; quit
