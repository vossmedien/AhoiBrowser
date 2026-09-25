#!/bin/bash
# usage: downloads-media-journey.sh <App.app> <outdir>
# CDP journey for DoD 7 (downloads and media, first slice) on an installed
# candidate. A seeded fresh profile downloads into its own directory (never
# the owner's ~/Downloads). Checks: an attachment download completes with the
# exact bytes, an <a download> link downloads, a PDF opens in the built-in
# viewer, and an audio element actually plays. No input events.
# Results: <outdir>/results.json.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd)
PORT=9383; SITE_PORT=${AHOI_E2E_SITE_PORT:-8804}
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-downloads-profile.XXXXXX); : > "$OUT/results.txt"
DL="$P-downloads"; SITE="$P-site"; mkdir -p "$DL" "$SITE" "$P/Default"
python3 - "$P/Default/Preferences" "$DL" <<'PY'
import json, sys
json.dump({"download": {"default_directory": sys.argv[2], "prompt_for_download": False,
                        "directory_upgrade": True},
           "savefile": {"default_directory": sys.argv[2]}}, open(sys.argv[1], "w"))
PY
python3 - "$SITE" <<'PY'
import os, sys, wave, struct, math
d = sys.argv[1]
open(os.path.join(d, "payload.bin"), "wb").write(bytes(range(256)) * 4096)  # 1 MiB
open(os.path.join(d, "note.txt"), "w").write("Ahoi download link\n")
# A minimal valid one-page PDF.
objs = ["<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>"]
out = b"%PDF-1.4\n"; offs = []
for i, o in enumerate(objs, 1):
    offs.append(len(out)); out += f"{i} 0 obj {o} endobj\n".encode()
x = len(out)
out += f"xref\n0 {len(objs)+1}\n0000000000 65535 f \n".encode()
out += b"".join(f"{o:010d} 00000 n \n".encode() for o in offs)
out += f"trailer << /Size {len(objs)+1} /Root 1 0 R >>\nstartxref\n{x}\n%%EOF\n".encode()
open(os.path.join(d, "doc.pdf"), "wb").write(out)
w = wave.open(os.path.join(d, "tone.wav"), "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(8000)
w.writeframes(b"".join(struct.pack("<h", int(3000 * math.sin(2 * math.pi * 440 * t / 8000))) for t in range(8000 * 3)))
w.close()
open(os.path.join(d, "media.html"), "w").write(
    '<title>Ahoi media</title><audio id="a" src="tone.wav" loop></audio>'
    '<a id="dl" href="note.txt" download="ahoi-note.txt">note</a>')
PY
cat > "$SITE/server.py" <<'PY'
import http.server, sys, functools
class H(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        if self.path.startswith("/payload.bin"):
            self.send_header("Content-Disposition", 'attachment; filename="ahoi-payload.bin"')
        super().end_headers()
    def log_message(self, *a): pass
http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])),
    functools.partial(H, directory=sys.argv[2])).serve_forever()
PY
python3 "$SITE/server.py" $SITE_PORT "$SITE" > "$OUT/site.log" 2>&1 &
SITE_PID=$!; BASE=http://127.0.0.1:$SITE_PORT
"$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run --no-default-browser-check \
  --remote-debugging-port=$PORT --autoplay-policy=no-user-gesture-required about:blank > "$OUT/browser.log" 2>&1 &
PID=$!; trap 'kill $SITE_PID 2>/dev/null; kill $PID 2>/dev/null' EXIT
echo "pid=$PID profile=$P downloads=$DL" > "$OUT/run.txt"
for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
sleep 3
record() { echo "$1 $2" >> "$OUT/results.txt"; }
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
eval_in() { # <url substring> <expression>
  CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True,"awaitPromise":True,"userGesture":True}))' "$2")" \
    | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'; }
wait_file() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do [ -f "$DL/$1" ] && ! ls "$DL" | grep -q crdownload && return 0; sleep 1; done; return 1; }

# 1 Attachment download completes with the exact bytes.
curl -s -X PUT "http://127.0.0.1:$PORT/json/new?$BASE/media.html" > /dev/null; sleep 3
eval_in media.html "location.href='$BASE/payload.bin'; 'ok'" > /dev/null
if wait_file ahoi-payload.bin 30 && cmp -s "$DL/ahoi-payload.bin" "$SITE/payload.bin"; then
  record attachment_download_complete PASS
else record attachment_download_complete FAIL; fi
# 2 <a download> link, from a freshly loaded page: Chromium's download
# request limiter allows one automatic download per page before it asks.
curl -s -X PUT "http://127.0.0.1:$PORT/json/new?$BASE/media.html?link" > /dev/null; sleep 3
eval_in "media.html?link" "document.getElementById('dl').click(); 'ok'" > /dev/null
wait_file ahoi-note.txt 20 && cmp -s "$DL/ahoi-note.txt" "$SITE/note.txt" \
  && record download_attribute PASS || record download_attribute FAIL
ls -la "$DL" > "$OUT/downloads-dir.txt"
# 3 Audio plays (currentTime advances, not paused).
PLAY=$(eval_in media.html "(async()=>{const a=document.getElementById('a');try{await a.play()}catch(e){return 'error:'+e.name}const t=a.currentTime;await new Promise(r=>setTimeout(r,1500));return (!a.paused&&a.currentTime>t)?'playing':'stalled:'+a.currentTime})()")
echo "audio: $PLAY" >> "$OUT/run.txt"
[ "$PLAY" = playing ] && record audio_plays PASS || record audio_plays FAIL
# 4 PDF opens in the built-in viewer (not downloaded).
curl -s -X PUT "http://127.0.0.1:$PORT/json/new?$BASE/doc.pdf" > /dev/null; sleep 5
curl -s "http://127.0.0.1:$PORT/json" > "$OUT/targets.json"
if python3 -c 'import json,sys;t=json.load(open(sys.argv[1]));sys.exit(0 if any("doc.pdf" in x["url"] or "chrome-extension://mhjfbmdgcfjbbpaeojofohoefgiehjai" in x["url"] for x in t) else 1)' "$OUT/targets.json" \
   && [ ! -f "$DL/doc.pdf" ]; then record pdf_opens_in_viewer PASS; else record pdf_opens_in_viewer FAIL; fi
python3 - "$OUT/results.txt" > "$OUT/results.json" <<'PY'
import json, sys
rows = dict(line.split() for line in open(sys.argv[1]) if line.strip())
print(json.dumps({"results": rows, "pass": all(v == "PASS" for v in rows.values())}, indent=1))
PY
cat "$OUT/results.json"
