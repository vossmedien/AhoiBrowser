#!/bin/bash
# usage: empty-workspace-navigation.sh <App.app> <outdir>
# PID-scoped AX + CDP journey for the typed current-tab navigation boundary
# (patch 0054): an empty Workspace must get a new tab, never a hidden one.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9344
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
mkdir -p $OUT; P=$(mktemp -d /private/tmp/ahoi-emptyws-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8791}; mkdir -p $P-site
printf '<title>Ahoi empty workspace probe</title><h1>probe</h1>' > $P-site/index.html
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > $OUT/site.log 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT; SITE=http://127.0.0.1:$SITE_PORT
tabs() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(json.dumps(sorted([(t["id"],t["url"]) for t in json.load(sys.stdin) if t["type"]=="page"])))'; }
"$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run --no-default-browser-check \
  --remote-debugging-port=$PORT --enable-features=AhoiWorkspaceWebsiteSessions about:blank > $OUT/browser.log 2>&1 &
PID=$!; echo "pid=$PID profile=$P" > $OUT/run.txt
for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
sleep 4; $AX activate $PID >> $OUT/steps.txt
waitax() { # <regex> <seconds>
  local end=$(( $(date +%s) + $2 ))
  while [ $(date +%s) -lt $end ]; do $AX dump $PID 14 | grep -q -E "$1" && return 0; sleep 1; done
  return 1
}
waiturl() { # <url-substring> <seconds>
  local end=$(( $(date +%s) + $2 ))
  while [ $(date +%s) -lt $end ]; do tabs | grep -q "$1" && return 0; sleep 1; done
  return 1
}
fail_setup() {
  echo "{\"pass\": false, \"setupFailed\": \"$1\"}" > $OUT/verdict.json
  $AX dump $PID 14 > $OUT/ax-setup-failure.txt; $AX key $PID 12 cmd >> $OUT/steps.txt
  cat $OUT/verdict.json; exit 4
}
newws() {
  $AX key $PID 53 >> $OUT/steps.txt; sleep 1
  for attempt in 1 2 3 4; do
    $AX press $PID "$1, Workspace wechseln" AXShowMenu >> $OUT/steps.txt
    waitax "Neuer Workspace…" 4 && $AX press $PID "Neuer Workspace…" >> $OUT/steps.txt && break
    $AX key $PID 53 >> $OUT/steps.txt; sleep 2
  done
  waitax "AXTextField \\| Workspace-Name" 8 || fail_setup "workspace dialog for $2 did not open"
  $AX setvalue $PID "Workspace-Name" "$2" >> $OUT/steps.txt; $AX press $PID "Erstellen" >> $OUT/steps.txt
  waitax "$2, Workspace wechseln" 10 || fail_setup "workspace $2 not active"
}
nav() { # <keycode> <url>
  $AX key $PID $1 cmd >> $OUT/steps.txt
  waitax "AXWindow \\| Suchen oder URL eingeben" 10 || fail_setup "command bar did not open"
  sleep 1; $AX key $PID 0 cmd >> $OUT/steps.txt
  $AX type $PID "$2" >> $OUT/steps.txt; sleep 1; $AX key $PID 36 >> $OUT/steps.txt
}
# Step A: first empty workspace, ⌘T (new tab) -> tab A in "Leer-Test".
newws Inbox Leer-Test; nav 17 "$SITE/?empty-probe=1"
waiturl "empty-probe=1" 20 || fail_setup "new tab did not load probe 1"; echo "afterA $(tabs)" >> $OUT/tabs.txt
# Step B: second empty workspace, ⌘L (current-tab navigation) — must create a new tab, not move tab A.
newws Leer-Test Leer-Zwei; BEFORE=$(tabs); nav 37 "$SITE/?empty-probe=2"
waiturl "empty-probe=2" 20; sleep 2; AFTER=$(tabs)
echo "beforeB $BEFORE" >> $OUT/tabs.txt; echo "afterB $AFTER" >> $OUT/tabs.txt
$AX dump $PID 14 | grep -E 'Workspace wechseln|AXRadioButton|AXWindow' > $OUT/ax-after.txt
python3 - "$BEFORE" "$AFTER" > $OUT/verdict.json <<'PY'
import json,sys
b=dict(json.loads(sys.argv[1])); a=dict(json.loads(sys.argv[2]))
moved=[i for i in b if i in a and a[i]!=b[i]]
new=[i for i in a if i not in b]
ok=not moved and len(new)==1 and a[new[0]].endswith("empty-probe=2")
print(json.dumps({"pass":ok,"hiddenTabsNavigated":moved,"newTabs":{i:a[i] for i in new}},indent=1))
PY
$AX key $PID 12 cmd >> $OUT/steps.txt; sleep 5; kill -0 $PID 2>/dev/null && echo "still running after quit" >> $OUT/run.txt
cat $OUT/verdict.json
