# Shared helpers for split-matrix-journey.sh and split-lifecycle-journey.sh.
# Sourced after APP, OUT, S, AX, PORT and SITE_PORT are set. Split view is
# driven without HID drag: the native Tab menu item "Tab zu neuer geteilter
# Ansicht hinzufügen" (IDC_NEW_SPLIT_TAB) makes a two-pane split, the popup
# overlay's "Popup im Split View öffnen" adds a pane to the opener's split
# (CreateOrAddToSplitFromDrop, the same model path as a sidebar drop), and the
# pane menu "Geteilte Ansicht anordnen", the sidebar row menu and the split
# shortcuts from keyboard_shortcuts.cc do the rest. The layout presets sit in
# the row menu's "Geteilte Ansicht anordnen" submenu (three or four panes);
# Ahoi has no pane-toolbar menu. German labels are from
# generated_resources.grd/_de.xtb (M153 composition). Layouts are read from
# each pane's CSS viewport against the single-tab baseline W0 x H0.
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-split-profile.XXXXXX); mkdir -p $P-site
: > "$OUT/steps.txt"; W0=0; H0=0; SNAPN=0; CHECKED=false
for n in A B C D E F G H; do
  low=$(echo $n | tr A-H a-h)
  printf '<title>Pane%s</title><h1>Pane %s</h1><input id=f>' $n $n > $P-site/$low.html
done
printf '<title>Solo</title><h1>Solo</h1><input id=f>' > $P-site/solo.html
case "${AHOI_E2E_SITE_FIXTURE:-split}" in
  split) python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 & ;;
  tab-cache) python3 "$S/tab_cache_fixture.py" --port "$SITE_PORT" --log "$OUT/cache-server.jsonl" > "$OUT/site.log" 2>&1 & ;;
  *) echo "unsupported local fixture" >&2; exit 4 ;;
esac
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT; SITE=http://127.0.0.1:$SITE_PORT
# Bind/listen readiness and exact fixture bytes precede any browser action.
# A target URL alone can describe a pending navigation with a blank document.
SITE_READY=false
for attempt in 1 2 3 4 5; do
  if curl -fsS --connect-timeout 1 --max-time 2 "$SITE/a.html" > "$OUT/site-readiness.html" 2> "$OUT/site-readiness-error.txt" &&
      grep -q '<title>PaneA</title>' "$OUT/site-readiness.html"; then
    SITE_READY=true; break
  fi
  kill -0 "$SITE_PID" 2>/dev/null || break
  sleep 1
done
if [ "$SITE_READY" != true ]; then
  echo '{"setupFailed":"local fixture server was not ready","pass":false}' > "$OUT/verdict.json"
  cat "$OUT/verdict.json"; exit 4
fi
# ⌘⌃1…4 and ⌘⌃0 can be taken system-wide (symbolic hotkeys); then the key
# never reaches Ahoi and a failed focus check is a Mac setting, not a defect.
python3 - >> "$OUT/steps.txt" <<'PY'
import plistlib, subprocess
raw = subprocess.run(["defaults", "export", "com.apple.symbolichotkeys", "-"], capture_output=True).stdout
keys = plistlib.loads(raw).get("AppleSymbolicHotKeys", {}) if raw else {}
for v in keys.values():
    p = v.get("value", {}).get("parameters", [0, 0, 0])
    if v.get("enabled") and len(p) == 3 and p[2] == 1310720 and p[1] in (18, 19, 20, 21, 29, 37, 13, 123, 124):
        print("info: macOS holds Cmd+Ctrl keycode %d" % p[1])
PY
cat > $P-ana.py <<'PY'
import json, sys
path, w0, h0, cmd = sys.argv[1], float(sys.argv[2] or 0), float(sys.argv[3] or 0), sys.argv[4]
args = sys.argv[5:]
rows = []
for line in open(path):
    parts = line.rstrip("\n").split("|", 2)
    try:
        d = json.loads(parts[2])
    except (IndexError, ValueError):
        continue
    d["id"], d["win"] = parts[0], parts[1]
    rows.append(d)
excl = [a[1:] for a in args if a.startswith("-")]
win = next((a[4:] for a in args if a.startswith("win=")), "")
hidden = "hidden" in args
vis = [r for r in rows if (hidden or r["v"] == "visible") and r["t"] not in excl
       and r["t"] != "Solo" and (not win or str(r["win"]) == win)]
def full(x, base):
    return base > 0 and x / base > 0.8
if cmd == "layout":
    n = len(vis)
    fw = sum(full(r["w"], w0) for r in vis)
    fh = sum(full(r["h"], h0) for r in vis)
    name = "unknown"
    if n == 1 and fw == 1 and fh == 1: name = "single"
    elif n in (2, 3) and fh == n and fw == 0: name = {2: "two-columns", 3: "three-columns"}[n]
    elif n in (2, 3) and fw == n and fh == 0: name = {2: "two-rows", 3: "three-rows"}[n]
    elif n == 3 and fh == 1 and fw == 0: name = "main-vertical"
    elif n == 3 and fw == 1 and fh == 0: name = "main-horizontal"
    elif n == 4 and all(0.3 < r["w"] / w0 < 0.7 and 0.3 < r["h"] / h0 < 0.7 for r in vis): name = "four-grid"
    print(name)
elif cmd == "visible": print(",".join(sorted(r["t"] for r in vis)))
elif cmd == "all": print(",".join(sorted(r["t"] for r in rows)))
elif cmd == "focused": print(",".join(sorted(r["t"] for r in vis if r.get("f"))))
elif cmd == "kept": print(",".join(sorted(r["t"] for r in vis if r.get("m") == "kept")))
elif cmd == "values": print(",".join(sorted("%s=%s" % (r["t"], r.get("val", "")) for r in vis)))
elif cmd == "size": print(next(("%d %d" % (r["w"], r["h"]) for r in rows if r["t"] == args[0]), "0 0"))
elif cmd == "diff":  # width of args[0] minus width of args[1], as a share of W0
    w = {r["t"]: r["w"] for r in vis}
    print("%.3f" % ((w.get(args[0], 0) - w.get(args[1], 0)) / w0))
elif cmd == "shares":  # each pane's share of the summed pane area
    total = sum(r["w"] * r["h"] for r in vis) or 1
    print(" ".join("%s:%.3f" % (r["t"], r["w"] * r["h"] / total) for r in sorted(vis, key=lambda r: r["t"])))
elif cmd == "win": print(next((str(r["win"]) for r in rows if r["t"] == args[0]), ""))
PY

CDP() { node "$S/cdp.mjs" $PORT "$@"; }
site_ids() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;[print(t["id"]) for t in json.load(sys.stdin) if t["type"]=="page" and t["url"].startswith(sys.argv[1])]' "$SITE"; }
urls() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(" ".join(sorted(t["url"].rsplit("/",1)[-1] for t in json.load(sys.stdin) if t["type"]=="page" and t["url"].startswith(sys.argv[1]))))' "$SITE"; }
OWNED_FOCUS_ACQUIRED=false
activate_owned() {
  if [ "${AHOI_E2E_YIELD_ON_FOCUS_LOSS:-0}" = 1 ] && [ "$OWNED_FOCUS_ACQUIRED" = true ]; then
    if ! "$AX" focused "$PID" | head -1 | grep -q " pid=$PID target=$PID$"; then
      echo 'owner focus returned elsewhere; yielding without activation or HID' >> "$OUT/steps.txt"
      echo '{"cancelled":"owner focus returned elsewhere","pass":false}' > "$OUT/verdict.json"
      # Close only this journey's fixture target, without keyboard/focus actions.
      local id
      id=$(site_ids | head -1)
      [ -z "$id" ] || CDP "$id" Browser.close '{}' > "$OUT/cancel-close.json" 2>&1
      exit 8
    fi
    return 0
  fi
  "$AX" activate "$PID" >> "$OUT/steps.txt"
  if "$AX" focused "$PID" | head -1 | grep -q " pid=$PID target=$PID$"; then
    OWNED_FOCUS_ACQUIRED=true
  fi
}
launch() { # [url] [nodevtools]
  local url=${1:-} dev=--remote-debugging-port=$PORT
  local archive_age_flag=""
  if [ -n "${AHOI_E2E_ARCHIVE_AGE:-}" ]; then
    case "$AHOI_E2E_ARCHIVE_AGE" in ''|*[!0-9]*) echo 'invalid synthetic archive age' >&2; exit 4 ;; esac
    [ "$AHOI_E2E_ARCHIVE_AGE" -ge 1 ] && [ "$AHOI_E2E_ARCHIVE_AGE" -le 3600 ] || exit 4
    archive_age_flag="--ahoi-e2e-archive-age-seconds=$AHOI_E2E_ARCHIVE_AGE"
  fi
  [ "${2:-}" = nodevtools ] && dev=""
  # The initial owner check is not permission for a later restart. Recheck
  # after each quit, before creating a new process or activating its window.
  python3 - "$APP" "$OUT" <<'PY'
import datetime, json, os, pathlib, plistlib, re, subprocess, sys
app, output = map(pathlib.Path, sys.argv[1:])
info = plistlib.loads((app / 'Contents/Info.plist').read_bytes())
source = info.get('AhoiSourceCommit')
phases = output / 'launch-preflight.jsonl'
previous = json.loads(phases.read_text().splitlines()[0]) if phases.exists() else None
commands = [c.strip() for c in subprocess.check_output(['ps', '-axww', '-o', 'comm='], text=True).splitlines()]
idle_raw = subprocess.check_output(['ioreg', '-c', 'IOHIDSystem'], text=True)
idle = int(re.search(r'"HIDIdleTime"\s*=\s*(\d+)', idle_raw).group(1)) // 10**9
sample = dict(at=datetime.datetime.now(datetime.timezone.utc).isoformat(),
              source=source, idleSeconds=idle,
              browserRunning=str(app / 'Contents/MacOS/AhoiBrowser') in commands,
              compilerRunning=any(re.search(r'/(?:clang\+\+|clang|ninja|siso|xcodebuild)$', c) for c in commands))
expected = os.environ.get('AHOI_E2E_EXPECTED_SOURCE_COMMIT')
if not source or (expected and expected != source) or (previous and previous['source'] != source):
    sample['refusal'] = 'installed candidate changed or lacks source metadata'
elif sample['browserRunning'] or sample['compilerRunning']:
    sample['refusal'] = 'another browser/build owns the launch boundary'
elif os.environ.get('AHOI_E2E_YIELD_ON_FOCUS_LOSS') == '1' and idle < 2:
    # Own Cmd-Q is followed by a two-second settle in both journeys. Newer
    # input must yield; never reset the focus guard and reclaim the desktop.
    sample['refusal'] = 'input returned before launch'
with phases.open('a') as file:
    file.write(json.dumps(sample) + '\n')
if 'refusal' in sample:
    (output / 'verdict.json').write_text(json.dumps(
        {'cancelled': sample['refusal'], 'pass': False}) + '\n')
    print(sample['refusal'], file=sys.stderr)
    sys.exit(8)
PY
  [ "$?" = 0 ] || exit 8
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run --no-default-browser-check \
    $archive_age_flag $dev $url >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$P devtools=${dev:+on}" >> "$OUT/run.txt"
  if [ -n "$dev" ]; then
    for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  else
    waitax "AXWindow \\|" 40
  fi
  OWNED_FOCUS_ACQUIRED=false
  sleep 4; activate_owned
}
quit() { key 12 cmd; for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || return 0; sleep 1; done; echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3; }
# Configure both startup paths through the live, trusted native setter before
# constructing the fixture. Offline edits to restore_on_startup invalidate its
# protected-pref hash and Chromium resets it to DEFAULT on the next launch.
# Close the temporary Settings tab before taking any tab/window baseline.
prefs_continue() {
  python3 - "$S/cdp.mjs" "$PORT" "$OUT/native-startup-prefs.json" <<'PY'
import json, subprocess, sys, time, urllib.request
cdp, port, output = sys.argv[1:]
def targets():
    with urllib.request.urlopen(f'http://127.0.0.1:{port}/json', timeout=2) as response:
        return json.load(response)
def call(target, method, params):
    result = subprocess.run(['node', cdp, port, target, method, json.dumps(params)],
                            capture_output=True, text=True, timeout=12)
    data = json.loads(result.stdout)
    if result.returncode or 'error' in data or 'exceptionDetails' in data:
        raise RuntimeError(f'{method} failed: {data}')
    return data
seed = next(t['id'] for t in targets() if t.get('type') == 'page')
settings = call(seed, 'Target.createTarget', {'url': 'chrome://settings/ahoi'})['targetId']
evidence = {}
try:
    deadline = time.monotonic() + 15
    while True:
        if any(t['id'] == settings and t.get('url', '').startswith('chrome://settings') for t in targets()):
            ready = call(settings, 'Runtime.evaluate', {
                'expression': "document.readyState === 'complete' && typeof chrome !== 'undefined' && !!chrome.settingsPrivate",
                'returnByValue': True})
            if ready.get('result', {}).get('value') is True:
                break
        if time.monotonic() >= deadline:
            raise RuntimeError('Settings document/native API did not become ready')
        time.sleep(0.25)
    expression = '''(async () => {
      const results = [];
      for (const [key, expected] of [['session.restore_on_startup', 1],
                                    ['ahoi.session.startup_mode', 'continue']]) {
        results.push(await new Promise(resolve =>
          chrome.settingsPrivate.setPref(key, expected, '', ok =>
            chrome.settingsPrivate.getPref(key, p =>
              resolve({key, expected, setterSucceeded:ok, value:p.value})))));
      }
      return results;
    })()'''
    evidence = call(settings, 'Runtime.evaluate',
                    {'expression': expression, 'awaitPromise': True, 'returnByValue': True})
    values = evidence.get('result', {}).get('value', [])
    if len(values) != 2 or any(v.get('setterSucceeded') is not True or v.get('value') != v['expected'] for v in values):
        raise RuntimeError('Native startup preference readback failed')
finally:
    with open(output, 'w') as file:
        json.dump(evidence, file, indent=2)
        file.write('\n')
    call(seed, 'Target.closeTarget', {'targetId': settings})
PY
  [ "$?" = 0 ] || fail_setup "trusted startup preferences did not confirm"
}
# Keys go through the HID event tap like a real keyboard; hidkey refuses
# unless the app is frontmost, so bring it forward and retry.
key() {
  for attempt in 1 2 3 4 5; do
    activate_owned; sleep 0.3
    $AX hidkey $PID "$@" >> "$OUT/steps.txt"
    local status=$?
    [ "$status" = 0 ] && return 0
    if [ "$status" = 8 ]; then
      echo '{"cancelled":"owner focus changed during key chord","pass":false}' > "$OUT/verdict.json"
      local id
      id=$(site_ids | head -1)
      [ -z "$id" ] || CDP "$id" Browser.close '{}' > "$OUT/cancel-close.json" 2>&1
      exit 8
    fi
    sleep 1
  done
  echo "hidkey gave up: $*" >> "$OUT/steps.txt"; return 1
}
# ⌘Z by its letter, not by the ANSI position: hidkey sends virtual key
# codes, and QWERTZ layouts (German, Swiss, Austrian, Czech, ...) carry Z
# on the ANSI Y key (16). Key 6 is Y there, and ⌘Y opens History: build
# 51 showed a "Verlauf" row instead of an undo in the cmd-move and merge
# journeys.
KBD_LAYOUT=$(defaults read ~/Library/Preferences/com.apple.HIToolbox \
  AppleCurrentKeyboardLayoutInputSourceID 2>/dev/null)
case "$KBD_LAYOUT" in
  *QWERTY*) Z_KEY=6 ;;
  *German*|*Swiss*|*Austrian*|*Czech*|*Slovak*|*Hungarian*) Z_KEY=16 ;;
  *Croatian*|*Slovenian*|*Serbian-Latin*|*Albanian*) Z_KEY=16 ;;
  *) Z_KEY=6 ;;
esac
undo_key() {
  echo "undo: key $Z_KEY on ${KBD_LAYOUT:-unknown layout}" >> "$OUT/steps.txt"
  key $Z_KEY cmd
}
type_in() {
  for attempt in 1 2 3; do
    $AX type $PID "$1" >> "$OUT/steps.txt"; sleep 1
    $AX dump $PID 14 | grep "AXTextField" | grep -F -q -- "| $1" && return 0
    echo "info: typed text missing, retyping" >> "$OUT/steps.txt"
    key 0 cmd; sleep 0.5
  done
  return 1
}
waitax() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do $AX dump $PID ${3:-40} | grep -q -E "$1" && return 0; sleep 1; done; return 1; }
waiturl() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do urls | grep -q "$1" && return 0; sleep 1; done; return 1; }
wait_document() { # <exact fixture URL> <seconds>, committed document, not target intent
  local end=$(( $(date +%s) + $2 )) id params
  params=$(python3 -c 'import json,sys;print(json.dumps({"expression":"location.href === "+json.dumps(sys.argv[1])+" && document.readyState === \"complete\"", "returnByValue":True}))' "$1")
  while [ $(date +%s) -lt "$end" ]; do
    for id in $(site_ids); do
      CDP "$id" Runtime.evaluate "$params" 2>/dev/null |
        python3 -c 'import json,sys;sys.exit(0 if json.load(sys.stdin).get("result",{}).get("value") is True else 1)' 2>/dev/null && return 0
    done
    sleep 1
  done
  return 1
}
dump_navigation() {
  curl -fsS --max-time 3 http://127.0.0.1:$PORT/json > "$OUT/targets-at-failure.json" 2> "$OUT/targets-at-failure-error.txt"
  local id
  for id in $(site_ids); do
    CDP "$id" Page.getFrameTree '{}' > "$OUT/frame-$id-at-failure.json" 2>&1
    CDP "$id" Runtime.evaluate '{"expression":"JSON.stringify({href:location.href,title:document.title,ready:document.readyState,body:document.body?.innerText?.slice(0,2000)})","returnByValue":true}' > "$OUT/document-$id-at-failure.json" 2>&1
  done
}
RESULTS=(); record() { RESULTS+=("\"$1\": $2"); echo "$1 -> $2" >> "$OUT/steps.txt"; }
check() { if eval "$2"; then record "$1" true; else record "$1" false; fi; } # <name> <shell condition>
finish() {
  local joined; joined=$(IFS=,; echo "${RESULTS[*]-}")
  local sep=""; [ -n "$joined" ] && sep=", "
  echo "{${joined}${1:+$sep\"setupFailed\": \"$1\"}}" | python3 -c 'import json,sys;d=json.load(sys.stdin);d["pass"]=("setupFailed" not in d) and all(v is True for k,v in d.items() if k!="setupFailed");print(json.dumps(d,indent=1))' > "$OUT/verdict.json"
  cat "$OUT/verdict.json"
  # A failed assertion must fail the command as well as the evidence verdict.
  python3 - "$OUT/verdict.json" <<'PY'
import json, sys
with open(sys.argv[1]) as file:
    verdict = json.load(file)
sys.exit(0 if verdict.get('pass') is True else 1)
PY
}
fail_setup() { dump_navigation; $AX dump $PID 40 > "$OUT/ax-setup-failure.txt"; finish "$1"; quit; exit 4; }
open_url() { # <url> ; ⌘T + type + Return
  local opened=0
  for attempt in 1 2 3; do
    activate_owned; sleep 1; key 17 cmd
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 14 && { opened=1; break; }
  done
  [ $opened = 1 ] || fail_setup "command bar did not open for $1"
  sleep 1; type_in "$1"; sleep 1; key 36
  waiturl "${1##*/}" 5 || { echo "info: Return repeated" >> "$OUT/steps.txt"; key 36; }
  waiturl "${1##*/}" 20 || fail_setup "did not load $1"
  wait_document "$1" 20 || fail_setup "fixture document did not commit $1"; sleep 2
}
# Every site page: title, CSS viewport, visibility, focus, reload mark, draft
# text and window id, one line each in $OUT/snap-<n>-<name>.txt.
snap() {
  SNAPN=$((SNAPN + 1)); SNAP="$OUT/snap-$SNAPN-$1.txt"; : > "$SNAP"
  local expr='JSON.stringify({t:document.title,w:innerWidth,h:innerHeight,v:document.visibilityState,f:document.hasFocus(),m:window.__m||"",val:document.getElementById("f")?document.getElementById("f").value:""})'
  local params; params=$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True}))' "$expr")
  for id in $(site_ids); do
    local js win
    js=$(CDP "$id" Runtime.evaluate "$params" 2>/dev/null | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))' 2>/dev/null)
    win=$(CDP "$id" Browser.getWindowForTarget '{}' 2>/dev/null | python3 -c 'import json,sys;print(json.load(sys.stdin).get("windowId",""))' 2>/dev/null)
    echo "$id|$win|$js" >> "$SNAP"
  done
}
q() { python3 $P-ana.py "$SNAP" "$W0" "$H0" "$@"; }
# Marks every site page and fills its draft field; a reload loses both.
mark_all() {
  for id in $(site_ids); do
    CDP "$id" Runtime.evaluate '{"expression":"window.__m=\"kept\";document.getElementById(\"f\").value=document.title+\"-draft\";1","returnByValue":true}' > /dev/null 2>&1
  done
}
file_of() { local t=${1:-PaneA}; echo "$(echo ${t#Pane} | tr A-H a-h).html"; }
focus_pane() { key $((17 + $1)) cmd ctrl; sleep 1.5; snap "focus-$1"; }  # ⌘⌃1…4
without() { echo "$1" | tr , '\n' | grep -v -x -F "$2" | paste -sd, -; } # <list> <title>
# Native Tab menu split of the active tab; the new pane (Chromium's split
# new-tab page) is navigated to <file>.
tab_menu_split() { # <file>; splits the active page with <file> via the picker
  # The new pane is Chromium's split tab picker ("Tab auswählen"); it is no
  # DevTools target (build 50), so the target is opened as a tab first and
  # then chosen in the picker, as a person would.
  local title; title="Pane$(echo "${1%.html}" | tr a-h A-H)"
  open_url "$SITE/$1"; key 48 ctrl opt; sleep 2
  $AX press $PID "AXMenuItem:Tab zu neuer geteilter Ansicht hinzufügen" >> "$OUT/steps.txt"; sleep 3
  waitax "AXWebArea \\| Tab auswählen" 8 || { echo "info: no tab picker" >> "$OUT/steps.txt"; return 1; }
  local tries; for tries in 1 2 3; do
    activate_owned; sleep 0.3
    $AX hidclick $PID "$title " >> "$OUT/steps.txt" && break; sleep 1
  done
  sleep 3; ! $AX dump $PID 14 | grep -q "AXWebArea | Tab auswählen"
}
# window.open with a user gesture from <opener file>; the popup overlay opens.
# Waits for the overlay's close button: the split button's name carried the
# refusal text on a full split up to build 58.
popup_from() { # <opener file> <popup file>
  CDP "/$1" Runtime.evaluate "{\"expression\":\"window.open('$SITE/$2','_blank','popup,width=520,height=420')?1:0\",\"userGesture\":true,\"returnByValue\":true}" >> "$OUT/steps.txt"
  echo >> "$OUT/steps.txt"
  waitax "AXButton \\| Popup schließen" 10
}
# The overlay's split action; ⌘⇧↩ is its keyboard equivalent.
popup_to_split() {
  $AX press $PID "AXButton:Popup im Split View öffnen" >> "$OUT/steps.txt" || key 36 cmd shift
  sleep 3
}
# Opens the sidebar row menu of <row title> until <menu item> shows
# (AXShowMenu, then an HID right-click).
open_row_menu() { # <row title> <menu item>
  local line role name
  line=$($AX dump $PID 40 | grep -o -E "AX(RadioButton|Tab|Row|Cell|Button) \| [^|]*$1[^|]*" | head -1 | sed -E 's/ *$//')
  echo "row for $1: $line" >> "$OUT/steps.txt"
  [ -n "$line" ] || return 1
  role=${line%% |*}; name=${line#*| }
  key 53; sleep 1
  $AX press $PID "$role:$name" AXShowMenu >> "$OUT/steps.txt"
  waitax "AXMenuItem \\| $2" 5 && return 0
  key 53; sleep 1; activate_owned; sleep 1
  $AX hidrightclick $PID "$role:$name" >> "$OUT/steps.txt"
  waitax "AXMenuItem \\| $2" 5
}
# Sidebar row menu and one item.
row_menu() { # <row title> <menu item>
  open_row_menu "$1" "$2" || { $AX dump $PID 40 > "$OUT/ax-row-menu-missing.txt"; return 1; }
  $AX press $PID "AXMenuItem:$2" >> "$OUT/steps.txt"; sleep 2
}
# The presets: PaneA's row menu, submenu "Geteilte Ansicht anordnen"
# (IDS_TAB_CXMENU_ARRANGE_SPLIT, three or four panes only). Optionally reads
# the check mark of <check item> into CHECKED, then presses <item> or
# closes submenu and menu.
SPLIT_MENU="Geteilte Ansicht anordnen"
split_menu() { # <item or ""> <check item or "">
  CHECKED=false
  local want=${1:-$2} opened=0
  for attempt in 1 2 3; do
    if open_row_menu PaneA "$SPLIT_MENU"; then
      # Pressing the submenu item opens it, so NSMenu refreshes the marks.
      $AX press $PID "AXMenuItem:$SPLIT_MENU" >> "$OUT/steps.txt"; sleep 1
      waitax "AXMenuItem \\| $want" 5 && { opened=1; break; }
    fi
    key 53; sleep 1
    key 53; sleep 1
  done
  [ $opened = 1 ] || { $AX dump $PID 40 > "$OUT/ax-split-menu-missing.txt"; return 1; }
  if [ -n "$2" ]; then $AX checked $PID "AXMenuItem:$2" >> "$OUT/steps.txt" && CHECKED=true; fi
  if [ -n "$1" ]; then
    $AX press $PID "AXMenuItem:$1" >> "$OUT/steps.txt"
  else
    key 53; sleep 0.5; key 53
  fi
  sleep 2
}
# Exit 0 when exactly these sidebar rows share one AXGroup parent and nothing
# else (Solo, other pages) sits in that group: the split is one sidebar row.
sidebar_group() { # <evidence name> <titles...>
  local f="$OUT/ax-sidebar-$1.txt"; shift; $AX dump $PID 40 > "$f"
  python3 "$S/split_sidebar_group.py" "$f" "$@" >> "$OUT/steps.txt"
}
