#!/bin/bash
# usage: safe-browsing-journey.sh <App.app> <outdir>
# CDP journey for PRIV-14 on an installed candidate; no input events. A fresh
# profile in the default mode opens Google's official Safe Browsing test
# pages (testsafebrowsing.appspot.com, built for exactly this check) and
# must show the security interstitial for the malware and phishing samples
# while a harmless control page loads normally. A fresh profile first has
# to fetch its Safe Browsing data, so each sample is retried for up to
# AHOI_SB_WAIT_SECONDS (default 300). Needs internet access to Google.
# With AHOI_SB_KEYCHAIN_KEY=1 the browser gets GOOGLE_API_KEY from the login
# Keychain (service ahoi-google-api-key, account safe-browsing) in its own
# environment only; the key is never printed, logged or written to disk.
# Results: <outdir>/results.json.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
PORT=9389; WAIT=${AHOI_SB_WAIT_SECONDS:-300}
T=https://testsafebrowsing.appspot.com
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; : > "$OUT/results.txt"; : > "$OUT/run.txt"
P=$(mktemp -d /private/tmp/ahoi-safe-browsing-profile.XXXXXX)
record() { echo "$1 $2" >> "$OUT/results.txt"; }
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
eval_in() { # <url substring> <expression>
  CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True,"awaitPromise":True}))' "$2")" \
    | python3 -c 'import json,sys;v=json.load(sys.stdin).get("result",{}).get("value","");print(v if isinstance(v,str) else json.dumps(v))'; }
open_tab() { curl -s -X PUT "http://127.0.0.1:$PORT/json/new?$(python3 -c 'import sys,urllib.parse;print(urllib.parse.quote(sys.argv[1],safe=""))' "$1")" > /dev/null; sleep "${2:-3}"; }
close_tabs() { # <url substring>
  curl -s http://127.0.0.1:$PORT/json/list | python3 -c '
import json,sys
for t in json.load(sys.stdin):
    if sys.argv[1] in t.get("url",""): print(t["id"])' "$1" \
    | while read -r id; do curl -s "http://127.0.0.1:$PORT/json/close/$id" > /dev/null; done; }
# The security interstitial's own markup: its main message and the details
# button exist on every Safe Browsing blocking page.
PROBE="(()=>{const m=document.getElementById('main-message');return m&&document.getElementById('details-button')?'interstitial:'+m.innerText.split('\\n')[0]:'page:'+document.title})()"

KEYED=no
if [ "${AHOI_SB_KEYCHAIN_KEY:-0}" = 1 ]; then
  if security find-generic-password -s ahoi-google-api-key -a safe-browsing >/dev/null 2>&1; then
    KEYED=yes
  else
    echo "no Keychain entry ahoi-google-api-key/safe-browsing" >&2; exit 7
  fi
fi
if [ "$KEYED" = yes ]; then
  GOOGLE_API_KEY=$(security find-generic-password -s ahoi-google-api-key -a safe-browsing -w) \
    "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT about:blank > "$OUT/browser.log" 2>&1 &
else
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT about:blank > "$OUT/browser.log" 2>&1 &
fi
PID=$!; echo "pid=$PID profile=$P api_key=$KEYED" >> "$OUT/run.txt"
trap 'kill $PID 2>/dev/null; sleep 2; kill -9 $PID 2>/dev/null; rm -rf "$P"' EXIT
for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
sleep 3

sample() { # <id> <path>: wait until the interstitial appears
  local id=$1 path=$2 end=$(( $(date +%s) + WAIT )) seen=""
  while :; do
    open_tab "$T$path" 6
    seen=$(eval_in "testsafebrowsing.appspot.com$path" "$PROBE")
    close_tabs "testsafebrowsing.appspot.com$path"
    echo "$id $(date -u +%H:%M:%S) $seen" >> "$OUT/run.txt"
    case "$seen" in interstitial:*) record "$id" PASS; return ;; esac
    [ "$(date +%s)" -ge "$end" ] && { record "$id" "FAIL:${seen:-no-answer}"; return; }
    sleep 20
  done
}

sample PRIV-14_malware_interstitial /s/malware.html
sample PRIV-14_phishing_interstitial /s/phishing.html
open_tab "$T/" 6
control=$(eval_in "testsafebrowsing.appspot.com/" "$PROBE")
echo "control $control" >> "$OUT/run.txt"
case "$control" in page:*) record PRIV-14_control_page_loads PASS ;;
  *) record PRIV-14_control_page_loads "FAIL:$control" ;; esac

python3 - "$OUT/results.txt" > "$OUT/results.json" <<'PY'
import json, sys
rows = dict(line.rstrip("\n").split(" ", 1) for line in open(sys.argv[1]) if line.strip())
print(json.dumps({"results": rows,
                  "pass": all(v == "PASS" for v in rows.values())}, indent=1))
PY
cat "$OUT/results.json"
