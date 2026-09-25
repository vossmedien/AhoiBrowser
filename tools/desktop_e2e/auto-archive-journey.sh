#!/bin/bash
# usage: auto-archive-journey.sh <App.app> <outdir>
# PID-scoped AX + CDP journey for automatic archiving (DoD 28): default policy
# is Never; with a policy enabled an inactive temporary tab moves to the
# restorable archive, a tab with an edited form stays open, and restoring the
# archived entry brings the same URL back. Uses the development-only
# --ahoi-e2e-archive-age-seconds seam, so it needs a non-official build.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9377; AGE=${AHOI_E2E_ARCHIVE_AGE:-25}
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-archive-profile.XXXXXX); : > "$OUT/steps.txt"; : > "$OUT/results.txt"
SITE_PORT=${AHOI_E2E_SITE_PORT:-8799}; mkdir -p "$P-site"
printf '<title>Ahoi archive idle</title><h1>idle page</h1>' > "$P-site/idle.html"
printf '<title>Ahoi archive form</title><form><input id="f" name="f"></form>' > "$P-site/form.html"
printf '<title>Ahoi archive active</title><h1>active page</h1>' > "$P-site/active.html"
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory "$P-site" > "$OUT/site.log" 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null' EXIT; SITE=http://127.0.0.1:$SITE_PORT
"$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run --no-default-browser-check \
  --remote-debugging-port=$PORT --ahoi-e2e-archive-age-seconds=$AGE about:blank > "$OUT/browser.log" 2>&1 &
PID=$!; echo "pid=$PID profile=$P age=$AGE" > "$OUT/run.txt"
for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
sleep 4

# Keys go through the HID event tap like a real keyboard (keys posted to the
# process are intermittently dropped by Chromium); hidkey refuses unless the
# app is frontmost, so bring it forward and retry.
ax() {
  if [ "$1" = key ]; then
    shift; local pid=$1; shift
    for attempt in 1 2 3 4 5; do
      "$AX" activate "$pid" >/dev/null 2>&1; sleep 0.3
      "$AX" hidkey "$pid" "$@" >> "$OUT/steps.txt" 2>&1 && return 0
      sleep 1
    done
    echo "hidkey gave up: $*" >> "$OUT/steps.txt"; return 1
  fi
  "$AX" "$@" >> "$OUT/steps.txt" 2>&1
}
record() { echo "$1 $2" >> "$OUT/results.txt"; echo "== $1 $2" >> "$OUT/steps.txt"; }
urls() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys
print(" ".join(sorted(t["url"] for t in json.load(sys.stdin) if t["type"]=="page")))'; }
waitax() { local end=$(( $(date +%s) + $2 ))
  while [ $(date +%s) -lt $end ]; do "$AX" dump $PID 40 | grep -q -E "$1" && return 0; sleep 1; done; return 1; }
open_tab() { local ok=1
  for i in 1 2 3; do ax activate $PID; sleep 1; ax key $PID 17 cmd
    waitax "AXWindow \| Suchen oder URL eingeben" 6 && { ok=0; break; }; done
  [ $ok = 0 ] || return 1
  ax key $PID 0 cmd; ax type $PID "$1"; sleep 1; ax key $PID 36; sleep 4; }
workspace_menu() { for i in 1 2 3; do ax press $PID "Inbox, Workspace wechseln" AXShowMenu
  waitax "Inaktive temporäre Tabs archivieren" 4 && return 0; ax key $PID 53; sleep 2; done; return 1; }
# CDP helper: evaluate JS in the page whose URL contains $1 (form typing only).
cdp_eval() { python3 - "$PORT" "$1" "$2" <<'PY'
import json, socket, sys, urllib.request, base64, os
port, needle, expr = sys.argv[1], sys.argv[2], sys.argv[3]
targets = json.load(urllib.request.urlopen(f"http://127.0.0.1:{port}/json"))
t = next(t for t in targets if t["type"] == "page" and needle in t["url"])
ws = t["webSocketDebuggerUrl"]; host, path = ws[5:].split("/", 1)
h, p = host.split(":"); s = socket.create_connection((h, int(p)))
key = base64.b64encode(os.urandom(16)).decode()
s.send(f"GET /{path} HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n".encode())
s.recv(4096)
msg = json.dumps({"id": 1, "method": "Runtime.evaluate", "params": {"expression": expr, "userGesture": True}}).encode()
mask = os.urandom(4); hdr = bytes([0x81]) + (bytes([0x80 | len(msg)]) if len(msg) < 126 else bytes([0x80 | 126]) + len(msg).to_bytes(2, "big"))
s.send(hdr + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(msg))); print(s.recv(65536)[2:200])
PY
}

# 0 Default policy is Never.
# The checked policy carries the native menu check mark.
workspace_menu && "$AX" checked $PID "AXMenuItem:Nie (Standard)" >> "$OUT/steps.txt" \
  && record default_never PASS || record default_never FAIL
ax key $PID 53; sleep 1
# 1 Open idle, form and active pages; edit the form (unsaved user input).
open_tab "$SITE/idle.html"; open_tab "$SITE/form.html"
cdp_eval form.html 'document.getElementById("f").focus(); document.execCommand("insertText", false, "draft");' >> "$OUT/steps.txt" 2>&1
open_tab "$SITE/active.html"
echo "before $(urls)" >> "$OUT/steps.txt"
# 2 Enable the shortest policy (age replaced by the E2E seam).
workspace_menu && ax press $PID "Nach 12 Stunden"; sleep 1
workspace_menu && "$AX" checked $PID "AXMenuItem:Nach 12 Stunden" >> "$OUT/steps.txt" \
  && record policy_selected PASS || record policy_selected FAIL
"$AX" dump $PID 45 | grep -q 'nichts wird gelöscht' && record policy_explained PASS || record policy_explained FAIL
ax key $PID 53
# 3 Wait past the age; the idle tab must leave, form + active must stay.
sleep $(( AGE * 2 + 10 )); AFTER=$(urls); echo "after $AFTER" >> "$OUT/steps.txt"
case "$AFTER" in *idle.html*) record idle_tab_archived FAIL ;; *) record idle_tab_archived PASS ;; esac
case "$AFTER" in *form.html*) record edited_form_protected PASS ;; *) record edited_form_protected FAIL ;; esac
case "$AFTER" in *active.html*) record active_tab_kept PASS ;; *) record active_tab_kept FAIL ;; esac
# 4 The archive lists the entry with its reason; restoring brings the URL back.
workspace_menu && ax press $PID "Archiv durchsuchen …"
waitax "Ahoi archive idle" 10 && record archive_lists_entry PASS || record archive_lists_entry FAIL
"$AX" dump $PID 45 | grep -i -E 'automatisch|automatic' -q && record archive_shows_reason PASS || record archive_shows_reason FAIL
"$AX" dump $PID 45 > "$OUT/archive-dialog.txt"
# Row action "Wiederherstellen …: <title>" opens the restore target menu.
ax press $PID "AXButton:Wiederherstellen …: Ahoi archive idle"
waitax "Am ursprünglichen Ort wiederherstellen" 6 && ax press $PID "Am ursprünglichen Ort wiederherstellen"
sleep 5
# Restoring puts the entry back without loading it, so an unloaded tab may be
# missing from the DevTools list; the sidebar row counts too once the archive
# dialog is closed. A failure notice always fails.
ax press $PID "AXButton:Schließen"; sleep 2
"$AX" dump $PID 45 > "$OUT/after-restore.txt"; echo "restored $(urls)" >> "$OUT/steps.txt"
if grep -q -E 'konnte nicht|could not complete' "$OUT/after-restore.txt"; then
  record restore_brings_url_back FAIL
elif urls | grep -q idle.html || grep -v -E 'Wiederherstellen|löschen' "$OUT/after-restore.txt" | grep -q 'Ahoi archive idle'; then
  record restore_brings_url_back PASS
else record restore_brings_url_back FAIL; fi
ax key $PID 53
# 5 Reset the policy to Never so the profile keeps the default.
workspace_menu && ax press $PID "Nie (Standard)"; sleep 1
workspace_menu && "$AX" checked $PID "AXMenuItem:Nie (Standard)" >> "$OUT/steps.txt" \
  && record policy_reset_to_never PASS || record policy_reset_to_never FAIL
ax key $PID 53

ax key $PID 12 cmd; sleep 5
python3 - "$OUT/results.txt" > "$OUT/results.json" <<'PY'
import json,sys
rows=[l.split(" ",1) for l in open(sys.argv[1]).read().splitlines() if l]
res={k:v for k,v in rows}
print(json.dumps({"pass":all(v=="PASS" for v in res.values()) and len(res)>=10,"steps":res},indent=1))
PY
cat "$OUT/results.json"
