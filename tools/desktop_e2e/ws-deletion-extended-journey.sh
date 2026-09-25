#!/bin/bash
# usage: ws-deletion-extended-journey.sh <App.app> <outdir>
# Crest handoff 015 extensions of the Workspace deletion journey:
# WS-DEL-07 (a page opened while the deletion prompt shows closes too),
# WS-DEL-08 (no page of the deleted Workspace appears in the fallback while
# closing), WS-DEL-04 (kill -9 right after the confirmation, relaunch
# finishes the cleanup) and WS-DEL-06 (startup cleanup never recreates the
# partition directory). Helpers are copied from ws-level-deletion-journey.sh.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9346
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-wsdelx-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8793}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
printf '<title>login</title><script>document.cookie="acct=kunde; max-age=3600; path=/"</script>logged in' > $P-site/login.html
printf '<title>check</title>check' > $P-site/check.html
printf '<title>unload</title><script>addEventListener("beforeunload",e=>{e.preventDefault();e.returnValue=""})</script><button id=b style="width:400px;height:300px">tap</button>' > $P-site/unload.html
printf '<title>shared</title>shared page' > $P-site/shared.html
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
cookie_of() { CDP "$1" Runtime.evaluate '{"expression":"document.cookie","returnByValue":true}' | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'; }
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
delete_active() { # <active>
  menu "$1" "Workspace löschen" || fail_setup "delete item missing for $1"
  $AX press $PID "$($AX dump $PID 14 | grep -o 'Workspace löschen[^|]*' | head -1 | sed 's/ *$//')" >> "$OUT/steps.txt"
  waitax "AXButton \\| Löschen" 8 || fail_setup "delete dialog for $1 did not open"
  $AX dump $PID 14 > "$OUT/ax-delete-$1.txt"
  $AX press $PID "AXButton:Löschen" >> "$OUT/steps.txt"
}

printf '<title>late</title>late page' > $P-site/late.html
storage() {
  python3 - "$P/Default/Preferences" "$P/Default" <<'PY'
import json,os,sys
try: p=json.load(open(sys.argv[1])).get("ahoi",{}).get("session",{})
except Exception: p={}
b=p.get("website_session_bindings",{}).get("workspaces",{})
root=os.path.join(sys.argv[2],"Storage","ext","ahoi")
dirs=sorted(os.listdir(root)) if os.path.isdir(root) else []
print(json.dumps({"ownBindings":[k for k,v in b.items() if v!="default"],"pending":p.get("website_session_pending_removals",[]),"partitionDirs":dirs}))
PY
}

launch
newws Inbox Kunde "Eigene Website-Sitzungen"
open_url "$SITE/login.html"
open_url "$SITE/unload.html"
CDP unload.html Input.dispatchMouseEvent '{"type":"mousePressed","x":100,"y":100,"button":"left","clickCount":1}' >/dev/null
CDP unload.html Input.dispatchMouseEvent '{"type":"mouseReleased","x":100,"y":100,"button":"left","clickCount":1}' >/dev/null
delete_active Kunde; sleep 3
# WS-DEL-07: while the prompt shows, try to open another page in the same
# Workspace. The native prompt may block that; then nothing late exists.
waitax "AXStaticText \\| Website verlassen" 8 && record promptShown true || record promptShown false
key 17 cmd
if waitax "AXWindow \\| Suchen oder URL eingeben" 4; then
  $AX type $PID "$SITE/late.html" >> "$OUT/steps.txt"; sleep 1; key 36; sleep 2
  echo "latePage opened" >> "$OUT/steps.txt"
else
  echo "latePage blocked by the prompt" >> "$OUT/steps.txt"
fi
echo "whilePrompt $(tabs)" >> "$OUT/tabs.txt"
unload_prompt unload.html accept >> "$OUT/steps.txt"
# WS-DEL-08: poll while the pages close; none may show up under the fallback.
REHOMED=false
for i in $(seq 1 15); do
  if $AX dump $PID 14 | grep -E 'Inbox, Workspace wechseln' -q && $AX dump $PID 14 | grep -E '\| (unload|late|login)( |$)' -q; then REHOMED=true; $AX dump $PID 14 > "$OUT/ax-rehomed-$i.txt"; fi
  sleep 0.3
done
[ $REHOMED = false ] && record noRehomingWhileClosing true || record noRehomingWhileClosing false
sleep 4; T=$(tabs); echo "afterDelete $T" >> "$OUT/tabs.txt"
{ ! echo "$T" | grep -q 'login.html\|unload.html\|late.html'; } && record latePageClosedToo true || record latePageClosedToo false
# WS-DEL-04: second own Workspace, killed right after the confirmation.
newws Inbox Zwei "Eigene Website-Sitzungen"
open_url "$SITE/login.html?zwei"
menu Zwei "Workspace löschen" || fail_setup "delete item missing for Zwei"
$AX press $PID "$($AX dump $PID 14 | grep -o 'Workspace löschen[^|]*' | head -1 | sed 's/ *$//')" >> "$OUT/steps.txt"
waitax "AXButton \\| Löschen" 8 || fail_setup "delete dialog for Zwei did not open"
$AX press $PID "AXButton:Löschen" >> "$OUT/steps.txt"; kill -9 $PID; sleep 2
echo "afterKill $(storage)" >> "$OUT/storage.txt"
# WS-DEL-06: relaunch; cleanup finishes and the directory never comes back.
launch
REAPPEARED=false
for i in $(seq 1 6); do
  sleep 5; S_NOW=$(storage); echo "t+$((i*5))s $S_NOW" >> "$OUT/storage.txt"
  # The first sample may still show the directory being deleted.
  if [ $i -ge 2 ] && ! echo "$S_NOW" | grep -q '"partitionDirs": \[\]'; then REAPPEARED=true; fi
done
[ $REAPPEARED = false ] && record partitionNotRecreatedAtStartup true || record partitionNotRecreatedAtStartup false
waitax "Inbox, Workspace wechseln" 6 && ! $AX dump $PID 14 | grep -q 'Zwei, Workspace wechseln' && record killedDeletionStaysDeleted true || record killedDeletionStaysDeleted false
quit
FINAL=$(storage); echo "final $FINAL" >> "$OUT/storage.txt"
echo "$FINAL" | python3 -c 'import json,sys;d=json.load(sys.stdin);sys.exit(0 if not d["ownBindings"] and not d["pending"] and not d["partitionDirs"] else 1)' \
  && record bindingAndPartitionGoneAfterCrash true || record bindingAndPartitionGoneAfterCrash false
finish ""
