#!/bin/bash
# usage: privacy-modes-journey.sh <App.app> <outdir>
# CDP journey for the PRIV suite on an installed candidate; no input events.
# Two fresh profiles against a loopback fixture (127.0.0.1 and localhost are
# different sites): the first keeps the default mode, the second is seeded
# with ahoi.privacy.global_mode=strict ("Mehr Schutz"). Checks:
#   PRIV-17 default is Chromium-compatible (no GPC, no referrer or parameter
#           rewriting, third-party cookie as in Chromium),
#   PRIV-01 first-party login works in strict mode,
#   PRIV-02 strict mode blocks the unpartitioned third-party cookie (only
#           judged when the default run could set it),
#   PRIV-04 Sec-GPC: 1 only in strict mode,
#   PRIV-05 strict mode reduces a cross-site referrer to the origin and strips
#           documented tracking parameters while keeping others,
#   PRIV-06 Topics and Protected Audience are unavailable,
#   PRIV-15 Safe Browsing standard is on and Enhanced Protection is off,
#   PRIV-18 no missing-API-key infobar.
# Results: <outdir>/results.json.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9389; SP=${AHOI_E2E_SITE_PORT:-8808}; HP=$((SP + 1))
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
for p in $PORT $SP $HP; do
  if lsof -nP -iTCP:$p -sTCP:LISTEN >/dev/null 2>&1; then echo "port $p busy" >&2; exit 6; fi
done
mkdir -p "$OUT"; : > "$OUT/results.txt"; : > "$OUT/run.txt"
A=http://127.0.0.1:$SP
# PRIV-02/03 need Secure cookies, so a cross-site HTTPS pair runs on $HP with
# a throwaway self-signed certificate for 127.0.0.1 and localhost. Only this
# key is trusted, and only in the journey's own test profiles.
TLS=$(mktemp -d /private/tmp/ahoi-privacy-tls.XXXXXX)
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -days 1 \
  -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
  -keyout "$TLS/key.pem" -out "$TLS/cert.pem" > "$OUT/tls.log" 2>&1 || exit 5
SPKI=$(openssl x509 -in "$TLS/cert.pem" -pubkey -noout | openssl pkey -pubin -outform der \
  | openssl dgst -sha256 -binary | base64)
trap 'rm -rf "$TLS"' EXIT
record() { echo "$1 $2" >> "$OUT/results.txt"; }
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
eval_in() { # <url substring> <expression>
  CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True,"awaitPromise":True}))' "$2")" \
    | python3 -c 'import json,sys;v=json.load(sys.stdin).get("result",{}).get("value","");print(v if isinstance(v,str) else json.dumps(v))'; }
# The target is URL-encoded: a raw '&' would split the /json/new query.
open_tab() { curl -s -X PUT "http://127.0.0.1:$PORT/json/new?$(python3 -c 'import sys,urllib.parse;print(urllib.parse.quote(sys.argv[1],safe=""))' "$1")" > /dev/null; sleep "${2:-3}"; }
# First fixture log entry for a path: <log> <path> <field>
logged() { python3 - "$@" <<'PY'
import json, sys
log, path, field = sys.argv[1:4]
for line in open(log):
    entry = json.loads(line)
    if entry["path"] == path:
        value = entry.get(field)
        print("" if value is None else value)
        break
else:
    print("<not requested>")
PY
}

run_mode() { # <label> <seeded mode or "">
  local label=$1 mode=$2 P LOG PID FIX
  P=$(mktemp -d /private/tmp/ahoi-privacy-profile.XXXXXX); LOG="$OUT/fixture-$label.jsonl"; : > "$LOG"
  if [ -n "$mode" ]; then
    mkdir -p "$P/Default"
    printf '{"ahoi":{"privacy":{"global_mode":"%s"}}}' "$mode" > "$P/Default/Preferences"
  fi
  python3 "$S/privacy_fixture.py" --port $SP --log "$LOG" --https-port $HP \
    --cert "$TLS/cert.pem" --key "$TLS/key.pem" > "$OUT/fixture-$label.stderr" 2>&1 &
  FIX=$!
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT --ignore-certificate-errors-spki-list="$SPKI" \
    about:blank > "$OUT/browser-$label.log" 2>&1 &
  PID=$!; echo "$label: pid=$PID profile=$P seeded=${mode:-none}" >> "$OUT/run.txt"
  for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  sleep 3
  open_tab "$A/top" 5
  local tp; tp=$(eval_in "localhost:$SP/frame" "String(window.tp)")
  open_tab "https://127.0.0.1:$HP/top3p" 5
  local tp3; tp3=$(eval_in "localhost:$HP/frame3p" "String(window.tp)")
  open_tab "$A/landing?utm_source=ahoi&keep=1" 3
  open_tab "$A/login" 2; open_tab "$A/whoami" 2
  local who; who=$(eval_in "/whoami" "document.title")
  open_tab "$A/ads" 2
  local topics fledge gpcjs
  gpcjs=$(eval_in "/ads" "String(navigator.globalPrivacyControl)")
  topics=$(eval_in "/ads" "(async()=>{if(typeof document.browsingTopics!=='function')return 'absent';try{const t=await document.browsingTopics();return 'topics:'+t.length}catch(e){return 'rejected:'+e.name}})()")
  fledge=$(eval_in "/ads" "(async()=>{if(typeof navigator.joinAdInterestGroup!=='function')return 'absent';try{await navigator.joinAdInterestGroup({owner:location.origin,name:'ahoi',lifetimeMs:60000},60);return 'joined'}catch(e){return 'rejected:'+e.name}})()")
  open_tab "chrome://prefs-internals" 4
  local prefs; prefs=$(eval_in "prefs-internals" "(()=>{let d;try{d=JSON.parse(document.body.innerText)}catch(e){return JSON.stringify({error:'unparsable'})}const g=k=>{let v=d;for(const p of k.split('.')){if(v==null)return '<unavailable>';v=v[p]}if(v===undefined)return '<unavailable>';if(v!==null&&typeof v==='object'){if('value' in v)v=v.value;else return JSON.stringify(v).slice(0,200)}return String(v)};return JSON.stringify({mode:g('ahoi.privacy.global_mode'),sb:g('safebrowsing.enabled'),enhanced:g('safebrowsing.enhanced'),topics:g('privacy_sandbox.m1.topics_enabled'),fledge:g('privacy_sandbox.m1.fledge_enabled'),measurement:g('privacy_sandbox.m1.ad_measurement_enabled')})})()")
  "$AX" dump $PID 30 > "$OUT/ax-$label.txt" 2>/dev/null
  local apikey=absent
  grep -q -i -E "API-Schlüssel|API keys" "$OUT/ax-$label.txt" && apikey=shown
  {
    echo "$label third_party_cookie_in_frame=$tp"
    echo "$label https_frame_cookies=$tp3"
    echo "$label gpc_on_top=$(logged "$LOG" /top gpc)"
    echo "$label pixel_referer=$(logged "$LOG" /pixel referer)"
    echo "$label pixel_gpc=$(logged "$LOG" /pixel gpc)"
    echo "$label landing_query=$(logged "$LOG" /landing query)"
    echo "$label whoami=$who"
    echo "$label topics=$topics fledge=$fledge gpc_js=$gpcjs"
    echo "$label prefs=$prefs"
    echo "$label api_key_infobar=$apikey"
  } >> "$OUT/run.txt"
  eval "${label}_TP3=\$tp3 ${label}_PIXGPC=\$(logged \"\$LOG\" /pixel gpc)"
  eval "${label}_TP=\$tp ${label}_GPC=\$(logged \"\$LOG\" /top gpc) ${label}_REF=\$(logged \"\$LOG\" /pixel referer)"
  eval "${label}_QUERY=\$(logged \"\$LOG\" /landing query) ${label}_WHO=\$who ${label}_TOPICS=\$topics ${label}_FLEDGE=\$fledge"
  eval "${label}_PREFS=\$prefs ${label}_APIKEY=\$apikey ${label}_GPCJS=\$gpcjs"
  kill $PID 2>/dev/null; sleep 3; kill -9 $PID 2>/dev/null
  kill $FIX 2>/dev/null; sleep 1
}

run_mode default ""
run_mode strict strict

pref() { python3 -c 'import json,sys
try: print(json.loads(sys.argv[1]).get(sys.argv[2],"<unavailable>"))
except Exception: print("<unavailable>")' "$1" "$2"; }

# PRIV-17: the default profile behaves like Chromium.
[ "$(pref "$default_PREFS" mode)" = chromium-compatible ] && record PRIV-17_default_mode PASS \
  || record PRIV-17_default_mode "FAIL:$(pref "$default_PREFS" mode)"
[ -z "$default_GPC" ] && [ "$default_REF" = "$A/top" ] && [ "$default_QUERY" = "utm_source=ahoi&keep=1" ] \
  && record PRIV-17_default_unchanged PASS \
  || record PRIV-17_default_unchanged "FAIL:gpc=$default_GPC,ref=$default_REF,query=$default_QUERY"
# PRIV-01: first-party login in strict mode.
[ "$strict_WHO" = "whoami session=fp" ] && record PRIV-01_first_party_login PASS \
  || record PRIV-01_first_party_login "FAIL:$strict_WHO"
# PRIV-02: only meaningful when the default run could set the cookie.
case "$default_TP3" in
  *tp=1*) case "$strict_TP3" in *tp=1*) record PRIV-02_third_party_cookie_blocked "FAIL:$strict_TP3" ;;
          *) record PRIV-02_third_party_cookie_blocked PASS ;; esac ;;
  *) record PRIV-02_third_party_cookie_blocked "INCONCLUSIVE:default-run-could-not-set:$default_TP3" ;;
esac
# PRIV-03 (CHIPS part): the partitioned cookie still works in strict mode.
case "$strict_TP3" in *chip=1*) record PRIV-03_chips_in_strict PASS ;;
  *) record PRIV-03_chips_in_strict "FAIL:$strict_TP3" ;; esac
# PRIV-04: the JS signal follows the mode (patches 0067/0068).
[ "$strict_GPCJS" = true ] && [ "$default_GPCJS" = undefined ] && record PRIV-04_gpc_js_signal PASS \
  || record PRIV-04_gpc_js_signal "FAIL:strict=$strict_GPCJS,default=$default_GPCJS"
# PRIV-04: subresources of a strict page carry Sec-GPC too (patch 0066).
[ "$strict_PIXGPC" = 1 ] && [ -z "$default_PIXGPC" ] && record PRIV-04_gpc_subresource PASS \
  || record PRIV-04_gpc_subresource "FAIL:strict=$strict_PIXGPC,default=$default_PIXGPC"
[ "$strict_GPC" = 1 ] && [ -z "$default_GPC" ] && record PRIV-04_gpc PASS \
  || record PRIV-04_gpc "FAIL:strict=$strict_GPC,default=$default_GPC"
# PRIV-05
[ "$strict_REF" = "$A/" ] && record PRIV-05_referrer_origin_only PASS || record PRIV-05_referrer_origin_only "FAIL:$strict_REF"
[ "$strict_QUERY" = "keep=1" ] && record PRIV-05_tracking_parameter_stripped PASS \
  || record PRIV-05_tracking_parameter_stripped "FAIL:$strict_QUERY"
# PRIV-06: Chromium resolves joinAdInterestGroup even when Protected Audience
# is off (no fingerprinting signal), so the prefs decide; Topics must reject.
ok=PASS
for v in "$default_TOPICS" "$strict_TOPICS"; do case "$v" in absent|rejected:*|topics:0) ;; *) ok="FAIL:topics=$v" ;; esac; done
for k in topics fledge measurement; do
  for pr in "$default_PREFS" "$strict_PREFS"; do [ "$(pref "$pr" $k)" = false ] || ok="FAIL:$k=$(pref "$pr" $k)"; done
done
record PRIV-06_ad_apis_disabled "$ok"
# PRIV-15
[ "$(pref "$default_PREFS" sb)" = true ] && [ "$(pref "$default_PREFS" enhanced)" = false ] \
  && record PRIV-15_standard_not_enhanced PASS \
  || record PRIV-15_standard_not_enhanced "FAIL:sb=$(pref "$default_PREFS" sb),enhanced=$(pref "$default_PREFS" enhanced)"
# PRIV-18
[ "$default_APIKEY" = absent ] && [ "$strict_APIKEY" = absent ] && record PRIV-18_no_api_key_infobar PASS \
  || record PRIV-18_no_api_key_infobar FAIL

python3 - "$OUT/results.txt" > "$OUT/results.json" <<'PY'
import json, sys
rows = dict(line.rstrip("\n").split(" ", 1) for line in open(sys.argv[1]) if line.strip())
print(json.dumps({"results": rows,
                  "pass": all(v == "PASS" for v in rows.values())}, indent=1))
PY
cat "$OUT/results.json"
