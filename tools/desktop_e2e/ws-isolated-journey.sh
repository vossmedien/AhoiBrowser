#!/bin/bash
# usage: ws-isolated-journey.sh <App.app> <outdir>
# PID-scoped AX + CDP journey for ADR 0011 level `isolated` (fully separated
# Workspace = own Profile): creation opens its own window (WS-ISO-01), its
# login is invisible to the main Profile (WS-ISO-02), switching hands the
# window over without reloading pages (WS-ISO-17), it stays reachable after a
# relaunch (WS-ISO-14), and deleting it from its own window brings back the
# main window without any Chromium profile UI (WS-ISO-16). The remaining
# WS-ISO-02 areas are separated too: history, a saved password, an
# autocomplete entry, a site permission (notifications) and a download,
# each present in the separated Profile only, and the main window's command
# bar never suggests the separated Profile's page; WS-ISO-03: uBlock Origin
# Classic installed in the separated window filters there and is absent
# from the main Profile (needs network for the pinned GitHub release; set
# AHOI_E2E_SKIP_NETWORK=1 to leave WS-ISO-03 out). Product default launch,
# disposable user data directory; downloads go to directories under it.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9346
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-wsiso-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8793}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
printf '<title>login</title><script>document.cookie="acct=getrennt; max-age=3600; path=/"</script>logged in' > $P-site/login.html
printf '<title>check</title>check' > $P-site/check.html
# WS-ISO-02 areas: a login form with a free text field, a notification
# request, a download link; WS-ISO-03: an ad slot uBO's generic cosmetic
# filters hide (served under *.localhost, see ubo-classic-journey.sh).
printf '<title>form</title><form action="done.html" method="get"><input id=u name=username autocomplete=username><input id=p type=password name=password autocomplete=current-password><input id=n name=ahoinote><button>Anmelden</button></form>' > $P-site/form.html
printf '<title>done</title>done' > $P-site/done.html
printf '<title>perm</title>perm' > $P-site/perm.html
printf '<title>dl</title><a id=d href="note.txt" download>note</a>' > $P-site/dl.html
printf 'Ahoi separated download\n' > $P-site/note.txt
printf '<title>adpage</title><ins class="adsbygoogle" id="ad" data-ad-client="ca-pub-0000000000000000" data-ad-slot="0000000000" style="display:block;width:300px;height:250px;background:#fc0"></ins>ad page' > $P-site/ad.html
# Downloads never go to the owner's ~/Downloads: the main Profile is seeded
# here, the separated one before the relaunch (it exists only at runtime).
DL_MAIN="$P-dl-main"; DL_ISO="$P-dl-iso"; mkdir -p "$DL_MAIN" "$DL_ISO" "$P/Default"
python3 - "$P/Default/Preferences" "$DL_MAIN" <<'PY'
import json, sys
json.dump({"download": {"default_directory": sys.argv[2], "prompt_for_download": False},
           "savefile": {"default_directory": sys.argv[2]}}, open(sys.argv[1], "w"))
PY
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT; SITE=http://127.0.0.1:$SITE_PORT
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
tabs() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(json.dumps(sorted([t["url"] for t in json.load(sys.stdin) if t["type"]=="page"])))'; }
launch() {
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT about:blank >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$P" >> "$OUT/run.txt"
  for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  sleep 5; $AX activate $PID >> "$OUT/steps.txt"
}
quit() { key 12 cmd; for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || return 0; sleep 1; done; echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3; }
# Keys go through the HID event tap like a real keyboard: keys posted to
# the process are intermittently dropped by Chromium (command-bar focus
# probes 4 and 5). hidkey refuses unless the app is frontmost, so bring it
# forward and retry instead of typing into another app.
key() {
  for attempt in 1 2 3 4 5; do
    $AX activate $PID >/dev/null; sleep 0.3
    $AX hidkey $PID "$@" >> "$OUT/steps.txt" && return 0
    sleep 1
  done
  echo "hidkey gave up: $*" >> "$OUT/steps.txt"; return 1
}
# Types into the focused command bar and checks that its text field really
# holds the text; a first keystroke can arrive before the field has focus
# (build 47: a later Return then closed an empty bar).
type_in() {
  for attempt in 1 2 3; do
    $AX type $PID "$1" >> "$OUT/steps.txt"; sleep 1
    $AX dump $PID 14 | grep "AXTextField" | grep -F -q -- "| $1" && return 0
    echo "info: typed text missing, retyping" >> "$OUT/steps.txt"
    key 0 cmd; sleep 0.5
  done
  return 1
}
waitax() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do $AX dump $PID 14 | grep -q -E "$1" && return 0; sleep 1; done; return 1; }
waiturl() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do tabs | grep -q "$1" && return 0; sleep 1; done; return 1; }
RESULTS=(); record() { RESULTS+=("\"$1\": $2"); echo "$1 -> $2" >> "$OUT/steps.txt"; }
finish() {
  local joined; joined=$(IFS=,; echo "${RESULTS[*]}")
  local sep=""; [ -n "$joined" ] && sep=", "
  echo "{${joined}${1:+$sep\"setupFailed\": \"$1\"}}" | python3 -c 'import json,sys;d=json.load(sys.stdin);d["pass"]=("setupFailed" not in d) and all(v is True for k,v in d.items() if k!="setupFailed");print(json.dumps(d,indent=1))' > "$OUT/verdict.json"
  cat "$OUT/verdict.json"
}
fail_setup() { $AX dump $PID 14 > "$OUT/ax-setup-failure.txt"; finish "$1"; quit; exit 4; }
# Escape before AXShowMenu goes straight to the process: an HID Escape
# arrives asynchronously and would close the menu just opened by AX.
menu() { # <active workspace name> <menu item regex>
  $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
  for attempt in 1 2 3 4; do
    $AX press $PID "$1, Workspace wechseln" AXShowMenu >> "$OUT/steps.txt"
    waitax "AXMenuItem \\| $2" 4 && return 0
    $AX key $PID 53 >> "$OUT/steps.txt"; sleep 2
  done
  return 1
}
# Workspace menu items carry their level in the title ("Kunde – Eigene
# Website-Sitzungen"); resolve a Workspace name to its full item title.
menuitem() { # <workspace name>
  $AX dump $PID 14 | grep -oE "AXMenuItem \| $1( – [^|]*)? \|" | head -1 | sed -E 's/^AXMenuItem \| //; s/ \|$//'
}
open_url() { # <url>
  local opened=0
  for attempt in 1 2 3; do
    sleep 1; key 17 cmd
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 && { opened=1; break; }
  done
  [ $opened = 1 ] || fail_setup "command bar did not open for $1"
  sleep 1; type_in "$1"; key 36
  waiturl "$1" 20 || fail_setup "did not load $1"; sleep 2
}
cookie_of() { CDP "$1" Runtime.evaluate '{"expression":"document.cookie","returnByValue":true}' | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'; }
origin_of() { CDP "$1" Runtime.evaluate '{"expression":"String(performance.timeOrigin)","returnByValue":true}' | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'; }
window_count() { $AX dump $PID 3 | grep -c 'AXWindow'; }
eval_in() { # <url substring> <expression> ; with a user gesture
  CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True,"awaitPromise":True,"userGesture":True}))' "$2")" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'
}
press_label() { # <regex of an AXButton title> ; presses the first match
  local title; title=$($AX dump $PID 14 | grep -o -E "AXButton \| ($1)[^|]*" | head -1 | sed -e 's/^AXButton | //' -e 's/ *$//')
  [ -n "$title" ] && $AX press $PID "AXButton:$title" >> "$OUT/steps.txt"
}
# Chromium's input protection drops a press on a prompt or bubble button
# that comes within the double-click interval after it showed or its window
# was activated; the press still returns 0. Builds 53/54 lost "Zulassen"
# and "Speichern" that way. Wait, press, and retry while it stays open.
press_protected() { # <regex of an AXButton title> <regex while open>
  for attempt in 1 2 3; do
    sleep 1; press_label "$1" || return 1
    local end=$(( $(date +%s) + 3 ))
    while [ $(date +%s) -lt $end ]; do
      $AX dump $PID 14 | grep -q -E "$2" || return 0; sleep 0.5
    done
    echo "info: $1 still open, pressing again" >> "$OUT/steps.txt"
  done
  return 1
}
# The separated Profile's directory ("Profile <n>", ADR 0011).
iso_dir() { ls -d "$P"/Profile\ * 2>/dev/null | head -1; }
# Rows of a query against a copy of a profile database (never the live file).
db_rows() { # <db file> <sql>
  python3 - "$1" "$2" <<'PY'
import os, shutil, sqlite3, sys, tempfile
db, sql = sys.argv[1], sys.argv[2]
if not os.path.exists(db):
    print("<no database>"); sys.exit(0)
t = tempfile.mkdtemp()
for suffix in ("", "-wal", "-shm", "-journal"):
    if os.path.exists(db + suffix):
        shutil.copy(db + suffix, os.path.join(t, "d" + suffix))
try:
    for row in sqlite3.connect(os.path.join(t, "d")).execute(sql):
        print("|".join(str(v) for v in row))
except sqlite3.Error as e:
    print("<error %s>" % e)
shutil.rmtree(t)
PY
}
UBO_ID=fkgkibajhfbepljeaefdnfnegdcjomkh
ubo_running() { curl -s http://127.0.0.1:$PORT/json | grep -q "chrome-extension://$UBO_ID/"; }
ad_display() { eval_in "$1" "getComputedStyle(document.getElementById('ad')).display"; }
hidden_within() { # <url substring> <seconds>; uBO compiles its lists first
  local end=$(( $(date +%s) + $2 )) reloaded=0
  while [ $(date +%s) -lt $end ]; do
    [ "$(ad_display "$1")" = none ] && return 0
    if [ $reloaded = 0 ] && [ $(( end - $(date +%s) )) -le $(( $2 - 10 )) ]; then
      CDP "$1" Page.reload '{}' >/dev/null 2>&1; reloaded=1; sleep 3
    fi
    sleep 1
  done
  return 1
}

launch
BEFORE_WINDOWS=$(window_count)
# WS-ISO-01: creation opens the Workspace in its own window.
menu Inbox "Neuer Workspace…" || fail_setup "workspace menu did not open"
$AX press $PID "Neuer Workspace…" >> "$OUT/steps.txt"
waitax "AXTextField \\| Workspace-Name" 8 || fail_setup "create dialog did not open"
$AX setvalue $PID "Workspace-Name" "Getrennt" >> "$OUT/steps.txt"; sleep 1
$AX press $PID "AXRadioButton:Vollständig getrennt" >> "$OUT/steps.txt"; sleep 1
$AX press $PID "Erstellen" >> "$OUT/steps.txt"
waitax "Getrennt, Workspace wechseln" 20 && record ownWindowCreated true || record ownWindowCreated false
sleep 3; $AX dump $PID 14 > "$OUT/ax-after-create.txt"
{ ! grep -qi 'Profil auswählen\|Wer verwendet\|Who.s using' "$OUT/ax-after-create.txt"; } \
  && record noProfilePicker true || record noProfilePicker false
[ "$(window_count)" -gt "$BEFORE_WINDOWS" ] && record secondWindow true || record secondWindow false
# WS-ISO-02: log in inside the separated window.
open_url "$SITE/login.html"
[ "$(cookie_of login.html)" = "acct=getrennt" ] && record loginInSeparated true || record loginInSeparated false
ORIGIN=$(origin_of login.html)
# Positive control for suggestionsSeparated below: the separated window's
# own command bar offers its page.
key 17 cmd
if waitax "AXWindow \\| Suchen oder URL eingeben" 6; then
  sleep 1; type_in "login"; sleep 2
  $AX dump $PID 14 > "$OUT/ax-separated-suggestions.txt"; key 53; sleep 1
  grep -q -E "AXStaticText \| login, " "$OUT/ax-separated-suggestions.txt" \
    && record suggestionInSeparated true || record suggestionInSeparated false
else
  record suggestionInSeparated false
fi
# WS-ISO-17: hand over to the main Workspace; the separated window hides.
menu Getrennt "Inbox" || fail_setup "separated window menu has no main Workspaces"
$AX dump $PID 14 > "$OUT/ax-menu-separated.txt"
$AX press $PID "$(menuitem Inbox)" >> "$OUT/steps.txt"
waitax "Inbox, Workspace wechseln" 10 && record handOverToMain true || record handOverToMain false
sleep 2; $AX dump $PID 14 > "$OUT/ax-after-handover.txt"
{ ! grep -q 'Getrennt, Workspace wechseln' "$OUT/ax-after-handover.txt"; } && record separatedHidden true || record separatedHidden false
# WS-ISO-02 suggestions: the main window's command bar never offers the
# separated Profile's visit ("login, <url>" row; "<query>, Google" is the
# search row).
key 17 cmd
if waitax "AXWindow \\| Suchen oder URL eingeben" 6; then
  sleep 1; type_in "login"; sleep 2
  $AX dump $PID 14 > "$OUT/ax-main-suggestions.txt"; key 53; sleep 1
  { ! grep -q -E "AXStaticText \| login, " "$OUT/ax-main-suggestions.txt"; } \
    && record suggestionsSeparated true || record suggestionsSeparated false
else
  record suggestionsSeparated false
fi
open_url "$SITE/check.html"
[ -z "$(cookie_of check.html)" ] && record mainNotLoggedIn true || record mainNotLoggedIn false
# Back to the separated Workspace: same page, no reload.
menu Inbox "Getrennt" || fail_setup "main menu has no separated Workspace"
$AX press $PID "$(menuitem Getrennt)" >> "$OUT/steps.txt"
waitax "Getrennt, Workspace wechseln" 10 && record handOverBack true || record handOverBack false
[ -n "$ORIGIN" ] && [ "$(origin_of login.html)" = "$ORIGIN" ] && record noReload true || record noReload false
quit
# WS-ISO-02 history: the separated Profile saw login.html, the main one
# check.html; neither has the other's visit.
ISO=$(iso_dir); echo "separated profile dir: $ISO" >> "$OUT/steps.txt"
H_ISO=$(db_rows "$ISO/History" "select url from urls"); H_MAIN=$(db_rows "$P/Default/History" "select url from urls")
printf 'separated:\n%s\nmain:\n%s\n' "$H_ISO" "$H_MAIN" > "$OUT/history.txt"
[ -n "$ISO" ] && echo "$H_ISO" | grep -q "login.html" && ! echo "$H_ISO" | grep -q "/check.html" \
  && echo "$H_MAIN" | grep -q "/check.html" && ! echo "$H_MAIN" | grep -q "login.html" \
  && record historySeparated true || record historySeparated false
[ -n "$ISO" ] && python3 - "$ISO/Preferences" "$DL_ISO" <<'PY'
import json, sys
path, target = sys.argv[1], sys.argv[2]
d = json.load(open(path))
d.setdefault("download", {}).update({"default_directory": target, "prompt_for_download": False})
d.setdefault("savefile", {})["default_directory"] = target
json.dump(d, open(path, "w"))
PY
# WS-ISO-14: after a relaunch the separated Workspace is still reachable.
launch
if waitax "Getrennt, Workspace wechseln" 5; then record reachableAfterRelaunch true; else
  menu Inbox "Getrennt" && $AX press $PID "$(menuitem Getrennt)" >> "$OUT/steps.txt"
  waitax "Getrennt, Workspace wechseln" 15 && record reachableAfterRelaunch true || record reachableAfterRelaunch false
fi
# The login is a persistent cookie of the separated Profile; tab restore of
# that Profile is a separate question, so check the cookie on a fresh page.
open_url "$SITE/check.html?relaunch"
[ "$(cookie_of 'check.html?relaunch')" = "acct=getrennt" ] && record loginKeptAfterRelaunch true || record loginKeptAfterRelaunch false
# ---- WS-ISO-02 remaining areas, created in the separated window.
# Site permission: notifications allowed through Chromium's prompt.
open_url "$SITE/perm.html"
CDP perm.html Runtime.evaluate '{"expression":"Notification.requestPermission().then(p=>window.__perm=p)","userGesture":true}' >> "$OUT/steps.txt"; echo >> "$OUT/steps.txt"
if waitax "AXButton \\| (Zulassen|Bei jedem Besuch zulassen)" 10; then
  $AX dump $PID 14 > "$OUT/ax-permission-prompt.txt"
  press_protected "Zulassen|Bei jedem Besuch zulassen" \
    "AXButton \\| (Zulassen|Bei jedem Besuch zulassen)"; sleep 2
fi
[ "$(eval_in perm.html 'Notification.permission')" = granted ] && record permissionGrantedInSeparated true || record permissionGrantedInSeparated false
# Saved password and autocomplete entry: typed (CDP insertText is user
# input), submitted, and saved through Chromium's "Passwort speichern?".
open_url "$SITE/form.html"
for field in "u anna" "p geheim-getrennt-1" "n Ahoihafen-getrennt"; do
  eval_in form.html "document.getElementById('${field%% *}').focus()" > /dev/null
  CDP form.html Input.insertText "{\"text\":\"${field#* }\"}" >> "$OUT/steps.txt"; echo >> "$OUT/steps.txt"
done
eval_in form.html "document.forms[0].requestSubmit()" > /dev/null
waiturl done.html 10 || echo "info: form did not submit" >> "$OUT/steps.txt"
if waitax "Passwort speichern" 10; then
  $AX dump $PID 14 > "$OUT/ax-password-bubble.txt"
  press_protected Speichern "Passwort speichern\\?"; sleep 2
  record passwordOfferedInSeparated true
else
  record passwordOfferedInSeparated false
fi
# Download into the separated Profile's own directory.
open_url "$SITE/dl.html"
eval_in dl.html "document.getElementById('d').click()" > /dev/null
end=$(( $(date +%s) + 20 )); while [ $(date +%s) -lt $end ] && [ ! -f "$DL_ISO/note.txt" ]; do sleep 1; done
[ -f "$DL_ISO/note.txt" ] && [ -z "$(ls -A "$DL_MAIN")" ] && record downloadInSeparatedDir true || record downloadInSeparatedDir false
# ---- WS-ISO-03: uBO in the separated Profile only.
UBO_SITE=http://isotest.localhost:$SITE_PORT; UBO_RAN=0
if [ "${AHOI_E2E_SKIP_NETWORK:-0}" = 1 ]; then
  echo "info: WS-ISO-03 skipped (AHOI_E2E_SKIP_NETWORK=1)" >> "$OUT/steps.txt"
else
  UBO_RAN=1; key 17 cmd
  waitax "AXWindow \\| Suchen oder URL eingeben" 6 || fail_setup "command bar did not open for uBO"
  sleep 1; type_in "uBlock Origin Classic"; sleep 2; key 36; sleep 2
  waitax "AXButton \\| (uBlock Origin Classic installieren|Sicher prüfen|Verifiziertes Paket herunterladen)" 10 \
    || fail_setup "uBO installer did not open in the separated window"
  end=$(( $(date +%s) + 120 ))
  while [ $(date +%s) -lt $end ] && ! ubo_running; do
    if $AX dump $PID 14 | grep -q -E "AXButton \| [^|]*hinzufügen"; then
      $AX dump $PID 14 > "$OUT/ax-ubo-prompt.txt"; press_label "[^|]*hinzufügen"; sleep 3
    else
      press_label "uBlock Origin Classic installieren" || press_label "Verifiziertes Paket herunterladen" || press_label "Sicher prüfen"
      sleep 3
    fi
  done
  ubo_running && record uboInstalledInSeparated true || record uboInstalledInSeparated false
  press_label "Schließen"; sleep 1
  open_url "$UBO_SITE/ad.html?getrennt"
  hidden_within 'ad.html?getrennt' 40 && record uboFiltersInSeparated true || record uboFiltersInSeparated false
fi
# The main Profile sees none of it.
menu Getrennt "Inbox" || fail_setup "separated window menu has no main Workspaces"
$AX press $PID "$(menuitem Inbox)" >> "$OUT/steps.txt"
waitax "Inbox, Workspace wechseln" 10 || fail_setup "hand-over to Inbox for the WS-ISO-02 checks failed"
open_url "$SITE/perm.html?main"
[ "$(eval_in 'perm.html?main' 'Notification.permission')" = default ] && record permissionNotInMain true || record permissionNotInMain false
if [ $UBO_RAN = 1 ]; then
  open_url "$UBO_SITE/ad.html?main"; sleep 5
  [ "$(ad_display 'ad.html?main')" = block ] && record uboNotActiveInMain true || record uboNotActiveInMain false
fi
menu Inbox "Getrennt" || fail_setup "main menu has no separated Workspace"
$AX press $PID "$(menuitem Getrennt)" >> "$OUT/steps.txt"
waitax "Getrennt, Workspace wechseln" 10 || fail_setup "hand-over back to Getrennt failed"
# Stored state per Profile, read after a clean quit.
quit
ISO=$(iso_dir)
L_ISO=$(db_rows "$ISO/Login Data" "select origin_url, username_value from logins")
L_MAIN=$(db_rows "$P/Default/Login Data" "select origin_url, username_value from logins")
A_ISO=$(db_rows "$ISO/Web Data" "select name, value from autofill")
A_MAIN=$(db_rows "$P/Default/Web Data" "select name, value from autofill")
D_ISO=$(db_rows "$ISO/History" "select target_path from downloads")
D_MAIN=$(db_rows "$P/Default/History" "select target_path from downloads")
printf 'logins separated:\n%s\nlogins main:\n%s\nautofill separated:\n%s\nautofill main:\n%s\ndownloads separated:\n%s\ndownloads main:\n%s\n' \
  "$L_ISO" "$L_MAIN" "$A_ISO" "$A_MAIN" "$D_ISO" "$D_MAIN" > "$OUT/profile-stores.txt"
echo "$L_ISO" | grep -q "127.0.0.1:$SITE_PORT.*|anna" && ! echo "$L_MAIN" | grep -q "anna" \
  && record passwordsSeparated true || record passwordsSeparated false
echo "$A_ISO" | grep -q "^ahoinote|Ahoihafen-getrennt" && ! echo "$A_MAIN" | grep -q "Ahoihafen" \
  && record autofillSeparated true || record autofillSeparated false
echo "$D_ISO" | grep -q "note.txt" && ! echo "$D_MAIN" | grep -q "note.txt" \
  && record downloadHistorySeparated true || record downloadHistorySeparated false
python3 - "$ISO/Preferences" "$P/Default/Preferences" "127.0.0.1:$SITE_PORT" > "$OUT/permissions.json" <<'PY'
import json, sys
def granted(path, host):
    try:
        ex = json.load(open(path))["profile"]["content_settings"]["exceptions"].get("notifications", {})
    except (OSError, KeyError, ValueError):
        return []
    return [k for k, v in ex.items() if host in k and v.get("setting") == 1]
print(json.dumps({"separated": granted(sys.argv[1], sys.argv[3]), "main": granted(sys.argv[2], sys.argv[3])}))
PY
python3 -c 'import json,sys;d=json.load(open(sys.argv[1]));sys.exit(0 if d["separated"] and not d["main"] else 1)' "$OUT/permissions.json" \
  && record sitePermissionsSeparated true || record sitePermissionsSeparated false
if [ $UBO_RAN = 1 ]; then
  [ -d "$ISO/Extensions/$UBO_ID" ] && [ ! -d "$P/Default/Extensions/$UBO_ID" ] \
    && record extensionsSeparated true || record extensionsSeparated false
fi
# Back to the separated window for the deletion below.
launch
if ! waitax "Getrennt, Workspace wechseln" 5; then
  menu Inbox "Getrennt" && $AX press $PID "$(menuitem Getrennt)" >> "$OUT/steps.txt"
  waitax "Getrennt, Workspace wechseln" 15 || fail_setup "separated Workspace not reachable before the deletion"
fi
# WS-ISO-16: delete from the separated window; the main window comes back.
menu Getrennt "Workspace löschen" || fail_setup "delete item missing in separated window"
$AX press $PID "$($AX dump $PID 14 | grep -o 'Workspace löschen[^|]*' | head -1 | sed 's/ *$//')" >> "$OUT/steps.txt"
waitax "AXButton \\| Löschen" 8 || fail_setup "delete dialog did not open"
$AX dump $PID 14 > "$OUT/ax-delete-dialog.txt"
grep -q 'vollständig getrennt' "$OUT/ax-delete-dialog.txt" && record deleteDialogNamesLevel true || record deleteDialogNamesLevel false
$AX press $PID "AXButton:Löschen" >> "$OUT/steps.txt"; sleep 8
$AX dump $PID 14 > "$OUT/ax-after-delete.txt"
{ ! grep -q 'Getrennt, Workspace wechseln' "$OUT/ax-after-delete.txt"; } && grep -q 'Inbox, Workspace wechseln' "$OUT/ax-after-delete.txt" \
  && record mainBackAfterDelete true || record mainBackAfterDelete false
{ ! grep -qi 'Profil auswählen\|Wer verwendet\|Who.s using' "$OUT/ax-after-delete.txt"; } && record noChromiumProfileUi true || record noChromiumProfileUi false
quit
launch; sleep 6; quit
python3 - "$P" > "$OUT/storage.json" <<'PY'
import json,os,sys
root=sys.argv[1]
ls=json.load(open(os.path.join(root,"Local State")))
entries=ls.get("ahoi",{}).get("isolated_profiles",[])
dirs=[d for d in os.listdir(root) if d.startswith("Profile ")]
print(json.dumps({"registry":entries,"profileDirs":dirs}))
PY
python3 -c 'import json,sys;d=json.load(open(sys.argv[1]));sys.exit(0 if not d["registry"] and not d["profileDirs"] else 1)' "$OUT/storage.json" \
  && record profileAndEntryGone true || record profileAndEntryGone false
finish ""
