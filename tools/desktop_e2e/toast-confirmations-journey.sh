#!/bin/bash
# usage: toast-confirmations-journey.sh <App.app> <outdir>
# Ahoi's action confirmations (user decision 6 October 2026): a ⌘-click on a
# link opens it in the background and shows "Im Hintergrund geöffnet"; ⌘D on
# the active temporary tab saves it and shows "Tab gespeichert". The toast is
# Chromium's toast widget, read through PID-scoped AX. CDP mouse input with
# the Command modifier, HID keys.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9348
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-toast-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8795}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
printf '<title>links</title><a id="l" href="target.html" style="font-size:40px">target</a>' > $P-site/links.html
printf '<title>target</title>target' > $P-site/target.html
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; SITE=http://127.0.0.1:$SITE_PORT
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
pages() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(" ".join(sorted(t["url"] for t in json.load(sys.stdin) if t["type"]=="page")))'; }
page_count() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(sum(1 for t in json.load(sys.stdin) if t["type"]=="page"))'; }
waiturl() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do pages | grep -q "$1" && return 0; sleep 1; done; return 1; }
. "$S/browser_launch.sh"
launch() {
  ahoi_launch_browser "$OUT/browser.log" --user-data-dir=$P --no-first-run \
    --no-default-browser-check --remote-debugging-port=$PORT about:blank
  echo "pid=$PID profile=$P" >> "$OUT/run.txt"
  for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  sleep 4; $AX activate $PID >> "$OUT/steps.txt"
}
quit() { key 12 cmd; for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || return 0; sleep 1; done; echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3; }
trap 'kill $SITE_PID 2>/dev/null; [ -n "${PID:-}" ] && kill -0 $PID 2>/dev/null && kill $PID' EXIT
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
RESULTS=(); record() { RESULTS+=("\"$1\": $2"); echo "$1 -> $2" >> "$OUT/steps.txt"; }
finish() {
  local joined; joined=$(IFS=,; echo "${RESULTS[*]-}")
  local sep=""; [ -n "$joined" ] && sep=", "
  echo "{${joined}${1:+$sep\"setupFailed\": \"$1\"}}" | python3 -c 'import json,sys;d=json.load(sys.stdin);d["pass"]=("setupFailed" not in d) and all(v is True for k,v in d.items() if k!="setupFailed");print(json.dumps(d,indent=1))' > "$OUT/verdict.json"
  cat "$OUT/verdict.json"
}

waitax() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do $AX dump $PID 14 | grep -q -E "$1" && return 0; sleep 0.3; done; return 1; }

launch
CDP about:blank Page.navigate "{\"url\":\"$SITE/links.html\"}" >> "$OUT/steps.txt"; echo >> "$OUT/steps.txt"
waiturl links.html 10 || { finish "links did not load"; quit; exit 4; }
sleep 2; $AX activate $PID >> "$OUT/steps.txt"
XY=$(CDP links.html Runtime.evaluate '{"expression":"(()=>{const r=document.getElementById(\"l\").getBoundingClientRect();return Math.round(r.left+r.width/2)+\" \"+Math.round(r.top+r.height/2)})()","returnByValue":true}' | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))')
x=${XY% *}; y=${XY#* }; echo "link at $x,$y" >> "$OUT/steps.txt"
# CDP modifiers: 4 = Meta (⌘), so the link opens in a background tab.
CDP links.html Input.dispatchMouseEvent "{\"type\":\"mousePressed\",\"x\":$x,\"y\":$y,\"button\":\"left\",\"clickCount\":1,\"modifiers\":4}" >/dev/null
CDP links.html Input.dispatchMouseEvent "{\"type\":\"mouseReleased\",\"x\":$x,\"y\":$y,\"button\":\"left\",\"clickCount\":1,\"modifiers\":4}" >/dev/null
if waitax "Im Hintergrund geöffnet|Opened in the background" 6; then record backgroundTabToast true; else
  $AX dump $PID 14 > "$OUT/ax-no-background-toast.txt"; record backgroundTabToast false; fi
waiturl target.html 5 && record backgroundTabOpened true || record backgroundTabOpened false
echo "pages: $(pages)" >> "$OUT/steps.txt"
# The links page stays active (background tab); ⌘D saves it (D is key 2).
sleep 5; key 2 cmd
if waitax "Tab gespeichert|Tab saved" 6; then record tabSavedToast true; else
  $AX dump $PID 14 > "$OUT/ax-no-saved-toast.txt"; record tabSavedToast false; fi
quit
finish ""
