#!/bin/bash
# Installed split archive/restore using the current native picker and shared guards.
# Five original semantic checks, synthetic data only; no drag or native test claim.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9379; SITE_PORT=${AHOI_E2E_SITE_PORT:-8802}
. "$S/split_journey_lib.sh"
: > "$OUT/results.txt"
record_archive() { echo "$1 $2" >> "$OUT/results.txt"; echo "$1 -> $2" >> "$OUT/steps.txt"; }
finish_archive() {
  $AX dump $PID 40 > "$OUT/ax-final.txt"
  quit
  python3 - "$OUT/results.txt" > "$OUT/results.json" <<'PYRESULT'
import json, sys
rows = dict(line.strip().split(' ', 1) for line in open(sys.argv[1]) if line.strip())
print(json.dumps({'results': rows, 'pass': len(rows) == 5 and all(value == 'PASS' for value in rows.values())}, indent=2))
PYRESULT
  cat "$OUT/results.json"
  python3 - "$OUT/results.json" <<'PYEXIT'
import json, sys
sys.exit(0 if json.load(open(sys.argv[1])).get('pass') is True else 1)
PYEXIT
  exit "$?"
}
launch "$SITE/solo.html"
open_url "$SITE/a.html"
tab_menu_split b.html || { record_archive split_created FAIL; finish_archive; }
snap split-created
[ "$(q visible)" = "PaneA,PaneB" ] && record_archive split_created PASS || { record_archive split_created FAIL; finish_archive; }
# Archive a resting complete split, not the currently visible/protected panes.
activate_owned
$AX dump $PID 40 > "$OUT/ax-archive-control.txt"
CONTROL=$(grep -o -E 'AX(RadioButton|Row|Cell|Button) \| [^|]*Solo[^|]*' "$OUT/ax-archive-control.txt" | head -1 | sed 's/ *$//')
[ -n "$CONTROL" ] || fail_setup "archive control row missing"
$AX press $PID "${CONTROL%% |*}:${CONTROL#*| }" >> "$OUT/steps.txt" || fail_setup "archive control action failed"
wait_document "$SITE/solo.html" 10 || fail_setup "archive control did not commit"
sleep 2
snap archive-control
python3 - "$SNAP" <<'PYCONTROL'
import json, sys
rows = [json.loads(line.split('|', 2)[2]) for line in open(sys.argv[1])]
solo = next((r for r in rows if r['t'] == 'Solo'), None)
split = [r for r in rows if r['t'] in ('PaneA', 'PaneB')]
sys.exit(0 if solo and solo['v'] == 'visible' and len(split) == 2 and all(r['v'] == 'hidden' for r in split) else 1)
PYCONTROL
[ "$?" = 0 ] || fail_setup "split is still visible/protected before archive"
open_row_menu PaneA 'Split archivieren|Archivieren \(inklusive Split\)' || fail_setup "split archive menu missing"
ARCH=$($AX dump $PID 40 | grep -o -E 'AXMenuItem \| (Split archivieren|Archivieren \(inklusive Split\))' | head -1 | sed 's/^AXMenuItem | //')
activate_owned
$AX press $PID "AXMenuItem:$ARCH" >> "$OUT/steps.txt"; sleep 5
case "$(urls)" in *a.html*|*b.html*) record_archive split_archived FAIL ;; *) record_archive split_archived PASS ;; esac
activate_owned
$AX press $PID "Inbox, Workspace wechseln" AXShowMenu >> "$OUT/steps.txt"
waitax 'Archiv durchsuchen' 5 || fail_setup "archive action missing"
$AX press $PID "Archiv durchsuchen …" >> "$OUT/steps.txt"
waitax 'Wiederherstellen …: ' 10 || { record_archive archive_lists_split FAIL; finish_archive; }
$AX dump $PID 40 > "$OUT/archive-dialog.txt"
RESTORE=$(grep -o -E 'AXButton \| Wiederherstellen …: [^|]*' "$OUT/archive-dialog.txt" | head -1 | sed 's/^AXButton | //; s/ *$//')
# Require both member titles and real Split metadata; fixture naming alone is insufficient.
case "$RESTORE" in *PaneA*PaneB*|*PaneB*PaneA*)
  grep -q 'Split ·' "$OUT/archive-dialog.txt" && record_archive archive_lists_split PASS || record_archive archive_lists_split FAIL ;;
*) record_archive archive_lists_split FAIL ;;
esac
activate_owned
$AX press $PID "AXButton:$RESTORE" >> "$OUT/steps.txt"
waitax 'Am ursprünglichen Ort wiederherstellen' 6 || fail_setup "archive restore placement missing"
$AX press $PID "Am ursprünglichen Ort wiederherstellen" >> "$OUT/steps.txt"; sleep 5
$AX dump $PID 40 > "$OUT/after-restore.txt"
grep -q -E 'konnte nicht|could not complete' "$OUT/after-restore.txt" && record_archive restore_without_error FAIL || record_archive restore_without_error PASS
activate_owned
$AX press $PID "AXButton:Schließen" >> "$OUT/steps.txt"; sleep 2
# Restore stays lazy; explicitly open the retained identity before testing panes.
ROW=$($AX dump $PID 40 | grep -v -E 'Wiederherstellen|löschen|AXMenuItem' | grep -o -E 'AX(RadioButton|Row|Cell|Button) \| [^|]*Pane(A|B)[^|]*' | head -1 | sed 's/ *$//')
[ -n "$ROW" ] || fail_setup "restored split row missing"
activate_owned
$AX press $PID "${ROW%% |*}:${ROW#*| }" >> "$OUT/steps.txt"; sleep 5
snap split-restored
[ "$(q visible)" = "PaneA,PaneB" ] && record_archive restore_brings_split_back PASS || record_archive restore_brings_split_back FAIL
finish_archive
