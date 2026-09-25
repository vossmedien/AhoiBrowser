#!/bin/bash
# usage: split-archive-restore-journey.sh <App.app> <outdir>
# PID-scoped AX + CDP journey for crest handoff 034 (S2) and DoD 28: a live
# two-pane split of temporary tabs is archived from its sidebar row and
# restored at its original place; the split must come back as a split (both
# panes visible at once). Run it on a candidate without 034 to reproduce the
# defect and on one with 034 to accept the fix. Results: <outdir>/results.json.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9379; SITE_PORT=${AHOI_E2E_SITE_PORT:-8802}
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-split-profile.XXXXXX); : > "$OUT/steps.txt"; : > "$OUT/results.txt"
mkdir -p "$P-site"
printf '<title>Ahoi split left</title><h1>left</h1>' > "$P-site/left.html"
printf '<title>Ahoi split right</title><h1>right</h1>' > "$P-site/right.html"
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory "$P-site" > "$OUT/site.log" 2>&1 &
SITE_PID=$!; SITE=http://127.0.0.1:$SITE_PORT
"$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run --no-default-browser-check \
  --remote-debugging-port=$PORT "$SITE/left.html" > "$OUT/browser.log" 2>&1 &
PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT; echo "pid=$PID profile=$P" > "$OUT/run.txt"
for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
sleep 4

ax() {
  if [ "$1" = key ]; then
    shift; local pid=$1; shift
    for attempt in 1 2 3 4 5; do
      "$AX" activate "$pid" >/dev/null 2>&1; sleep 0.3
      "$AX" hidkey "$pid" "$@" >> "$OUT/steps.txt" 2>&1 && return 0
      sleep 1
    done
    echo "hidkey gave up: $*" >> "$OUT/steps.txt"; return 1
  fi
  "$AX" "$@" >> "$OUT/steps.txt" 2>&1
}
record() { echo "$1 $2" >> "$OUT/results.txt"; echo "== $1 $2" >> "$OUT/steps.txt"; }
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
waitax() { local end=$(( $(date +%s) + $2 ))
  while [ $(date +%s) -lt $end ]; do "$AX" dump $PID 40 | grep -q -E "$1" && return 0; sleep 1; done; return 1; }
# Titles of all page targets whose document is visible right now.
visible_titles() {
  curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;[print(t["id"]) for t in json.load(sys.stdin) if t["type"]=="page"]' | while read -r id; do
    CDP "$id" Runtime.evaluate '{"expression":"document.visibilityState===\"visible\"?document.title:\"\"","returnByValue":true}' \
      | python3 -c 'import json,sys;v=json.load(sys.stdin).get("result",{}).get("value","");v and print(v)'
  done | sort | tr '\n' '|'
}
both_visible() { case "$(visible_titles)" in *"Ahoi split left"*"Ahoi split right"*) return 0 ;; *) return 1 ;; esac; }
finish() {
  "$AX" dump $PID 45 > "$OUT/ax-final.txt"
  ax key $PID 12 cmd; sleep 5
  python3 - "$OUT/results.txt" > "$OUT/results.json" <<'PY'
import json,sys
rows=dict(l.split(None,1) for l in open(sys.argv[1]) if l.strip())
rows={k:v.strip() for k,v in rows.items()}
print(json.dumps({"results":rows,"pass":all(v=="PASS" for v in rows.values())},indent=1))
PY
  cat "$OUT/results.json"; exit 0
}

# 1 Split the active page with a new pane via the native Tab menu, then load
# the right page into the new (blank) pane.
ax activate $PID; sleep 1
ITEM=$("$AX" dump $PID 45 | grep -o -E 'AXMenuItem \| [^|]*(geteilte[rn]? Ansicht|[Ss]plit [Vv]iew)[^|]*' | head -1 | sed 's/^AXMenuItem | //; s/ *$//')
echo "split menu item: $ITEM" >> "$OUT/steps.txt"
[ -n "$ITEM" ] && ax press $PID "AXMenuItem:$ITEM"; sleep 3
NEW=$(curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys
t=[x for x in json.load(sys.stdin) if x["type"]=="page" and "left.html" not in x["url"]]
print(t[0]["id"] if t else "")')
[ -n "$NEW" ] && CDP "$NEW" Page.navigate "{\"url\":\"$SITE/right.html\"}" >> "$OUT/steps.txt"
sleep 4
both_visible && record split_created PASS || { record split_created FAIL; finish; }
"$AX" dump $PID 45 > "$OUT/ax-split.txt"

# 2 Archive the split from its sidebar row.
ROW=$(grep -o -E 'AX(RadioButton|Row|Cell|Button) \| [^|]*Ahoi split left[^|]*' "$OUT/ax-split.txt" | head -1 | sed -E 's/ *$//')
echo "row: $ROW" >> "$OUT/steps.txt"
NAME=${ROW#*| }; ROLE=${ROW%% |*}
ax press $PID "$ROLE:$NAME" AXShowMenu
waitax "AXMenuItem \| Archivieren \(inklusive Split\)" 5 && ax press $PID "AXMenuItem:Archivieren (inklusive Split)"
sleep 5
case "$(visible_titles)" in *"Ahoi split"*) record split_archived FAIL ;; *) record split_archived PASS ;; esac

# 3 Restore at the original place from the archive.
ax press $PID "Inbox, Workspace wechseln" AXShowMenu
waitax "Archiv durchsuchen" 5 && ax press $PID "Archiv durchsuchen …"
waitax "Wiederherstellen …: " 10 || { record archive_lists_split FAIL; finish; }
"$AX" dump $PID 45 > "$OUT/archive-dialog.txt"
RESTORE=$(grep -o -E 'AXButton \| Wiederherstellen …: [^|]*' "$OUT/archive-dialog.txt" | head -1 | sed 's/^AXButton | //; s/ *$//')
echo "restore: $RESTORE" >> "$OUT/steps.txt"
case "$RESTORE" in *split*|*Split*) record archive_lists_split PASS ;; *) record archive_lists_split FAIL ;; esac
ax press $PID "AXButton:$RESTORE"
waitax "Am ursprünglichen Ort wiederherstellen" 6 && ax press $PID "Am ursprünglichen Ort wiederherstellen"
sleep 5
"$AX" dump $PID 45 > "$OUT/after-restore.txt"
grep -q -E 'konnte nicht|could not complete' "$OUT/after-restore.txt" && record restore_without_error FAIL || record restore_without_error PASS
ax press $PID "AXButton:Schließen"; sleep 2
# The restored entry may stay unloaded; open it from the sidebar, then both
# panes must be visible together again.
ROW2=$("$AX" dump $PID 45 | grep -v -E 'Wiederherstellen|löschen' | grep -o -E 'AX(RadioButton|Row|Cell|Button) \| [^|]*Ahoi split (left|right)[^|]*' | head -1 | sed -E 's/ *$//')
echo "restored row: $ROW2" >> "$OUT/steps.txt"
[ -n "$ROW2" ] && ax press $PID "${ROW2%% |*}:${ROW2#*| }"
sleep 5
both_visible && record restore_brings_split_back PASS || record restore_brings_split_back FAIL
echo "visible after restore: $(visible_titles)" >> "$OUT/steps.txt"
finish
