#!/bin/bash
# shellcheck disable=SC2034  # variables are read inside check's eval conditions
# usage: split-matrix-journey.sh <App.app> <outdir>
# DoD 5 / gap row 9 on the installed candidate, without HID drag: two-,
# three- and four-pane splits and the 2×2 grid, every layout by menu and by
# ⌘⌃L, divider keys, pane focus and reorder shortcuts, reload bound to the
# active pane, a refused fifth pane, one sidebar row per split, and the 2×2
# group across ⌘Q: first Ahoi's own "continue" start without DevTools (AX),
# then a start with DevTools through Chromium's restore-last-session
# (geometry, ratios, focus). Entry points and label sources:
# split_journey_lib.sh. Close/unsplit/second window: split-lifecycle-journey.sh.
# PID-scoped AX, HID keys, CDP reads. Results: <outdir>/verdict.json.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}
PORT=9407; SITE_PORT=${AHOI_E2E_SITE_PORT:-8825}
. "$S/split_journey_lib.sh"

launch "$SITE/solo.html"
open_url "$SITE/a.html"
snap base; read -r W0 H0 < <(q size PaneA); echo "baseline ${W0}x${H0}" >> "$OUT/steps.txt"
[ "$W0" -gt 0 ] && [ "$(q visible)" = PaneA ] || fail_setup "PaneA is not the only visible page"

# SPLIT-07 (non-drag entry point), SPLIT-02 default: the Tab menu splits the
# active page into two columns next to a new pane.
tab_menu_split b.html || fail_setup "Tab menu split did not load b.html"
mark_all; snap two
check twoPaneSplitCreated '[ "$(q visible)" = "PaneA,PaneB" ] && [ "$(q layout)" = two-columns ]'
# DoD 5 / SPLIT-34 (sidebar mirrors the group): one sidebar row, Solo outside.
check sidebarOneRowTwoPanes 'sidebar_group two PaneA PaneB'

# SPLIT-08 (menu path): the sidebar row menu switches rows/columns, no reload.
row_menu PaneA "Gestapelte Tabs anzeigen"; snap rows-menu
check twoRowsViaRowMenu '[ "$(q layout)" = two-rows ] && [ "$(q kept)" = "PaneA,PaneB" ]'
row_menu PaneA "Nebeneinander anzeigen"; snap columns-menu
check twoColumnsViaRowMenu '[ "$(q layout)" = two-columns ] && [ "$(q kept)" = "PaneA,PaneB" ]'
# SPLIT-08 / SPLIT-13: ⌘⌃L toggles the same without reload.
key 37 cmd ctrl; sleep 2; snap rows-key; L1=$(q layout)
key 37 cmd ctrl; sleep 2; snap columns-key; L2=$(q layout)
echo "⌘⌃L: $L1 then $L2" >> "$OUT/steps.txt"
check twoPaneLayoutShortcut '[ "$L1" = two-rows ] && [ "$L2" = two-columns ] && [ "$(q kept)" = "PaneA,PaneB" ]'

# SPLIT-12 (keyboard part) / SPLIT-13: ⌘⌃→ and ⌘⌃← move the divider by a
# visible step each way (0.5 -> 0.55 -> 0.45 -> 0.5).
key 124 cmd ctrl; sleep 2; snap divider-right; D1=$(q diff PaneA PaneB)
key 123 cmd ctrl; sleep 1; key 123 cmd ctrl; sleep 2; snap divider-left; D2=$(q diff PaneA PaneB)
echo "divider width difference: $D1 then $D2" >> "$OUT/steps.txt"
check dividerShortcutsResize 'python3 -c "import sys;a,b=map(float,sys.argv[1:]);sys.exit(0 if abs(a)>=0.06 and abs(b)>=0.06 and a*b<0 else 1)" "$D1" "$D2"'
key 124 cmd ctrl; sleep 1

# SPLIT-09 (non-drag analog) / SPLIT-01: the popup overlay adds a third pane
# to the opener's split; three columns by default, existing panes unreloaded.
focus_pane 1
popup_from "$(file_of "$(q focused)")" c.html || fail_setup "popup overlay for c.html did not open"
popup_to_split; snap three
check threePaneSplitCreated '[ "$(q visible)" = "PaneA,PaneB,PaneC" ] && [ "$(q layout)" = three-columns ] && [ "$(q kept)" = "PaneA,PaneB" ]'
check sidebarOneRowThreePanes 'sidebar_group three PaneA PaneB PaneC'
mark_all

# SPLIT-10 / SPLIT-13: ⌘⌃L walks all six three-pane layouts and wraps.
SEQ=""; for i in 1 2 3 4 5 6; do key 37 cmd ctrl; sleep 2; snap "cycle3-$i"; SEQ="$SEQ $(q layout)"; done
echo "three-pane ⌘⌃L cycle:$SEQ" >> "$OUT/steps.txt"
check threePaneLayoutCycle '[ "$SEQ" = " three-rows main-vertical main-vertical main-horizontal main-horizontal three-columns" ] && [ "$(q kept)" = "PaneA,PaneB,PaneC" ]'
# SPLIT-10: the row menu's preset submenu applies each preset and checks it.
OK=true; PREV=""
for pair in "Drei Zeilen:three-rows" "Großes Pane links:main-vertical" "Großes Pane rechts:main-vertical" \
            "Großes Pane oben:main-horizontal" "Großes Pane unten:main-horizontal" "Drei Spalten:three-columns"; do
  ITEM=${pair%%:*}; WANT=${pair##*:}
  split_menu "$ITEM" "$PREV" || { OK=false; echo "pane menu without $ITEM" >> "$OUT/steps.txt"; continue; }
  [ -n "$PREV" ] && [ "$CHECKED" != true ] && { OK=false; echo "$PREV not checked" >> "$OUT/steps.txt"; }
  snap "preset-$WANT"; GOT=$(q layout); echo "preset $ITEM -> $GOT" >> "$OUT/steps.txt"
  [ "$GOT" = "$WANT" ] || OK=false; PREV=$ITEM
done
split_menu "" "$PREV" && [ "$CHECKED" = true ] || OK=false
check threePaneMenuPresets '$OK && [ "$(q kept)" = "PaneA,PaneB,PaneC" ]'

# SPLIT-13 / SPLIT-18: ⌘⌃1…3 reach every pane once, without reloading.
ORDER=""; for n in 1 2 3; do focus_pane $n; ORDER="$ORDER $(q focused)"; done
echo "pane order:$ORDER" >> "$OUT/steps.txt"
check paneFocusShortcuts '[ "$(echo $ORDER | tr " " "\n" | sort -u | paste -sd, -)" = "PaneA,PaneB,PaneC" ] && [ "$(q kept)" = "PaneA,PaneB,PaneC" ]'
# SPLIT-11 / SPLIT-13: ⌘⌃⇧→ moves pane 1 to place 2; URLs, drafts, state stay.
set -- $ORDER; FIRST=${1:-}; SECOND=${2:-}
focus_pane 1; key 124 cmd ctrl shift; sleep 2
focus_pane 2; NOW2=$(q focused); focus_pane 1; NOW1=$(q focused)
echo "after reorder: pane1=$NOW1 pane2=$NOW2 (was $FIRST $SECOND)" >> "$OUT/steps.txt"
check paneReorderShortcut '[ -n "$FIRST" ] && [ "$NOW2" = "$FIRST" ] && [ "$NOW1" = "$SECOND" ] && [ "$(q kept)" = "PaneA,PaneB,PaneC" ] && [ "$(q values)" = "PaneA=PaneA-draft,PaneB=PaneB-draft,PaneC=PaneC-draft" ]'
# SPLIT-18 / SPLIT-37 (reload part): ⌘R reloads only the focused pane.
focus_pane 2; T=$(q focused); key 15 cmd; sleep 3; snap reload
check reloadTargetsActivePane '[ -n "$T" ] && [ "$(q visible)" = "PaneA,PaneB,PaneC" ] && [ "$(q kept)" = "$(without PaneA,PaneB,PaneC "$T")" ]'
mark_all

# SPLIT-33 (non-drag analog) / SPLIT-02: a fourth pane makes the 2×2 grid.
focus_pane 1
popup_from "$(file_of "$(q focused)")" d.html || fail_setup "popup overlay for d.html did not open"
popup_to_split; snap four
check fourPaneGridCreated '[ "$(q visible)" = "PaneA,PaneB,PaneC,PaneD" ] && [ "$(q layout)" = four-grid ] && [ "$(q kept)" = "PaneA,PaneB,PaneC" ]'
check sidebarOneRowFourPanes 'sidebar_group four PaneA PaneB PaneC PaneD'
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
# The refusal is the status line (AXStaticText) after the press; up to
# build 58 the split button's own name carried the same sentence.
popup_to_split
waitax "AXStaticText \\| Dieser Split hat bereits vier Bereiche" 6 && REFUSED=true || REFUSED=false
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
quit; sleep 2; prefs_continue
launch "" nodevtools; sleep 4
check restoredOneRowProductStart 'sidebar_group relaunch PaneA PaneB PaneC PaneD'
AREAS=$($AX dump $PID 40 | grep -o -E "AXWebArea \| Pane[A-D]" | sort -u | wc -l | tr -d " ")
check restoredFourPanesProductStart '[ "$AREAS" = 4 ]'
quit; sleep 2
launch; sleep 4; snap relaunch
NOWSHARES=$(q shares); echo "after restart: focus=$(q focused) shares=$NOWSHARES urls=$(urls)" >> "$OUT/steps.txt"
check restoredMembershipAndLayout '[ "$(q visible)" = "PaneA,PaneB,PaneC,PaneD" ] && [ "$(q layout)" = four-grid ]'
check restoredWithoutPhantomTabs '[ "$(urls)" = "$URLS" ]'
check restoredRatios 'python3 -c "import sys
a=dict(x.split(\":\") for x in sys.argv[1].split());b=dict(x.split(\":\") for x in sys.argv[2].split())
v=[float(x) for x in a.values()] or [0]
sys.exit(0 if max(v)-min(v)>=0.05 and a.keys()==b.keys() and all(abs(float(a[k])-float(b[k]))<0.02 for k in a) else 1)" "$SHARES" "$NOWSHARES"'
check restoredFocusedPane '[ -n "$FOCUS" ] && [ "$(q focused)" = "$FOCUS" ]'
$AX dump $PID 40 > "$OUT/ax-final.txt"
finish; quit
