# Shared helpers for split-matrix-journey.sh and split-lifecycle-journey.sh.
# Sourced after APP, OUT, S, AX, PORT and SITE_PORT are set. Split view is
# driven without HID drag: the native Tab menu item "Tab zu neuer geteilter
# Ansicht hinzufügen" (IDC_NEW_SPLIT_TAB) makes a two-pane split, the popup
# overlay's "Popup im Split View öffnen" adds a pane to the opener's split
# (CreateOrAddToSplitFromDrop, the same model path as a sidebar drop), and the
# pane menu "Geteilte Ansicht anordnen", the sidebar row menu and the split
# shortcuts from keyboard_shortcuts.cc do the rest. German labels are from
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
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT; SITE=http://127.0.0.1:$SITE_PORT
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
launch() { # [url] [nodevtools]
  local url=${1:-} dev=--remote-debugging-port=$PORT
  [ "${2:-}" = nodevtools ] && dev=""
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run --no-default-browser-check \
    $dev $url >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$P devtools=${dev:+on}" >> "$OUT/run.txt"
  if [ -n "$dev" ]; then
    for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  else
    waitax "AXWindow \\|" 40
  fi
  sleep 4; $AX activate $PID >> "$OUT/steps.txt"
}
quit() { key 12 cmd; for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || return 0; sleep 1; done; echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3; }
# Continue the last session on the next start: Ahoi's own choice for a start
# without DevTools, Chromium's restore-last-session for a start with
# --remote-debugging-port (an explicit startup intent Ahoi defers on).
prefs_continue() {
  python3 - "$P/Default/Preferences" <<'PY'
import json, sys
p = sys.argv[1]; d = json.load(open(p))
d.setdefault("ahoi", {}).setdefault("session", {})["startup_mode"] = "continue"
d.setdefault("session", {})["restore_on_startup"] = 1
json.dump(d, open(p, "w"))
PY
}
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
RESULTS=(); record() { RESULTS+=("\"$1\": $2"); echo "$1 -> $2" >> "$OUT/steps.txt"; }
check() { if eval "$2"; then record "$1" true; else record "$1" false; fi; } # <name> <shell condition>
finish() {
  local joined; joined=$(IFS=,; echo "${RESULTS[*]-}")
  local sep=""; [ -n "$joined" ] && sep=", "
  echo "{${joined}${1:+$sep\"setupFailed\": \"$1\"}}" | python3 -c 'import json,sys;d=json.load(sys.stdin);d["pass"]=("setupFailed" not in d) and all(v is True for k,v in d.items() if k!="setupFailed");print(json.dumps(d,indent=1))' > "$OUT/verdict.json"
  cat "$OUT/verdict.json"
}
fail_setup() { $AX dump $PID 40 > "$OUT/ax-setup-failure.txt"; finish "$1"; quit; exit 4; }
open_url() { # <url> ; ⌘T + type + Return
  local opened=0
  for attempt in 1 2 3; do
    $AX activate $PID >> "$OUT/steps.txt"; sleep 1; key 17 cmd
    waitax "AXWindow \\| Suchen oder URL eingeben" 6 14 && { opened=1; break; }
  done
  [ $opened = 1 ] || fail_setup "command bar did not open for $1"
  sleep 1; type_in "$1"; sleep 1; key 36
  waiturl "${1##*/}" 5 || { echo "info: Return repeated" >> "$OUT/steps.txt"; key 36; }
  waiturl "${1##*/}" 20 || fail_setup "did not load $1"; sleep 2
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
tab_menu_split() {
  $AX press $PID "AXMenuItem:Tab zu neuer geteilter Ansicht hinzufügen" >> "$OUT/steps.txt"; sleep 3
  local new; new=$(curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys
t=[x for x in json.load(sys.stdin) if x["type"]=="page" and not x["url"].startswith(sys.argv[1]) and not x["url"].startswith("devtools")]
print(t[0]["id"] if t else "")' "$SITE")
  echo "split new-tab pane: $new" >> "$OUT/steps.txt"
  [ -n "$new" ] && CDP "$new" Page.navigate "{\"url\":\"$SITE/$1\"}" >> "$OUT/steps.txt"
  waiturl "$1" 15 && sleep 2
}
# window.open with a user gesture from <opener file>; the popup overlay opens.
popup_from() { # <opener file> <popup file>
  CDP "/$1" Runtime.evaluate "{\"expression\":\"window.open('$SITE/$2','_blank','popup,width=520,height=420')?1:0\",\"userGesture\":true,\"returnByValue\":true}" >> "$OUT/steps.txt"
  echo >> "$OUT/steps.txt"
  waitax "AXButton \\| Popup im Split View öffnen" 10
}
# The overlay's split action; ⌘⇧↩ is its keyboard equivalent.
popup_to_split() {
  $AX press $PID "AXButton:Popup im Split View öffnen" >> "$OUT/steps.txt" || key 36 cmd shift
  sleep 3
}
# Sidebar row menu (AXShowMenu, then an HID right-click) and one item.
row_menu() { # <row title> <menu item>
  local line role name
  line=$($AX dump $PID 40 | grep -o -E "AX(RadioButton|Tab|Row|Cell|Button) \| [^|]*$1[^|]*" | head -1 | sed -E 's/ *$//')
  echo "row for $1: $line" >> "$OUT/steps.txt"
  [ -n "$line" ] || return 1
  role=${line%% |*}; name=${line#*| }
  $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
  $AX press $PID "$role:$name" AXShowMenu >> "$OUT/steps.txt"
  if ! waitax "AXMenuItem \\| $2" 5; then
    $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1; $AX activate $PID >> "$OUT/steps.txt"; sleep 1
    $AX hidrightclick $PID "$role:$name" >> "$OUT/steps.txt"
    waitax "AXMenuItem \\| $2" 5 || { $AX dump $PID 40 > "$OUT/ax-row-menu-missing.txt"; return 1; }
  fi
  $AX press $PID "AXMenuItem:$2" >> "$OUT/steps.txt"; sleep 2
}
# Pane menu "Geteilte Ansicht anordnen": optionally reads the check mark of
# <check item> into CHECKED, then presses <item> (or closes the menu).
split_menu() { # <item or ""> <check item or "">
  CHECKED=false; $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
  local want=${1:-$2} opened=0
  for attempt in 1 2 3; do
    $AX press $PID "Geteilte Ansicht anordnen" >> "$OUT/steps.txt"
    waitax "AXMenuItem \\| $want" 5 && { opened=1; break; }
    $AX key $PID 53 >> "$OUT/steps.txt"; sleep 1
  done
  [ $opened = 1 ] || { $AX dump $PID 40 > "$OUT/ax-split-menu-missing.txt"; return 1; }
  if [ -n "$2" ]; then $AX checked $PID "AXMenuItem:$2" >> "$OUT/steps.txt" && CHECKED=true; fi
  if [ -n "$1" ]; then $AX press $PID "AXMenuItem:$1" >> "$OUT/steps.txt"; else $AX key $PID 53 >> "$OUT/steps.txt"; fi
  sleep 2
}
# Exit 0 when exactly these sidebar rows share one AXGroup parent and nothing
# else (Solo, other pages) sits in that group: the split is one sidebar row.
sidebar_group() { # <evidence name> <titles...>
  local f="$OUT/ax-sidebar-$1.txt"; shift; $AX dump $PID 40 > "$f"
  python3 - "$f" "$@" >> "$OUT/steps.txt" <<'PY'
import re, sys
lines = open(sys.argv[1]).read().splitlines(); want = sys.argv[2:]
row = re.compile(r'^( *)AX(RadioButton|Tab|Row|Cell|Button)\b[^|]* \| (.*)$')
indent = lambda l: len(l) - len(l.lstrip(" "))
def parent(i):
    for j in range(i - 1, -1, -1):
        if indent(lines[j]) < indent(lines[i]): return j
    return -1
rows = [(i, m.group(3)) for i, l in enumerate(lines) for m in [row.match(l)] if m]
found = {}
for t in want:
    hit = next((i for i, text in rows if any(p.startswith(t) for p in text.split(" | "))), None)
    if hit is not None: found[t] = parent(hit)
parents = set(found.values())
p = parents.pop() if len(parents) == 1 else -1
members = [i for i, _ in rows if p >= 0 and parent(i) == p]
solo = next((i for i, text in rows if text.split(" | ")[0].startswith("Solo")), None)
ok = (len(found) == len(want) and p >= 0 and lines[p].strip().startswith("AXGroup")
      and len(members) == len(want) and (solo is None or parent(solo) != p))
print("sidebar group %s: found=%s parent=%r members=%d -> %s" % (want, sorted(found), lines[p].strip() if p >= 0 else None, len(members), ok))
sys.exit(0 if ok else 1)
PY
}
