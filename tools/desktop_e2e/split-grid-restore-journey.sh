#!/bin/bash
# Resume only the eleven unexecuted grid/refusal/startup/restore assertions.
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
mark_all
ORDER4=""; for n in 1 2 3 4; do focus_pane $n; ORDER4="$ORDER4 $(q focused)"; done
echo "four-pane order:$ORDER4" >> "$OUT/steps.txt"
check fourPaneFocusShortcuts '[ "$(echo $ORDER4 | tr " " "\n" | sort -u | paste -sd, -)" = "PaneA,PaneB,PaneC,PaneD" ]'
# SPLIT-34: both 2×2 traversal presets keep the grid and carry the check.
split_menu "2-mal-2-Raster, Zeilen zuerst" ""; snap grid-rows; G1=$(q layout)
split_menu "2-mal-2-Raster, Spalten zuerst" "2-mal-2-Raster, Zeilen zuerst"; C1=$CHECKED; snap grid-columns; G2=$(q layout)
split_menu "" "2-mal-2-Raster, Spalten zuerst"; C2=$CHECKED
check fourGridPresets '[ "$G1" = four-grid ] && [ "$G2" = four-grid ] && $C1 && $C2 && [ "$(q kept)" = "PaneA,PaneB,PaneC,PaneD" ]'
# SPLIT-34 / SPLIT-13: ⌘⌃L on four panes switches traversal, grid stays.
key 37 cmd ctrl; sleep 2; snap grid-key
check fourGridLayoutShortcut '[ "$(q layout)" = four-grid ] && [ "$(q kept)" = "PaneA,PaneB,PaneC,PaneD" ]'

# SPLIT-15 / SPLIT-01 (non-drag analog): a fifth pane is refused with a
# visible reason; nothing is replaced, hidden, closed or reloaded.
focus_pane 1
popup_from "$(file_of "$(q focused)")" e.html || fail_setup "popup overlay for e.html did not open"
# The status label is exposed as AXGroup on the M154 macOS Views bridge;
# accept that exact visible message or its AXStaticText representation.
popup_to_split
waitax "AX(StaticText|Group) \\| Dieser Split hat bereits vier Bereiche" 6 && REFUSED=true || REFUSED=false
$AX dump $PID 40 > "$OUT/ax-fifth-pane.txt"; snap fifth
check fifthPaneRefused '$REFUSED && [ "$(q visible -PaneE)" = "PaneA,PaneB,PaneC,PaneD" ] && [ "$(q layout -PaneE)" = four-grid ] && [ "$(q kept -PaneE)" = "PaneA,PaneB,PaneC,PaneD" ]'
$AX press $PID "AXButton:Popup schließen" >> "$OUT/steps.txt"; sleep 2; snap popup-closed
check refusedPopupClosesCleanly '! q all | grep -q PaneE && [ "$(q layout)" = four-grid ]'

# SPLIT-21 / SPLIT-03: quit with the 2×2 group, a moved primary divider and
# pane 3 focused; both restart paths bring back membership, the other
# values need DevTools. restoredRatios also requires the moved divider (area
# shares at least 5 points apart), so default ratios cannot pass it. Only
# the primary ratio has a shortcut; the secondary 2×2 ratio needs a drag.
key 124 cmd ctrl; sleep 1; key 124 cmd ctrl; sleep 1
focus_pane 3; FOCUS=$(q focused); SHARES=$(q shares); URLS=$(urls)
echo "before quit: focus=$FOCUS shares=$SHARES urls=$URLS" >> "$OUT/steps.txt"
quit; sleep 2
launch "" nodevtools; sleep 4
check restoredOneRowProductStart 'sidebar_group relaunch PaneA PaneB PaneC PaneD'
AREAS=$($AX dump $PID 40 | grep -o -E "AXWebArea \| Pane[A-D]" | sort -u | wc -l | tr -d " ")
check restoredFourPanesProductStart '[ "$AREAS" = 4 ]'
quit; sleep 2
# Both native preferences were set before fixture construction; preserve their
# protected hashes across both launches instead of editing JSON after quit.
launch; sleep 4
waiturl a.html 20 || echo "debug restore has no PaneA target" >> "$OUT/steps.txt"
snap relaunch
NOWSHARES=$(q shares); echo "after restart: focus=$(q focused) shares=$NOWSHARES urls=$(urls)" >> "$OUT/steps.txt"
check restoredMembershipAndLayout '[ "$(q visible)" = "PaneA,PaneB,PaneC,PaneD" ] && [ "$(q layout)" = four-grid ]'
check restoredWithoutPhantomTabs '[ "$(urls)" = "$URLS" ]'
check restoredRatios 'python3 -c "import sys
a=dict(x.split(\":\") for x in sys.argv[1].split());b=dict(x.split(\":\") for x in sys.argv[2].split())
v=[float(x) for x in a.values()] or [0]
sys.exit(0 if max(v)-min(v)>=0.05 and a.keys()==b.keys() and all(abs(float(a[k])-float(b[k]))<0.02 for k in a) else 1)" "$SHARES" "$NOWSHARES"'
check restoredFocusedPane '[ -n "$FOCUS" ] && [ "$(q focused)" = "$FOCUS" ]'
$AX dump $PID 40 > "$OUT/ax-final.txt"
finish; STATUS=$?; quit; exit "$STATUS"
