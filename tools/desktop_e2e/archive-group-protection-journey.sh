#!/bin/bash
# New installed auto-archive exceptions; fresh synthetic data, native protections.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9395; SITE_PORT=8805
export AHOI_E2E_ARCHIVE_AGE=${AHOI_E2E_ARCHIVE_AGE:-25}
. "$S/split_journey_lib.sh"
policy_menu() {
  activate_owned
  $AX press $PID "Inbox, Workspace wechseln" AXShowMenu >> "$OUT/steps.txt"
  waitax 'Inaktive temporäre Tabs archivieren' 5 || fail_setup "archive policy menu missing"
}
launch "$SITE/solo.html"
open_url "$SITE/a.html"
tab_menu_split b.html || fail_setup "protected split fixture failed"
params='{"expression":"document.getElementById(\"f\").focus();document.execCommand(\"insertText\",false,\"draft\");document.getElementById(\"f\").value","userGesture":true,"returnByValue":true}'
CDP b.html Runtime.evaluate "$params" > "$OUT/protected-group-form.json"
python3 - "$OUT/protected-group-form.json" <<'PYFORM'
import json, sys
sys.exit(0 if json.load(open(sys.argv[1])).get('result', {}).get('value') == 'draft' else 1)
PYFORM
[ "$?" = 0 ] || fail_setup "protected group form input not confirmed"
open_url "$SITE/c.html"
key 2 cmd; sleep 2
$AX dump $PID 40 > "$OUT/saved-page-before.txt"
# Saving must be visible before it can support the saved-page exclusion.
python3 "$S/tab_tree_state.py" "$P/Default/Ahoi Tab Tree" dump > "$OUT/saved-page-model.json" || fail_setup "saved-page model readback failed"
python3 - "$OUT/saved-page-model.json" "$SITE/c.html" <<'PYSAVED'
import json, sys
rows = [n for n in json.load(open(sys.argv[1]))['nodes'] if n['url'] == sys.argv[2]]
sys.exit(0 if len(rows) == 1 and rows[0]['temp'] == 0 and rows[0]['tomb'] == 0 else 1)
PYSAVED
[ "$?" = 0 ] || fail_setup "saved-page fixture state not confirmed"
open_url "$SITE/d.html"
CDP d.html Runtime.evaluate '{"expression":"window.onbeforeunload=e=>{e.preventDefault();e.returnValue=\"keep\";return \"keep\";};typeof window.onbeforeunload===\"function\"","userGesture":true,"returnByValue":true}' > "$OUT/before-unload-installed.json"
python3 - "$OUT/before-unload-installed.json" <<'PYUNLOAD'
import json, sys
sys.exit(0 if json.load(open(sys.argv[1])).get('result', {}).get('value') is True else 1)
PYUNLOAD
[ "$?" = 0 ] || fail_setup "before-unload fixture not confirmed"
open_url "$SITE/e.html"
activate_owned
$AX dump $PID 40 > "$OUT/solo-control.txt"
ROW=$(grep -o -E 'AX(RadioButton|Row|Cell|Button) \| [^|]*Solo[^|]*' "$OUT/solo-control.txt" | head -1 | sed 's/ *$//')
[ -n "$ROW" ] || fail_setup "active control row missing"
$AX press $PID "${ROW%% |*}:${ROW#*| }" >> "$OUT/steps.txt"
sleep 2
snap before-age
python3 - "$SNAP" <<'PYBACKGROUND'
import json, sys
rows = [json.loads(line.split('|', 2)[2]) for line in open(sys.argv[1])]
solo = [r for r in rows if r['t'] == 'Solo']
members = [r for r in rows if r['t'] in ('PaneA', 'PaneB')]
sys.exit(0 if len(solo) == 1 and solo[0]['v'] == 'visible' and solo[0]['f']
         and len(members) == 2 and all(r['v'] == 'hidden' for r in members) else 1)
PYBACKGROUND
[ "$?" = 0 ] || fail_setup "archive protection control/group background state not confirmed"
policy_menu
$AX press $PID "Nach 12 Stunden" >> "$OUT/steps.txt"
key 53
sleep $((AHOI_E2E_ARCHIVE_AGE * 2 + 10))
activate_owned
snap after-age
check wholeSplitRetainedWhenOneMemberEdited 'urls | grep -q a.html && urls | grep -q b.html'
CDP b.html Runtime.evaluate '{"expression":"document.getElementById(\"f\").value","returnByValue":true}' > "$OUT/group-form-after-age.json"
check protectedSplitDraftRetained 'python3 -c "import json,sys;sys.exit(0 if json.load(open(sys.argv[1])).get(\"result\",{}).get(\"value\")==\"draft\" else 1)" "$OUT/group-form-after-age.json"'
check savedPageNotAutomaticallyArchived 'urls | grep -q c.html'
check beforeUnloadTabNotAutomaticallyArchived 'urls | grep -q d.html'
check unprotectedControlIsArchived '! urls | grep -q e.html'
check activeControlIsRetained 'urls | grep -q solo.html'
policy_menu
$AX press $PID "Nie (Standard)" >> "$OUT/steps.txt"
key 53
# Remove only this synthetic handler before normal native quit; avoid a prompt.
CDP d.html Runtime.evaluate '{"expression":"window.onbeforeunload=null;1","returnByValue":true}' > "$OUT/before-unload-cleanup.json"
finish; STATUS=$?; quit; exit "$STATUS"
