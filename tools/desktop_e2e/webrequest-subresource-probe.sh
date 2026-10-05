#!/bin/bash
# usage: webrequest-subresource-probe.sh <App.app> <outdir>
# Regression probe for the 5 October 2026 P0: with any webRequest extension
# installed (uBO Classic, AnyChat), installed e86c929f aborted on the first
# document subresource request in WebRequestInfoInitParams
# (DCHECK_EQ(is_navigation_request, navigation_id.has_value())). A fresh
# profile loads a minimal MV3 extension with the webRequest permission, then
# a local page with image, script and stylesheet subresources is loaded and
# reloaded. Passes when the browser stays alive and the extension observed
# the main frame and the subresources. CDP and process checks only, no input
# events, so it also runs while the console is locked.
# Results: <outdir>/results.json.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
PORT=9395; SP=${AHOI_E2E_SITE_PORT:-8823}
for p in $PORT $SP; do
  if lsof -nP -iTCP:$p -sTCP:LISTEN >/dev/null 2>&1; then echo "port $p busy" >&2; exit 6; fi
done
mkdir -p "$OUT"; : > "$OUT/results.txt"
P=$(mktemp -d /private/tmp/ahoi-webrequest-probe.XXXXXX)
mkdir -p "$P/ext" "$P/site"
cat > "$P/ext/manifest.json" <<'JSON'
{"manifest_version": 3, "name": "Ahoi webRequest probe", "version": "1.0",
 "permissions": ["webRequest"], "host_permissions": ["<all_urls>"],
 "background": {"service_worker": "bg.js"}}
JSON
cat > "$P/ext/bg.js" <<'JS'
globalThis.seen = [];
chrome.webRequest.onBeforeRequest.addListener(
  (d) => { globalThis.seen.push(d.type + " " + d.url); },
  {urls: ["<all_urls>"]});
JS
printf '<title>Probe</title><link rel=stylesheet href=s.css><script src=a.js></script><img src=i1.svg><img src=i2.svg><h1>probe</h1>' > "$P/site/index.html"
printf 'h1{color:red}' > "$P/site/s.css"; printf 'window.probe=1' > "$P/site/a.js"
printf '<svg xmlns="http://www.w3.org/2000/svg" width="4" height="4"/>' > "$P/site/i1.svg"
cp "$P/site/i1.svg" "$P/site/i2.svg"
python3 -m http.server $SP --bind 127.0.0.1 --directory "$P/site" > "$OUT/site.log" 2>&1 &
SITE_PID=$!
"$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P/profile" --no-first-run \
  --no-default-browser-check --remote-debugging-port=$PORT \
  --disable-features=DisableLoadExtensionCommandLineSwitch \
  --load-extension="$P/ext" "http://127.0.0.1:$SP/index.html" > "$OUT/browser.log" 2>&1 &
BPID=$!
cleanup() {
  kill $BPID 2>/dev/null; sleep 2; kill -9 $BPID 2>/dev/null
  kill $SITE_PID 2>/dev/null; rm -rf "$P"
}
trap cleanup EXIT
record() { echo "$1 $2" >> "$OUT/results.txt"; }
alive() { kill -0 $BPID 2>/dev/null; }
seen() { # prints the extension's observed request list as JSON
  local sw; sw=$(curl -s http://127.0.0.1:$PORT/json | python3 -c '
import json,sys
for t in json.load(sys.stdin):
    if t["type"]=="service_worker" and t["url"].endswith("/bg.js"): print(t["id"]); break')
  [ -n "$sw" ] || { echo '{"error":"no extension worker"}'; return; }
  node "$S/cdp.mjs" $PORT "$sw" Runtime.evaluate \
    '{"expression":"JSON.stringify(globalThis.seen)","returnByValue":true}'
}
for i in $(seq 1 30); do curl -s http://127.0.0.1:$PORT/json >/dev/null 2>&1 && break; sleep 1; done
sleep 6
alive && record browserAliveAfterLoad PASS || record browserAliveAfterLoad FAIL
# The startup page may load before the extension worker listens; navigate
# again so new document factories are created with the proxy in place.
node "$S/cdp.mjs" $PORT index.html Page.navigate \
  "{\"url\":\"http://127.0.0.1:$SP/index.html?again\"}" > /dev/null 2>&1
sleep 5
alive && record browserAliveAfterNavigate PASS || record browserAliveAfterNavigate FAIL
seen > "$OUT/seen-first.json"
check_types() { python3 - "$1" <<'PY'
import json,sys
r=json.load(open(sys.argv[1])); v=r.get("result",{}).get("value")
seen=json.loads(v) if isinstance(v,str) else []
need={"main_frame","stylesheet","script","image"}
got={s.split(" ")[0] for s in seen if "127.0.0.1" in s}
print("PASS" if need<=got else "FAIL:"+",".join(sorted(got)))
PY
}
record extensionSawSubresources "$(check_types "$OUT/seen-first.json")"
node "$S/cdp.mjs" $PORT index.html Page.reload '{}' > /dev/null 2>&1
sleep 5
alive && record browserAliveAfterReload PASS || record browserAliveAfterReload FAIL
grep -q "web_request_info.cc" "$OUT/browser.log" && record noWebRequestDcheck FAIL || record noWebRequestDcheck PASS
python3 - "$OUT" <<'PY'
import json,sys,pathlib
o=pathlib.Path(sys.argv[1]); r={}
for line in (o/"results.txt").read_text().split("\n"):
    if line.strip(): k,v=line.split(" ",1); r[k]=v
(o/"results.json").write_text(json.dumps({"results":r,"pass":all(v=="PASS" for v in r.values())},indent=1)+"\n")
print(json.dumps(r))
PY
python3 -c 'import json,sys; sys.exit(0 if json.load(open(sys.argv[1]))["pass"] else 1)' "$OUT/results.json"
