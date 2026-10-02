#!/bin/bash
# Native archive persistence and separate permanent-delete confirmation.
# Only this invocation's fresh synthetic two-page split is deleted.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9393; SITE_PORT=8803
. "$S/split_journey_lib.sh"
archive_search() {
  activate_owned
  $AX press $PID "Inbox, Workspace wechseln" AXShowMenu >> "$OUT/steps.txt"
  waitax 'Archiv durchsuchen' 5 || fail_setup "archive search command missing"
  $AX press $PID "Archiv durchsuchen …" >> "$OUT/steps.txt"
  waitax 'Archiv durchsuchen|Das Archiv ist leer' 8 || fail_setup "archive search did not open"
  sleep 1
}
entry_count() {
  $AX dump $PID 40 > "$OUT/ax-entry-readback.txt"
  grep -c -E 'AXButton \| Wiederherstellen …: .*PaneA.*PaneB' "$OUT/ax-entry-readback.txt"
}
delete_entry() {
  $AX dump $PID 40 > "$OUT/ax-delete-entry.txt"
  local label
  label=$(grep -o -E 'AXButton \| Endgültig löschen …: [^|]*PaneA[^|]*PaneB[^|]*' "$OUT/ax-delete-entry.txt" | head -1 | sed 's/^AXButton | //; s/ *$//')
  [ -n "$label" ] || fail_setup "own archive delete button missing"
  activate_owned
  $AX press $PID "AXButton:$label" >> "$OUT/steps.txt"
  waitax 'Archiveintrag endgültig löschen' 6 || fail_setup "separate delete confirmation missing"
}
launch "$SITE/solo.html"
open_url "$SITE/a.html"
tab_menu_split b.html || fail_setup "persistence fixture split failed"
snap split-created
[ "$(q visible)" = "PaneA,PaneB" ] || fail_setup "persistence fixture split not visible"
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
case "$(urls)" in *a.html*|*b.html*) fail_setup "persistence fixture archive did not close both members" ;; esac
# The archive entry already exists; only new persistence/delete contracts follow.
quit; sleep 2
launch
open_url "$SITE/solo.html"
BEFORE=$(urls)
archive_search
check archiveSurvivesRestart '[ "$(entry_count)" = 1 ]'
check archivedPagesNotOpenedByRestart '! urls | grep -q -E "a.html|b.html"'
delete_entry
$AX dump $PID 40 > "$OUT/delete-confirmation.txt"
check separateDeleteConfirmation 'grep -q "Archiveintrag endgültig löschen" "$OUT/delete-confirmation.txt" && grep -q "AXButton | Abbrechen" "$OUT/delete-confirmation.txt"'
activate_owned
$AX press $PID "AXButton:Abbrechen" >> "$OUT/steps.txt"; sleep 2
archive_search
check cancelledDeleteRetainsEntry '[ "$(entry_count)" = 1 ]'
check cancelledDeleteKeepsOpenPages '[ "$(urls)" = "$BEFORE" ]'
delete_entry
activate_owned
$AX press $PID "AXButton:Endgültig löschen" >> "$OUT/steps.txt"
waitax 'Das Archiv ist leer' 8 || fail_setup "confirmed archive delete did not complete"
$AX dump $PID 40 > "$OUT/after-delete.txt"
check confirmedDeleteRemovesEntry '[ "$(entry_count)" = 0 ] && grep -q "Das Archiv ist leer" "$OUT/after-delete.txt"'
check confirmedDeleteKeepsOpenPages '[ "$(urls)" = "$BEFORE" ]'
key 53
quit; sleep 2
launch
archive_search
check deletedEntryStaysDeletedAfterRestart '[ "$(entry_count)" = 0 ]'
check deletedPagesNotRestored '! urls | grep -q -E "a.html|b.html"'
key 53
finish; STATUS=$?; quit; exit "$STATUS"
