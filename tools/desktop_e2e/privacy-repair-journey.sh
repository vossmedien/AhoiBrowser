#!/bin/bash
# usage: privacy-repair-journey.sh <App.app> <outdir>
# Visible PRIV-07 journey on an installed candidate. Uses only accessibility
# actions (AXPress), no mouse or keyboard events. A "Mehr Schutz" profile
# opens the loopback fixture's cross-site page, whose embedded frame cannot
# keep its third-party cookie (the deliberately incompatible case). Through
# the privacy button in the address bar the site is switched to "Maximale
# Website-Kompatibilität" with the panel's repair action. Checks:
#   PRIV-07_repair_applies  the exception is stored for exactly that site
#                           and the reloaded page arrives without Sec-GPC,
#   PRIV-07_effect_explained the panel names the compatibility mode and
#                           says that built-in browser security stays on,
#   PRIV-07_no_jargon       the panel text avoids internal vocabulary.
# Results: <outdir>/results.json; AX dumps of the panel in <outdir>.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9389; SP=${AHOI_E2E_SITE_PORT:-8808}
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
for p in $PORT $SP; do
  if lsof -nP -iTCP:$p -sTCP:LISTEN >/dev/null 2>&1; then echo "port $p busy" >&2; exit 6; fi
done
mkdir -p "$OUT"; : > "$OUT/results.txt"; : > "$OUT/run.txt"
A=http://127.0.0.1:$SP
P=$(mktemp -d /private/tmp/ahoi-privacy-repair-profile.XXXXXX); mkdir -p "$P/Default"
printf '{"ahoi":{"privacy":{"global_mode":"strict"}}}' > "$P/Default/Preferences"
record() { echo "$1 $2" >> "$OUT/results.txt"; }
LOG="$OUT/fixture.jsonl"; : > "$LOG"
python3 "$S/privacy_fixture.py" --port $SP --log "$LOG" > "$OUT/fixture.stderr" 2>&1 &
FIX=$!
"$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run --no-default-browser-check \
  --remote-debugging-port=$PORT "$A/top" > "$OUT/browser.log" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null; sleep 3; kill -9 $PID 2>/dev/null; kill $FIX 2>/dev/null; rm -rf "$P"' EXIT
for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
sleep 5
"$AX" enable $PID >> "$OUT/run.txt" 2>&1; sleep 2
# press <label>...: the first label that exists (German or English UI).
press() { local l; for l in "$@"; do "$AX" press $PID "$l" >> "$OUT/run.txt" 2>&1 && return 0; done; return 1; }
press "Adressleiste einblenden" "Show address bar"; sleep 2
press "Datenschutzmodus: Mehr Schutz" "Privacy mode: More protection" || { record PRIV-07_panel_opens "FAIL:no-privacy-button"; }
sleep 2
"$AX" dump $PID 40 > "$OUT/ax-panel-before.txt" 2>/dev/null
press "Kompatibilität für diese Website maximieren" "Fix this website with maximum compatibility" \
  || record PRIV-07_repair_action "FAIL:no-repair-action"
sleep 5
# The repair reloads the page, which closes the panel; reopen it under the
# button's new name to read the effect description of the site's mode.
press "Adressleiste einblenden" "Show address bar"; sleep 2
press "Datenschutzmodus: Maximale Website-Kompatibilität" \
  "Privacy mode: Maximum website compatibility" \
  || record PRIV-07_panel_reopens "FAIL:no-compatibility-button"
sleep 2
"$AX" dump $PID 40 > "$OUT/ax-panel-after.txt" 2>/dev/null

# The live preference; the Preferences file is written lazily.
curl -s -X PUT "http://127.0.0.1:$PORT/json/new?chrome://prefs-internals" > /dev/null; sleep 4
expr="(()=>{try{const d=JSON.parse(document.body.innerText);let v=((d.ahoi||{}).privacy||{}).origin_modes;if(v&&typeof v==='object'&&'value' in v)v=v.value;return (v||{})['$A']||'<absent>'}catch(e){return '<unreadable>'}})()"
stored=$(node "$S/cdp.mjs" $PORT prefs-internals Runtime.evaluate \
  "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True}))' "$expr")" \
  | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))')
last_gpc=$(python3 - "$LOG" <<'PY'
import json, sys
values = [e.get("gpc") for e in map(json.loads, open(sys.argv[1])) if e["path"] == "/top"]
print(",".join(str(v) for v in values))
PY
)
echo "stored=$stored top_gpc_sequence=$last_gpc" >> "$OUT/run.txt"
case "$last_gpc" in
  1,*None) [ "$stored" = chromium-compatible ] && record PRIV-07_repair_applies PASS \
             || record PRIV-07_repair_applies "FAIL:stored=$stored" ;;
  *) record PRIV-07_repair_applies "FAIL:stored=$stored,gpc=$last_gpc" ;;
esac
panel=$(cat "$OUT/ax-panel-after.txt")
if grep -q -E "Maximale Website-Kompatibilität|Maximum website compatibility" <<<"$panel" \
   && grep -q -E "Browsersicherheit[^|]*bleibt aktiv|browser security[^|]*stays on" <<<"$panel"; then
  record PRIV-07_effect_explained PASS
else
  record PRIV-07_effect_explained "FAIL:security-sentence-missing"
fi
jargon=$(grep -o -i -E "Sec-GPC|\bGPC\b|Referr?er|CHIPS|partitioniert|partitioned|throttle|Origin\b|Drittanbieter-Cookie-Partition" "$OUT/ax-panel-after.txt" | sort -u | tr '\n' ' ')
[ -z "$jargon" ] && record PRIV-07_no_jargon PASS || record PRIV-07_no_jargon "FAIL:$jargon"

python3 - "$OUT/results.txt" > "$OUT/results.json" <<'PY'
import json, sys
rows = dict(line.rstrip("\n").split(" ", 1) for line in open(sys.argv[1]) if line.strip())
print(json.dumps({"results": rows,
                  "pass": all(v == "PASS" for v in rows.values())}, indent=1))
PY
cat "$OUT/results.json"
