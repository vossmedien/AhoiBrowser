#!/bin/bash
# usage: privacy-exceptions-journey.sh <App.app> <outdir>
# CDP journey for the PRIV origin exceptions on an installed candidate; no
# input events. One fresh profile in "Mehr Schutz" (strict) with an origin
# exception for the loopback fixture's 127.0.0.1 site set to "Maximale
# Website-Kompatibilität"; localhost on the same port is a different site
# without an exception and serves as the strict control. Checks:
#   PRIV-08 the exception is active (no Sec-GPC header, no JS signal) while
#           the control site stays strict, after a reload and after a real
#           browser restart on the same profile,
#   PRIV-09 removing the exception (from the stored preference, browser
#           stopped) restores strict behavior on that site,
#   PRIV-10 the strict control site's protection holds with no uBlock Origin
#           identity (Classic, former Web Store Classic, Lite) in the
#           profile, i.e. browser protection alone; a profile where uBO was
#           installed and then disabled is not covered.
# The exception's visible indication in the privacy bubble is not covered
# here. Results: <outdir>/results.json.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
PORT=9389; SP=${AHOI_E2E_SITE_PORT:-8808}
for p in $PORT $SP; do
  if lsof -nP -iTCP:$p -sTCP:LISTEN >/dev/null 2>&1; then echo "port $p busy" >&2; exit 6; fi
done
mkdir -p "$OUT"; : > "$OUT/results.txt"; : > "$OUT/run.txt"
A=http://127.0.0.1:$SP; B=http://localhost:$SP
P=$(mktemp -d /private/tmp/ahoi-privacy-exception-profile.XXXXXX)
trap 'rm -rf "$P"' EXIT
record() { echo "$1 $2" >> "$OUT/results.txt"; }
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
eval_in() { # <url substring> <expression>
  CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True,"awaitPromise":True}))' "$2")" \
    | python3 -c 'import json,sys;v=json.load(sys.stdin).get("result",{}).get("value","");print(v if isinstance(v,str) else json.dumps(v))'; }
open_tab() { curl -s -X PUT "http://127.0.0.1:$PORT/json/new?$(python3 -c 'import sys,urllib.parse;print(urllib.parse.quote(sys.argv[1],safe=""))' "$1")" > /dev/null; sleep "${2:-3}"; }
# Every Sec-GPC value logged for <host prefix> and <path>, comma-separated.
gpc_values() { python3 - "$@" <<'PY'
import json, sys
log, host, path = sys.argv[1:4]
values = [str(e.get("gpc")) for e in map(json.loads, open(log))
          if e["path"] == path and e["host"].startswith(host)]
print(",".join(values) or "<not requested>")
PY
}

mkdir -p "$P/Default"
printf '{"ahoi":{"privacy":{"global_mode":"strict","origin_modes":{"%s":"chromium-compatible"}}}}' "$A" \
  > "$P/Default/Preferences"

# One browser launch on the shared profile: loads the excepted site twice
# (the second load is the reload) and the control site once.
launch() { # <label>
  local label=$1 LOG="$OUT/fixture-$1.jsonl" PID FIX
  : > "$LOG"
  python3 "$S/privacy_fixture.py" --port $SP --log "$LOG" > "$OUT/fixture-$label.stderr" 2>&1 &
  FIX=$!
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT about:blank > "$OUT/browser-$label.log" 2>&1 &
  PID=$!; echo "$label: pid=$PID profile=$P" >> "$OUT/run.txt"
  for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  sleep 3
  open_tab "$A/landing" 3
  open_tab "$A/landing" 3
  local js_a js_b
  js_a=$(eval_in "127.0.0.1:$SP/landing" "String(navigator.globalPrivacyControl)")
  open_tab "$B/landing" 3
  js_b=$(eval_in "localhost:$SP/landing" "String(navigator.globalPrivacyControl)")
  eval "${label}_A=\$(gpc_values \"\$LOG\" 127.0.0.1 /landing) ${label}_B=\$(gpc_values \"\$LOG\" localhost /landing)"
  eval "${label}_JSA=\$js_a ${label}_JSB=\$js_b"
  echo "$label excepted_gpc=$(gpc_values "$LOG" 127.0.0.1 /landing) excepted_js=$js_a control_gpc=$(gpc_values "$LOG" localhost /landing) control_js=$js_b" >> "$OUT/run.txt"
  kill $PID 2>/dev/null
  for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || break; sleep 1; done
  kill -9 $PID 2>/dev/null; kill $FIX 2>/dev/null; sleep 1
}

stored_exception() { python3 - "$P/Default/Preferences" "$A" <<'PY'
import json, sys
prefs = json.load(open(sys.argv[1]))
print(prefs.get("ahoi", {}).get("privacy", {}).get("origin_modes", {}).get(sys.argv[2], "<absent>"))
PY
}

exception_active() { # <label>: excepted site without GPC twice, control strict
  local a b ja jb
  eval "a=\$${1}_A b=\$${1}_B ja=\$${1}_JSA jb=\$${1}_JSB"
  [ "$a" = "None,None" ] && [ "$ja" = undefined ] && [ "$b" = 1 ] && [ "$jb" = true ]
}

launch first
exception_active first && record PRIV-08_exception_active_after_reload PASS \
  || record PRIV-08_exception_active_after_reload "FAIL:gpc=$first_A,js=$first_JSA,control=$first_B/$first_JSB"
echo "stored after first run: $(stored_exception)" >> "$OUT/run.txt"
ubo=$(cat "$P/Default/Preferences" "$P/Default/Secure Preferences" 2>/dev/null \
  | grep -o -E "fkgkibajhfbepljeaefdnfnegdcjomkh|cjpalhdlnbpafiamejdnhcphjbkeiagm|ddkjiahejlhfcafbddmgiahcphecmpfh" | sort -u | tr '\n' ' ')
[ -z "$ubo" ] && [ "$first_B" = 1 ] && [ "$first_JSB" = true ] \
  && record PRIV-10_protection_without_ubo PASS \
  || record PRIV-10_protection_without_ubo "FAIL:ubo=${ubo:-none},control=$first_B/$first_JSB"

launch restarted
exception_active restarted && [ "$(stored_exception)" = chromium-compatible ] \
  && record PRIV-08_exception_survives_restart PASS \
  || record PRIV-08_exception_survives_restart "FAIL:gpc=$restarted_A,js=$restarted_JSA,control=$restarted_B/$restarted_JSB,stored=$(stored_exception)"

python3 - "$P/Default/Preferences" "$A" <<'PY'
import json, sys
path, origin = sys.argv[1:3]
prefs = json.load(open(path))
prefs.get("ahoi", {}).get("privacy", {}).get("origin_modes", {}).pop(origin, None)
json.dump(prefs, open(path, "w"))
PY
launch removed
[ "$removed_A" = "1,1" ] && [ "$removed_JSA" = true ] && [ "$(stored_exception)" = "<absent>" ] \
  && record PRIV-09_exception_removed_restores_strict PASS \
  || record PRIV-09_exception_removed_restores_strict "FAIL:gpc=$removed_A,js=$removed_JSA,stored=$(stored_exception)"

python3 - "$OUT/results.txt" > "$OUT/results.json" <<'PY'
import json, sys
rows = dict(line.rstrip("\n").split(" ", 1) for line in open(sys.argv[1]) if line.strip())
print(json.dumps({"results": rows,
                  "pass": all(v == "PASS" for v in rows.values())}, indent=1))
PY
cat "$OUT/results.json"
