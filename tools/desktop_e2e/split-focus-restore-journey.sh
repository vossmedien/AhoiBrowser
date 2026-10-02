#!/bin/bash
# Diagnose the changed grid/focus/native-quit restart chain only.
# Copy a receipt-bound own synthetic four-pane session; preserve original data.
set -u
APP=$1; OUT=$2; RETAINED_RECEIPT=$3
S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}
PORT=9407; SITE_PORT=8825
. "$S/split_journey_lib.sh"
python3 - "$RETAINED_RECEIPT" "$P" "$APP" "$OUT/retained-grid-seed.json" <<'PYSEED'
import hashlib, json, pathlib, plistlib, re, shutil, sys
receipt_path, target, app, output = map(pathlib.Path, sys.argv[1:])
r = json.loads(receipt_path.read_text())
assert plistlib.loads((app / 'Contents/Info.plist').read_bytes())['AhoiSourceCommit'] == r['candidate']
assert r['observedAssertions']['fourPaneGridCreated'] and r['observedAssertions']['sidebarOneRowFourPanes']
retained = pathlib.Path(r['syntheticProfile']).resolve()
assert retained.parent == pathlib.Path('/private/tmp') and retained.name.startswith('ahoi-split-profile.')
assert target.resolve().parent == pathlib.Path('/private/tmp') and target.resolve() != retained
selected = max(r['sessionEvidence'], key=lambda value: value['name'])
original = retained / 'Default/Sessions' / selected['name']
assert hashlib.sha256(original.read_bytes()).hexdigest() == selected['sha256']
steps = receipt_path.parent / 'steps.txt'
assert hashlib.sha256(steps.read_bytes()).hexdigest() == r['filesSha256']['steps.txt']
baseline = re.search(r'^baseline (\d+)x(\d+)$', steps.read_text(), re.MULTILINE)
assert baseline
shutil.copytree(retained, target, dirs_exist_ok=True, ignore=shutil.ignore_patterns('Singleton*', 'LOCK', 'lockfile'))
for file in (target / 'Default/Sessions').glob('Session_*'):
    if file.name != selected['name']: file.unlink()
seed = dict(candidate=r['candidate'], original=str(original), originalSha256=selected['sha256'],
            receiptSha256=hashlib.sha256(receipt_path.read_bytes()).hexdigest(),
            width=int(baseline.group(1)), height=int(baseline.group(2)))
pathlib.Path(output).write_text(json.dumps(seed, indent=2) + '\n')
assert hashlib.sha256(original.read_bytes()).hexdigest() == selected['sha256']
PYSEED
[ "$?" = 0 ] || exit 4
read -r W0 H0 < <(python3 - "$OUT/retained-grid-seed.json" <<'PYBASE'
import json, sys
s = json.load(open(sys.argv[1])); print(s['width'], s['height'])
PYBASE
)
launch
for file in a b c d; do
  wait_document "$SITE/$file.html" 20 || fail_setup "retained grid document did not commit"
done
snap retained-four
[ "$(q visible)" = "PaneA,PaneB,PaneC,PaneD" ] && [ "$(q layout)" = four-grid ] || fail_setup "retained four-grid input did not restore"
# Diagnostic context only: original traversal/resize before quit; no ten-check replay.
split_menu "2-mal-2-Raster, Zeilen zuerst" ""
split_menu "2-mal-2-Raster, Spalten zuerst" "2-mal-2-Raster, Zeilen zuerst"
split_menu "" "2-mal-2-Raster, Spalten zuerst"
key 37 cmd ctrl; sleep 2
key 124 cmd ctrl; sleep 1; key 124 cmd ctrl; sleep 1
focus_pane 3; FOCUS=$(q focused)
$AX focused $PID > "$OUT/focus-before-first-quit.txt"
$AX dump $PID 40 > "$OUT/ax-before-first-quit.txt"
echo "before first quit: focused=$FOCUS shares=$(q shares)" >> "$OUT/steps.txt"
quit; sleep 2
python3 "$S/session_selected_readback.py" "$P" > "$OUT/selected-after-first-quit.json"
launch "" nodevtools; sleep 4
$AX focused $PID > "$OUT/focus-after-product-start.txt"
$AX dump $PID 40 > "$OUT/ax-after-product-start.txt"
quit; sleep 2
python3 "$S/session_selected_readback.py" "$P" > "$OUT/selected-after-product-quit.json"
launch; sleep 4
snap final-restart
$AX focused $PID > "$OUT/focus-after-debug-start.txt"
echo "final debug focus: $(q focused)" >> "$OUT/steps.txt"
check focusedPaneSurvivesExactQuitChain '[ "$(q focused)" = "$FOCUS" ]'
finish; STATUS=$?; quit; exit "$STATUS"
