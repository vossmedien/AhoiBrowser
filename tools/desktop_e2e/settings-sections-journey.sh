#!/bin/bash
# usage: settings-sections-journey.sh <App.app> <outdir>
# CDP journey for the Ahoi settings page on an installed candidate; no input
# events. Opens chrome://settings/ahoi in a fresh profile and checks that the
# link routing card (<settings-ahoi-link-routing>) and the keyboard shortcut
# editor (<settings-ahoi-shortcuts>) render inside the page with content
# from their handlers: a titled card, the default route selector and at least
# one shortcut command row. Results: <outdir>/results.json.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
PORT=9389
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; : > "$OUT/results.txt"; : > "$OUT/run.txt"
P=$(mktemp -d /private/tmp/ahoi-settings-profile.XXXXXX)
record() { echo "$1 $2" >> "$OUT/results.txt"; }
eval_in() { # <url substring> <expression>
  node "$S/cdp.mjs" $PORT "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True,"awaitPromise":True}))' "$2")" \
    | python3 -c 'import json,sys;r=json.load(sys.stdin);v=r.get("result",{}).get("value");print(v if isinstance(v,str) else json.dumps(v if v is not None else r))'; }
"$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run --no-default-browser-check \
  --remote-debugging-port=$PORT chrome://settings/ahoi > "$OUT/browser.log" 2>&1 &
PID=$!; echo "pid=$PID profile=$P" >> "$OUT/run.txt"
trap 'kill $PID 2>/dev/null; sleep 2; kill -9 $PID 2>/dev/null; rm -rf "$P"' EXIT
python3 - "$0" "$APP/Contents/Info.plist" >> "$OUT/run.txt" <<'PY'
import hashlib, json, plistlib, sys
from pathlib import Path
info = plistlib.loads(Path(sys.argv[2]).read_bytes())
print(json.dumps({"journeySha256": hashlib.sha256(
    Path(sys.argv[1]).read_bytes()).hexdigest(),
    "appSourceCommit": info.get("AhoiSourceCommit"),
    "chromiumVersion": info.get("AhoiChromiumVersion")}))
PY
for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
sleep 8
# This journey measures Settings rendering, independent of startup URL
# routing. Build 59 opened only a New Tab target for the command-line URL.
# Navigate that real page explicitly, retaining any protocol failure as proof.
TARGET=$(curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(next((t["id"] for t in json.load(sys.stdin) if t["type"]=="page"),""))')
if [ -n "$TARGET" ]; then
  node "$S/cdp.mjs" $PORT "$TARGET" Page.navigate \
    '{"url":"chrome://settings/ahoi"}' >> "$OUT/run.txt"
fi
for i in $(seq 1 20); do
  curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;sys.exit(0 if any(t["type"]=="page" and "settings/ahoi" in t["url"] for t in json.load(sys.stdin)) else 1)' && break
  sleep 1
done
# Walks open shadow roots from settings-ui down to the Ahoi page.
PROBE='(async () => {
  const find = (root, selector) => {
    const direct = root.querySelector(selector);
    if (direct) return direct;
    for (const el of root.querySelectorAll("*")) {
      if (el.shadowRoot) { const hit = find(el.shadowRoot, selector); if (hit) return hit; }
    }
    return null;
  };
  // cdp.mjs gives up after 8 s; answer before that with diagnostics.
  for (let i = 0; i < 12; i++) {
    const page = find(document, "settings-ahoi-page");
    const routing = page && page.shadowRoot && page.shadowRoot.querySelector("settings-ahoi-link-routing");
    const shortcuts = page && page.shadowRoot && page.shadowRoot.querySelector("settings-ahoi-shortcuts");
    const card = routing && routing.shadowRoot && routing.shadowRoot.querySelector("#ahoiLinkRouting");
    const title = routing && routing.shadowRoot && routing.shadowRoot.querySelector("#ahoiLinkRoutingTitle");
    const editor = shortcuts && shortcuts.shadowRoot && shortcuts.shadowRoot.querySelector("#ahoiShortcuts");
    const rows = editor ? editor.querySelectorAll("[data-command-id]").length : 0;
    if (card && title && title.textContent.trim() && editor && rows > 0) {
      return JSON.stringify({page: true, routingTitle: title.textContent.trim(),
                             routingSelects: card.querySelectorAll("select").length,
                             shortcutRows: rows});
    }
    await new Promise(r => setTimeout(r, 500));
  }
  const page = find(document, "settings-ahoi-page");
  return JSON.stringify({page: !!page, url: location.href,
    routing: !!(page && page.shadowRoot && page.shadowRoot.querySelector("settings-ahoi-link-routing")),
    shortcuts: !!(page && page.shadowRoot && page.shadowRoot.querySelector("settings-ahoi-shortcuts"))});
})()'
result=$(eval_in "settings/ahoi" "$PROBE")
[ -z "$result" ] && sleep 5 && result=$(eval_in "settings/ahoi" "$PROBE")
curl -s http://127.0.0.1:$PORT/json >> "$OUT/run.txt"
echo "probe $result" >> "$OUT/run.txt"
python3 - "$result" "$OUT/results.txt" <<'PY'
import json, sys
raw, out = sys.argv[1:3]
try:
    r = json.loads(raw)
except Exception:
    r = {}
rows = []
rows.append(("SETTINGS_link_routing_card_renders",
             "PASS" if r.get("routingTitle") and r.get("routingSelects", 0) > 0 else "FAIL:" + raw[:200]))
rows.append(("SETTINGS_shortcut_editor_renders",
             "PASS" if r.get("shortcutRows", 0) > 0 else "FAIL:" + raw[:200]))
with open(out, "a") as f:
    for k, v in rows:
        f.write(f"{k} {v}\n")
PY
python3 - "$OUT/results.txt" > "$OUT/results.json" <<'PY'
import json, sys
rows = dict(line.rstrip("\n").split(" ", 1) for line in open(sys.argv[1]) if line.strip())
print(json.dumps({"results": rows,
                  "pass": all(v == "PASS" for v in rows.values())}, indent=1))
PY
cat "$OUT/results.json"
