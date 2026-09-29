#!/bin/bash
# usage: media-mini-player-transient-journey.sh <App.app> <outdir>
# Crest adoption A5 on an installed candidate: a short notification sound in
# a background tab must not take the sidebar MiniPlayer card from a paused
# music tab, must not linger as a second source afterwards, and a muted
# controllable tab keeps its card so it can be unmuted there; unmuting keeps
# the card and its selection while a second paused source exists. Audio plays
# through CDP (autoplay allowed by flag); the MiniPlayer is read and pressed
# through PID-scoped AX only. No keyboard, mouse or clipboard input.
# Run it only while holding the desktop e2e.lock (the queue runner does).
# Negative control: before the fix the card shows the chime tab while it
# plays and "Nächste Medienquelle" stays after it ended; before the unmute
# fix the card moves to the second source after "Ton einschalten".
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9351
DEPTH=${AHOI_E2E_AX_DEPTH:-30}
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
SITE_PORT=${AHOI_E2E_SITE_PORT:-8796}
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-miniplayer-profile.XXXXXX); SITE=$P-site; mkdir -p "$SITE"
python3 - "$SITE" <<'PY'
import math, os, struct, sys, wave
d = sys.argv[1]
def tone(name, seconds, hz):
    w = wave.open(os.path.join(d, name), "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(8000)
    w.writeframes(b"".join(struct.pack("<h", int(2000 * math.sin(2 * math.pi * hz * t / 8000)))
                           for t in range(8000 * seconds)))
    w.close()
tone("music-30s.wav", 30, 330)   # persistent: longer than Chromium's 5 s
tone("chime-1s.wav", 1, 880)     # transient: 5 s or shorter
open(os.path.join(d, "music.html"), "w").write(
    '<title>music page</title><audio id="a" src="music-30s.wav"></audio><script>'
    'navigator.mediaSession.metadata=new MediaMetadata({title:"Ahoi Musik",artist:"Ahoi"});'
    '</script>')
open(os.path.join(d, "second.html"), "w").write(
    '<title>second page</title><audio id="a" src="music-30s.wav"></audio><script>'
    'navigator.mediaSession.metadata=new MediaMetadata({title:"Ahoi Zweitquelle",artist:"Ahoi"});'
    '</script>')
open(os.path.join(d, "chime.html"), "w").write(
    '<title>chime page</title><script>'
    'window.chime=()=>new Promise(r=>{const a=new Audio("chime-1s.wav");'
    'a.onended=()=>r("ended");a.play().catch(e=>r("error:"+e.name))});</script>')
PY
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory "$SITE" > "$OUT/site.log" 2>&1 &
SITE_PID=$!; BASE=http://127.0.0.1:$SITE_PORT
"$APP/Contents/MacOS/AhoiBrowser" --user-data-dir="$P" --no-first-run --no-default-browser-check \
  --remote-debugging-port=$PORT --autoplay-policy=no-user-gesture-required about:blank > "$OUT/browser.log" 2>&1 &
PID=$!; trap 'kill $SITE_PID 2>/dev/null; kill $PID 2>/dev/null' EXIT
echo "pid=$PID profile=$P" > "$OUT/run.txt"
for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
sleep 3; $AX enable $PID >> "$OUT/steps.txt"
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
eval_in() { # <url substring> <expression>
  CDP "$1" Runtime.evaluate "$(python3 -c 'import json,sys;print(json.dumps({"expression":sys.argv[1],"returnByValue":True,"awaitPromise":True}))' "$2")" \
    | python3 -c 'import json,sys;print(json.load(sys.stdin).get("result",{}).get("value",""))'; }
dump() { $AX dump $PID $DEPTH; }
waitax() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do dump | grep -q -E "$1" && return 0; sleep 1; done; return 1; }
RESULTS=(); record() { RESULTS+=("\"$1\": $2"); echo "$1 -> $2" >> "$OUT/steps.txt"; }
check() { if "${@:2}"; then record "$1" true; else record "$1" false; fi; }
finish() {
  local joined; joined=$(IFS=,; echo "${RESULTS[*]}")
  local sep=""; [ -n "$joined" ] && sep=", "
  echo "{${joined}${1:+$sep\"setupFailed\": \"$1\"}}" | python3 -c 'import json,sys;d=json.load(sys.stdin);d["pass"]=("setupFailed" not in d) and all(v is True for k,v in d.items() if k!="setupFailed");print(json.dumps(d,indent=1))' > "$OUT/verdict.json"
  cat "$OUT/verdict.json"
}
fail_setup() { dump > "$OUT/ax-setup-failure.txt"; finish "$1"; exit 4; }
card_on_music() { dump | grep -q "Ahoi Musik"; }
no_second_source() { ! dump | grep -q "Nächste Medienquelle"; }

# Tab A: a controllable music session, started and then paused.
curl -s -X PUT "http://127.0.0.1:$PORT/json/new?$BASE/music.html" > /dev/null; sleep 3
PLAY=$(eval_in music.html "(async()=>{const a=document.getElementById('a');try{await a.play()}catch(e){return 'error:'+e.name}await new Promise(r=>setTimeout(r,1500));return a.paused?'paused':'playing'})()")
echo "music play: $PLAY" >> "$OUT/steps.txt"
[ "$PLAY" = playing ] || fail_setup "music did not play: $PLAY"
waitax "Ahoi Musik" 10 || fail_setup "MiniPlayer did not show the music session"
eval_in music.html "document.getElementById('a').pause();'ok'" >> "$OUT/steps.txt"
waitax "AXButton \\| Wiedergabe" 8 || fail_setup "MiniPlayer did not show the paused music session"
dump > "$OUT/ax-music-paused.txt"

# Tab B: a one-second chime. Sample the card while it plays, then after.
curl -s -X PUT "http://127.0.0.1:$PORT/json/new?$BASE/chime.html" > /dev/null; sleep 3
eval_in chime.html "window.chime().then(r=>window.chimeResult=r);'started'" >> "$OUT/steps.txt"
sleep 0.5; dump > "$OUT/ax-during-chime.txt"
grep -q "Ahoi Musik" "$OUT/ax-during-chime.txt" && record chimeKeepsMusicCard true || record chimeKeepsMusicCard false
{ ! grep -q "Nächste Medienquelle" "$OUT/ax-during-chime.txt"; } && record chimeIsNoSecondSource true || record chimeIsNoSecondSource false
sleep 3
echo "chime: $(eval_in chime.html "String(window.chimeResult)")" >> "$OUT/steps.txt"
dump > "$OUT/ax-after-chime.txt"
check musicCardAfterChime card_on_music
check noLingeringChimeSource no_second_source

# Tab C: a second controllable source, played and paused, so a lost
# selection after unmuting would show up. Then play and pause the music tab
# again so the paused music card is selected before it is muted.
playpause() { # <url substring>
  eval_in "$1" "(async()=>{const a=document.getElementById('a');try{await a.play()}catch(e){return 'error:'+e.name}await new Promise(r=>setTimeout(r,1500));a.pause();return 'ok'})()"; }
curl -s -X PUT "http://127.0.0.1:$PORT/json/new?$BASE/second.html" > /dev/null; sleep 3
echo "second: $(playpause second.html)" >> "$OUT/steps.txt"
waitax "Ahoi Zweitquelle" 8 || fail_setup "MiniPlayer did not show the second source"
echo "music again: $(playpause music.html)" >> "$OUT/steps.txt"
waitax "Ahoi Musik" 8 || fail_setup "MiniPlayer did not return to the music session"
waitax "Nächste Medienquelle" 8 || fail_setup "second source is not a relevant source"
dump > "$OUT/ax-two-sources.txt"

# Mute the music tab from its card: the card stays so it can be unmuted.
$AX press $PID "AXButton:Stummschalten" >> "$OUT/steps.txt"
waitax "AXButton \\| Ton einschalten" 8 && record mutedCardShowsUnmute true || record mutedCardShowsUnmute false
sleep 3; dump > "$OUT/ax-muted.txt"
check mutedCardStays card_on_music
$AX press $PID "AXButton:Ton einschalten" >> "$OUT/steps.txt"
waitax "AXButton \\| Stummschalten" 8 && record unmuteFromCard true || record unmuteFromCard false
sleep 2; dump > "$OUT/ax-final.txt"
grep -q "Ahoi Musik" "$OUT/ax-final.txt" && record unmutedCardStaysOnMusic true || record unmutedCardStaysOnMusic false
finish
