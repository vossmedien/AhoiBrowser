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
# Reuse the current archive journeys' PID/profile-scoped input-yield guard.
. "$S/archive_focus_guard.sh"
owned_ax() { archive_check_focus; "$AX" "$@"; }
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
  # Recheck every restart against the exact installed candidate and live owner.
  python3 - "$APP" "$OUT" <<'LAUNCH_PREFLIGHT'
import datetime, json, os, pathlib, plistlib, re, subprocess, sys
app, out = map(pathlib.Path, sys.argv[1:])
source = plistlib.loads((app / "Contents/Info.plist").read_bytes()).get("AhoiSourceCommit")
receipt = out / "launch-preflight.jsonl"
previous = json.loads(receipt.read_text().splitlines()[0]) if receipt.exists() else None
commands = subprocess.check_output(["ps", "-axww", "-o", "comm="], text=True).splitlines()
idle = int(re.search(r'"HIDIdleTime"\s*=\s*(\d+)', subprocess.check_output(
    ["ioreg", "-c", "IOHIDSystem"], text=True)).group(1)) // 10**9
sample = dict(at=datetime.datetime.now(datetime.timezone.utc).isoformat(),
              source=source, idleSeconds=idle)
ax = os.environ.get("AHOI_AXTOOL", "/private/tmp/ahoi-axtool")
focus = subprocess.run([ax, "focused", "2147483647"], capture_output=True, text=True)
first_line = focus.stdout.splitlines()[0] if focus.stdout.splitlines() else ""
sample["foregroundProbe"] = first_line
expected = os.environ.get("AHOI_E2E_EXPECTED_SOURCE_COMMIT")
if not source or (expected and source != expected) or (previous and source != previous["source"]):
    sample["refusal"] = "installed candidate changed or lacks source metadata"
elif focus.returncode or not first_line.startswith("frontmostApp:"):
    sample["refusal"] = "target GUI foreground could not be verified"
elif first_line.startswith(("frontmostApp: loginwindow ", "frontmostApp: none ")):
    sample["refusal"] = "target GUI is locked or logged out"
elif str(app / "Contents/MacOS/AhoiBrowser") in [c.strip() for c in commands]:
    sample["refusal"] = "another browser owns the launch boundary"
elif os.environ.get("AHOI_E2E_YIELD_ON_FOCUS_LOSS") == "1" and idle < 2:
    sample["refusal"] = "input returned before launch"
with receipt.open("a") as stream:
    print(json.dumps(sample), file=stream)
if "refusal" in sample:
    with (out / "verdict.json").open("w") as stream:
        json.dump({"cancelled": sample["refusal"], "pass": False}, stream)
    print(sample["refusal"], file=sys.stderr)
    sys.exit(8)
LAUNCH_PREFLIGHT
  [ "$?" = 0 ] || exit 8
  ARCHIVE_FOCUS_ACQUIRED=false
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run \
    --no-default-browser-check --remote-debugging-port=$PORT about:blank \
    >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$P" >> "$OUT/run.txt"
  for i in $(seq 1 60); do
    curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break
    sleep 2
  done
  sleep 5; archive_activate_owned >> "$OUT/steps.txt"
}
quit() {
  key 12 cmd || return 1
  for i in $(seq 1 20); do
    if ! kill -0 "$PID" 2>/dev/null; then
      wait "$PID"; local browser_exit=$?
      echo "native browser quit exit=$browser_exit" >> "$OUT/steps.txt"
      [ "$browser_exit" = 0 ]; return $?
    fi
    sleep 1
  done
  echo "still running after native quit; preserve owned process/profile for diagnosis" >> "$OUT/run.txt"
  return 1
}
# Keys go through the HID tap. Acquire focus once, then yield if it leaves
# this invocation rather than bringing the browser ahead of another owner.
key() {
  for attempt in 1 2 3 4 5; do
    archive_activate_owned >/dev/null; sleep 0.3
    owned_ax hidkey $PID "$@" >> "$OUT/steps.txt" && return 0
    sleep 1
  done
  echo "hidkey gave up: $*" >> "$OUT/steps.txt"; return 1
}
# ⌘Z by its letter, as split_journey_lib.sh does (this journey does not
# source it): QWERTZ layouts carry Z on the ANSI Y key (16). Build 54 ran
# the undo step without any key, because undo_key was not defined here.
. "$S/keyboard_layout.sh"
KBD_LAYOUT=$(current_keyboard_layout)
case "$KBD_LAYOUT" in
  *QWERTY*) Z_KEY=6 ;;
  *German*|*Swiss*|*Austrian*|*Czech*|*Slovak*|*Hungarian*) Z_KEY=16 ;;
  *Croatian*|*Slovenian*|*Serbian-Latin*|*Albanian*) Z_KEY=16 ;;
  *) Z_KEY=6 ;;
esac
undo_key() {
  echo "undo: key $Z_KEY on ${KBD_LAYOUT:-unknown layout}" >> "$OUT/steps.txt"
  key $Z_KEY cmd
}
type_in() {
  for attempt in 1 2 3; do
    owned_ax type $PID "$1" >> "$OUT/steps.txt"; sleep 1
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
  python3 - "$OUT/verdict.json" <<'VERDICT_EXIT'
import json, sys
with open(sys.argv[1]) as stream:
    result = json.load(stream)
sys.exit(0 if result.get("pass") is True else 1)
VERDICT_EXIT
}
fail_setup() {
  $AX dump $PID 40 > "$OUT/ax-setup-failure.txt"; finish "$1"; quit; exit 4
}
# Escape before AXShowMenu goes straight to the process: an HID Escape
# arrives asynchronously and would close the menu just opened by AX.
menu() { # <active workspace name> <menu item regex>
  owned_ax key $PID 53 >> "$OUT/steps.txt"; sleep 1
  for attempt in 1 2 3 4; do
    owned_ax press $PID "$1, Workspace wechseln" AXShowMenu >> "$OUT/steps.txt"
    waitax "AXMenuItem \\| $2" 4 14 && return 0
    owned_ax key $PID 53 >> "$OUT/steps.txt"; sleep 2
  done
  return 1
}
menuitem() { # <workspace name> -> its full item title (with the level)
  $AX dump $PID 14 | grep -oE "AXMenuItem \| $1( – [^|]*)? \|" | head -1 \
    | sed -E 's/^AXMenuItem \| //; s/ \|$//'
}
switch_to() { # <from> <to>
  menu "$1" "$2" || return 1
  owned_ax press $PID "$(menuitem "$2")" >> "$OUT/steps.txt"
  waitax "$2, Workspace wechseln" 15 14
}
# Back to Inbox after a step in Getrennt. When the window did not follow
# (a red check), Inbox is already in front and its switcher has no
# "Getrennt" button; switching then would fail the setup and hide every
# later check (build 54: four "NOT FOUND" after the folder).
back_to_inbox() {
  if waitax "Inbox, Workspace wechseln" 2 14; then
    echo "info: Inbox already in front" >> "$OUT/steps.txt"; return 0
  fi
  switch_to Getrennt Inbox
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
  owned_ax key $PID 53 >> "$OUT/steps.txt"; sleep 1
  owned_ax press $PID "$row" AXShowMenu >> "$OUT/steps.txt"
  if ! waitax "AXMenuItem \\| $2" 5; then
    owned_ax key $PID 53 >> "$OUT/steps.txt"; sleep 1
    archive_activate_owned >> "$OUT/steps.txt"; sleep 1
    owned_ax hidrightclick $PID "$row" >> "$OUT/steps.txt"
    waitax "AXMenuItem \\| $2" 5 \
      || { $AX dump $PID 40 > "$OUT/ax-row-menu-missing.txt"; return 1; }
  fi
  owned_ax press $PID "AXMenuItem:$2" >> "$OUT/steps.txt"; sleep 2
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
  owned_ax pressin $PID "AXMenuItem:Verschieben nach" "AXMenuItem:$item" \
    >> "$OUT/steps.txt" || return 1
  waitax "AXButton \\| Verschieben" 8
}
dialog_dump() { # <evidence name>
  AHOI_AX_VALUE_MAX=600 $AX dump $PID 40 > "$OUT/ax-$1.txt"
}
has() { grep -q -F -- "$2" "$OUT/ax-$1.txt"; } # <evidence> <text>
move_confirmation_ready() {
  # Native focus and input protection settle after the source bubble closes.
  sleep 2
  for attempt in 1 2 3 4 5; do
    "$AX" focused "$PID" > "$OUT/move-confirmation-focus.txt"
    grep -q '^focusedWindow: AXWindow | Nach „Getrennt“ verschieben?' "$OUT/move-confirmation-focus.txt" && break
    sleep 1
  done
  grep -q '^focusedWindow: AXWindow | Nach „Getrennt“ verschieben?' "$OUT/move-confirmation-focus.txt" || fail_setup "move confirmation did not acquire native focus"
  "$AX" enabled "$PID" "Verschieben" > "$OUT/move-confirmation-enabled.txt"
  grep -q 'AXButton | Verschieben .*enabled=true' "$OUT/move-confirmation-enabled.txt" || fail_setup "move button unavailable"
}
confirm_dialog() { # <evidence name>
  dialog_dump "confirm-$1"
  move_confirmation_ready
  owned_ax hidclick $PID "AXButton | Verschieben |" >> "$OUT/steps.txt" || fail_setup "move confirmation click failed"
  sleep 6
}
# Native Tab menu split of the active tab with <file>, chosen in
# Chromium's tab picker as a person would (split_journey_lib.sh).
tab_menu_split() { # <file> <title>
  open_url "$SITE/$1"; key 48 ctrl opt; sleep 2
  owned_ax press $PID "AXMenuItem:Tab zu neuer geteilter Ansicht hinzufügen" \
    >> "$OUT/steps.txt"; sleep 3
  waitax "AXWebArea \\| Tab auswählen" 8 14 || return 1
  for tries in 1 2 3; do
    archive_activate_owned >/dev/null; sleep 0.3
    owned_ax hidclick $PID "$2 " >> "$OUT/steps.txt" && break; sleep 1
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
# A sidebar row titled <text>. A bare title also matches the menu bar's
# History items ("AXMenuItem | Login", build 54), the window title and
# notice text, so a "gone" check could never pass.
ROW_RE='AX(RadioButton|Tab|Row|Cell|Button) \| [^|]*'
# The AX subtree of the window whose title ends in " – <Profile>" only.
# A whole-app dump also holds the source window, so after the undo the
# restored Inbox row "Login" failed "target empty" (builds 55, 56).
window_ax() { # <profile name>
  $AX dump $PID 40 | awk -v s=" – $1" '
    /^  [^ ]/ { t = substr($0, length($0) - length(s) + 1)
      w = ($1 == "AXWindow" && t == s) }
    w'
}
# No row matching <regex> in the <profile>'s window within 10 s; the
# copy closes asynchronously after the undo. A closed window holds none.
gone_from() { # <profile name> <regex>
  local end=$(( $(date +%s) + 10 ))
  while [ $(date +%s) -lt $end ]; do
    window_ax "$1" | grep -q -E "$2" || return 0; sleep 1
  done
  return 1
}

launch
# The fully separated Workspace, created once; its window takes over.
menu Inbox "Neuer Workspace…" || fail_setup "workspace menu did not open"
owned_ax press $PID "Neuer Workspace…" >> "$OUT/steps.txt"
waitax "AXTextField \\| Workspace-Name" 8 14 \
  || fail_setup "create dialog did not open"
owned_ax setvalue $PID "Workspace-Name" "Getrennt" >> "$OUT/steps.txt"; sleep 1
owned_ax press $PID "AXRadioButton:Vollständig getrennt" >> "$OUT/steps.txt"
sleep 1; owned_ax press $PID "Erstellen" >> "$OUT/steps.txt"
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
move_confirmation_ready
owned_ax hidclick $PID "AXButton | Abbrechen" >> "$OUT/steps.txt" || fail_setup "move cancel click failed"
sleep 3
unchanged() {
  waitax "Inbox, Workspace wechseln" 4 14 && in_main login.html \
    && [ "$(origin_of login.html)" = "$ORIGIN" ] \
    && ! waitax "AXWindow \\| Nach „Getrennt“ verschieben?" 2
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
check undoLeavesTargetEmpty gone_from Getrennt "${ROW_RE}Login"
$AX dump $PID 40 > "$OUT/ax-after-undo.txt"
window_ax Getrennt | grep -q . \
  || echo "info: no Getrennt window after undo" >> "$OUT/steps.txt"
back_to_inbox || fail_setup "no hand-over to Inbox after undo"

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
back_to_inbox || fail_setup "no hand-over to Inbox after split"

# (4) A folder with its page via "Verschieben nach" on the folder row.
open_url "$SITE/note.html"
row_menu Notiz "Neue Gruppe mit diesem Tab…" \
  || fail_setup "group item missing"
waitax "AXTextField \\| Gruppenname" 8 || fail_setup "group dialog missing"
owned_ax setvalue $PID "Gruppenname" "Mappe" >> "$OUT/steps.txt"; sleep 1
owned_ax press $PID "AXButton:Erstellen" >> "$OUT/steps.txt"; sleep 3
move_menu Mappe folder || fail_setup "move menu for the folder missing"
confirm_dialog folder
check folderNoticeNamesFolder has confirm-folder "Der Ordner „Mappe“"
arrived() {
  waitax "Getrennt, Workspace wechseln" 15 14 && waitax "${ROW_RE}Mappe" 10
}
check folderArrivesInTarget arrived
check folderPageReopened in_target note.html
back_to_inbox || fail_setup "no hand-over to Inbox after folder"
check folderLeftInbox not_ax "${ROW_RE}Mappe"

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
quit || { finish "browser failed while quitting before relaunch"; exit 4; }

# (6) After a relaunch the moved folder is still in Getrennt only.
launch
if ! waitax "Getrennt, Workspace wechseln" 5 14; then
  switch_to Inbox Getrennt || fail_setup "Getrennt missing after relaunch"
fi
check movedFolderPersists waitax "${ROW_RE}Mappe" 10
switch_to Getrennt Inbox || fail_setup "no hand-over to Inbox at the end"
check inboxStaysWithoutFolder not_ax "${ROW_RE}Mappe"
finish ""; STATUS=$?
quit || { finish "browser failed while quitting after cross-level journey"; exit 4; }
exit "$STATUS"
