#!/bin/bash
# usage: sandbox-isolation-readback.sh <App.app> <outdir>
# Read-only readback for DoD 2 (sandbox and site isolation) on an installed
# candidate: a fresh profile loads two cross-site loopback pages; every
# renderer, GPU and utility child must be inside the macOS sandbox
# (sandbox_check), no process may carry --no-sandbox or a site-isolation
# opt-out, the two sites must render in different processes, and
# chrome://process-internals must report site-per-process. No input events;
# CDP and process inspection only. Results: <outdir>/results.json.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
PORT=9388; SITE_PORT=${AHOI_E2E_SITE_PORT:-8801}
CHECK=/private/tmp/ahoi-sandbox-check
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
if [ ! -x "$CHECK" ]; then
  printf '%s\n' '#include <stdio.h>' '#include <stdlib.h>' '#include <sys/types.h>' \
    'int sandbox_check(pid_t pid, const char *operation, int type, ...);' \
    'int main(int c, char **v) { for (int i = 1; i < c; i++) printf("%s %d\n", v[i], sandbox_check(atoi(v[i]), NULL, 0)); return 0; }' \
    > "$CHECK.c"
  xcrun clang -O2 -o "$CHECK" "$CHECK.c" || exit 5
fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-sandbox-profile.XXXXXX); : > "$OUT/results.txt"
mkdir -p "$P-site"; printf '<title>Ahoi site A</title><h1>a</h1>' > "$P-site/a.html"
printf '<title>Ahoi site B</title><h1>b</h1>' > "$P-site/b.html"
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory "$P-site" > "$OUT/site.log" 2>&1 &
SITE_PID=$!
"$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run --no-default-browser-check \
  --remote-debugging-port=$PORT about:blank > "$OUT/browser.log" 2>&1 &
PID=$!; trap 'kill $SITE_PID 2>/dev/null; kill $PID 2>/dev/null' EXIT
echo "pid=$PID profile=$P" > "$OUT/run.txt"
for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
record() { echo "$1 $2" >> "$OUT/results.txt"; }
# 127.0.0.1 and localhost are different sites.
curl -s -X PUT "http://127.0.0.1:$PORT/json/new?http://127.0.0.1:$SITE_PORT/a.html" > /dev/null
curl -s -X PUT "http://127.0.0.1:$PORT/json/new?http://localhost:$SITE_PORT/b.html" > /dev/null
curl -s -X PUT "http://127.0.0.1:$PORT/json/new?chrome://process-internals" > /dev/null
sleep 6

# Every child of the browser process, with its command line.
pgrep -P $PID > "$OUT/children.txt"
for c in $(cat "$OUT/children.txt"); do
  printf '%s\t%s\n' "$c" "$(ps -o command= -p $c)"
done > "$OUT/processes.tsv"
ps -o command= -p $PID > "$OUT/browser-command.txt"
if cat "$OUT/browser-command.txt" "$OUT/processes.tsv" | grep -q -E -- '--no-sandbox|--disable-site-isolation-trials|--disable-web-security'; then
  record no_sandbox_opt_out FAIL
else record no_sandbox_opt_out PASS; fi

# Renderer/GPU/utility children must be sandboxed; the network service may
# run unsandboxed on macOS only if upstream does, so it is listed separately.
SANDBOXED_TYPES='--type=renderer|--type=gpu-process|--type=utility'
awk -F'\t' -v re="$SANDBOXED_TYPES" '$2 ~ re && $2 !~ /network.mojom.NetworkService/ {print $1}' \
  "$OUT/processes.tsv" > "$OUT/must-sandbox.txt"
"$CHECK" $(cat "$OUT/must-sandbox.txt") > "$OUT/sandbox-check.txt" 2>&1
if [ -s "$OUT/must-sandbox.txt" ] && ! awk '$2 != 1 {bad=1} END {exit bad}' "$OUT/sandbox-check.txt"; then
  record children_sandboxed FAIL
elif [ -s "$OUT/must-sandbox.txt" ]; then record children_sandboxed PASS
else record children_sandboxed FAIL; fi
grep -c -- '--type=renderer' "$OUT/processes.tsv" > "$OUT/renderer-count.txt"

# The two sites must render in different processes; process-internals lists
# each frame with its process id.
# innerText skips the hidden tabs, and Site Isolation / Frame Trees fill in
# when their tab is selected: select each tab, then read all text.
node "$S/cdp.mjs" $PORT process-internals Runtime.evaluate \
  '{"expression":"(async()=>{const all=[];const walk=r=>{for(const e of r.querySelectorAll(\"*\")){all.push(e);if(e.shadowRoot)walk(e.shadowRoot)}};walk(document);for(const t of all.filter(e=>e.getAttribute&&e.getAttribute(\"role\")===\"tab\")){t.click();await new Promise(r=>setTimeout(r,1500))}const text=[];const grab=r=>{for(const e of r.querySelectorAll(\"*\")){if(e.shadowRoot)grab(e.shadowRoot)}text.push(r.textContent||\"\")};grab(document);return text.join(\"\\n\")})()","returnByValue":true,"awaitPromise":true}' > "$OUT/process-internals.json"
python3 "$S/process_internals_frames.py" "$OUT/process-internals.json" "$SITE_PORT" "$OUT/results.txt"
python3 - "$OUT/results.txt" > "$OUT/results.json" <<'PY'
import json, sys
rows = dict(line.split() for line in open(sys.argv[1]) if line.strip())
print(json.dumps({"results": rows, "pass": all(v == "PASS" for v in rows.values())}, indent=1))
PY
cat "$OUT/results.json"
