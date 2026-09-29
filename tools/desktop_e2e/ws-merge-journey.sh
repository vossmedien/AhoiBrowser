#!/bin/bash
# usage: ws-merge-journey.sh <App.app> <outdir>
# ADR 0012 section 1 on the installed candidate: merging Workspaces through
# the Workspace menu's "Zusammenführen mit" and the command bar's
# "Zusammenführen mit: <Workspace>" (both open the same dialog).
#   WS-MERGE-01  shared "Quelle" (a folder with a child, a split, a running
#                tab) into shared "Ziel": one folder "Quelle" at the end of
#                Ziel, folder and split keep their IDs, the tab keeps its
#                WebContents (same DevTools target, no reload), Quelle is
#                gone; the same after a relaunch.
#   WS-MERGE-02  unchecking "Als Ordner „Flach“ ablegen" ("Ohne Ordner")
#                appends Flach's roots flat, in order.
#   WS-MERGE-03  ⌘Z restores Quelle with the same ID and name, its nodes and
#                its tabs; the wrapper folder is gone.
#   WS-MERGE-04  "Kunde" with own website sessions into Ziel: the dialog says
#                there is no undo, the open pages are asked (before-unload)
#                and close, the saved pages arrive, no cookie of Kunde in
#                Ziel, no undo receipt, binding and partition gone.
#   WS-MERGE-05  a hard kill right after confirming leaves all or nothing.
# Tree state is read from a copy of the "Ahoi Tab Tree" SQLite database;
# labels are the German strings of the overlay (StructureText) and of
# generated_resources.grd/_de.xtb. PID-scoped AX, HID keys, CDP reads.
# Written for the owner's run; not run by its author (no GUI on that host).
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9421
SITE_PORT=${AHOI_E2E_SITE_PORT:-8841}
# The split helpers (tab picker split, sidebar row menu, split group check,
# launch/key/type_in/waitax/record/finish) are the proven ones.
. "$S/split_journey_lib.sh"
printf '<title>Login</title><script>document.cookie="acct=kunde; max-age=3600; path=/"</script>logged in' > $P-site/login.html
printf '<title>Unload</title><script>addEventListener("beforeunload",e=>{e.preventDefault();e.returnValue=""})</script><button id=b style="width:400px;height:300px">tap</button>' > $P-site/unload.html
printf '<title>Check</title>check' > $P-site/check.html
DB="$P/Default/Ahoi Tab Tree"
tree() { python3 "$S/tab_tree_state.py" "$DB" "$@" 2>> "$OUT/tree-errors.txt"; }
tree_dump() { tree dump > "$OUT/tree-$1.json"; }
tabs() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(json.dumps(sorted([t["url"] for t in json.load(sys.stdin) if t["type"]=="page"])))'; }
target_of() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(next((t["id"] for t in json.load(sys.stdin) if t["type"]=="page" and t["url"].endswith(sys.argv[1])),""))' "$1"; }
eval_in() { CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True}))' "$2")" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'; }
# Escape before AXShowMenu goes straight to the process: an HID Escape
# arrives asynchronously and would close the menu just opened by AX.
menu() { # <active workspace name> <menu item regex>
  $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
  for attempt in 1 2 3 4; do
    $AX press $PID "$1, Workspace wechseln" AXShowMenu >> "$OUT/steps.txt"
    waitax "AXMenuItem \\| $2" 4 14 && return 0
    $AX key $PID 53 >> "$OUT/steps.txt"; sleep 2
  done
  return 1
}
menuitem() { # <workspace name>; menu items carry a non-shared level in the title
  $AX dump $PID 14 | grep -oE "AXMenuItem \| $1( – [^|]*)? \|" | head -1 | sed -E 's/^AXMenuItem \| //; s/ \|$//'
}
newws() { # <active> <name> <level radio label or "">
  menu "$1" "Neuer Workspace…" || fail_setup "workspace menu did not open for $2"
  $AX press $PID "Neuer Workspace…" >> "$OUT/steps.txt"
  waitax "AXTextField \\| Workspace-Name" 8 14 || fail_setup "workspace dialog for $2 did not open"
  $AX setvalue $PID "Workspace-Name" "$2" >> "$OUT/steps.txt"; sleep 1
  if [ -n "$3" ]; then $AX press $PID "AXRadioButton:$3" >> "$OUT/steps.txt"; sleep 1; fi
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
# "Neue Gruppe mit diesem Tab…" on a sidebar row, named in the group dialog.
group_with() { # <row title> <group name>
  row_menu "$1" "Neue Gruppe mit diesem Tab…" || fail_setup "no group item for $1"
  waitax "AXTextField \\| Gruppenname" 8 14 || fail_setup "group dialog for $2 did not open"
  $AX setvalue $PID "Gruppenname" "$2" >> "$OUT/steps.txt"; sleep 1
  $AX press $PID "AXButton:Erstellen" >> "$OUT/steps.txt"
  local end=$(( $(date +%s) + 8 ))
  while [ $(date +%s) -lt $end ] && [ -z "$(tree nodeid "title:$2")" ]; do sleep 1; done
  [ -n "$(tree nodeid "title:$2")" ] || fail_setup "group $2 not stored"
}
# Waits for the merge dialog and keeps its full text as evidence.
merge_dialog() { # <evidence name>
  waitax "AXButton \\| Zusammenführen" 10 14 || return 1
  AHOI_AX_VALUE_MAX=600 $AX dump $PID 14 > "$OUT/ax-merge-dialog-$1.txt"
}
# Workspace menu > "Zusammenführen mit" > <target>. The submenu repeats the
# switcher's names, so the target is pressed inside the submenu only.
menu_merge() { # <source> <target>
  menu "$1" "Zusammenführen mit" || return 1
  $AX press $PID "AXMenuItem:Zusammenführen mit" >> "$OUT/steps.txt"; sleep 1
  $AX pressin $PID "AXMenuItem:Zusammenführen mit" "AXMenuItem:$2" >> "$OUT/steps.txt" || {
    $AX dump $PID 16 > "$OUT/ax-merge-submenu-missing.txt"; $AX key $PID 53; return 1; }
  merge_dialog "menu-$1"
}
# Command bar: "Zusammenführen mit: <target>" for the window's Workspace.
bar_merge() { # <source> <target>
  local want="Zusammenführen mit: $2" opened=0 order idx i
  for attempt in 1 2 3; do
    $AX activate $PID >> "$OUT/steps.txt"; sleep 1; key 17 cmd
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 14 && { opened=1; break; }
  done
  [ $opened = 1 ] || return 1
  sleep 1; type_in "$want" || return 1
  waitax "AX[A-Za-z]+ \\| $want" 8 40 || { $AX dump $PID 40 > "$OUT/ax-bar-merge-$1.txt"; key 53; return 1; }
  sleep 0.5; $AX dump $PID 40 > "$OUT/ax-bar-merge-$1.txt"
  # Item rows come before the search row that repeats the typed text.
  order=$(grep -o -E "AX[A-Za-z]+ \| (Zusammenführen mit|In Workspace verschieben): [^|]*" "$OUT/ax-bar-merge-$1.txt" \
    | sed -E 's/^AX[A-Za-z]+ \| //; s/ *$//' | awk '!seen[$0]++')
  idx=$(printf '%s\n' "$order" | grep -n -x -F "$want" | head -1 | cut -d: -f1)
  [ -n "$idx" ] || { key 53; return 1; }
  i=1; while [ $i -lt $idx ]; do $AX key $PID 125 >> "$OUT/steps.txt"; sleep 0.3; i=$((i+1)); done
  key 36
  merge_dialog "bar-$1"
}
# The Workspace this window shows, from the switcher button's name.
here() { $AX dump $PID 14 | grep -o -E "[^|]+, Workspace wechseln" | head -1 | sed -E 's/^ *//; s/, Workspace wechseln$//'; }
# Exit 0 when <after> is <before> with at least one root appended.
appended() { case "$2" in "$1",?*) return 0 ;; ?*) [ -z "$1" ] ;; *) return 1 ;; esac; }
confirm_merge() { $AX press $PID "AXButton:Zusammenführen" >> "$OUT/steps.txt"; }
until_state() { # <workspace id> <state prefix> <seconds>
  local end=$(( $(date +%s) + $3 ))
  while [ $(date +%s) -lt $end ]; do tree wsstate "$1" | grep -q "^$2" && return 0; sleep 1; done
  return 1
}
# The before-unload question of a group close is a native alert
# ("Website verlassen?"); CDP answers only a page dialog.
unload_prompt() { # <page url substring> <accept|cancel>
  local button=Abbrechen; [ "$2" = accept ] && button=Verlassen
  if waitax "AXStaticText \\| Website verlassen" 8 14; then
    $AX dump $PID 14 > "$OUT/ax-unload-prompt-$2.txt"
    $AX press $PID "$button" >> "$OUT/steps.txt"; return 0
  fi
  CDP "$1" Page.handleJavaScriptDialog "{\"accept\":$([ "$2" = accept ] && echo true || echo false)}" > "$OUT/dialog-$2.json"
  ! grep -q '"error"' "$OUT/dialog-$2.json"
}
activate_page() { # <url substring>; sticky user activation for before-unload
  for attempt in 1 2 3; do
    osascript -e 'tell application id "app.ahoibrowser.AhoiBrowser" to activate' >/dev/null 2>&1
    CDP "$1" Page.bringToFront '{}' >/dev/null; sleep 1
    CDP "$1" Input.dispatchMouseEvent '{"type":"mousePressed","x":100,"y":100,"button":"left","clickCount":1}' >/dev/null
    CDP "$1" Input.dispatchMouseEvent '{"type":"mouseReleased","x":100,"y":100,"button":"left","clickCount":1}' >/dev/null
    [ "$(eval_in "$1" 'navigator.userActivation.hasBeenActive')" = True ] && return 0
    [ "$(eval_in "$1" 'String(navigator.userActivation.hasBeenActive)')" = true ] && return 0
  done
  return 1
}

launch about:blank
waitax "Inbox, Workspace wechseln" 20 14 || fail_setup "Inbox not shown after launch"
# ---- Setup: Ziel with one page, Quelle with folder, split and running tab.
newws Inbox Ziel ""; open_url "$SITE/e.html"
newws Ziel Quelle ""
open_url "$SITE/a.html"; group_with PaneA Projekt
open_url "$SITE/b.html"; tab_menu_split c.html || fail_setup "split b+c not created"
sidebar_group split-before PaneB PaneC || fail_setup "split b+c is not one sidebar row"
open_url "$SITE/d.html"
mark_all
SRC=$(tree wsid Quelle); DST=$(tree wsid Ziel)
FOLDER=$(tree nodeid title:Projekt); NODE_A=$(tree nodeid /a.html)
NODE_B=$(tree nodeid /b.html); NODE_C=$(tree nodeid /c.html); NODE_D=$(tree nodeid /d.html)
TARGET_D=$(target_of /d.html); ORIGIN_D=$(eval_in /d.html 'String(performance.timeOrigin)')
echo "src=$SRC dst=$DST folder=$FOLDER a=$NODE_A b=$NODE_B c=$NODE_C d=$NODE_D target_d=$TARGET_D" >> "$OUT/steps.txt"
[ -n "$SRC" ] && [ -n "$DST" ] && [ -n "$FOLDER" ] && [ -n "$NODE_B" ] && [ -n "$NODE_D" ] && [ -n "$TARGET_D" ] \
  || fail_setup "setup ids missing"
ROOTS_SRC=$(tree roots "$SRC"); ROOTS_DST=$(tree roots "$DST"); tree_dump before-merge01
echo "roots Quelle=$ROOTS_SRC Ziel=$ROOTS_DST" >> "$OUT/steps.txt"

# ---- WS-MERGE-01 through the Workspace menu, default "as folder".
menu_merge Quelle Ziel || fail_setup "merge dialog from the Workspace menu did not open"
F="$OUT/ax-merge-dialog-menu-Quelle.txt"
grep -q "offene Tabs laufen weiter" "$F" && grep -q "Rückgängig stellt ihn wieder her" "$F" \
  && record merge01DialogNamesUndo true || record merge01DialogNamesUndo false
grep -q "AXCheckBox | Als Ordner „Quelle“ ablegen" "$F" && record merge01FolderOptionShown true || record merge01FolderOptionShown false
confirm_merge
until_state "$SRC" "gone|$DST" 10 && record merge01SourceGone true || record merge01SourceGone false
sleep 2; tree_dump after-merge01
# Saved roots go into the folder; open temporary rows stay flat next to it
# (TabTreeStore::MergeWorkspace). Both come after Ziel's own roots.
WRAP_ID=$(tree nodeid title:Quelle); ROOTS_AFTER=$(tree roots "$DST")
echo "roots Ziel after merge01=$ROOTS_AFTER" >> "$OUT/steps.txt"
appended "$ROOTS_DST" "$ROOTS_AFTER" && echo ",${ROOTS_AFTER#"$ROOTS_DST"}," | grep -q ",Quelle," \
  && [ "$(tree nodeinfo "$WRAP_ID" | cut -d' ' -f1-3)" = "$DST - 0" ] \
  && record merge01OneFolderAtEnd true || record merge01OneFolderAtEnd false
[ "$(tree nodeinfo "$FOLDER" | cut -d' ' -f1-2)" = "$DST $WRAP_ID" ] && [ "$(tree nodeinfo "$NODE_A" | cut -d' ' -f1-2)" = "$DST $FOLDER" ] \
  && record merge01FolderKeepsIdAndChild true || record merge01FolderKeepsIdAndChild false
for n in "$NODE_B" "$NODE_C" "$NODE_D"; do tree nodeinfo "$n"; done > "$OUT/merge01-moved.txt"
[ "$(cut -d' ' -f1 "$OUT/merge01-moved.txt" | sort -u)" = "$DST" ] && record merge01PagesInTarget true || record merge01PagesInTarget false
[ "$(target_of /d.html)" = "$TARGET_D" ] && [ "$(eval_in /d.html 'String(performance.timeOrigin)')" = "$ORIGIN_D" ] \
  && [ "$(eval_in /d.html 'window.__m||""')" = kept ] && record merge01TabKeepsRunning true || record merge01TabKeepsRunning false
sleep 2; [ "$(here)" = Ziel ] || switchws "$(here)" Ziel
sidebar_group split-after-merge01 PaneB PaneC && [ "$(tree nodeid /b.html)" = "$NODE_B" ] \
  && record merge01SplitIntact true || record merge01SplitIntact false
menu "$(here)" "Neuer Workspace…"; $AX dump $PID 14 > "$OUT/ax-menu-after-merge01.txt"; $AX key $PID 53
{ ! grep -q "AXMenuItem | Quelle" "$OUT/ax-menu-after-merge01.txt"; } && record merge01NotInSwitcher true || record merge01NotInSwitcher false

# ---- WS-MERGE-03: ⌘Z restores Quelle with its ID, name, nodes and tabs.
key 6 cmd
until_state "$SRC" "live|Quelle" 10 && record merge03SameIdAndName true || record merge03SameIdAndName false
sleep 2; tree_dump after-undo03
[ "$(tree roots "$SRC")" = "$ROOTS_SRC" ] && [ "$(tree roots "$DST")" = "$ROOTS_DST" ] && [ -z "$(tree nodeid title:Quelle)" ] \
  && record merge03NodesBack true || record merge03NodesBack false
[ "$(tree nodeinfo "$FOLDER" | cut -d' ' -f1-2)" = "$SRC -" ] && [ "$(tree nodeinfo "$NODE_D" | cut -d' ' -f1)" = "$SRC" ] \
  && [ "$(target_of /d.html)" = "$TARGET_D" ] && record merge03TabsBack true || record merge03TabsBack false
menu "$(here)" "Quelle" && $AX dump $PID 14 > "$OUT/ax-menu-after-undo03.txt"; $AX key $PID 53
grep -q "AXMenuItem | Quelle" "$OUT/ax-menu-after-undo03.txt" 2>/dev/null && record merge03InSwitcher true || record merge03InSwitcher false

# ---- WS-MERGE-01 again, this time from the command bar (ADR 0012 entry).
[ "$(here)" = Quelle ] || switchws "$(here)" Quelle
bar_merge Quelle Ziel && record barMergeOpensDialog true || record barMergeOpensDialog false
grep -q "Rückgängig stellt ihn wieder her" "$OUT/ax-merge-dialog-bar-Quelle.txt" 2>/dev/null \
  && record barMergeSameDialog true || record barMergeSameDialog false
confirm_merge
until_state "$SRC" "gone|$DST" 10 && [ -n "$(tree nodeid title:Quelle)" ] \
  && [ "$(tree nodeinfo "$FOLDER" | cut -d' ' -f1)" = "$DST" ] \
  && record barMergeMergesIntoFolder true || record barMergeMergesIntoFolder false

# ---- WS-MERGE-02: "Ohne Ordner" appends flat, in order.
newws "$(here)" Flach ""
open_url "$SITE/f.html"; open_url "$SITE/g.html"; open_url "$SITE/h.html"; group_with PaneH Hgruppe
FLAT=$(tree wsid Flach); ROOTS_FLAT=$(tree roots "$FLAT"); ROOTS_DST=$(tree roots "$DST")
echo "roots Flach=$ROOTS_FLAT Ziel=$ROOTS_DST" >> "$OUT/steps.txt"
bar_merge Flach Ziel || fail_setup "merge dialog for Flach did not open"
$AX press $PID "AXCheckBox:Als Ordner „Flach“ ablegen" >> "$OUT/steps.txt"; sleep 1
confirm_merge
until_state "$FLAT" "gone|$DST" 10 && record merge02SourceGone true || record merge02SourceGone false
sleep 2; tree_dump after-merge02
[ "$(tree roots "$DST")" = "$ROOTS_DST,$ROOTS_FLAT" ] && [ -z "$(tree nodeid title:Flach)" ] \
  && record merge02FlatInOrder true || record merge02FlatInOrder false

# ---- WS-MERGE-04: own website sessions into the shared Ziel.
newws "$(here)" Kunde "Eigene Website-Sitzungen"
KUNDE=$(tree wsid Kunde)
open_url "$SITE/login.html"; [ "$(eval_in login.html document.cookie)" = "acct=kunde" ] || fail_setup "no login in Kunde"
group_with Login Konten
open_url "$SITE/unload.html"; activate_page unload.html || echo "info: unload page without activation" >> "$OUT/steps.txt"
NODE_LOGIN=$(tree nodeid /login.html); UNDO_BEFORE=$(tree undocount)
bar_merge Kunde Ziel || fail_setup "merge dialog for Kunde did not open"
F="$OUT/ax-merge-dialog-bar-Kunde.txt"
grep -q "Das lässt sich nicht rückgängig machen" "$F" && grep -q "Anmeldungen ziehen nicht mit" "$F" \
  && record merge04DialogSaysNoUndo true || record merge04DialogSaysNoUndo false
confirm_merge
unload_prompt unload.html accept && record merge04PagesAsked true || record merge04PagesAsked false
until_state "$KUNDE" "gone|$DST" 15 && record merge04SourceGone true || record merge04SourceGone false
sleep 4; T=$(tabs); echo "after merge04 $T" >> "$OUT/tabs.txt"; tree_dump after-merge04
{ ! echo "$T" | grep -q 'login.html\|unload.html'; } && record merge04OwnPagesClosed true || record merge04OwnPagesClosed false
[ "$(tree nodeinfo "$NODE_LOGIN" | cut -d' ' -f1)" = "$DST" ] && [ -n "$(tree nodeid title:Konten)" ] \
  && record merge04SavedPagesArrive true || record merge04SavedPagesArrive false
[ "$(tree undocount)" = "$UNDO_BEFORE" ] && record merge04NoUndoReceipt true || record merge04NoUndoReceipt false
[ "$(here)" = Ziel ] || switchws "$(here)" Ziel
open_url "$SITE/check.html"; [ -z "$(eval_in check.html document.cookie)" ] \
  && record merge04NoCookieInTarget true || record merge04NoCookieInTarget false

# ---- WS-MERGE-05: kill hard right after confirming; all or nothing.
newws "$(here)" Absturz ""
open_url "$SITE/solo.html"; group_with Solo Kiste
CRASH=$(tree wsid Absturz); KISTE=$(tree nodeid title:Kiste); ROOTS_CRASH=$(tree roots "$CRASH"); ROOTS_DST=$(tree roots "$DST")
menu_merge Absturz Ziel || fail_setup "merge dialog for Absturz did not open"
confirm_merge; sleep 0.2; kill -9 $PID; sleep 3
echo "killed $PID right after confirming the Absturz merge" >> "$OUT/run.txt"

# ---- Relaunch: WS-MERGE-01/02/04 persist, WS-MERGE-05 is atomic.
launch; sleep 6; tree_dump after-relaunch
[ "$(tree integrity)" = ok ] && record merge05DatabaseIntact true || record merge05DatabaseIntact false
STATE=$(tree wsstate "$CRASH"); echo "Absturz after kill: $STATE" >> "$OUT/steps.txt"
case "$STATE" in
  live\|*) [ "$(tree roots "$CRASH")" = "$ROOTS_CRASH" ] && [ "$(tree roots "$DST")" = "$ROOTS_DST" ] ;;
  gone\|*) [ -z "$(tree roots "$CRASH")" ] && appended "$ROOTS_DST" "$(tree roots "$DST")" \
            && [ "$(tree nodeinfo "$KISTE" | cut -d' ' -f1)" = "$DST" ] ;;
  *) false ;;
esac && record merge05AllOrNothing true || record merge05AllOrNothing false
[ "$(tree wsstate "$SRC")" = "gone|$DST" ] && [ "$(tree nodeinfo "$FOLDER" | cut -d' ' -f1)" = "$DST" ] \
  && [ -n "$(tree nodeid title:Quelle)" ] && record merge01PersistsAfterRelaunch true || record merge01PersistsAfterRelaunch false
[ "$(tree wsstate "$FLAT")" = "gone|$DST" ] && [ -z "$(tree nodeid title:Flach)" ] \
  && record merge02PersistsAfterRelaunch true || record merge02PersistsAfterRelaunch false
menu "$(here)" "Neuer Workspace…"; $AX dump $PID 14 > "$OUT/ax-menu-after-relaunch.txt"; $AX key $PID 53
{ ! grep -q -E "AXMenuItem \| (Quelle|Flach|Kunde)( |$)" "$OUT/ax-menu-after-relaunch.txt"; } \
  && record mergedWorkspacesStayGone true || record mergedWorkspacesStayGone false
quit
python3 - "$P/Default/Preferences" "$P/Default" > "$OUT/storage.json" <<'PY'
import json,os,sys
p=json.load(open(sys.argv[1])).get("ahoi",{}).get("session",{})
b=p.get("website_session_bindings",{}).get("workspaces",{})
own=[k for k,v in b.items() if v!="default"]
root=os.path.join(sys.argv[2],"Storage","ext","ahoi")
dirs=os.listdir(root) if os.path.isdir(root) else []
print(json.dumps({"ownBindings":own,"pending":p.get("website_session_pending_removals",[]),"partitionDirs":dirs}))
PY
python3 -c 'import json,sys;d=json.load(open(sys.argv[1]));sys.exit(0 if not d["ownBindings"] and not d["pending"] and not d["partitionDirs"] else 1)' "$OUT/storage.json" \
  && record merge04BindingAndPartitionGone true || record merge04BindingAndPartitionGone false
finish ""
