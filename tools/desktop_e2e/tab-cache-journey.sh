#!/bin/bash
# Installed native tab-cache control, same-origin split peer and retained worker.
# No real profile, secret, global cache purge or extension state is touched.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9396; SITE_PORT=8806
export AHOI_E2E_SITE_FIXTURE=tab-cache
. "$S/split_journey_lib.sh"

# Seed only this fresh, not-yet-launched profile. Startup preferences are set
# through the trusted live native Settings path below, never edited offline.
mkdir -p "$P/Default"
python3 - "$P/Default/Preferences" "$SITE" <<'PY'
import json, sys
profile = {"name": "Synthetic tab cache acceptance", "assets": [],
           "user_agent": {"enabled": False, "value": ""},
           "headers": {"enabled": True, "sync_enabled": False,
                       "rules": [{"name": "X-Ahoi-Dev", "action": "set", "value": "configured"}]},
           "response_headers": {"enabled": True, "sync_enabled": False,
                                "advanced_mode_acknowledged": False,
                                "rules": [{"name": "X-Ahoi-Resp", "action": "set", "value": "present"}]},
           "cache_disabled": False}
with open(sys.argv[1], "w") as file:
    json.dump({"ahoi": {"developer_toolkit": {"enabled": True,
              "profiles": {"version": 2, "origins": {sys.argv[2]: profile}}}}}, file)
PY

evaluate() { # target, expression, evidence filename
  local params
  params=$(python3 - "$2" <<'PY'
import json, sys
print(json.dumps({"expression": sys.argv[1], "returnByValue": True, "awaitPromise": True}))
PY
)
  CDP "$1" Runtime.evaluate "$params" > "$OUT/$3.json" || fail_setup "fixture evaluation failed"
}
fetch_pair() {
  evaluate "$1" '(async()=>{const values=[];for(let i=0;i<2;i++){const r=await fetch("/cache?tag=shared");values.push({...await r.json(),responseHeader:r.headers.get("X-Ahoi-Resp")});}return values;})()' "$2"
}
worker_pair() {
  evaluate a.html 'new Promise((resolve,reject)=>{window.cacheWorker.onmessage=e=>resolve(e.data);window.cacheWorker.onerror=()=>reject("fixture worker failed");window.cacheWorker.postMessage("fetch");})' "$1"
}
expect_pair() { # evidence, first count, second count
  python3 - "$OUT/$1.json" "$2" "$3" <<'PY'
import json, sys
doc = json.load(open(sys.argv[1]))
values = doc.get('result', {}).get('value')
expected = [int(sys.argv[2]), int(sys.argv[3])]
ok = not doc.get('exceptionDetails') and isinstance(values, list) and len(values) == 2
ok = ok and [v.get('count') for v in values] == expected
ok = ok and all(v.get('requestHeader') == 'configured' and v.get('responseHeader') == 'present' for v in values)
sys.exit(0 if ok else 1)
PY
}
find_button() { # output file, exact permitted labels
  activate_owned
  $AX dump $PID 40 > "$OUT/$1.txt"
  python3 - "$OUT/$1.txt" "${@:2}" <<'PY'
import sys
allowed = set(sys.argv[2:])
matches = set()
for line in open(sys.argv[1]):
    fields = [part.strip() for part in line.split('|')]
    if fields[0] != 'AXButton':
        continue
    for text in fields[1:]:
        if text in allowed:
            matches.add('AXButton:' + text)
if len(matches) != 1:
    sys.exit(1)
print(next(iter(matches)))
PY
}
open_toolkit() {
  local name
  name=$(find_button toolkit-entry 'Entwicklerwerkzeuge' 'Developer toolkit' 'Entwickler-Helfer öffnen') || fail_setup "native Toolkit entry missing or ambiguous"
  activate_owned
  $AX press $PID "$name" >> "$OUT/steps.txt" || fail_setup "Toolkit did not open"
  waitax 'Cache bis zum Schließen dieses Tabs umgehen' 5 || fail_setup "candidate lacks native tab-cache action"
}
toggle_and_cancel() { # evidence prefix
  local button
  open_toolkit
  activate_owned
  $AX press $PID 'Cache bis zum Schließen dieses Tabs umgehen (lädt neu)' >> "$OUT/steps.txt" || fail_setup "tab-cache action unavailable"
  waitax 'AXButton.*(Abbrechen|Bleiben|Cancel|Stay)' 10 || fail_setup "native before-unload dialog missing"
  button=$(find_button "$1-dialog" 'Abbrechen' 'Bleiben' 'Cancel' 'Stay') || fail_setup "native cancel button ambiguous"
  activate_owned
  $AX press $PID "$button" >> "$OUT/steps.txt" || fail_setup "native reload cancellation failed"
  sleep 2
  key 53
  evaluate a.html '({draft:document.getElementById("f").value,worker:!!window.cacheWorker})' "$1-retained"
  CDP a.html Page.getFrameTree '{}' > "$OUT/$1-frame.json"
}
expect_retained() {
  python3 - "$OUT/$1-retained.json" "$OUT/$1-frame.json" "$OUT/original-frame.json" <<'PY'
import json, sys
value = json.load(open(sys.argv[1])).get('result', {}).get('value')
frame = json.load(open(sys.argv[2])).get('frameTree', {}).get('frame', {})
original = json.load(open(sys.argv[3])).get('frameTree', {}).get('frame', {})
ok = value == {'draft': 'keep-this-draft', 'worker': True}
ok = ok and frame.get('id') == original.get('id') and bool(original.get('loaderId'))
ok = ok and frame.get('loaderId') == original.get('loaderId')
sys.exit(0 if ok else 1)
PY
}

launch "$SITE/solo.html"
prefs_continue
open_url "$SITE/a.html"
tab_menu_split b.html || fail_setup "same-origin native split peer missing"
fetch_pair a.html warm-a
fetch_pair b.html warm-b
check warmCacheSharedBySameOriginPeer 'expect_pair warm-a 1 1 && expect_pair warm-b 1 1'
evaluate a.html 'window.cacheWorker=new Worker("/worker.js");true' worker-created
worker_pair warm-worker
check existingWorkerUsesNativeWarmCache 'expect_pair warm-worker 1 1'
activate_owned
$AX hidclick $PID 'AXTextField:Cache-Test-Entwurf-A' >> "$OUT/steps.txt" || fail_setup "visible form control missing"
type_in keep-this-draft
evaluate a.html 'window.onbeforeunload=e=>{e.preventDefault();e.returnValue="";};document.getElementById("f").value' draft-installed
python3 - "$OUT/draft-installed.json" <<'PY'
import json, sys
sys.exit(0 if json.load(open(sys.argv[1])).get('result', {}).get('value') == 'keep-this-draft' else 1)
PY
[ "$?" = 0 ] || fail_setup "native typed draft in selected pane not confirmed"
CDP a.html Page.getFrameTree '{}' > "$OUT/original-frame.json"
toggle_and_cancel cache-on
check nativeReloadCancelRetainsDocumentDraftAndWorker 'expect_retained cache-on'
open_toolkit
$AX dump $PID 40 > "$OUT/cache-chip-on.txt"
check currentPaneShowsExplicitTabCacheChip 'grep -q "TAB CACHE OFF" "$OUT/cache-chip-on.txt"'
key 53
fetch_pair a.html bypass-a
fetch_pair b.html cached-peer
check selectedTabBypassesCacheAndKeepsBothHeaderDirections 'expect_pair bypass-a 2 3'
check sameOriginSplitPeerCacheRemainsNative 'expect_pair cached-peer 1 1'
worker_pair bypass-worker
check existingWorkerPolicyChangesWithoutRecreation 'expect_pair bypass-worker 2 3'
toggle_and_cancel cache-off
open_toolkit
$AX dump $PID 40 > "$OUT/cache-chip-off.txt"
check reversedTabChoiceRemovesItsChip ' ! grep -q "TAB CACHE OFF" "$OUT/cache-chip-off.txt"'
key 53
fetch_pair a.html warm-again
check cacheOffReversalRestoresWarmCacheWithoutLosingDraft 'expect_retained cache-off && expect_pair warm-again 1 1'
# Close while the local choice is ON, so restart tests its actual lifetime.
toggle_and_cancel before-close
check finalReloadCancelStillRetainsDocument 'expect_retained before-close'
evaluate a.html 'window.onbeforeunload=null;true' close-fixture-handler
quit
sleep 2
launch
wait_document "$SITE/a.html" 20 || fail_setup "native continued session missing"
fetch_pair a.html restored
check tabChoiceNotPersistedAcrossNativeQuitAndRestore 'expect_pair restored 1 1'
python3 - "$P/Default/Preferences" "$SITE" > "$OUT/persistent-origin-cache.json" <<'PY'
import json, sys
profile = json.load(open(sys.argv[1]))['ahoi']['developer_toolkit']['profiles']['origins'][sys.argv[2]]
print(json.dumps({'cache_disabled': profile['cache_disabled']}))
sys.exit(0 if profile['cache_disabled'] is False else 1)
PY
check tabChoiceNeverBecomesPersistentOriginCache 'python3 -c "import json,sys;sys.exit(0 if json.load(open(sys.argv[1]))[\"cache_disabled\"] is False else 1)" "$OUT/persistent-origin-cache.json"'
finish; STATUS=$?; quit; exit "$STATUS"
