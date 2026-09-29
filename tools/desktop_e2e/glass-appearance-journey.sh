#!/bin/bash
# usage: glass-appearance-journey.sh <App.app> <outdir>
# DoD 6 Liquid Glass on an installed candidate. Toggles Ahoi's own Glass
# setting OFF and back ON through the real settings control
# (#ahoiGlassEnabled on chrome://settings/ahoi, clicked via CDP inside the
# page) and records what CDP and PID-scoped AX can observe:
#   - the persisted pref follows the control (ahoi.appearance.glass_enabled);
#   - the active page's pixels are identical with Glass ON and OFF
#     (CDP Page.captureScreenshot of web content only): glass never draws
#     over, tints or blurs web content;
#   - the browser window's AX tree before/after each toggle (saved for the
#     owner; the native glass view is not an AX element, so presence is not
#     asserted from AX);
#   - the current macOS Reduce Transparency / Increase Contrast / Reduce
#     Motion values, read-only. This journey never changes macOS settings;
#     the fallback matrix for those signals is covered by
#     ahoi_appearance_unittests (glass_material_unittest.cc).
# No keyboard, mouse or clipboard input and no screen capture: the milky
# look itself (frame, Sidebar, command bar, notch, MiniPlayer) is the
# owner's visual judgement. Run only while holding the desktop e2e.lock
# (the queue runner does); results: <outdir>/results.json.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9357
SITE_PORT=${AHOI_E2E_SITE_PORT:-8797}; DEPTH=${AHOI_E2E_AX_DEPTH:-14}
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] \
  || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
for port in $PORT $SITE_PORT; do
  if lsof -nP -iTCP:$port -sTCP:LISTEN >/dev/null 2>&1; then
    echo "port $port busy" >&2; exit 6
  fi
done
mkdir -p "$OUT"; : > "$OUT/results.txt"; : > "$OUT/run.txt"
P=$(mktemp -d /private/tmp/ahoi-glass-profile.XXXXXX); SITE=$P-site
mkdir -p "$SITE"
record() { echo "$1 $2" >> "$OUT/results.txt"; }
log() { echo "$*" >> "$OUT/run.txt"; }

# Fixture with fixed text/background pairs and hard edges: any tint, blur or
# material over the page would change these pixels.
cat > "$SITE/contrast.html" <<'HTML'
<!doctype html><title>glass contrast fixture</title>
<style>
 body{margin:0;font:16px -apple-system,sans-serif;background:#fff;color:#111}
 .row{display:flex;height:120px} .row div{flex:1;padding:12px}
 .a{background:#fff;color:#111}.b{background:#000;color:#eee}
 .c{background:#0a84ff;color:#fff}.d{background:#f2f2f2;color:#333}
</style>
<div class=row><div class=a>Black on white</div><div class=b>White on
black</div><div class=c>White on blue</div><div class=d>Grey</div></div>
<p id=probe>Readable body text must keep its contrast with Glass ON.</p>
HTML
(cd "$SITE" && exec python3 -m http.server $SITE_PORT --bind 127.0.0.1 \
  > "$OUT/site.log" 2>&1) &
SITE_PID=$!
SYS_RT=$(defaults read com.apple.universalaccess reduceTransparency 2>/dev/null || echo unset)
SYS_IC=$(defaults read com.apple.universalaccess increaseContrast 2>/dev/null || echo unset)
SYS_RM=$(defaults read com.apple.universalaccess reduceMotion 2>/dev/null || echo unset)
log "system reduceTransparency=$SYS_RT increaseContrast=$SYS_IC reduceMotion=$SYS_RM"

"$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run \
  --no-default-browser-check --remote-debugging-port=$PORT \
  about:blank > "$OUT/browser.log" 2>&1 &
PID=$!; log "pid=$PID profile=$P"
trap 'kill $PID 2>/dev/null; kill $SITE_PID 2>/dev/null; sleep 2;
      kill -9 $PID 2>/dev/null; rm -rf "$P" "$SITE"' EXIT
for i in $(seq 1 60); do
  curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2
done
sleep 6
# chrome:// pages are not opened from the command line; open Settings via
# DevTools (build 51 found no control because Settings never loaded).
curl -s -X PUT "http://127.0.0.1:$PORT/json/new?chrome://settings/ahoi" \
  > /dev/null
sleep 4
# The fixture becomes the active tab; Settings stays open in the background.
curl -s -X PUT \
  "http://127.0.0.1:$PORT/json/new?http://127.0.0.1:$SITE_PORT/contrast.html" \
  > /dev/null
sleep 4
"$AX" enable $PID >> "$OUT/run.txt" 2>&1

cdp() { # <url substring> <method> <params-json>
  node "$S/cdp.mjs" $PORT "$1" "$2" "$3"; }
eval_in() { # <url substring> <expression>
  cdp "$1" Runtime.evaluate "$(python3 -c 'import json,sys
print(json.dumps({"expression":sys.argv[1],"returnByValue":True,
                  "awaitPromise":True}))' "$2")" \
  | python3 -c 'import json,sys
v=json.load(sys.stdin).get("result",{}).get("value","")
print(v if isinstance(v,str) else json.dumps(v))'; }

# Finds the Ahoi Glass control through open shadow roots, optionally clicks
# it like a user, then reports the control and persisted pref state.
glass_probe() { # <click:true|false>
  eval_in "settings/ahoi" '(async () => {
  const find = (root, sel) => {
    const hit = root.querySelector(sel); if (hit) return hit;
    for (const el of root.querySelectorAll("*")) {
      if (el.shadowRoot) { const h = find(el.shadowRoot, sel); if (h) return h; }
    }
    return null;
  };
  let toggle = null;
  for (let i = 0; i < 20 && !toggle; i++) {
    toggle = find(document, "#ahoiGlassEnabled");
    if (!toggle) await new Promise(r => setTimeout(r, 500));
  }
  if (!toggle) return JSON.stringify({found: false});
  if ('"$1"') { toggle.scrollIntoView(); toggle.click(); }
  await new Promise(r => setTimeout(r, 800));
  const pref = await new Promise(r => chrome.settingsPrivate.getPref(
      "ahoi.appearance.glass_enabled", p => r(p ? p.value : null)));
  return JSON.stringify({found: true, checked: !!toggle.checked, pref});
})()'; }

shot() { # <name> -> sha256 of the active page's pixels
  cdp "contrast.html" Page.captureScreenshot '{"format":"png"}' \
    | python3 -c 'import base64,hashlib,json,sys
d=json.load(sys.stdin).get("data","")
raw=base64.b64decode(d) if d else b""
open(sys.argv[1],"wb").write(raw)
print(hashlib.sha256(raw).hexdigest() if raw else "none")' \
      "$OUT/page-$1.png"; }

contrast() {
  eval_in "contrast.html" '(() => {
  const s = getComputedStyle(document.getElementById("probe"));
  return JSON.stringify({color: s.color,
    background: getComputedStyle(document.body).backgroundColor});
})()'; }

settle() { sleep 2; }

initial=$(glass_probe false); log "initial $initial"
settle; ax_on=$("$AX" dump $PID $DEPTH > "$OUT/ax-glass-on.txt" 2>&1; echo $?)
hash_on=$(shot glass-on); style_on=$(contrast)
off=$(glass_probe true); log "after-toggle-1 $off"
settle; "$AX" dump $PID $DEPTH > "$OUT/ax-glass-off.txt" 2>&1
hash_off=$(shot glass-off); style_off=$(contrast)
on_again=$(glass_probe true); log "after-toggle-2 $on_again"
settle; "$AX" dump $PID $DEPTH > "$OUT/ax-glass-on-again.txt" 2>&1
hash_on_again=$(shot glass-on-again)
log "hash on=$hash_on off=$hash_off on_again=$hash_on_again"
log "style on=$style_on off=$style_off"

python3 - "$OUT/results.txt" "$initial" "$off" "$on_again" "$hash_on" \
  "$hash_off" "$hash_on_again" "$style_on" "$style_off" "$ax_on" \
  "$OUT/ax-glass-on.txt" <<'PY'
import json, sys
(out, initial, off, on_again, h_on, h_off, h_on2, s_on, s_off, ax_rc,
 ax_file) = sys.argv[1:]
def load(raw):
    try:
        return json.loads(raw)
    except Exception:
        return {}
i, o, a = load(initial), load(off), load(on_again)
rows = []
rows.append(("GLASS_control_present",
             "PASS" if i.get("found") else "FAIL:" + initial[:160]))
rows.append(("GLASS_default_on",
             "PASS" if i.get("pref") is True else "FAIL:" + initial[:160]))
rows.append(("GLASS_toggle_off_persists",
             "PASS" if o.get("pref") is False and not o.get("checked")
             else "FAIL:" + off[:160]))
rows.append(("GLASS_toggle_on_persists",
             "PASS" if a.get("pref") is True and a.get("checked")
             else "FAIL:" + on_again[:160]))
ok = h_on != "none" and h_on == h_off == h_on2
rows.append(("GLASS_web_content_pixels_unchanged",
             "PASS" if ok else f"FAIL:on={h_on[:12]} off={h_off[:12]} "
                               f"on2={h_on2[:12]}"))
rows.append(("GLASS_page_text_contrast_unchanged",
             "PASS" if s_on and s_on == s_off else f"FAIL:{s_on} vs {s_off}"))
try:
    ax = open(ax_file).read()
except OSError:
    ax = ""
rows.append(("GLASS_browser_ax_tree_readable",
             "PASS" if ax_rc == "0" and "AXWindow" in ax
             else f"FAIL:rc={ax_rc}"))
with open(out, "a") as f:
    for k, v in rows:
        f.write(f"{k} {v}\n")
PY
python3 - "$OUT/results.txt" "$SYS_RT" "$SYS_IC" "$SYS_RM" \
  > "$OUT/results.json" <<'PY'
import json, sys
rows = dict(line.rstrip("\n").split(" ", 1)
            for line in open(sys.argv[1]) if line.strip())
print(json.dumps({
    "results": rows,
    "system_accessibility_read_only": {
        "reduceTransparency": sys.argv[2],
        "increaseContrast": sys.argv[3],
        "reduceMotion": sys.argv[4]},
    "owner_visual_review": [
        "Glass ON: milky frame gutters and docked Sidebar read as one pane",
        "Glass OFF: calm opaque chrome surface, no saturated frame colour",
        "Command bar and developer tools: native glass panel, no grey sheet",
        "Floating navigation row and reveal notch share one tint",
        "Light and dark appearance; Reduce Transparency / Increase Contrast",
    ],
    "pass": all(v == "PASS" for v in rows.values())}, indent=1))
PY
cat "$OUT/results.json"
