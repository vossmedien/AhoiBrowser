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
  $AX dump $PID 40 > "$OUT/ax-entry-readback.txt" || return 2
  python3 - "$OUT/ax-entry-readback.txt" <<'PYCOUNT'
import re, sys
lines = open(sys.argv[1]).read().splitlines()
windows = [i for i, line in enumerate(lines) if line.strip() == 'AXWindow | Archiv']
if len(windows) != 1:
    sys.exit(2)
start = windows[0]
indent = len(lines[start]) - len(lines[start].lstrip())
end = next((i for i in range(start + 1, len(lines))
            if lines[i].strip() and len(lines[i]) - len(lines[i].lstrip()) <= indent), len(lines))
dialog = lines[start:end]
if not any('AXTextField | Archiv durchsuchen' in line for line in dialog):
    sys.exit(2)
print(sum(bool(re.search(r'AXButton \| Wiederherstellen …: .*PaneA.*PaneB', line)) for line in dialog))
PYCOUNT
}
delete_entry() {
  $AX dump $PID 40 > "$OUT/ax-delete-entry.txt"
  local label
  label=$(grep -o -E 'AXButton \| Endgültig löschen …: [^|]*PaneA[^|]*PaneB[^|]*' "$OUT/ax-delete-entry.txt" | head -1 | sed 's/^AXButton | //; s/ *$//')
  [ -n "$label" ] || fail_setup "own archive delete button missing"
  activate_owned
  $AX press $PID "AXButton:$label" >> "$OUT/steps.txt"
  waitax 'Archiveintrag endgültig löschen' 6 || fail_setup "separate delete confirmation missing"
  # The AX window title precedes native layout/activation completion when the
  # archive bubble hands off to the confirmation. Wait for that transition
  # before obtaining screen coordinates for a real button click.
  sleep 2
  for attempt in 1 2 3 4 5; do
    $AX focused $PID > "$OUT/confirmation-focus.txt"
    grep -q '^focusedWindow: AXWindow | Archiveintrag endgültig löschen?' "$OUT/confirmation-focus.txt" && break
    sleep 1
  done
  grep -q '^focusedWindow: AXWindow | Archiveintrag endgültig löschen?' "$OUT/confirmation-focus.txt" || fail_setup "delete confirmation did not acquire native focus"
  $AX enabled $PID "Abbrechen" > "$OUT/confirmation-cancel-enabled.txt"
  grep -q 'AXButton | Abbrechen .*enabled=true' "$OUT/confirmation-cancel-enabled.txt" || fail_setup "cancel button unavailable"
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
[ "$(entry_count)" = 1 ] || fail_setup "own persisted archive entry not uniquely confirmed"
grep -q 'Split ·' "$OUT/ax-entry-readback.txt" || fail_setup "persisted entry lost its split metadata"
check archivedPagesNotOpenedByRestart '! urls | grep -q -E "a.html|b.html"'
delete_entry
$AX dump $PID 40 > "$OUT/delete-confirmation.txt"
check separateDeleteConfirmation 'grep -q "Archiveintrag endgültig löschen" "$OUT/delete-confirmation.txt" && grep -q "AXButton | Abbrechen" "$OUT/delete-confirmation.txt"'
activate_owned
# AXPress reports success for this model-host button without dispatching its
# action on the current macOS candidate. Use a real click and observe dismissal.
$AX hidclick $PID "AXButton | Abbrechen" >> "$OUT/steps.txt" || fail_setup "cancel click failed"
for attempt in 1 2 3 4 5; do
  $AX dump $PID 40 > "$OUT/after-cancel.txt"
  grep -q 'AXWindow | Archiveintrag endgültig löschen?' "$OUT/after-cancel.txt" || break
  sleep 1
done
check cancelledDeleteDismissesConfirmation '! grep -q "AXWindow | Archiveintrag endgültig löschen?" "$OUT/after-cancel.txt"'
grep -q 'AXWindow | Archiveintrag endgültig löschen?' "$OUT/after-cancel.txt" && fail_setup "cancel did not dismiss confirmation"
sleep 2
archive_search
check cancelledDeleteRetainsEntry '[ "$(entry_count)" = 1 ]'
check cancelledDeleteKeepsOpenPages '[ "$(urls)" = "$BEFORE" ]'
delete_entry
# The browser previously crashed during native window deactivation with this
# confirmation open. Cmd-Q must exit normally and leave the unconfirmed entry.
$AX dump $PID 40 > "$OUT/confirmation-before-quit.txt"
quit
wait "$PID"; BROWSER_EXIT=$?
printf 'confirmation browser exit=%s\n' "$BROWSER_EXIT" >> "$OUT/steps.txt"
check quitWithOpenConfirmationIsClean '[ "$BROWSER_EXIT" = 0 ]'
if [ "$BROWSER_EXIT" != 0 ]; then
  finish "browser failed while closing an open archive confirmation"
  exit 4
fi
sleep 2
launch
open_url "$SITE/solo.html"
BEFORE=$(urls)
archive_search
check unconfirmedDeleteRetainsEntryAfterQuit '[ "$(entry_count)" = 1 ]'
delete_entry
activate_owned
$AX hidclick $PID "AXButton | Endgültig löschen |" >> "$OUT/steps.txt" || fail_setup "confirm delete click failed"
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
