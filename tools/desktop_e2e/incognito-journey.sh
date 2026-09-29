#!/bin/bash
# usage: incognito-journey.sh <App.app> <outdir>
# INC-01/02/03/05 on the installed candidate: ⌘⇧N opens a real off-the-record
# window (cookies of the normal session are invisible there and vice versa),
# nothing of it reaches history, tree, session files or any profile file,
# and after a crash with both windows open only the normal session comes
# back. INC-04 (extension only after explicit incognito allowance) needs an
# installed extension and is not covered here. HID keys, AX, CDP reads.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9395
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-incognito-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8815}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
printf '<title>normalpage</title><script>document.cookie="acct=normal; max-age=3600; path=/"</script>normal page' > $P-site/normal.html
printf '<title>incpage</title><script>document.cookie="inc=secret; max-age=3600; path=/"</script>incognito page' > $P-site/inc.html
printf '<title>checkpage</title>check page' > $P-site/check.html
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT; SITE=http://127.0.0.1:$SITE_PORT
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
pages() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(json.dumps(sorted([t["url"] for t in json.load(sys.stdin) if t["type"]=="page"])))'; }
target_of() { # <url substring> ; DevTools target ids of matching pages
  curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(" ".join(t["id"] for t in json.load(sys.stdin) if t["type"]=="page" and sys.argv[1] in t["url"]))' "$1"
}
launch() {
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT about:blank >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$P" >> "$OUT/run.txt"
  for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  sleep 4; $AX activate $PID >> "$OUT/steps.txt"
}
quit() { key 12 cmd; for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || return 0; sleep 1; done; echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3; }
# Keys go through the HID event tap like a real keyboard; hidkey refuses
# unless the app is frontmost, so bring it forward and retry.
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
waiturl() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do pages | grep -q "$1" && return 0; sleep 1; done; return 1; }
RESULTS=(); record() { RESULTS+=("\"$1\": $2"); echo "$1 -> $2" >> "$OUT/steps.txt"; }
finish() {
  local joined; joined=$(IFS=,; echo "${RESULTS[*]}")
  local sep=""; [ -n "$joined" ] && sep=", "
  echo "{${joined}${1:+$sep\"setupFailed\": \"$1\"}}" | python3 -c 'import json,sys;d=json.load(sys.stdin);d["pass"]=("setupFailed" not in d) and all(v is True for k,v in d.items() if k!="setupFailed");print(json.dumps(d,indent=1))' > "$OUT/verdict.json"
  cat "$OUT/verdict.json"
}
fail_setup() { $AX dump $PID 14 > "$OUT/ax-setup-failure.txt"; finish "$1"; quit; exit 4; }
eval_in() { # <url substring> <expression>
  CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True}))' "$2")" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'
}
open_url() { # <url> ; ⌘T + type + Return in the normal window
  local opened=0
  for attempt in 1 2 3; do
    $AX activate $PID >> "$OUT/steps.txt"; sleep 1; key 17 cmd
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 && { opened=1; break; }
  done
  [ $opened = 1 ] || fail_setup "command bar did not open for $1"
  sleep 1; type_in "$1"; key 36
  waiturl "$1" 20 || fail_setup "did not load $1"; sleep 2
}


cookie_of() { eval_in "$1" 'document.cookie'; }
MARK=inc.html
launch
open_url "$SITE/normal.html"
[ "$(cookie_of normal.html)" = "acct=normal" ] && record normalCookieSet true || record normalCookieSet false
# INC-01: ⌘⇧N opens a new window whose pages see none of the normal cookies.
key 45 cmd shift; sleep 3
$AX dump $PID 14 > "$OUT/ax-incognito-window.txt"
open_url "$SITE/$MARK"
C=$(cookie_of $MARK); echo "incognito cookies: $C" >> "$OUT/steps.txt"
[ "$C" = "inc=secret" ] && record incognitoIsolatedFromNormal true || record incognitoIsolatedFromNormal false
# INC-03 (tree): the incognito page never shows up in the sidebar tree.
$AX dump $PID 14 | grep -q 'incpage — ' && record notInTree false || record notInTree true
# INC-02: closing the incognito window leaves the normal session as it was.
key 13 cmd shift; sleep 3
pages | grep -q "$MARK" && record incognitoWindowClosed false || record incognitoWindowClosed true
C=$(cookie_of normal.html); echo "normal cookies after close: $C" >> "$OUT/steps.txt"
[ "$C" = "acct=normal" ] && record normalSessionUntouched true || record normalSessionUntouched false
# INC-05: crash with a normal and an incognito window open.
key 45 cmd shift; sleep 3; open_url "$SITE/$MARK?crash"
kill -9 $PID; sleep 3
grep -rla --exclude-dir=Crashpad "$MARK" "$P" > "$OUT/profile-hits-after-crash.txt" 2>/dev/null
[ ! -s "$OUT/profile-hits-after-crash.txt" ] && record nothingOnDiskAfterCrash true || record nothingOnDiskAfterCrash false
launch; sleep 4
$AX dump $PID 14 > "$OUT/ax-after-relaunch.txt"
grep -q 'AXWindow | .*\(incpage\|Inkognito\)' "$OUT/ax-after-relaunch.txt" && record noIncognitoWindowBack false || record noIncognitoWindowBack true
# Chromium's crash prompt offers the restore; accept it and check that only
# the normal page comes back (the button text carries soft hyphens).
RESTORE=$(grep -o 'AXButton | Wieder[^|]*' "$OUT/ax-after-relaunch.txt" | head -1 | sed -e 's/^AXButton | //' -e 's/ *$//')
if [ -n "$RESTORE" ]; then
  record restoreOffered true
  $AX press $PID "$RESTORE" >> "$OUT/steps.txt"
  waiturl normal.html 15 && record normalSessionRestored true || record normalSessionRestored false
else
  record restoreOffered false
fi
sleep 2; AFTER=$(pages); echo "after restore $AFTER" >> "$OUT/pages.txt"
echo "$AFTER" | grep -q "$MARK" && record incognitoNotRestored false || record incognitoNotRestored true
$AX dump $PID 14 > "$OUT/ax-final.txt"
quit
# INC-03 (history, session restore, any profile file): nothing on disk.
grep -rla --exclude-dir=Crashpad "$MARK" "$P" > "$OUT/profile-hits-final.txt" 2>/dev/null
[ ! -s "$OUT/profile-hits-final.txt" ] && record nothingOnDiskFinal true || record nothingOnDiskFinal false
[ -f "$P/Default/History" ] && python3 - "$P/Default/History" > "$OUT/history.txt" <<'PY'
import shutil,sqlite3,sys,tempfile
t=tempfile.mktemp(); shutil.copy(sys.argv[1],t)
print("\n".join(r[0] for r in sqlite3.connect(t).execute("select url from urls")))
PY
grep -q normal.html "$OUT/history.txt" 2>/dev/null && ! grep -q "$MARK" "$OUT/history.txt" \
  && record historyOnlyNormal true || record historyOnlyNormal false
finish
