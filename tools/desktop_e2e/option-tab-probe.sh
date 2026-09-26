#!/bin/bash
# usage: option-tab-probe.sh <App.app> <outdir>
# Diagnostic for WORKFLOW-03: where does ⌥⇥ go? A fresh profile opens two
# pages that record every keydown; the probe presses ⌥⇥, ⌃⇥ and ⌥⌘K through
# the HID tap and reads (a) which page keydowns arrived and (b) whether the
# browser logged the Ahoi shortcut. A page that sees altKey+Tab means the
# browser never claimed the key; no keydown and no log means Cocoa or the
# window consumed it first. Results: <outdir>/probe.txt.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9386; SITE_PORT=${AHOI_E2E_SITE_PORT:-8806}
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-optab-profile.XXXXXX); mkdir -p "$P-site"
for page in one two; do
  printf '<title>%s</title><script>window.k=[];addEventListener("keydown",e=>k.push((e.altKey?"alt+":"")+(e.ctrlKey?"ctrl+":"")+(e.metaKey?"cmd+":"")+e.key),true)</script>%s' $page $page > "$P-site/$page.html"
done
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory "$P-site" > "$OUT/site.log" 2>&1 &
SITE_PID=$!; SITE=http://127.0.0.1:$SITE_PORT
"$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run --no-default-browser-check \
  --remote-debugging-port=$PORT --enable-logging=stderr \
  --vmodule=browser_view=1,session_bridge_session=1,native_widget_mac_nswindow=1,render_widget_host_view_cocoa=1 "$SITE/one.html" > "$OUT/browser.log" 2>&1 &
PID=$!; trap 'kill $SITE_PID 2>/dev/null; kill $PID 2>/dev/null' EXIT
for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
sleep 4
curl -s -X PUT "http://127.0.0.1:$PORT/json/new?$SITE/two.html" > /dev/null; sleep 3
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
keys_in() { CDP "$1" Runtime.evaluate '{"expression":"JSON.stringify(window.k||[])","returnByValue":true}' | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'; }
visible() {
  curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;[print(t["id"]) for t in json.load(sys.stdin) if t["type"]=="page"]' | while read -r id; do
    CDP "$id" Runtime.evaluate '{"expression":"document.visibilityState===\"visible\"?document.title:\"\"","returnByValue":true}' | python3 -c 'import json,sys;v=json.load(sys.stdin).get("result",{}).get("value","");v and print(v)'
  done | head -1
}
press() { # <label> <key code> <modifiers...>
  local label=$1; shift
  for attempt in 1 2 3 4 5; do
    "$AX" activate $PID >/dev/null 2>&1; sleep 0.3
    "$AX" hidkey $PID "$@" >> "$OUT/steps.txt" 2>&1 && break; sleep 1
  done
  sleep 2
  echo "$label: visible=$(visible) one=$(keys_in one.html) two=$(keys_in two.html) handled=$(grep -a -c 'Ahoi shortcut tab.last-used' "$OUT/browser.log")" >> "$OUT/probe.txt"
}
: > "$OUT/probe.txt"; echo "start: visible=$(visible)" >> "$OUT/probe.txt"
press "opt-tab (page focused)" 48 opt
press "ctrl-tab" 48 ctrl
press "opt-tab after ctrl-tab" 48 opt
press "opt-cmd-k (not bound)" 40 cmd opt
grep -a -E "Ahoi (shortcut|last-used|key trace)" "$OUT/browser.log" > "$OUT/ahoi-log.txt"
"$AX" hidkey $PID 12 cmd >/dev/null 2>&1; sleep 3
cat "$OUT/probe.txt"
