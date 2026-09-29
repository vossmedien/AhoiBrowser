#!/bin/bash
# usage: cmd-move-journey.sh <App.app> <outdir>
# CMD-MOVE-01 (ADR 0012 section 2) on the installed candidate: the command
# bar's "In Workspace verschieben: <Workspace>" moves the focused sidebar
# folder with all its children (a page and a subfolder) to the root of the
# chosen Workspace, keeps their IDs and the child's running tab, leaves the
# active page outside the folder where it is, is findable by typing the
# Workspace's name, and ⌘Z moves everything back. The folder is focused the
# keyboard way: the command bar's folder row reveals and selects it.
# Tree state is read from a copy of the "Ahoi Tab Tree" database. Labels:
# overlay strings (command_execution_adapter.cc) and
# generated_resources.grd/_de.xtb. Written for the owner's run; not run by
# its author (no GUI on that host).
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9423
SITE_PORT=${AHOI_E2E_SITE_PORT:-8843}
. "$S/split_journey_lib.sh"
DB="$P/Default/Ahoi Tab Tree"
tree() { python3 "$S/tab_tree_state.py" "$DB" "$@" 2>> "$OUT/tree-errors.txt"; }
tree_dump() { tree dump > "$OUT/tree-$1.json"; }
target_of() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(next((t["id"] for t in json.load(sys.stdin) if t["type"]=="page" and t["url"].endswith(sys.argv[1])),""))' "$1"; }
eval_in() { CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True}))' "$2")" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'; }
here() { $AX dump $PID 14 | grep -o -E "[^|]+, Workspace wechseln" | head -1 | sed -E 's/^ *//; s/, Workspace wechseln$//'; }
menu() { # <active workspace name> <menu item regex>
  $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
  for attempt in 1 2 3 4; do
    $AX press $PID "$1, Workspace wechseln" AXShowMenu >> "$OUT/steps.txt"
    waitax "AXMenuItem \\| $2" 4 14 && return 0
    $AX key $PID 53 >> "$OUT/steps.txt"; sleep 2
  done
  return 1
}
menuitem() { $AX dump $PID 14 | grep -oE "AXMenuItem \| $1( – [^|]*)? \|" | head -1 | sed -E 's/^AXMenuItem \| //; s/ \|$//'; }
newws() { # <active> <name>
  menu "$1" "Neuer Workspace…" || fail_setup "workspace menu did not open for $2"
  $AX press $PID "Neuer Workspace…" >> "$OUT/steps.txt"
  waitax "AXTextField \\| Workspace-Name" 8 14 || fail_setup "workspace dialog for $2 did not open"
  $AX setvalue $PID "Workspace-Name" "$2" >> "$OUT/steps.txt"; sleep 1
  $AX press $PID "Erstellen" >> "$OUT/steps.txt"
  waitax "$2, Workspace wechseln" 8 14 || fail_setup "workspace $2 not active"
  local end=$(( $(date +%s) + 6 ))
  while [ $(date +%s) -lt $end ] && $AX dump $PID 14 | grep -q "AXButton | Erstellen"; do sleep 1; done
}
switchws() { # <active> <target>
  menu "$1" "$2" || fail_setup "menu to switch to $2 did not open"
  $AX press $PID "$(menuitem "$2")" >> "$OUT/steps.txt"
  waitax "$2, Workspace wechseln" 8 14 || fail_setup "switch to $2 failed"
}
# A sidebar row's group item ("Neue Gruppe mit diesem Tab…" or, on a folder,
# "Neue Untergruppe…"), named in the group dialog.
group_from() { # <row title> <menu item> <group name>
  row_menu "$1" "$2" || fail_setup "no \"$2\" for $1"
  waitax "AXTextField \\| Gruppenname" 8 14 || fail_setup "group dialog for $3 did not open"
  $AX setvalue $PID "Gruppenname" "$3" >> "$OUT/steps.txt"; sleep 1
  $AX press $PID "AXButton:Erstellen" >> "$OUT/steps.txt"
  local end=$(( $(date +%s) + 8 ))
  while [ $(date +%s) -lt $end ] && [ -z "$(tree nodeid "title:$3")" ]; do sleep 1; done
  [ -n "$(tree nodeid "title:$3")" ] || fail_setup "group $3 not stored"
}
bar_open() {
  for attempt in 1 2 3; do
    $AX activate $PID >> "$OUT/steps.txt"; sleep 1; key 17 cmd
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 14 && { sleep 1; return 0; }
  done
  return 1
}
# Types <query> and accepts the row titled <row>; item rows come before the
# search row that repeats the typed text, and the index counts only rows
# matching <family> (a regex of titles), which rank together.
bar_pick() { # <evidence> <query> <row> <family regex>
  local order idx i
  bar_open || return 1
  type_in "$2" || { key 53; return 1; }
  waitax "AX[A-Za-z]+ \\| $3( \\||$)" 8 40 || { $AX dump $PID 40 > "$OUT/ax-bar-$1.txt"; key 53; return 1; }
  sleep 0.5; $AX dump $PID 40 > "$OUT/ax-bar-$1.txt"
  order=$(grep -o -E "AX[A-Za-z]+ \| ($4)( \||$)" "$OUT/ax-bar-$1.txt" \
    | sed -E 's/^AX[A-Za-z]+ \| //; s/ \|$//; s/ *$//' | awk '!seen[$0]++')
  idx=$(printf '%s\n' "$order" | grep -n -x -F "$3" | head -1 | cut -d: -f1)
  [ -n "$idx" ] || { key 53; return 1; }
  i=1; while [ $i -lt $idx ]; do $AX key $PID 125 >> "$OUT/steps.txt"; sleep 0.3; i=$((i+1)); done
  key 36; sleep 2
}

launch about:blank
waitax "Inbox, Workspace wechseln" 20 14 || fail_setup "Inbox not shown after launch"
newws Inbox Ziel; open_url "$SITE/e.html"
switchws Ziel Inbox
open_url "$SITE/a.html"; group_from PaneA "Neue Gruppe mit diesem Tab…" Projekt
group_from Projekt "Neue Untergruppe…" Unter
# The active page stays outside the folder: without the folder's focus the
# command would move this page instead, which the checks would catch.
open_url "$SITE/b.html"; mark_all
INBOX=$(tree wsid Inbox); ZIEL=$(tree wsid Ziel)
FOLDER=$(tree nodeid title:Projekt); SUB=$(tree nodeid title:Unter)
NODE_A=$(tree nodeid /a.html); NODE_B=$(tree nodeid /b.html)
TARGET_A=$(target_of /a.html); ORIGIN_A=$(eval_in /a.html 'String(performance.timeOrigin)')
CHILDREN=$(tree children "$FOLDER"); ROOTS_INBOX=$(tree roots "$INBOX"); ROOTS_ZIEL=$(tree roots "$ZIEL")
echo "inbox=$INBOX ziel=$ZIEL folder=$FOLDER sub=$SUB a=$NODE_A b=$NODE_B children=$CHILDREN" >> "$OUT/steps.txt"
[ -n "$INBOX" ] && [ -n "$ZIEL" ] && [ -n "$FOLDER" ] && [ -n "$SUB" ] && [ -n "$NODE_A" ] && [ -n "$TARGET_A" ] \
  || fail_setup "setup ids missing"
[ "$(tree nodeinfo "$SUB" | cut -d' ' -f2)" = "$FOLDER" ] && [ "$(tree nodeinfo "$NODE_A" | cut -d' ' -f2)" = "$FOLDER" ] \
  || fail_setup "folder does not hold the page and the subfolder"
tree_dump before

# Findable by the Workspace's name after the command's words.
bar_open && type_in "Ziel" && waitax "AX[A-Za-z]+ \\| In Workspace verschieben: Ziel" 8 40 \
  && record moveFindableByWorkspaceName true || record moveFindableByWorkspaceName false
$AX dump $PID 40 > "$OUT/ax-bar-find-by-name.txt"; key 53; sleep 1
# The window's own Workspace is never offered as a target.
{ ! grep -q "In Workspace verschieben: Inbox" "$OUT/ax-bar-find-by-name.txt"; } \
  && record ownWorkspaceNotOffered true || record ownWorkspaceNotOffered false

# CMD-MOVE-01: focus the folder, then move it by command.
bar_pick reveal Projekt Projekt "Projekt" || fail_setup "folder row not offered"
bar_pick move "In Workspace verschieben: Ziel" "In Workspace verschieben: Ziel" \
  "In Workspace verschieben: [^|]*|Zusammenführen mit: [^|]*" || fail_setup "move row not offered"
sleep 2; tree_dump after-move
[ "$(tree nodeinfo "$FOLDER" | cut -d' ' -f1-2)" = "$ZIEL -" ] && record folderMovedToTargetRoot true || record folderMovedToTargetRoot false
[ "$(tree children "$FOLDER")" = "$CHILDREN" ] && [ "$(tree nodeinfo "$SUB" | cut -d' ' -f1-2)" = "$ZIEL $FOLDER" ] \
  && [ "$(tree nodeinfo "$NODE_A" | cut -d' ' -f1-2)" = "$ZIEL $FOLDER" ] && record childrenMovedWithIds true || record childrenMovedWithIds false
[ "$(target_of /a.html)" = "$TARGET_A" ] && [ "$(eval_in /a.html 'String(performance.timeOrigin)')" = "$ORIGIN_A" ] \
  && [ "$(eval_in /a.html 'window.__m||""')" = kept ] && record childTabKeepsRunning true || record childTabKeepsRunning false
[ "$(tree nodeinfo "$NODE_B" | cut -d' ' -f1)" = "$INBOX" ] && [ "$(here)" = Inbox ] \
  && record activePageStays true || record activePageStays false
case "$(tree roots "$ZIEL")" in "$ROOTS_ZIEL",*Projekt*|Projekt*) record folderAppendedInTarget true ;; *) record folderAppendedInTarget false ;; esac

# Undo: ⌘Z moves the folder and its children back.
key 6 cmd; sleep 3; tree_dump after-undo
[ "$(tree nodeinfo "$FOLDER" | cut -d' ' -f1-2)" = "$INBOX -" ] && [ "$(tree roots "$INBOX")" = "$ROOTS_INBOX" ] \
  && [ "$(tree roots "$ZIEL")" = "$ROOTS_ZIEL" ] && record undoMovesFolderBack true || record undoMovesFolderBack false
[ "$(tree nodeinfo "$SUB" | cut -d' ' -f1-2)" = "$INBOX $FOLDER" ] && [ "$(tree nodeinfo "$NODE_A" | cut -d' ' -f1-2)" = "$INBOX $FOLDER" ] \
  && [ "$(target_of /a.html)" = "$TARGET_A" ] && record undoRestoresChildren true || record undoRestoresChildren false
$AX dump $PID 40 > "$OUT/ax-final.txt"
finish ""; quit
