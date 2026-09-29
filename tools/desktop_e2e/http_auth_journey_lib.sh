# Shared AX/CDP helpers for http-auth-journey.sh. Sourced after S, AX, OUT,
# PORT and PID are set; every function reads them when it runs, so a relaunch
# (a new PID or profile) needs no re-sourcing. German labels are the M153
# composition's.

# Keys go through the HID event tap like a real keyboard (keys posted to the
# process are intermittently dropped by Chromium); hidkey refuses unless the
# app is frontmost, so bring it forward and retry.
ax() {
  if [ "$1" = key ]; then
    shift; local pid=$1; shift
    for attempt in 1 2 3 4 5; do
      "$AX" activate "$pid" >/dev/null 2>&1; sleep 0.3
      "$AX" hidkey "$pid" "$@" >> "$OUT/steps.txt" 2>&1 && return 0
      sleep 1
    done
    echo "hidkey gave up: $*" >> "$OUT/steps.txt"; return 1
  fi
  "$AX" "$@" >> "$OUT/steps.txt" 2>&1
}
# Evidence for a lost input: HID idle time (near 0 means someone else used
# the keyboard or mouse during the step, as in build 52's run with idle 0 s)
# and which app, window and element had keyboard focus.
note_focus() {
  { echo "-- input check: hid idle $(idle_seconds)s"; $AX focused $PID; } \
    >> "$OUT/steps.txt" 2>&1; }
title() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys
p=[t for t in json.load(sys.stdin) if t["type"]=="page"]
print(p[0]["title"] if p else "")'; }
waitax() { local end=$(( $(date +%s) + $2 ))
  while [ $(date +%s) -lt $end ]; do
    $AX dump $PID 40 | grep -q -E "$1" && return 0; sleep 1
  done; return 1; }
waittitle() { local end=$(( $(date +%s) + $2 ))
  while [ $(date +%s) -lt $end ]; do
    title | grep -q "$1" && return 0; sleep 1
  done; return 1; }
record() { echo "$1 $2" >> "$OUT/results.txt"
  echo "== $1 $2" >> "$OUT/steps.txt"; }
# rec <step> <command...>: PASS when the command succeeds.
rec() { local n=$1; shift
  if "$@"; then record "$n" PASS; else record "$n" FAIL; fi; }
alive() { kill -0 $PID 2>/dev/null; }
has_ax() { $AX dump $PID 40 | grep -q -E -- "$1"; }
no_ax() { alive && ! $AX dump $PID 40 | grep -q -E -- "$1"; }
# Login Data rows (signon realm | username) of one PasswordForm scheme
# (1 Basic, 2 Digest). The copy lives next to the profile, never in $OUT.
store_q() {
  cp "$STORE_P/Default/Login Data" "$P-ld.db" 2>/dev/null \
    || { echo ""; return; }
  sqlite3 "$P-ld.db" "select signon_realm||'|'||username_value from logins
    where scheme=$1 order by 1;" | tr '\n' ' '
  rm -f "$P-ld.db"; }
store() { store_q 1; }
# Login Data is written after the signed-in page loaded (lookup, then an
# async write): poll up to 10 s for the expected rows.
wait_store() { local i; for i in 1 2 3 4 5 6 7 8 9 10; do
  [ "$(store)" = "$1" ] && return 0; sleep 1; done; return 1; }

# ---- Command bar ------------------------------------------------------------
# The command bar is key while it is open, so Escape reaches only the bar.
BAR='Suchen oder URL eingeben'
bar_open() { $AX dump $PID 3 | grep -q "AXWindow | $BAR"; }
closed_cmdbar() { local end=$(( $(date +%s) + 10 ))
  while [ $(date +%s) -lt $end ]; do
    $AX dump $PID 3 | grep -q "$BAR" || return 0; sleep 1
  done; return 1; }
# A bar that is shown but not key ignores Escape (build 52, own-sessions
# step): bring the app forward once more before giving up.
close_cmdbar() { bar_open || return 0
  ax key $PID 53; closed_cmdbar && return 0
  note_focus; ax activate $PID; sleep 1
  ax key $PID 53; closed_cmdbar && return 0
  echo "-- command bar did not close on Escape" >> "$OUT/steps.txt"
  return 1; }
# A bar that ignored Return is kept as AX evidence, then closed, so it can no
# longer swallow the next steps (build 50: one open bar failed 12 steps).
STUCK=0
stuck_cmdbar() { STUCK=$((STUCK+1))
  echo "-- command bar still open after Return ($1);" \
    "see ax-cmdbar-stuck-$STUCK.txt" >> "$OUT/steps.txt"
  $AX dump $PID 40 > "$OUT/ax-cmdbar-stuck-$STUCK.txt" 2>&1
  note_focus; close_cmdbar; }
# Never press ⌘T into an open bar: it re-creates the bubble on the same anchor
# ("anchor_view has already anchored a focusable widget", build 50), and the
# re-created bar never executed Return there.
cmdbar() { local ok=1
  close_cmdbar
  for i in 1 2 3; do ax activate $PID; sleep 1; ax key $PID 17 cmd
    waitax "AXWindow \| $BAR" 6 && { ok=0; break; }; done
  [ $ok = 0 ] || note_focus
  sleep 1; return $ok; }
# Types into the open command bar and checks that its text field really holds
# the text; a first keystroke can arrive before the field has focus (proven in
# keyboard-shortcuts-journey.sh, build 47). A retry first re-activates the app:
# typed text goes to the key window, which is not the bar once another app
# took the focus in between.
type_in() {
  for attempt in 1 2 3; do
    ax type $PID "$1"; sleep 1
    AHOI_AX_VALUE_MAX=300 $AX dump $PID 14 | grep "AXTextField" \
      | grep -F -q -- "| $1" && return 0
    echo "info: typed text missing, retyping" >> "$OUT/steps.txt"
    note_focus; ax activate $PID
    ax key $PID 0 cmd; sleep 0.5
  done
  return 1; }
goto() {
  cmdbar || { echo "-- command bar did not open for $1" >> "$OUT/steps.txt"
    return 1; }
  ax key $PID 0 cmd
  type_in "$1" || { echo "-- could not type $1" >> "$OUT/steps.txt"
    close_cmdbar; return 1; }
  ax key $PID 36
  closed_cmdbar && return 0
  stuck_cmdbar "goto $1"; return 1; }
# A challenge the harness disturbed shows only the 401 page; one explicit
# reload re-issues it. Every use is recorded so a product-side cancel stays
# visible.
challenge() { dialog "${1:-30}" && return 0
  title | grep -q "Ahoi auth required" || return 1
  echo "-- reload to re-issue challenge" >> "$OUT/steps.txt"
  echo reload >> "$OUT/reloads.txt"
  ax activate $PID; ax key $PID 15 cmd; dialog 20; }
# HTTP-auth commands appear below the "HTTP" query; the first is preselected.
# A full-text query would instead preselect the web search row. The row index
# is taken from the offered rows, because "forget" and "manage" are hidden
# where they cannot run (no active realm, incognito). Every failure closes the
# bar again.
CMD_SWITCH="HTTP-Authentifizierungskonto wechseln"
CMD_FORGET="Gespeicherte HTTP-Zugangsdaten für diesen Schutzbereich vergessen"
CMD_MANAGE="Gespeicherte HTTP-Zugänge verwalten"
command() { local want order idx
  case "$1" in
    switch) want=$CMD_SWITCH;; forget) want=$CMD_FORGET;;
    manage) want=$CMD_MANAGE;;
  esac
  cmdbar || { echo "-- command bar did not open for $1" >> "$OUT/steps.txt"
    return 1; }
  type_in "HTTP" || { echo "-- could not type the $1 query" \
    >> "$OUT/steps.txt"; close_cmdbar; return 1; }
  if ! waitax "AXStaticText \| ($CMD_SWITCH|$CMD_MANAGE)" 8; then
    echo "-- no HTTP-auth rows for $1" >> "$OUT/steps.txt"
    close_cmdbar; return 1
  fi
  sleep 0.5
  order=$($AX dump $PID 40 \
    | grep -o -E "AXStaticText \| ($CMD_SWITCH|$CMD_FORGET|$CMD_MANAGE)" \
    | sed 's/^AXStaticText | //' | awk '!seen[$0]++')
  idx=$(printf '%s\n' "$order" | grep -n -x -F "$want" | cut -d: -f1)
  if [ -z "$idx" ]; then
    echo "-- command $1 not offered; rows: $(echo $order)" \
      >> "$OUT/steps.txt"; close_cmdbar; return 1
  fi
  # Not `seq 1 $idx`: BSD seq counts down, so idx=0 pressed Down twice.
  local i=1
  while [ $i -lt $idx ]; do ax key $PID 125; sleep 0.3; i=$((i+1)); done
  ax key $PID 36
  closed_cmdbar || { stuck_cmdbar "command $1"; return 1; }; }

# ---- Login dialog -----------------------------------------------------------
dialog() { waitax "AXHeading \| Anmelden" "${1:-20}" && return 0
  { echo "-- dialog timeout; windows and page:"
    $AX dump $PID 3 | grep AXWindow; title; } >> "$OUT/steps.txt"
  return 1; }
# Last AX field of the first line matching <field>; the bare field name
# means no value.
field_value() { AHOI_AX_VALUE_MAX=300 $AX dump $PID 40 \
    | grep -m1 "AXTextField | $1" | awk -F' [|] ' -v n="$1" \
      '{v=$NF; gsub(/ +$/,"",v); if (v==n) v=""; print v}'; }
# Username the dialog shows now ("" when empty).
prefilled() { field_value Nutzername; }
# Characters in the password field (AX shows one bullet per character).
password_length() { field_value Passwort \
    | python3 -c 'import sys
print(len(sys.stdin.buffer.read().decode("utf-8").strip()))'; }
# Types both fields and checks that they hold the typed values before the
# save choice and submit: build 52 submitted the unchanged prefilled account
# (fixture: 200 for alice's saved password instead of a 401 for the typed
# wrong one) and a field holding "di" typed by someone else (hidkey refused:
# target not frontmost), which failed AUTH-06 and AUTH-11 for input reasons.
login() { # <user> <password> <save-option-label or ''>
  local try
  for try in 1 2 3; do
    ax focus $PID "AXTextField:Nutzername" || {
      echo "-- login $1: no dialog" >> "$OUT/steps.txt"; return 1; }
    ax key $PID 0 cmd; ax type $PID "$1"
    ax focus $PID "AXTextField:Passwort"; ax key $PID 0 cmd
    ax type $PID "$2"; sleep 1
    [ "$(prefilled)" = "$1" ] && [ "$(password_length)" = "${#2}" ] && break
    echo "-- login $1: fields not as typed (try $try): user" \
      "\"$(prefilled)\", $(password_length) password chars" \
      >> "$OUT/steps.txt"
    note_focus
  done
  [ -n "$3" ] && ax press $PID "$3"
  ax press $PID "AXButton:Anmelden"; }
menu_items() { $AX dump $PID 45 | awk '/AXMenuBar$/{exit} {print}' \
    | grep -o -E 'AXMenuItem \| [a-z]+ \|' | sort -u | awk '{print $3}' \
    | tr '\n' ' '; }
# The username field is a Views EditableCombobox whose menu LoginView filters
# (patch 0078): while the field is empty or names a saved account (the
# prefilled preferred one, a menu choice, the name kept after a failure) it
# lists every account; other text narrows it by prefix. Open it with the
# arrow button and never clear the field first: a prefilled "alice" must
# still offer "bob" (build 50 hid it). Never close it with Escape: without an
# open menu Escape cancels the login dialog (build 50 lost the realm-B, port,
# Digest, /z/, preferred-account and proxy dialogs that way).
menu_open() { [ -n "$(menu_items)" ]; }
# The arrow button toggles the menu. Right after an AX menu choice or a
# delete in the same dialog one press showed nothing (build 52, AUTH-17,
# while the next press listed "alice"), so a second press follows once. An
# empty account list never opens a menu; both presses then only cost time.
open_account_menu() {
  ax focus $PID "AXTextField:Nutzername" || return 1
  echo "-- account menu opened over \"$(prefilled)\"" >> "$OUT/steps.txt"
  local try end
  for try in 1 2; do
    ax press $PID "AXButton:Nutzername"
    end=$(( $(date +%s) + 3 ))
    while [ $(date +%s) -lt $end ]; do menu_open && return 0; sleep 0.5; done
  done
  return 1; }
close_account_menu() { menu_open || return 0
  ax press $PID "AXButton:Nutzername"; sleep 1
  menu_open || return 0
  # Only while the menu is open: then the menu controller consumes Escape.
  $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
  menu_open && { echo "-- account menu still open" >> "$OUT/steps.txt"
    return 1; }
  return 0; }
accounts() { dialog 2 || { echo "no-dialog"; return; }
  open_account_menu; sleep 1
  menu_items
  close_account_menu
  dialog 2 || echo "-- login dialog lost while listing accounts" \
    >> "$OUT/steps.txt"; }
pick() { # <username>: choose a saved account from the unfiltered list
  open_account_menu || {
    echo "-- account menu did not open for $1" >> "$OUT/steps.txt"
    return 1; }
  ax press $PID "AXMenuItem:$1" || { close_account_menu; return 1; }
  sleep 1; }

# ---- Credential manager -----------------------------------------------------
# The manager is window-modal (a sheet on macOS) and blocks ⌘T until closed.
# Its button is "Schließen", not "Abbrechen": build 52 pressed a missing
# Abbrechen, the sheet stayed and every later command bar failed
# (auth18_save_after_reset, forget_realm, auth17_empty_after_forget).
MANAGER="AX(Window|Sheet) \| HTTP-Zugänge"
close_manager() { has_ax "$MANAGER" || return 0
  ax pressin $PID "HTTP-Zugänge" "AXButton:Schließen"
  local end=$(( $(date +%s) + 8 ))
  while [ $(date +%s) -lt $end ]; do
    no_ax "$MANAGER" && return 0; sleep 1
  done
  echo "-- credential manager did not close" >> "$OUT/steps.txt"
  note_focus; return 1; }

# Between sections: close a stale manager or command bar and cancel a login
# dialog an earlier failure left open, so one failure cannot fail the next
# section.
settle() { close_manager; close_cmdbar
  local i; for i in 1 2 3; do
    has_ax "AXHeading \| Anmelden" || return 0
    echo "-- settle: cancelling a leftover login dialog" >> "$OUT/steps.txt"
    ax press $PID "AXButton:Abbrechen"; sleep 2
  done; }
# Title of the first page whose URL contains the marker.
realm_title() { curl -s http://127.0.0.1:$PORT/json | python3 -c '
import json,sys
p=[t for t in json.load(sys.stdin)
   if t["type"]=="page" and sys.argv[1] in t["url"]]
print(p[0]["title"] if p else "<no tab>")' "$1"; }
wait_rt() { # <url marker> <expected title> <seconds>
  local end=$(( $(date +%s) + $3 ))
  while [ $(date +%s) -lt $end ]; do
    [ "$(realm_title "$1")" = "$2" ] && return 0; sleep 1
  done
  echo "-- $1 title: $(realm_title "$1")" >> "$OUT/steps.txt"; return 1; }
seen() { curl -s "http://127.0.0.1:$A/__seen?key=$1"; }
wait_seen() { local end=$(( $(date +%s) + $2 ))
  while [ $(date +%s) -lt $end ]; do
    [ "$(seen "$1")" != none ] && return 0; sleep 1
  done; return 1; }
eval_in() { # <url substring> <expression>
  node "$S/cdp.mjs" $PORT "$1" Runtime.evaluate "$(python3 -c '
import json,sys
print(json.dumps({"expression":sys.argv[1],"returnByValue":True}))' "$2")" \
    >> "$OUT/steps.txt" 2>&1; }
quit() { ax key $PID 12 cmd
  for i in $(seq 1 20); do alive || return 0; sleep 1; done
  echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3; }
