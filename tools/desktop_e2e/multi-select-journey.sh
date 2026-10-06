#!/bin/bash
# usage: multi-select-journey.sh <App.app> <outdir>
# Sidebar multi-selection (user decision 6 October 2026): ⌘-click adds a row
# without activating it, ⇧-click selects the range from the anchor, Escape
# clears, and the selection's context menu closes the open tabs with one
# confirmation toast. Delta stays open so the window survives. Real HID clicks with modifiers; selection read from the
# rows' AXSelected state.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9349
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-multi-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8796}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
for n in Alpha Beta Gamma Delta; do printf '<title>Mehrfach-%s</title>%s' $n $n > $P-site/$n.html; done
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; SITE=http://127.0.0.1:$SITE_PORT
page_count() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(sum(1 for t in json.load(sys.stdin) if t["type"]=="page"))'; }
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
# The sidebar row's label ("AXRow | Mehrfach-Beta"), not the window title.
row() { $AX dump $PID 40 | grep -o -E "AX(Row|Cell|RadioButton|Tab|Button) \| [^|]*Mehrfach-$1[^|]*" | head -1 | sed -E 's/ *$//'; }
click() { # <name> [modifiers]
  local r; r=$(row "$1"); [ -n "$r" ] || { echo "no row for $1" >> "$OUT/steps.txt"; return 1; }
  $AX activate $PID >/dev/null; sleep 0.3
  shift; $AX hidclick $PID "$r" "$@" >> "$OUT/steps.txt"; sleep 1
}
# Sorted names of the selected sidebar rows, e.g. "Alpha Gamma".
selected() { $AX selected $PID | grep -o -E 'Mehrfach-[A-Za-z]+' | sed 's/Mehrfach-//' | sort -u | tr '\n' ' ' | sed 's/ $//'; }
expect_selected() { local got; got=$(selected); echo "selected: $got" >> "$OUT/steps.txt"; [ "$got" = "$2" ] && record "$1" true || record "$1" false; }
quit() {
  $AX activate $PID >/dev/null; $AX hidkey $PID 12 cmd >> "$OUT/steps.txt"
  for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || return 0; sleep 1; done
  echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3
}

ahoi_launch_browser "$OUT/browser.log" --user-data-dir=$P --no-first-run \
  --no-default-browser-check --remote-debugging-port=$PORT \
  $SITE/Alpha.html $SITE/Beta.html $SITE/Gamma.html $SITE/Delta.html
echo "pid=$PID profile=$P" >> "$OUT/run.txt"
for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
sleep 5; $AX activate $PID >> "$OUT/steps.txt"
waitax "Mehrfach-Delta" 15 || { $AX dump $PID 40 > "$OUT/ax-no-rows.txt"; finish "rows missing"; quit; exit 4; }
PAGES0=$(page_count); echo "pages before: $PAGES0" >> "$OUT/steps.txt"

click Alpha || { finish "Alpha row not clickable"; quit; exit 4; }
expect_selected plainClickSelectsOne "Alpha"
click Gamma cmd
expect_selected commandClickAdds "Alpha Gamma"
# ⌘-click must not open or activate anything.
[ "$(page_count)" = "$PAGES0" ] && record commandClickKeepsTabs true || record commandClickKeepsTabs false
click Beta shift
expect_selected shiftClickSelectsRange "Beta Gamma"
$AX activate $PID >/dev/null; $AX hidkey $PID 53 >> "$OUT/steps.txt"; sleep 1
# Escape leaves only the active row selected.
expect_selected escapeClears "Alpha"

click Beta cmd
click Gamma cmd
expect_selected reselect "Alpha Beta Gamma"
r=$(row Beta); $AX activate $PID >/dev/null
$AX hidrightclick $PID "${r/ | /:}" >> "$OUT/steps.txt"
if waitax "3 Einträge ausgewählt|3 items selected" 5; then record menuTitle true; else
  $AX dump $PID 14 > "$OUT/ax-no-menu.txt"; record menuTitle false; fi
$AX press $PID "AXMenuItem:Offene Tabs schließen" >> "$OUT/steps.txt" \
  || $AX press $PID "AXMenuItem:Close open tabs" >> "$OUT/steps.txt"
if waitax "3 Tabs geschlossen|3 tabs closed" 6; then record closedToast true; else
  $AX dump $PID 14 > "$OUT/ax-no-closed-toast.txt"; record closedToast false; fi
sleep 2; echo "pages after: $(page_count)" >> "$OUT/steps.txt"
[ "$(page_count)" -le $((PAGES0 - 3)) ] && record tabsClosed true || record tabsClosed false
quit
finish ""
