#!/bin/bash
# usage: devtoolkit-journey.sh <App.app> <outdir>
# CDP journey for the developer toolkit's persistent per-site profile on an
# installed candidate; no input events. A fresh profile is seeded with the
# toolkit enabled and one developer profile for the fixture's 127.0.0.1
# site; localhost on the same port is a different site without a profile
# and serves as the unmodified control. Checks:
#   DEV-01 the saved CSS applies, after a reload and after a real restart,
#   DEV-05 the saved JavaScript runs in an isolated world: its DOM change is
#          visible, its global is not visible to the page,
#   DEV-09 with cache off the cacheable stylesheet reaches the server on
#          every load, while the control site uses the cache,
#   DEV-14 the request header rule reaches the server, not on the control,
#   DEV-15 the response header rule is visible to the page.
# Results: <outdir>/results.json.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
PORT=9389; SP=${AHOI_E2E_SITE_PORT:-8808}
for p in $PORT $SP; do
  if lsof -nP -iTCP:$p -sTCP:LISTEN >/dev/null 2>&1; then echo "port $p busy" >&2; exit 6; fi
done
mkdir -p "$OUT"; : > "$OUT/results.txt"; : > "$OUT/run.txt"
A=http://127.0.0.1:$SP; B=http://localhost:$SP
P=$(mktemp -d /private/tmp/ahoi-devtoolkit-profile.XXXXXX)
trap 'rm -rf "$P"' EXIT
record() { echo "$1 $2" >> "$OUT/results.txt"; }
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
eval_in() { # <url substring> <expression>
  CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True,"awaitPromise":True}))' "$2")" \
    | python3 -c 'import json,sys;v=json.load(sys.stdin).get("result",{}).get("value","");print(v if isinstance(v,str) else json.dumps(v))'; }
open_tab() { curl -s -X PUT "http://127.0.0.1:$PORT/json/new?$(python3 -c 'import sys,urllib.parse;print(urllib.parse.quote(sys.argv[1],safe=""))' "$1")" > /dev/null; sleep "${2:-3}"; }

mkdir -p "$P/Default"
python3 - "$P/Default/Preferences" "$A" <<'PY'
import json, sys
path, origin = sys.argv[1:3]
def asset(asset_id, kind, source, world="isolated"):
    return {"id": asset_id, "name": asset_id, "kind": kind, "language": "css",
            "enabled": True, "source": source, "compiled_css": "",
            "compiled_style_version": 0,
            "scope": {"kind": "origin", "value": origin},
            "domain_scope_warning_accepted": False, "lifetime": "restart",
            "sync_enabled": False, "world": world,
            "main_world_warning_accepted": False}
profile = {
    "name": "Journey",
    "assets": [
        asset("journey-css", "style", "body{outline:7px solid rgb(1, 2, 3)}"),
        asset("journey-js", "javascript",
              "document.documentElement.setAttribute('data-ahoi-dev','isolated');"
              "window.ahoiDevGlobal = 1;"),
    ],
    "user_agent": {"enabled": False, "value": ""},
    "headers": {"enabled": True, "sync_enabled": False,
                "rules": [{"name": "X-Ahoi-Dev", "action": "set", "value": "journey"}]},
    "response_headers": {"enabled": True, "sync_enabled": False,
                         "advanced_mode_acknowledged": False,
                         "rules": [{"name": "X-Ahoi-Resp", "action": "set", "value": "yes"}]},
    "cache_disabled": True,
}
prefs = {"ahoi": {"developer_toolkit": {
    "enabled": True,
    "profiles": {"version": 2, "origins": {origin: profile}}}}}
json.dump(prefs, open(path, "w"))
PY

launch() { # <label>
  local label=$1 LOG="$OUT/fixture-$1.jsonl" PID FIX
  : > "$LOG"
  python3 "$S/devtoolkit_fixture.py" --port $SP --log "$LOG" > "$OUT/fixture-$label.stderr" 2>&1 &
  FIX=$!
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT about:blank > "$OUT/browser-$label.log" 2>&1 &
  PID=$!; echo "$label: pid=$PID profile=$P" >> "$OUT/run.txt"
  for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  sleep 3
  open_tab "$A/page" 4
  eval "${label}_OUTLINE=\$(eval_in \"127.0.0.1:$SP/page\" \"getComputedStyle(document.body).outlineColor\")"
  eval "${label}_ATTR=\$(eval_in \"127.0.0.1:$SP/page\" \"String(document.documentElement.getAttribute('data-ahoi-dev'))\")"
  eval "${label}_GLOBAL=\$(eval_in \"127.0.0.1:$SP/page\" \"typeof window.ahoiDevGlobal\")"
  eval "${label}_RESP=\$(eval_in \"127.0.0.1:$SP/page\" \"fetch('/echo').then(r=>String(r.headers.get('x-ahoi-resp')))\")"
  open_tab "$A/page" 4
  eval "${label}_OUTLINE2=\$(eval_in \"127.0.0.1:$SP/page\" \"getComputedStyle(document.body).outlineColor\")"
  open_tab "$B/page" 4; open_tab "$B/page" 4
  eval "${label}_LOG=\$LOG"
  kill $PID 2>/dev/null
  for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || break; sleep 1; done
  kill -9 $PID 2>/dev/null; kill $FIX 2>/dev/null; sleep 1
}

# The fixture does not log the Host; split by the order of loads instead:
# first two /page loads are 127.0.0.1, the last two localhost.
split() { # <log>: prints "<a_css_hits> <b_css_hits> <a_header> <b_header>"
  python3 - "$1" <<'PY'
import json, sys
entries = list(map(json.loads, open(sys.argv[1])))
pages = [i for i, e in enumerate(entries) if e["path"] == "/page"]
if len(pages) < 4:
    print("0 0 none none"); sys.exit()
a = entries[pages[0]:pages[2]]; b = entries[pages[2]:]
css = lambda part: sum(1 for e in part if e["path"] == "/style.css")
hdr = lambda part: next((e["headers"].get("X-Ahoi-Dev", "none") for e in part if e["path"] == "/page"), "none")
print(css(a), css(b), hdr(a), hdr(b))
PY
}

launch first
read -r a_css b_css a_hdr b_hdr <<<"$(split "$first_LOG")"
echo "first outline=$first_OUTLINE/$first_OUTLINE2 attr=$first_ATTR global=$first_GLOBAL resp=$first_RESP css_hits=$a_css/$b_css header=$a_hdr/$b_hdr" >> "$OUT/run.txt"
[ "$first_OUTLINE" = "rgb(1, 2, 3)" ] && [ "$first_OUTLINE2" = "rgb(1, 2, 3)" ] \
  && record DEV-01_css_applies_after_reload PASS \
  || record DEV-01_css_applies_after_reload "FAIL:$first_OUTLINE/$first_OUTLINE2"
[ "$first_ATTR" = isolated ] && [ "$first_GLOBAL" = undefined ] \
  && record DEV-05_javascript_isolated_world PASS \
  || record DEV-05_javascript_isolated_world "FAIL:attr=$first_ATTR,global=$first_GLOBAL"
[ "$a_css" = 2 ] && [ "$b_css" = 1 ] && record DEV-09_cache_off_reaches_server PASS \
  || record DEV-09_cache_off_reaches_server "FAIL:site=$a_css,control=$b_css"
[ "$a_hdr" = journey ] && [ "$b_hdr" = none ] && record DEV-14_request_header_rule PASS \
  || record DEV-14_request_header_rule "FAIL:site=$a_hdr,control=$b_hdr"
[ "$first_RESP" = yes ] && record DEV-15_response_header_rule PASS \
  || record DEV-15_response_header_rule "FAIL:$first_RESP"

launch restarted
echo "restarted outline=$restarted_OUTLINE" >> "$OUT/run.txt"
[ "$restarted_OUTLINE" = "rgb(1, 2, 3)" ] && record DEV-01_css_applies_after_restart PASS \
  || record DEV-01_css_applies_after_restart "FAIL:$restarted_OUTLINE"

python3 - "$OUT/results.txt" > "$OUT/results.json" <<'PY'
import json, sys
rows = dict(line.rstrip("\n").split(" ", 1) for line in open(sys.argv[1]) if line.strip())
print(json.dumps({"results": rows,
                  "pass": all(v == "PASS" for v in rows.values())}, indent=1))
PY
cat "$OUT/results.json"
