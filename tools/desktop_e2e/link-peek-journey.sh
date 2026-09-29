#!/bin/bash
# usage: link-peek-journey.sh <App.app> <outdir>
# WORKFLOW-02 Link-Peek on the installed candidate: the link context menu
# previews a link over its page in the Workspace's own website session;
# closing keeps the page below unchanged; promotion to a tab keeps the same
# page without a reload. Every Peek repeats the link's own request (Crest
# adoption A1): the site server logs Referer and Sec-Fetch-Site per request.
# Automatic Peek also takes plain target=_blank clicks of saved pages, loaded
# once, while modifier clicks, same-site links and temporary pages keep their
# tab (A4). HID keys, PID-scoped AX, CDP reads and clicks.
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
OTHER=http://localhost:$SITE_PORT; SMALL="display:block;width:320px;height:40px;background:#cdf"
cat > $P-site/page.html <<HTML
<title>page</title><script>document.cookie="acct=kunde; max-age=3600; path=/"</script><input id=draft>
<a id=l rel=noreferrer href="/target.html" style="display:block;width:320px;height:90px;background:#ddd">target link</a>
<a id=x href="$OTHER/target.html" style="display:block;width:320px;height:90px;background:#cdf">other site</a>
<a id=xr rel=noreferrer href="$OTHER/target.html?noreferrer" style="$SMALL">noreferrer</a>
<a id=xb target=_blank href="$OTHER/target.html?blank" style="$SMALL">blank</a>
<a id=xc target=_blank href="$OTHER/target.html?cmdshift" style="$SMALL">cmd shift</a>
<a id=xs target=_blank href="/target.html?samesite" style="$SMALL">same site blank</a>
HTML
printf '<title>target</title>target page' > $P-site/target.html
# The site server logs what each request carried (A1 evidence).
REQ="$OUT/requests.jsonl"; : > "$REQ"
cat > $P-server.py <<'PY'
import http.server, json, sys
port, root, log = int(sys.argv[1]), sys.argv[2], sys.argv[3]
class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=root, **kwargs)
    def do_GET(self):
        with open(log, "a") as f:
            f.write(json.dumps({"path": self.path, "host": self.headers.get("Host"),
                                "referer": self.headers.get("Referer"),
                                "site": self.headers.get("Sec-Fetch-Site")}) + "\n")
        super().do_GET()
http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
PY
python3 $P-server.py $SITE_PORT $P-site "$REQ" > "$OUT/site.log" 2>&1 &
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
PAGE_MATCH=page.html
# Request log: mark before an action, then read the first matching request
# after the mark. Prints "-" for an absent header, "missing" without request.
mark() { wc -l < "$REQ" | tr -d ' '; }
req_field() { # <mark> <path substring> <referer|site>
  python3 - "$REQ" "$@" <<'PY'
import json, sys
log, start, needle, field = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
hits = [json.loads(l) for l in open(log).read().splitlines()[start:]]
hits = [h for h in hits if needle in h["path"]]
print("missing" if not hits else (hits[0].get(field) or "-"))
PY
}
req_count() { # <mark> <path substring>
  tail -n +$(( $1 + 1 )) "$REQ" | grep -c -F -- "$2"
}
check_req() { # <name> <mark> <path substring> <referer|site> <expected>
  local got; got=$(req_field "$2" "$3" "$4")
  echo "$1: $4=$got" >> "$OUT/steps.txt"
  [ "$got" = "$5" ] && record "$1" true || record "$1" false
}
# CDP modifiers: Alt 1, Ctrl 2, Meta (Cmd) 4, Shift 8.
click_link() { # <link id> <CDP modifiers bitmask>; a real left click
  local xy; xy=$(eval_in "$PAGE_MATCH" "(()=>{const e=document.getElementById('$1');e.scrollIntoView({block:'center'});const r=e.getBoundingClientRect();return Math.round(r.x+r.width/2)+' '+Math.round(r.y+r.height/2)})()")
  local x=${xy% *} y=${xy#* }
  CDP "$PAGE_MATCH" Input.dispatchMouseEvent "{\"type\":\"mousePressed\",\"x\":$x,\"y\":$y,\"button\":\"left\",\"clickCount\":1,\"modifiers\":$2}" >/dev/null
  CDP "$PAGE_MATCH" Input.dispatchMouseEvent "{\"type\":\"mouseReleased\",\"x\":$x,\"y\":$y,\"button\":\"left\",\"clickCount\":1,\"modifiers\":$2}" >/dev/null
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
M=$(mark)
peek_link && record peekShown true || record peekShown false
waiturl "target.html" 10 && record peekLoadsLink true || record peekLoadsLink false
# #l is rel=noreferrer: the context-menu Peek sends no Referer (it sent the
# page URL before patch 0077).
check_req contextPeekKeepsNoReferrer "$M" /target.html referer -
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
# Opt-in entry points (both default off): automatic Peek from saved pages to
# other sites, and Shift-click. Set in this test profile before a relaunch.
quit
python3 - "$P/Default/Preferences" <<'PY'
import json,sys
p=sys.argv[1]; d=json.load(open(p))
d.setdefault("ahoi",{}).setdefault("peek",{}).update({"auto_from_saved_pages":True,"shift_click":True})
json.dump(d,open(p,"w"))
PY
launch
open_url "$SITE/page.html?saved"; PAGE_MATCH="page.html?saved"
key 2 cmd; sleep 2   # ⌘D saves the page to the tree
M=$(mark); click_link x 0
waitax "Popup schließen" 8 && record autoPeekFromSavedPage true || record autoPeekFromSavedPage false
tabs | grep -q 'page.html?saved' && record savedPageStays true || record savedPageStays false
# The page's default policy sends only its origin to the other site.
check_req autoPeekSendsOriginOnly "$M" /target.html referer "$SITE/"
check_req autoPeekIsCrossSite "$M" /target.html site cross-site
$AX press $PID "Popup schließen" >> "$OUT/steps.txt"; sleep 2
M=$(mark); click_link xr 0
waitax "Popup schließen" 8 && record autoPeekNoReferrerShown true || record autoPeekNoReferrerShown false
check_req autoPeekKeepsNoReferrer "$M" "?noreferrer" referer -
$AX press $PID "Popup schließen" >> "$OUT/steps.txt"; sleep 2
# A4: a plain target=_blank click previews the page, requested once, and
# leaves no tab behind.
M=$(mark); click_link xb 0
waitax "Popup schließen" 8 && record blankLinkPeeks true || record blankLinkPeeks false
sleep 2; [ "$(req_count "$M" "?blank")" = 1 ] && record blankLinkLoadsOnce true || record blankLinkLoadsOnce false
$AX press $PID "Popup schließen" >> "$OUT/steps.txt"; sleep 2
{ ! tabs | grep -q -F "?blank"; } && record blankLinkLeavesNoTab true || record blankLinkLeavesNoTab false
M=$(mark); click_link l 8   # Shift-click
waitax "Popup schließen" 8 && record shiftClickPeeks true || record shiftClickPeeks false
check_req shiftClickKeepsNoReferrer "$M" /target.html referer -
$AX press $PID "Popup schließen" >> "$OUT/steps.txt"; sleep 1
# Command entry: Shift+Return in the command bar previews the address.
key 17 cmd
if waitax "AXWindow \\| Suchen oder URL eingeben" 6; then
  M=$(mark); sleep 1; $AX type $PID "$SITE/target.html?command" >> "$OUT/steps.txt"; sleep 1; key 36 shift
  waitax "Popup schließen" 8 && record commandShiftReturnPeeks true || record commandShiftReturnPeeks false
  # Like the omnibox: no Referer and no initiator.
  check_req commandPeekSendsNoReferer "$M" "?command" referer -
  check_req commandPeekIsBrowserInitiated "$M" "?command" site none
  $AX press $PID "Popup schließen" >> "$OUT/steps.txt"; sleep 1
else
  record commandShiftReturnPeeks false
fi
# A4 exclusions. Each opens a real tab, so bring the saved page back first.
CDP "$PAGE_MATCH" Page.bringToFront '{}' >/dev/null; sleep 1
click_link xc 12   # Cmd+Shift-click on a target=_blank link
{ ! waitax "Popup schließen" 4; } && waiturl "?cmdshift" 10 \
  && record cmdShiftBlankStaysTab true || record cmdShiftBlankStaysTab false
CDP "$PAGE_MATCH" Page.bringToFront '{}' >/dev/null; sleep 1
click_link xs 0    # same-site target=_blank
{ ! waitax "Popup schließen" 4; } && waiturl "?samesite" 10 \
  && record sameSiteBlankStaysTab true || record sameSiteBlankStaysTab false
open_url "$SITE/page.html?temp"; PAGE_MATCH="page.html?temp"
click_link xb 0    # target=_blank from a temporary, unsaved tab
{ ! waitax "Popup schließen" 4; } && waiturl "?blank" 10 \
  && record temporaryPageBlankStaysTab true || record temporaryPageBlankStaysTab false
$AX dump $PID 14 > "$OUT/ax-final.txt"
finish; quit
