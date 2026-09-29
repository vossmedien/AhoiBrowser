#!/bin/bash
# usage: ws-cross-level-move-journey.sh <App.app> <outdir>
# PID-scoped AX + CDP journey for WS-ISO-05 (ADR 0011): a tab, a split and
# a folder move from the shared Workspace "Inbox" into the fully separated
# Workspace "Getrennt" (its own Profile) through "Verschieben nach" and the
# command bar. The confirmation says that sign-ins do not move along,
# Cancel changes nothing, the pages reopen by URL in the target without
# the login, a split arrives whole (never split across Profiles), Cmd+Z
# brings the tab back, and the moved structure survives a relaunch.
# Drag-and-drop between two Profiles' windows needs an HID drag, which
# axtool does not offer; it stays a manual CU step. Product default
# launch, disposable user data directory. Helpers follow
# ws-isolated-journey.sh; ports 9431/8851 are used by no other journey.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9431
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] \
  || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() {
  ioreg -c IOHIDSystem \
    | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'
}
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive" >&2
  exit 7
fi
busy() { lsof -nP -iTCP:$1 -sTCP:LISTEN >/dev/null 2>&1; }
if busy $PORT; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-wsmove-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8851}; mkdir -p $P-site
if busy $SITE_PORT; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
# Only ?set writes the cookie, so a reopened login.html shows whether the
# login came along.
cat > $P-site/login.html <<'HTML'
<title>Login</title><script>if (location.search == "?set")
document.cookie = "acct=gemeinsam; max-age=3600; path=/"</script>login
HTML
printf '<title>PaneA</title><h1>Pane A</h1>' > $P-site/a.html
printf '<title>PaneB</title><h1>Pane B</h1>' > $P-site/b.html
printf '<title>Notiz</title>note' > $P-site/note.html
printf '<title>Solo</title>solo' > $P-site/solo.html
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site \
  > "$OUT/site.log" 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT
SITE=http://127.0.0.1:$SITE_PORT
: > "$OUT/steps.txt"
# DevTools page list queries: urls, count <file>, id <file>.
cat > $P-q.py <<'PY'
import json, sys
pages = [t for t in json.load(sys.stdin) if t["type"] == "page"]
cmd, arg = sys.argv[1], (sys.argv[2:] or [""])[0]
hits = [t for t in pages if t["url"].endswith("/" + arg)]
if cmd == "urls":
    print(json.dumps(sorted(t["url"] for t in pages)))
elif cmd == "count":
    print(len(hits))
elif cmd == "id":
    print(hits[0]["id"] if hits else "")
PY
cat > $P-v.py <<'PY'
import json, sys
try:
    d = json.load(sys.stdin)
except ValueError:
    d = {}
print(d.get("windowId", d.get("result", {}).get("value", "")))
PY
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
pages() { curl -s http://127.0.0.1:$PORT/json | python3 $P-q.py "$@"; }
tabs() { pages urls; }
count_of() { pages count "$1"; }
target_id() { pages id "$1"; }
window_of() {
  CDP "$(target_id "$1")" Browser.getWindowForTarget '{}' 2>/dev/null \
    | python3 $P-v.py
}
js_of() { # <file> <expression>
  CDP "$(target_id "$1")" Runtime.evaluate \
    "{\"expression\":\"$2\",\"returnByValue\":true}" 2>/dev/null \
    | python3 $P-v.py
}
cookie_of() { js_of "$1" document.cookie; }
origin_of() { js_of "$1" "String(performance.timeOrigin)"; }
launch() {
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run \
    --no-default-browser-check --remote-debugging-port=$PORT about:blank \
    >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$P" >> "$OUT/run.txt"
  for i in $(seq 1 60); do
    curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break
    sleep 2
  done
  sleep 5; $AX activate $PID >> "$OUT/steps.txt"
}
quit() {
  key 12 cmd
  for i in $(seq 1 20); do
    kill -0 $PID 2>/dev/null || return 0; sleep 1
  done
  echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3
}
# Keys go through the HID event tap; hidkey refuses unless the app is
# frontmost, so bring it forward and retry instead of typing elsewhere.
key() {
  for attempt in 1 2 3 4 5; do
    $AX activate $PID >/dev/null; sleep 0.3
    $AX hidkey $PID "$@" >> "$OUT/steps.txt" && return 0
    sleep 1
  done
  echo "hidkey gave up: $*" >> "$OUT/steps.txt"; return 1
}
type_in() {
  for attempt in 1 2 3; do
    $AX type $PID "$1" >> "$OUT/steps.txt"; sleep 1
    $AX dump $PID 14 | grep "AXTextField" | grep -F -q -- "| $1" \
      && return 0
    echo "info: typed text missing, retyping" >> "$OUT/steps.txt"
    key 0 cmd; sleep 0.5
  done
  return 1
}
waitax() { # <regex> <seconds> [depth]
  local end=$(( $(date +%s) + $2 ))
  while [ $(date +%s) -lt $end ]; do
    $AX dump $PID ${3:-40} | grep -q -E "$1" && return 0; sleep 1
  done
  return 1
}
waiturl() {
  local end=$(( $(date +%s) + $2 ))
  while [ $(date +%s) -lt $end ]; do
    tabs | grep -q "$1" && return 0; sleep 1
  done
  return 1
}
RESULTS=()
record() { RESULTS+=("\"$1\": $2"); echo "$1 -> $2" >> "$OUT/steps.txt"; }
check() { # <name> <command...>
  local name=$1; shift
  if "$@"; then record "$name" true; else record "$name" false; fi
}
finish() {
  local joined; joined=$(IFS=,; echo "${RESULTS[*]-}")
  local sep=""; [ -n "$joined" ] && sep=", "
  echo "{${joined}${1:+$sep\"setupFailed\": \"$1\"}}" | python3 -c '
import json, sys
d = json.load(sys.stdin)
d["pass"] = "setupFailed" not in d and all(
    v is True for k, v in d.items() if k != "setupFailed")
print(json.dumps(d, indent=1))' > "$OUT/verdict.json"
  cat "$OUT/verdict.json"
}
fail_setup() {
  $AX dump $PID 40 > "$OUT/ax-setup-failure.txt"; finish "$1"; quit; exit 4
}
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
menuitem() { # <workspace name> -> its full item title (with the level)
  $AX dump $PID 14 | grep -oE "AXMenuItem \| $1( – [^|]*)? \|" | head -1 \
    | sed -E 's/^AXMenuItem \| //; s/ \|$//'
}
switch_to() { # <from> <to>
  menu "$1" "$2" || return 1
  $AX press $PID "$(menuitem "$2")" >> "$OUT/steps.txt"
  waitax "$2, Workspace wechseln" 15 14
}
open_url() { # <url>
  local opened=0
  for attempt in 1 2 3; do
    sleep 1; key 17 cmd
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 14 \
      && { opened=1; break; }
  done
  [ $opened = 1 ] || fail_setup "command bar did not open for $1"
  sleep 1; type_in "$1"; key 36
  waiturl "${1##*/}" 20 || fail_setup "did not load $1"; sleep 2
}
# The sidebar row of <title> as "role:name" for axtool, or empty.
row_of() {
  $AX dump $PID 40 \
    | grep -o -E "AX(RadioButton|Tab|Row|Cell|Button) \| [^|]*$1[^|]*" \
    | head -1 | sed -E 's/ *$//; s/^(AX[A-Za-z]+) \| /\1:/'
}
# Sidebar row menu (AXShowMenu, then an HID right-click) and one item.
row_menu() { # <row title> <menu item>
  local row; row=$(row_of "$1"); echo "row for $1: $row" >> "$OUT/steps.txt"
  [ -n "$row" ] || return 1
  $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
  $AX press $PID "$row" AXShowMenu >> "$OUT/steps.txt"
  if ! waitax "AXMenuItem \\| $2" 5; then
    $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
    $AX activate $PID >> "$OUT/steps.txt"; sleep 1
    $AX hidrightclick $PID "$row" >> "$OUT/steps.txt"
    waitax "AXMenuItem \\| $2" 5 \
      || { $AX dump $PID 40 > "$OUT/ax-row-menu-missing.txt"; return 1; }
  fi
  $AX press $PID "AXMenuItem:$2" >> "$OUT/steps.txt"; sleep 2
}
# "Verschieben nach" -> Getrennt (label may carry the Workspace icon);
# leaves the confirmation open. The title must end at " |": the section
# header "Getrennte Anmeldungen" starts with the same word. The press is
# scoped to the submenu, because the menu bar's Profile menu has a
# "Getrennt" item (switchToProfileFromMenu:) that a global press finds
# first (builds 51, 53, 54).
TARGET_ITEM='AXMenuItem \| ([^|]*  )?Getrennt \|'
# The row menu right after a closed dialog may not open on the first try
# (AXShowMenu -25204 on build 54); retry once after closing any leftover.
move_menu() { # <row title> <evidence name>
  move_menu_once "$@" && return 0
  echo "-- move menu retry" >> "$OUT/steps.txt"
  key 53; sleep 2
  move_menu_once "$@"
}
move_menu_once() { # <row title> <evidence name>
  row_menu "$1" "Verschieben nach" || return 1
  waitax "$TARGET_ITEM" 5 \
    || { $AX dump $PID 40 > "$OUT/ax-move-submenu-missing.txt"; return 1; }
  $AX dump $PID 40 > "$OUT/ax-move-menu-$2.txt"
  local item; item=$(grep -oE "$TARGET_ITEM" "$OUT/ax-move-menu-$2.txt" \
    | head -1 | sed -E 's/^AXMenuItem \| //; s/ \|$//')
  $AX pressin $PID "AXMenuItem:Verschieben nach" "AXMenuItem:$item" \
    >> "$OUT/steps.txt" || return 1
  waitax "AXButton \\| Verschieben" 8
}
dialog_dump() { # <evidence name>
  AHOI_AX_VALUE_MAX=600 $AX dump $PID 40 > "$OUT/ax-$1.txt"
}
has() { grep -q -F -- "$2" "$OUT/ax-$1.txt"; } # <evidence> <text>
confirm_dialog() { # <evidence name>
  dialog_dump "confirm-$1"
  # A press within the double-click interval after the dialog showed is
  # dropped by Chromium's dialog input protection.
  sleep 1; $AX press $PID "AXButton:Verschieben" >> "$OUT/steps.txt"; sleep 6
}
# Native Tab menu split of the active tab with <file>, chosen in
# Chromium's tab picker as a person would (split_journey_lib.sh).
tab_menu_split() { # <file> <title>
  open_url "$SITE/$1"; key 48 ctrl opt; sleep 2
  $AX press $PID "AXMenuItem:Tab zu neuer geteilter Ansicht hinzufügen" \
    >> "$OUT/steps.txt"; sleep 3
  waitax "AXWebArea \\| Tab auswählen" 8 14 || return 1
  for tries in 1 2 3; do
    $AX activate $PID >/dev/null; sleep 0.3
    $AX hidclick $PID "$2 " >> "$OUT/steps.txt" && break; sleep 1
  done
  sleep 3; ! $AX dump $PID 14 | grep -q "AXWebArea | Tab auswählen"
}
# Predicates for check().
one_of() { [ "$(count_of "$1")" = 1 ]; }
in_main() { one_of "$1" && [ "$(window_of "$1")" = "$MAIN_WINDOW" ]; }
in_target() {
  one_of "$1" && [ -n "$(window_of "$1")" ] \
    && [ "$(window_of "$1")" != "$MAIN_WINDOW" ]
}
same_window() { [ "$(window_of "$1")" = "$(window_of "$2")" ]; }
no_login() { [ -z "$(cookie_of "$1")" ]; }
logged_in() { [ "$(cookie_of "$1")" = "acct=gemeinsam" ]; }
not_ax() { ! waitax "$1" 3; }

launch
# The fully separated Workspace, created once; its window takes over.
menu Inbox "Neuer Workspace…" || fail_setup "workspace menu did not open"
$AX press $PID "Neuer Workspace…" >> "$OUT/steps.txt"
waitax "AXTextField \\| Workspace-Name" 8 14 \
  || fail_setup "create dialog did not open"
$AX setvalue $PID "Workspace-Name" "Getrennt" >> "$OUT/steps.txt"; sleep 1
$AX press $PID "AXRadioButton:Vollständig getrennt" >> "$OUT/steps.txt"
sleep 1; $AX press $PID "Erstellen" >> "$OUT/steps.txt"
waitax "Getrennt, Workspace wechseln" 20 14 \
  || fail_setup "separated Workspace did not open"
sleep 2; switch_to Getrennt Inbox || fail_setup "no hand-over to Inbox"

# (1) A logged-in tab via "Verschieben nach": the menu marks the other
# Profile, the confirmation says sign-ins stay, Cancel changes nothing.
open_url "$SITE/login.html?set"
CDP "$(target_id 'login.html?set')" Page.navigate \
  "{\"url\":\"$SITE/login.html\"}" >> "$OUT/steps.txt"; sleep 2
logged_in login.html || fail_setup "login cookie not set in Inbox"
MAIN_WINDOW=$(window_of login.html); ORIGIN=$(origin_of login.html)
check confirmationShown move_menu Login tab
check menuMarksSeparateSignIns has move-menu-tab "Getrennte Anmeldungen"
dialog_dump confirm-cancel
check noticeSaysSignInsStay has confirm-cancel "Anmeldungen ziehen nicht mit"
check noticeNamesTarget has confirm-cancel "Nach „Getrennt“ verschieben?"
check noticeOffersUndo has confirm-cancel "⌘Z holt alles zurück"
$AX press $PID "AXButton:Abbrechen" >> "$OUT/steps.txt"; sleep 3
unchanged() {
  waitax "Inbox, Workspace wechseln" 4 14 && in_main login.html \
    && [ "$(origin_of login.html)" = "$ORIGIN" ]
}
check cancelChangesNothing unchanged
# Confirm: the window follows the moved active tab into Getrennt, and the
# page reopens there by URL, without the login.
move_menu Login tab2 || fail_setup "move menu for the tab did not reopen"
confirm_dialog tab
check windowFollowsTab waitax "Getrennt, Workspace wechseln" 15 14
waiturl login.html 15
check tabReopenedInTarget in_target login.html
check loginDidNotMove no_login login.html
reloaded() { [ "$(origin_of login.html)" != "$ORIGIN" ]; }
check tabReloadedNotCarried reloaded

# (2) Cmd+Z with the sidebar focused undoes the move as a pair: the copy
# closes in Getrennt and the page reopens, logged in, in Inbox.
ROW=$(row_of Login)
[ -n "$ROW" ] && $AX focus $PID "$ROW" >> "$OUT/steps.txt"; sleep 1
undo_key; sleep 6
check undoRestoresSource in_main login.html
check undoBringsLoginBack logged_in login.html
check undoLeavesTargetEmpty not_ax 'AX[A-Za-z]+ \| [^|]*Login'
switch_to Getrennt Inbox || fail_setup "no hand-over to Inbox after undo"

# (3) A split moves as a whole: both panes arrive in one window of the
# separated Profile, never one pane per Profile.
open_url "$SITE/a.html"
tab_menu_split b.html PaneB || fail_setup "split did not form"
move_menu PaneA split || fail_setup "move menu for the split did not open"
confirm_dialog split
check splitNoticeSaysWhole has confirm-split "als Ganzes"
split_whole() {
  in_target a.html && in_target b.html && same_window a.html b.html
}
check splitMovedWhole split_whole
switch_to Getrennt Inbox || fail_setup "no hand-over to Inbox after split"

# (4) A folder with its page via "Verschieben nach" on the folder row.
open_url "$SITE/note.html"
row_menu Notiz "Neue Gruppe mit diesem Tab…" \
  || fail_setup "group item missing"
waitax "AXTextField \\| Gruppenname" 8 || fail_setup "group dialog missing"
$AX setvalue $PID "Gruppenname" "Mappe" >> "$OUT/steps.txt"; sleep 1
$AX press $PID "AXButton:Erstellen" >> "$OUT/steps.txt"; sleep 3
move_menu Mappe folder || fail_setup "move menu for the folder missing"
confirm_dialog folder
check folderNoticeNamesFolder has confirm-folder "Der Ordner „Mappe“"
arrived() { waitax "Getrennt, Workspace wechseln" 15 14 && waitax Mappe 10; }
check folderArrivesInTarget arrived
check folderPageReopened in_target note.html
switch_to Getrennt Inbox || fail_setup "no hand-over to Inbox after folder"
check folderLeftInbox not_ax Mappe

# (5) The command bar's move item lists the other Profile's Workspace
# with the sign-in hint and moves the active tab there.
open_url "$SITE/solo.html"
key 17 cmd
waitax "AXWindow \\| Suchen oder URL eingeben" 6 14 \
  || fail_setup "command bar did not open"
type_in "In Workspace verschieben: Getrennt"; sleep 2
dialog_dump command-bar
check commandBarOffersTarget has command-bar \
  "In Workspace verschieben: Getrennt"
check commandBarHintsSignIns has command-bar "Anmeldungen ziehen nicht mit"
key 36
waitax "AXButton \\| Verschieben" 8 || fail_setup "command move did not ask"
confirm_dialog command
followed() { waitax "Getrennt, Workspace wechseln" 15 14; }
check commandMoveFollows followed
check commandMoveReopened in_target solo.html
echo "info: drag-and-drop between the Profiles' windows is a manual" \
  "CU step (axtool has no HID drag)" >> "$OUT/steps.txt"
echo "tabs before relaunch: $(tabs)" >> "$OUT/steps.txt"
quit

# (6) After a relaunch the moved folder is still in Getrennt only.
launch
if ! waitax "Getrennt, Workspace wechseln" 5 14; then
  switch_to Inbox Getrennt || fail_setup "Getrennt missing after relaunch"
fi
check movedFolderPersists waitax Mappe 10
switch_to Getrennt Inbox || fail_setup "no hand-over to Inbox at the end"
check inboxStaysWithoutFolder not_ax Mappe
finish ""; quit
