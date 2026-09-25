#!/bin/bash
# usage: link-peek-journey.sh <App.app> <outdir>
# WORKFLOW-02 Link-Peek on the installed candidate: the link context menu
# previews a link over its page in the Workspace's own website session;
# closing keeps the page below unchanged; promotion to a tab keeps the same
# page without a reload. HID keys, PID-scoped AX, CDP reads and clicks.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9347
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-peek-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8794}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
printf '<title>page</title><script>document.cookie="acct=kunde; max-age=3600; path=/"</script><input id=draft><a id=l href="/target.html" style="display:block;width:320px;height:90px;background:#ddd">target link</a>' > $P-site/page.html
printf '<title>target</title>target page' > $P-site/target.html
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT; SITE=http://127.0.0.1:$SITE_PORT
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
tabs() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(json.dumps(sorted([t["url"] for t in json.load(sys.stdin) if t["type"]=="page"])))'; }
launch() {
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT about:blank >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$P" >> "$OUT/run.txt"
  for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  sleep 4; $AX activate $PID >> "$OUT/steps.txt"
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
    waitax "$2" 4 && return 0
    $AX key $PID 53 >> "$OUT/steps.txt"; sleep 2
  done
  return 1
}
# Workspace menu items carry their level in the title ("Kunde – Eigene
# Website-Sitzungen"); resolve a Workspace name to its full item title.
menuitem() { # <workspace name>
  $AX dump $PID 14 | grep -oE "AXMenuItem \| $1( – [^|]*)? \|" | head -1 | sed -E 's/^AXMenuItem \| //; s/ \|$//'
}
newws() { # <active> <name> <level radio label or "">
  menu "$1" "Neuer Workspace…" || fail_setup "workspace menu did not open for $2"
  $AX press $PID "Neuer Workspace…" >> "$OUT/steps.txt"
  waitax "AXTextField \\| Workspace-Name" 8 || fail_setup "workspace dialog for $2 did not open"
  $AX dump $PID 14 > "$OUT/ax-dialog-$2.txt"
  $AX setvalue $PID "Workspace-Name" "$2" >> "$OUT/steps.txt"; sleep 1
  if [ -n "$3" ]; then $AX press $PID "AXRadioButton:$3" >> "$OUT/steps.txt"; sleep 1; fi
  $AX press $PID "Erstellen" >> "$OUT/steps.txt"
  waitax "$2, Workspace wechseln" 8 || fail_setup "workspace $2 not active"
  # The create dialog must be gone, or it keeps key focus.
  local end=$(( $(date +%s) + 6 ))
  while [ $(date +%s) -lt $end ] && $AX dump $PID 14 | grep -q "AXButton | Erstellen"; do sleep 1; done
  if $AX dump $PID 14 | grep -q "AXButton | Erstellen"; then
    $AX dump $PID 14 > "$OUT/ax-dialog-still-open-$2.txt"; record "dialogClosed_$2" false
  else
    record "dialogClosed_$2" true
  fi
}
open_url() { # <url> ; ⌘T + type + Return
  local opened=0
  for attempt in 1 2 3; do
    $AX activate $PID >> "$OUT/steps.txt"; sleep 1; key 17 cmd
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 && { opened=1; break; }
  done
  [ $opened = 1 ] || fail_setup "command bar did not open for $1"
  sleep 1; $AX type $PID "$1" >> "$OUT/steps.txt"; sleep 1; key 36
  waiturl "$1" 20 || fail_setup "did not load $1"; sleep 2
}
switchws() { # <active> <target>
  menu "$1" "$2" || fail_setup "menu to switch to $2 did not open"
  $AX press $PID "$(menuitem "$2")" >> "$OUT/steps.txt"; waitax "$2, Workspace wechseln" 8 || fail_setup "switch to $2 failed"
}
cookie_of() { # <page url substring> ; retries while the page settles
  local value="" i
  for i in 1 2 3 4 5; do
    value=$(CDP "$1" Runtime.evaluate '{"expression":"document.cookie","returnByValue":true}' | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))')
    [ -n "$value" ] && break; sleep 1
  done
  echo "cookie $1: $value" >> "$OUT/steps.txt"; echo "$value"
}
# The before-unload question of a group close is a native macOS alert
# ("Website verlassen?"), not a page dialog CDP can answer; press its button
# through AX and fall back to CDP only when no alert appears.
unload_prompt() { # <page url substring> <accept|cancel>
  local button=Abbrechen; [ "$2" = accept ] && button=Verlassen
  if waitax "AXStaticText \\| Website verlassen" 8; then
    $AX dump $PID 14 > "$OUT/ax-unload-prompt-$2.txt"
    $AX press $PID "$button" >> "$OUT/steps.txt"; return 0
  fi
  CDP "$1" Page.handleJavaScriptDialog "{\"accept\":$([ "$2" = accept ] && echo true || echo false)}" > "$OUT/dialog-$2.json"
  ! grep -q '"error"' "$OUT/dialog-$2.json"
}
eval_in() { # <url substring> <expression>
  CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True}))' "$2")" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'
}
peek_link() { # right-click the link, choose the Peek item
  local xy; xy=$(eval_in page.html "(()=>{const r=document.getElementById('l').getBoundingClientRect();return Math.round(r.x+r.width/2)+' '+Math.round(r.y+r.height/2)})()")
  local x=${xy% *} y=${xy#* }
  CDP page.html Input.dispatchMouseEvent "{\"type\":\"mousePressed\",\"x\":$x,\"y\":$y,\"button\":\"right\",\"clickCount\":1}" >/dev/null
  CDP page.html Input.dispatchMouseEvent "{\"type\":\"mouseReleased\",\"x\":$x,\"y\":$y,\"button\":\"right\",\"clickCount\":1}" >/dev/null
  waitax "AXMenuItem \\| Link in Vorschau öffnen" 6 || return 1
  $AX dump $PID 14 > "$OUT/ax-context-menu.txt"
  $AX press $PID "Link in Vorschau öffnen" >> "$OUT/steps.txt"
  waitax "Popup schließen" 10
}

launch
newws Inbox Kunde "Eigene Website-Sitzungen"
open_url "$SITE/page.html"
eval_in page.html "document.getElementById('draft').value='entwurf';'ok'" >> "$OUT/steps.txt"
# WORKFLOW-02 Peek: offered in the link menu, shown over the page.
peek_link && record peekShown true || record peekShown false
waiturl "target.html" 10 && record peekLoadsLink true || record peekLoadsLink false
# Same website session as the page below (the Workspace's own login).
[ "$(cookie_of target.html)" = "acct=kunde" ] && record peekKeepsWebsiteSession true || record peekKeepsWebsiteSession false
# Close keeps the page below exactly as it was.
$AX press $PID "Popup schließen" >> "$OUT/steps.txt"; sleep 2
{ ! waitax "Popup schließen" 2; } && record peekCloses true || record peekCloses false
{ ! tabs | grep -q target.html; } && record peekLeavesNoTab true || record peekLeavesNoTab false
[ "$(eval_in page.html "document.getElementById('draft').value")" = "entwurf" ] && record pageStateKept true || record pageStateKept false
# Promotion to a tab keeps the same page (no reload).
peek_link || fail_setup "second peek did not open"
waiturl "target.html" 10 || fail_setup "second peek did not load"
eval_in target.html "window.__ahoiPeekMark=7;'marked'" >> "$OUT/steps.txt"
$AX press $PID "Popup als Tab öffnen" >> "$OUT/steps.txt"; sleep 3
{ ! waitax "Popup schließen" 2; } && [ "$(eval_in target.html "String(window.__ahoiPeekMark)")" = "7" ] \
  && record promoteKeepsSamePage true || record promoteKeepsSamePage false
waitax "Kunde, Workspace wechseln" 3 && record promotedInSameWorkspace true || record promotedInSameWorkspace false
$AX dump $PID 14 > "$OUT/ax-final.txt"
finish; quit
