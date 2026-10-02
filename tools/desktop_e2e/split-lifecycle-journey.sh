#!/bin/bash
# shellcheck disable=SC2034  # variables are read inside check's eval conditions
# usage: split-lifecycle-journey.sh <App.app> <outdir>
# DoD 5 / gap row 9, second half, on the installed candidate without HID
# drag: ⌘⌃W closes one pane at a time (4 -> 3 -> 2 -> 1) without reloading
# the survivors, ⌘⌃0 and the sidebar row menu's "Ansichten trennen" leave a
# split with both pages open, and a split in a second window stays in that
# window, including across ⌘Q and a restart through Chromium's
# restore-last-session (the DevTools start). Entry points and label sources:
# split_journey_lib.sh. PID-scoped AX, HID keys, CDP reads.
# Results: <outdir>/verdict.json.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}
PORT=9409; SITE_PORT=${AHOI_E2E_SITE_PORT:-8827}
. "$S/split_journey_lib.sh"

launch "$SITE/solo.html"
prefs_continue
open_url "$SITE/a.html"
snap base; read -r W0 H0 < <(q size PaneA); echo "baseline ${W0}x${H0}" >> "$OUT/steps.txt"
[ "$W0" -gt 0 ] && [ "$(q visible)" = PaneA ] || fail_setup "PaneA is not the only visible page"
tab_menu_split b.html || fail_setup "Tab menu split did not load b.html"
for f in c.html d.html; do
  focus_pane 1
  popup_from "$(file_of "$(q focused)")" $f || fail_setup "popup overlay for $f did not open"
  popup_to_split
done
snap four; [ "$(q layout)" = four-grid ] || fail_setup "no four-pane split ($(q layout))"
mark_all

# SPLIT-14 (keyboard close path): ⌘⌃W closes only the focused pane; the
# group degrades 4 -> 3 -> 2 -> 1 with a valid layout and no survivor reloads.
REMAIN="PaneA,PaneB,PaneC,PaneD"
for step in 3 2 1; do
  focus_pane 1; T=$(q focused); key 13 cmd ctrl; sleep 3; snap "closed-to-$step"
  REMAIN=$(without "$REMAIN" "$T"); GOT=$(q layout)
  echo "⌘⌃W on $T: layout=$GOT visible=$(q visible) all=$(q all)" >> "$OUT/steps.txt"
  case $step in
    3) OKLAYOUT='three-columns|three-rows|main-vertical|main-horizontal' ;;
    2) OKLAYOUT='two-columns|two-rows' ;;
    1) OKLAYOUT='single' ;;
  esac
  check "closePaneTo$step" '[ -n "$T" ] && echo "$GOT" | grep -q -x -E "$OKLAYOUT" && [ "$(q visible)" = "$REMAIN" ] && [ "$(q kept)" = "$REMAIN" ] && ! q all | tr , "\n" | grep -q -x "$T"'
done
LAST=$REMAIN
# The survivor is an ordinary sidebar row again, no one-member group.
check lastPaneIsNormalRow '! sidebar_group collapsed "$LAST"'

# SPLIT-13 (leave split): ⌘⌃0 separates; both pages stay open, nothing reloads.
tab_menu_split e.html || fail_setup "Tab menu split did not load e.html"
mark_all; snap split-e
[ "$(q layout)" = two-columns ] || fail_setup "no two-pane split with PaneE ($(q layout))"
key 29 cmd ctrl; sleep 2; snap unsplit-key
check unsplitShortcutKeepsBothTabs '[ "$(q layout)" = single ] && q all | tr , "\n" | grep -q -x "$LAST" && q all | tr , "\n" | grep -q -x PaneE && [ -n "$(q kept)" ]'
check unsplitRowsSeparated '! sidebar_group unsplit-key "$LAST" PaneE'
# SPLIT-14 / SPLIT-13 (menu path): "Ansichten trennen" from the row menu.
tab_menu_split f.html || fail_setup "Tab menu split did not load f.html"
mark_all; snap split-f
[ "$(q layout)" = two-columns ] || fail_setup "no two-pane split with PaneF ($(q layout))"
PARTNER=$(q visible -PaneF)
row_menu PaneF "Ansichten trennen"; snap unsplit-menu
check unsplitMenuKeepsBothTabs '[ -n "$PARTNER" ] && [ "$(q layout)" = single ] && q all | tr , "\n" | grep -q -x PaneF && q all | tr , "\n" | grep -q -x "$PARTNER" && [ -n "$(q kept)" ]'
check unsplitMenuRowsSeparated '! sidebar_group unsplit-menu PaneF "$PARTNER"'

# DoD row 9 / SPLIT-22 (local part): a split in a second window belongs to
# that window; window 1 keeps its tabs, the pane shortcuts stay inside it.
W1=$(q win Solo); BEFORE1=$(q all)
key 45 cmd; sleep 3
open_url "$SITE/g.html"
popup_from g.html h.html || fail_setup "popup overlay for h.html did not open"
popup_to_split; snap second-window
WG=$(q win PaneG); WH=$(q win PaneH)
echo "windows: first=$W1 g=$WG h=$WH" >> "$OUT/steps.txt"
check secondWindowSplit '[ -n "$WG" ] && [ "$WG" = "$WH" ] && [ "$WG" != "$W1" ] && [ "$(q visible win=$WG)" = "PaneG,PaneH" ] && [ "$(q layout win=$WG)" = two-columns ]'
check secondWindowOneRow 'sidebar_group second-window PaneG PaneH'
check firstWindowUntouched '[ "$(q all | tr , "\n" | grep -v -x -E "PaneG|PaneH" | paste -sd, -)" = "$BEFORE1" ] && [ "$(q win Solo)" = "$W1" ]'
mark_all
focus_pane 2; F2=$(q focused win=$WG); focus_pane 1; F1=$(q focused win=$WG)
check paneShortcutsInSecondWindow '[ -n "$F1" ] && [ -n "$F2" ] && [ "$F1" != "$F2" ] && [ "$(q kept win=$WG)" = "PaneG,PaneH" ]'

# SPLIT-21 (windows) / SPLIT-22: after ⌘Q both windows return; the split is
# back in the second window, window 1 has no split, no phantom tabs.
URLS=$(urls); echo "before quit: $URLS" >> "$OUT/steps.txt"
quit; sleep 2
launch; sleep 4; snap relaunch
RG=$(q win PaneG); RH=$(q win PaneH); RS=$(q win Solo)
echo "after restart: urls=$(urls) windows g=$RG h=$RH solo=$RS" >> "$OUT/steps.txt"
check restoredSecondWindowSplit '[ -n "$RG" ] && [ "$RG" = "$RH" ] && [ -n "$RS" ] && [ "$RS" != "$RG" ] && [ "$(q layout win=$RG hidden)" = two-columns ]'
check restoredWithoutPhantomTabs '[ "$(urls)" = "$URLS" ]'
check restoredSecondWindowOneRow 'sidebar_group relaunch PaneG PaneH'
$AX dump $PID 40 > "$OUT/ax-final.txt"
finish; quit
