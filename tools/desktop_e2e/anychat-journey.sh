#!/bin/bash
# usage: anychat-journey.sh <App.app> <outdir>
# DoD 8 AnyChat on the installed candidate with a fresh throwaway profile,
# through the ordinary Chrome Web Store flow. The owner approved AnyChat
# 1.0.8 with 14 AI-site origins, New Tab and favicon access (5 Sep, again
# 29 Sep 2026); the journey installs only while the Store still shows 1.0.8
# and Chromium's prompt asks for a host list, not all sites; else it cancels.
# Then: exact manifest version and host set, extension running, New Tab
# resolves to AnyChat's page, still installed and running after
# Cmd+Q/relaunch, and the owner's own Ahoi profile is left as it was. No
# prompt is sent and no account is used. Needs network access to the Chrome Web Store.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9401
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-anychat-profile.XXXXXX)
ID=khpefodpgnkegiohbolbaaeabnfdegln; VERSION=1.0.8
STORE="https://chromewebstore.google.com/detail/anychat-ai-powered-browsi/$ID"
APPROVED_HOSTS="chat.deepseek.com chat.mistral.ai chatgpt.com claude.ai copilot.com copilot.microsoft.com gemini.google.com grok.com m365.cloud.microsoft meta.ai openrouter.ai perplexity.ai www.meta.ai www.perplexity.ai"
OWNER_PROFILE="$HOME/Library/Application Support/AhoiBrowser/Default"
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
pages() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(json.dumps(sorted([t["url"] for t in json.load(sys.stdin) if t["type"]=="page"])))'; }
extension_running() { curl -s http://127.0.0.1:$PORT/json | grep -q "chrome-extension://$ID/"; }
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
waitax() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do $AX dump $PID 14 | grep -q -E "$1" && return 0; sleep 1; done; return 1; }
waiturl() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do pages | grep -q -F "$1" && return 0; sleep 1; done; return 1; }
RESULTS=(); record() { RESULTS+=("\"$1\": $2"); echo "$1 -> $2" >> "$OUT/steps.txt"; }
finish() {
  local joined; joined=$(IFS=,; echo "${RESULTS[*]}")
  local sep=""; [ -n "$joined" ] && sep=", "
  echo "{${joined}${1:+$sep\"setupFailed\": \"$1\"}}" | python3 -c 'import json,sys;d=json.load(sys.stdin);d["pass"]=("setupFailed" not in d) and all(v is True for k,v in d.items() if k!="setupFailed");print(json.dumps(d,indent=1))' > "$OUT/verdict.json"
  cat "$OUT/verdict.json"
}
fail_setup() { $AX dump $PID 14 > "$OUT/ax-setup-failure.txt"; finish "$1"; quit; exit 4; }
eval_in() { # <url substring> <expression> [userGesture]
  CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True,"userGesture":sys.argv[2]=="1"}))' "$2" "${3:-0}")" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'
}
open_url() { # <url> <expected substring>
  local opened=0
  for attempt in 1 2 3; do
    $AX activate $PID >> "$OUT/steps.txt"; sleep 1; key 17 cmd
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 && { opened=1; break; }
  done
  [ $opened = 1 ] || fail_setup "command bar did not open for $1"
  sleep 1; $AX type $PID "$1" >> "$OUT/steps.txt"; sleep 1; key 36
  waiturl "$2" 5 || { echo "info: Return repeated" >> "$OUT/steps.txt"; key 36; }
  waiturl "$2" 30 || fail_setup "did not load $1"; sleep 4
}
press_label() { # <regex of an AXButton title> ; presses the first match
  local title; title=$($AX dump $PID 14 | grep -o -E "AXButton \| ($1)[^|]*" | head -1 | sed -e 's/^AXButton | //' -e 's/ *$//')
  [ -n "$title" ] && $AX press $PID "$title" >> "$OUT/steps.txt"
}
manifest() { ls "$P"/Default/Extensions/$ID/*/manifest.json 2>/dev/null | head -1; }

[ -d "$OWNER_PROFILE/Extensions/$ID" ] && OWNER_BEFORE=present || OWNER_BEFORE=absent
launch
open_url "$STORE" "$ID"
# A fresh profile first meets Google's cookie consent page; decline all
# (the privacy-preserving choice) and let it return to the Store listing.
sleep 4
if curl -s http://127.0.0.1:$PORT/json | grep -q "consent.google.com"; then
  eval_in "consent.google.com" "(()=>{const b=[...document.querySelectorAll('button')].find(b=>/^(Alle ablehnen|Reject all)$/.test(b.innerText.trim()));if(!b)return 0;b.click();return 1})()" 1 >> "$OUT/steps.txt"
  echo "info: declined Google cookie consent" >> "$OUT/steps.txt"
  end=$(( $(date +%s) + 20 ))
  while [ $(date +%s) -lt $end ] && curl -s http://127.0.0.1:$PORT/json | grep -q "consent.google.com"; do sleep 1; done
  sleep 4
fi
# The approval covers this exact Store version only.
eval_in "$ID" "document.body.innerText" > "$OUT/store-text.txt"
grep -q -E "(^|[^0-9.])$VERSION([^0-9.]|$)" "$OUT/store-text.txt" && record storeShowsApprovedVersion true \
  || { record storeShowsApprovedVersion false; fail_setup "store no longer shows $VERSION; new approval needed"; }
# Chrome's own "switch to Chrome" banner may offer a dismissal first.
eval_in "$ID" "(()=>{const b=[...document.querySelectorAll('button')].find(b=>/^Nein, danke$/.test(b.innerText.trim()));if(b){b.click();return 1}return 0})()" 1 >> "$OUT/steps.txt"
sleep 2
CLICKED=$(eval_in "$ID" "(()=>{const b=[...document.querySelectorAll('button')].find(b=>/^(Hinzufügen|Zu Chrome hinzufügen|Add to Chrome)$/.test(b.innerText.trim()));if(!b)return 0;b.click();return 1})()" 1)
[ "$CLICKED" = 1 ] || fail_setup "store add button not found"
waitax "AXButton \\| Erweiterung hinzufügen" 20 || fail_setup "Chromium's install prompt did not open"
$AX dump $PID 14 > "$OUT/ax-install-prompt.txt"
# Chromium names a host list only as "various websites"; the exact host set
# is checked in the installed manifest below. All-sites access was not
# approved, so its warning cancels the install.
if grep -q "Eigene Daten auf verschiedenen Websites lesen und ändern" "$OUT/ax-install-prompt.txt" \
  && ! grep -q "auf allen Websites" "$OUT/ax-install-prompt.txt"; then
  record promptScopeMatchesApproval true
  press_label "Erweiterung hinzufügen"
else
  record promptScopeMatchesApproval false
  press_label "Abbrechen"; sleep 2; fail_setup "prompt scope differs from the approved one; cancelled"
fi
end=$(( $(date +%s) + 90 )); while [ $(date +%s) -lt $end ] && [ -z "$(manifest)" ]; do sleep 2; done
M=$(manifest); [ -n "$M" ] || fail_setup "AnyChat was not installed"
cp "$M" "$OUT/manifest.json"
python3 - "$M" "$VERSION" $APPROVED_HOSTS > "$OUT/manifest-check.txt" <<'PY'
import json, sys, urllib.parse
manifest = json.load(open(sys.argv[1])); version = sys.argv[2]; approved = set(sys.argv[3:])
patterns = manifest.get("host_permissions", []) + [
    p for p in manifest.get("permissions", []) if "://" in p]
for script in manifest.get("content_scripts", []):
    patterns += script.get("matches", [])
hosts = {urllib.parse.urlsplit(p.replace("*.", "")).hostname for p in patterns}
print("version", manifest.get("version"))
print("hosts", sorted(hosts))
print("extra", sorted(hosts - approved))
print("VERSION_OK" if manifest.get("version") == version else "VERSION_DIFFERS")
print("HOSTS_OK" if hosts <= approved else "HOSTS_EXCEED_APPROVAL")
PY
grep -q VERSION_OK "$OUT/manifest-check.txt" && record installedApprovedVersion true || record installedApprovedVersion false
grep -q HOSTS_OK "$OUT/manifest-check.txt" && record hostsWithinApproval true || record hostsWithinApproval false
end=$(( $(date +%s) + 30 )); while [ $(date +%s) -lt $end ] && ! extension_running; do sleep 2; done
extension_running && record extensionRunning true || record extensionRunning false
$AX dump $PID 14 > "$OUT/ax-after-install.txt"
press_label "Schließen"; sleep 1
# New Tab: AnyChat replaces it (Chrome may ask whether to keep the change).
open_url "chrome://newtab" "chrome"
sleep 3; pages > "$OUT/pages-newtab.txt"
grep -q "chrome-extension://$ID/" "$OUT/pages-newtab.txt" && record newTabIsAnyChat true || record newTabIsAnyChat false
$AX dump $PID 14 > "$OUT/ax-newtab.txt"
press_label "Änderung beibehalten|Beibehalten"; sleep 1
# Restart keeps the extension installed and running.
quit; sleep 3; launch
end=$(( $(date +%s) + 40 )); while [ $(date +%s) -lt $end ] && ! extension_running; do sleep 2; done
extension_running && [ -n "$(manifest)" ] && record runningAfterRestart true || record runningAfterRestart false
$AX dump $PID 14 > "$OUT/ax-after-restart.txt"
OWNER_AFTER=absent; [ -d "$OWNER_PROFILE/Extensions/$ID" ] && OWNER_AFTER=present
echo "owner profile AnyChat: $OWNER_BEFORE -> $OWNER_AFTER" >> "$OUT/steps.txt"
[ "$OWNER_AFTER" = "$OWNER_BEFORE" ] \
  && record ownerProfileUntouched true || record ownerProfileUntouched false
finish; quit
