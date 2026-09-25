#!/bin/bash
# usage: import-sources-journey.sh <App.app> <outdir>
# Read-only CDP journey for IMPORT-ZEN-06 and the import source list: a fresh
# profile opens chrome://settings/importData and reads the offered sources.
# Zen must appear only when an exact Zen bundle and a safe profile exist
# (checked against the host), so on a Mac without Zen it must be absent.
# Nothing is imported. Results: <outdir>/results.json.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); PORT=9385
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-import-profile.XXXXXX); : > "$OUT/results.txt"
"$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run --no-default-browser-check \
  --remote-debugging-port=$PORT "chrome://settings/importData" > "$OUT/browser.log" 2>&1 &
PID=$!; trap 'kill $PID 2>/dev/null' EXIT; echo "pid=$PID profile=$P" > "$OUT/run.txt"
for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
sleep 8
record() { echo "$1 $2" >> "$OUT/results.txt"; }
# The source <select> sits in settings-import-data-dialog's shadow root.
node "$S/cdp.mjs" $PORT "chrome://settings" Runtime.evaluate '{"expression":"(()=>{const all=[];const walk=r=>{for(const e of r.querySelectorAll(\"*\")){all.push(e);if(e.shadowRoot)walk(e.shadowRoot)}};walk(document);const s=all.find(e=>e.id===\"browserSelect\");return s?[...s.options].map(o=>o.textContent.trim()).join(\"|\"):\"<no select>\"})()","returnByValue":true}' > "$OUT/sources.json"
SOURCES=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1])).get("result",{}).get("value",""))' "$OUT/sources.json")
echo "sources: $SOURCES" >> "$OUT/run.txt"
[ -n "$SOURCES" ] && [ "$SOURCES" != "<no select>" ] && record dialog_lists_sources PASS || record dialog_lists_sources FAIL
ZEN_APP=false
for b in /Applications/Zen.app "/Applications/Zen Browser.app" "/Applications/Zen Twilight.app" "$HOME/Applications/Zen.app"; do [ -d "$b" ] && ZEN_APP=true; done
echo "zen bundle on host: $ZEN_APP" >> "$OUT/run.txt"
case "$SOURCES" in *Zen*) listed=true ;; *) listed=false ;; esac
[ "$listed" = "$ZEN_APP" ] && record zen_listed_only_when_installed PASS || record zen_listed_only_when_installed FAIL
ARC_APP=false; [ -d /Applications/Arc.app ] && ARC_APP=true
echo "arc bundle on host: $ARC_APP" >> "$OUT/run.txt"
# Informational: Arc may come through Ahoi's own Arc import instead.
case "$SOURCES" in *Arc*) echo "arc listed in dialog: true" ;; *) echo "arc listed in dialog: false" ;; esac >> "$OUT/run.txt"
python3 - "$OUT/results.txt" > "$OUT/results.json" <<'PY'
import json, sys
rows = dict(line.split() for line in open(sys.argv[1]) if line.strip())
print(json.dumps({"results": rows, "pass": all(v == "PASS" for v in rows.values())}, indent=1))
PY
cat "$OUT/results.json"
