#!/bin/bash
# usage: tab-switcher-journey.sh <App.app> <outdir>
# Arc-style tab switcher (user decision 6 October 2026): ⌃T opens the panel
# on the previously used tab, so Return switches back; W closes the focused
# tile's tab; Escape closes the panel. HID keys, AX for the panel and its
# focus, CDP only to activate the setup tabs and read the visible page.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9350
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-switcher-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8797}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
for n in Eins Zwei Drei; do printf '<title>Wechsel-%s</title>%s' $n $n > $P-site/$n.html; done
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; SITE=http://127.0.0.1:$SITE_PORT
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
targets() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;[print(t["id"], t["title"]) for t in json.load(sys.stdin) if t["type"]=="page"]'; }
page_count() { targets | wc -l | tr -d ' '; }
visible() {
  targets | while read -r id _; do
    CDP "$id" Runtime.evaluate '{"expression":"document.visibilityState===\"visible\"?document.title:\"\"","returnByValue":true}' | python3 -c 'import json,sys;v=json.load(sys.stdin).get("result",{}).get("value","");v and print(v)'
  done | head -1
}
activate_tab() { local id; id=$(targets | awk -v t="Wechsel-$1" '$2==t {print $1; exit}'); curl -s "http://127.0.0.1:$PORT/json/activate/$id" >> "$OUT/steps.txt"; echo >> "$OUT/steps.txt"; sleep 1.5; }
. "$S/browser_launch.sh"
trap 'kill $SITE_PID 2>/dev/null; [ -n "${PID:-}" ] && kill -0 $PID 2>/dev/null && kill $PID' EXIT
RESULTS=(); record() { RESULTS+=("\"$1\": $2"); echo "$1 -> $2" >> "$OUT/steps.txt"; }
finish() {
  local joined; joined=$(IFS=,; echo "${RESULTS[*]-}")
  local sep=""; [ -n "$joined" ] && sep=", "
  echo "{${joined}${1:+$sep\"setupFailed\": \"$1\"}}" | python3 -c 'import json,sys;d=json.load(sys.stdin);d["pass"]=("setupFailed" not in d) and all(v is True for k,v in d.items() if k!="setupFailed");print(json.dumps(d,indent=1))' > "$OUT/verdict.json"
  cat "$OUT/verdict.json"
}
waitax() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do $AX dump $PID 14 | grep -q -E "$1" && return 0; sleep 0.3; done; return 1; }
gone() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do $AX dump $PID 14 | grep -q -E "$1" || return 0; sleep 0.3; done; return 1; }
key() {
  for attempt in 1 2 3 4 5; do
    $AX activate $PID >/dev/null; sleep 0.3
    $AX hidkey $PID "$@" >> "$OUT/steps.txt" && return 0
    sleep 1
  done
  echo "hidkey gave up: $*" >> "$OUT/steps.txt"; return 1
}
focused_tile() { $AX focused $PID | grep focusedElement | grep -o -E 'Wechsel-[A-Za-z]+' | head -1; }
quit() { key 12 cmd; for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || return 0; sleep 1; done; echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3; }

ahoi_launch_browser "$OUT/browser.log" --user-data-dir=$P --no-first-run \
  --no-default-browser-check --remote-debugging-port=$PORT \
  $SITE/Eins.html $SITE/Zwei.html $SITE/Drei.html
echo "pid=$PID profile=$P" >> "$OUT/run.txt"
for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
sleep 5; $AX activate $PID >> "$OUT/steps.txt"
[ "$(targets | grep -c Wechsel-)" = 3 ] || { targets > "$OUT/targets.txt"; finish "pages missing"; quit; exit 4; }
# Usage order Drei, Eins, Zwei: Zwei is active, Eins the previous tab.
activate_tab Drei; activate_tab Eins; activate_tab Zwei
echo "visible before: $(visible)" >> "$OUT/steps.txt"

# ⌃T (T is key 17).
key 17 ctrl
if waitax "Tabs wechseln|Switch tabs" 6; then record panelOpens true; else
  $AX dump $PID 14 > "$OUT/ax-no-panel.txt"; record panelOpens false; fi
sleep 1; tile=$(focused_tile); echo "focused tile: $tile" >> "$OUT/steps.txt"
[ "$tile" = Wechsel-Eins ] && record previousTabFocused true || record previousTabFocused false
key 36
gone "Tabs wechseln|Switch tabs" 5 && record returnClosesPanel true || record returnClosesPanel false
sleep 1; v=$(visible); echo "visible after return: $v" >> "$OUT/steps.txt"
[ "$v" = Wechsel-Eins ] && record returnSwitchesBack true || record returnSwitchesBack false

# Again: the previous tab is now Zwei. W closes it, Escape closes the panel.
PAGES0=$(page_count)
key 17 ctrl
waitax "Tabs wechseln|Switch tabs" 6 || { $AX dump $PID 14 > "$OUT/ax-no-panel-2.txt"; }
sleep 1; echo "focused tile 2: $(focused_tile)" >> "$OUT/steps.txt"
key 13
sleep 2; [ "$(page_count)" -eq $((PAGES0 - 1)) ] && ! targets | grep -q Wechsel-Zwei \
  && record wClosesFocusedTab true || { targets > "$OUT/targets-after-w.txt"; record wClosesFocusedTab false; }
key 53
gone "Tabs wechseln|Switch tabs" 5 && record escapeClosesPanel true || record escapeClosesPanel false
[ "$(visible)" = Wechsel-Eins ] && record escapeKeepsTab true || record escapeKeepsTab false
quit
finish ""
