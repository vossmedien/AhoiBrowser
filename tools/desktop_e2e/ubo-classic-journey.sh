#!/bin/bash
# usage: ubo-classic-journey.sh <App.app> <outdir>
# DoD 8 / INC-04 on the installed candidate with a fresh profile: the
# command bar opens Ahoi's verified uBlock Origin Classic installer, the
# pinned Official GitHub release installs through Chromium's prompt, its
# generic cosmetic filtering hides an ad slot, it stays inactive in an
# incognito window until "Allow in Incognito" is switched on, and then
# filters there too. Needs network access to the pinned GitHub release.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9397
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-ubo-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8817}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
printf '<title>adpage</title><ins class="adsbygoogle" id="ad" data-ad-client="ca-pub-0000000000000000" data-ad-slot="0000000000" style="display:block;width:300px;height:250px;background:#fc0"></ins>ad page' > $P-site/ad.html
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
  sleep 1; $AX type $PID "$1" >> "$OUT/steps.txt"; sleep 1; key 36
  # A Return lost before the bar has key focus leaves the URL typed but
  # unsubmitted (build-46 run); submit once more before giving up.
  waiturl "$1" 5 || { echo "info: Return repeated" >> "$OUT/steps.txt"; key 36; }
  waiturl "$1" 20 || fail_setup "did not load $1"; sleep 2
}




UBO_ID=fkgkibajhfbepljeaefdnfnegdcjomkh
ad_display() { eval_in "$1" "getComputedStyle(document.getElementById('ad')).display"; }
hidden_within() { # <url substring> <seconds>; uBO loads its bundled lists first
  local end=$(( $(date +%s) + $2 ))
  while [ $(date +%s) -lt $end ]; do [ "$(ad_display "$1")" = none ] && return 0; sleep 1; done
  return 1
}
ubo_running() { curl -s http://127.0.0.1:$PORT/json | grep -q "chrome-extension://$UBO_ID/"; }
press_label() { # <regex of an AXButton title> ; presses the first match
  local title; title=$($AX dump $PID 14 | grep -o -E "AXButton \| ($1)[^|]*" | head -1 | sed -e 's/^AXButton | //' -e 's/ *$//')
  [ -n "$title" ] && $AX press $PID "$title" >> "$OUT/steps.txt"
}

launch
open_url "$SITE/ad.html"
[ "$(ad_display ad.html)" = block ] && record adVisibleWithoutUbo true || record adVisibleWithoutUbo false
# The command bar entry opens the verified installer.
key 17 cmd
waitax "AXWindow \\| Suchen oder URL eingeben" 6 || fail_setup "command bar did not open"
sleep 1; $AX type $PID "ubo" >> "$OUT/steps.txt"; sleep 2
$AX dump $PID 14 > "$OUT/ax-command-bar.txt"
grep -q "uBlock Origin Classic…" "$OUT/ax-command-bar.txt" && record installerOfferedInCommandBar true || record installerOfferedInCommandBar false
key 36; sleep 2
waitax "AXButton \\| (uBlock Origin Classic installieren|Sicher prüfen|Verifiziertes Paket herunterladen)" 10 || fail_setup "installer dialog did not open"
$AX dump $PID 14 > "$OUT/ax-installer.txt"
# Drive the installer to Chromium's own prompt, then confirm there.
end=$(( $(date +%s) + 120 )); prompted=0
while [ $(date +%s) -lt $end ] && ! ubo_running; do
  if $AX dump $PID 14 | grep -q -E "AXButton \| [^|]*hinzufügen"; then
    $AX dump $PID 14 > "$OUT/ax-chromium-prompt.txt"; press_label "[^|]*hinzufügen"; prompted=1; sleep 3
  else
    press_label "uBlock Origin Classic installieren" || press_label "Verifiziertes Paket herunterladen" || press_label "Sicher prüfen"
    sleep 3
  fi
done
[ $prompted = 1 ] && record chromiumPromptShown true || record chromiumPromptShown false
ubo_running && record uboInstalledAndRunning true || { record uboInstalledAndRunning false; fail_setup "uBO did not start"; }
$AX dump $PID 14 > "$OUT/ax-after-install.txt"
press_label "Schließen"; sleep 1
# Normal window: generic cosmetic filtering hides the ad slot.
open_url "$SITE/ad.html?normal"
hidden_within 'ad.html?normal' 20 && record adHiddenInNormalWindow true || record adHiddenInNormalWindow false
# INC-04: not active in incognito until explicitly allowed.
key 45 cmd shift; sleep 3; open_url "$SITE/ad.html?inc1"; sleep 3
[ "$(ad_display 'ad.html?inc1')" = block ] && record inactiveInIncognitoByDefault true || record inactiveInIncognitoByDefault false
key 13 cmd shift; sleep 2
open_url "chrome://extensions/?id=$UBO_ID"; sleep 2
ALLOWED=$(eval_in "extensions/?id=$UBO_ID" "(()=>{const d=document.querySelector('extensions-manager').shadowRoot.querySelector('extensions-detail-view');const t=d.shadowRoot.querySelector('#allow-incognito');if(!t.checked)t.click();return String(t.checked)})()")
echo "allow-incognito after click: $ALLOWED" >> "$OUT/steps.txt"
[ "$ALLOWED" = true ] && record incognitoAllowanceSwitchedOn true || record incognitoAllowanceSwitchedOn false
sleep 3; key 45 cmd shift; sleep 3; open_url "$SITE/ad.html?inc2"
hidden_within 'ad.html?inc2' 20 && record activeInIncognitoAfterAllowance true || record activeInIncognitoAfterAllowance false
$AX dump $PID 14 > "$OUT/ax-final.txt"
quit
finish
