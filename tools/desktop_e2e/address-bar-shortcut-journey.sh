#!/bin/bash
# usage: address-bar-shortcut-journey.sh <App.app> <outdir>
# ⌥⌘T (catalog command browser.focus-address-bar, user request 6 October
# 2026) reveals the floating navigation, focuses the address and selects it:
# typing a new address and Return loads it in the same tab (an unselected
# address would get the text appended), without a new tab. ⇧⌘T keeps
# Chromium's "Reopen closed tab". HID keys, PID-scoped AX, CDP reads.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9347
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-addressbar-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8794}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
for page in alpha beta gamma; do printf '<title>%s</title>%s' $page $page > $P-site/$page.html; done
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

launch
CDP about:blank Page.navigate "{\"url\":\"$SITE/alpha.html\"}" >> "$OUT/steps.txt"; echo >> "$OUT/steps.txt"
waiturl alpha.html 10 || { finish "alpha did not load"; quit; exit 4; }
sleep 2; BEFORE=$(page_count)
# ⌥⌘T: T is key code 17.
key 17 cmd opt; sleep 1.5
$AX dump $PID 14 > "$OUT/ax-after-option-command-t.txt"
# The selected address is replaced by what is typed; Return loads it here.
$AX type $PID "127.0.0.1:$SITE_PORT/beta.html" >> "$OUT/steps.txt"; sleep 1
key 36
if waiturl beta.html 10; then
  NOW=$(pages); echo "pages after typing: $NOW" >> "$OUT/steps.txt"
  { ! echo "$NOW" | grep -q alpha.html; } && [ "$(page_count)" = "$BEFORE" ] \
    && record addressReplacedInSameTab true || record addressReplacedInSameTab false
else
  echo "pages after typing: $(pages)" >> "$OUT/steps.txt"; record addressReplacedInSameTab false
fi
# A typed-over address would have produced "…alpha.html127.0.0.1…".
{ ! pages | grep -q "alpha.html127"; } && record addressWasSelected true || record addressWasSelected false
# ⇧⌘T still reopens the last closed tab.
# Opened like a user would: ⌘T, the command bar's new-tab mode, address, Return.
key 17 cmd; sleep 1.5
$AX type $PID "127.0.0.1:$SITE_PORT/gamma.html" >> "$OUT/steps.txt"; sleep 1; key 36
waiturl gamma.html 10 || echo "info: gamma did not open" >> "$OUT/steps.txt"
sleep 2; key 13 cmd; sleep 2   # ⌘W closes gamma, the active tab
pages | grep -q gamma.html && echo "info: gamma still open after ⌘W" >> "$OUT/steps.txt"
key 17 cmd shift
waiturl gamma.html 10 && record shiftCommandTReopensClosedTab true || record shiftCommandTReopensClosedTab false
quit
finish ""
