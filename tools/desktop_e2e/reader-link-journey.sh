#!/bin/bash
# usage: reader-link-journey.sh <App.app> <outdir>
# WORKFLOW-07 on the installed candidate, driven through the command bar:
# "Link der aktiven Seite kopieren" puts the credential-free URL on the
# clipboard, "... als Markdown kopieren" a CommonMark link whose label keeps
# the page title literal (punctuation escaped) and whose destination escapes
# parentheses; a copy from an incognito window is marked concealed
# (org.nspasteboard.ConcealedType) so clipboard history skips it; "Aktive
# Seite im Lesemodus öffnen" opens Chromium's reading mode panel without
# navigating the page. The owner's text clipboard is saved and restored.
set -u
APP=$1; OUT=$2; S=$(cd "$(dirname "$0")" && pwd); AX=${AHOI_AXTOOL:-/private/tmp/ahoi-axtool}; PORT=9399
[ -x "$AX" ] && [ "$AX" -nt "$S/axtool.swift" ] || xcrun swiftc -O -o "$AX" "$S/axtool.swift" || exit 5
idle_seconds() { ioreg -c IOHIDSystem | awk '/HIDIdleTime/ {print int($NF/1000000000); exit}'; }
if [ "$(idle_seconds)" -lt "${AHOI_E2E_MIN_IDLE:-300}" ]; then
  echo "owner active (idle $(idle_seconds)s); refusing to drive the desktop" >&2; exit 7
fi
if lsof -nP -iTCP:$PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "DevTools port $PORT busy" >&2; exit 6; fi
mkdir -p "$OUT"; P=$(mktemp -d /private/tmp/ahoi-reader-profile.XXXXXX)
SITE_PORT=${AHOI_E2E_SITE_PORT:-8819}; mkdir -p $P-site
if lsof -nP -iTCP:$SITE_PORT -sTCP:LISTEN >/dev/null 2>&1; then echo "site port $SITE_PORT busy" >&2; exit 6; fi
TITLE='Ahoi *Test* [Artikel] (1)'
{ printf '<title>%s</title><article><h1>Leuchtturm</h1>' "$TITLE"
  for i in 1 2 3 4 5 6; do
    printf '<p>Absatz %s: Der Leuchtturm steht seit vielen Jahren an der Küste und weist den Schiffen bei Nacht und Nebel den sicheren Weg in den Hafen. Die Wärter führen ein ruhiges Leben.</p>' $i
  done; printf '</article>'; } > $P-site/article.html
SAVED=$P-clipboard.txt; pbpaste > "$SAVED" 2>/dev/null
python3 -m http.server $SITE_PORT --bind 127.0.0.1 --directory $P-site > "$OUT/site.log" 2>&1 &
SITE_PID=$!; trap 'kill $SITE_PID 2>/dev/null; pbcopy < "$SAVED"' EXIT; SITE=http://127.0.0.1:$SITE_PORT
PAGE="$SITE/article.html?x=(y)"
CDP() { node "$S/cdp.mjs" $PORT "$@"; }
pages() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print(json.dumps(sorted([t["url"] for t in json.load(sys.stdin) if t["type"]=="page"])))'; }
targets() { curl -s http://127.0.0.1:$PORT/json | python3 -c 'import json,sys;print("\n".join(t["type"]+" "+t["url"] for t in json.load(sys.stdin)))'; }
launch() {
  "$APP/Contents/MacOS/AhoiBrowser" --user-data-dir=$P --no-first-run --no-default-browser-check \
    --remote-debugging-port=$PORT about:blank >> "$OUT/browser.log" 2>&1 &
  PID=$!; echo "pid=$PID profile=$P" >> "$OUT/run.txt"
  for i in $(seq 1 60); do curl -s http://127.0.0.1:$PORT/json/version >/dev/null && break; sleep 2; done
  sleep 4; $AX activate $PID >> "$OUT/steps.txt"
}
quit() { key 12 cmd; for i in $(seq 1 20); do kill -0 $PID 2>/dev/null || return 0; sleep 1; done; echo "still running after quit" >> "$OUT/run.txt"; kill $PID; sleep 3; }
# Keys go through the HID event tap like a real keyboard; hidkey refuses
# unless the app is frontmost, so bring it forward and retry.
key() {
  for attempt in 1 2 3 4 5; do
    $AX activate $PID >/dev/null; sleep 0.3
    $AX hidkey $PID "$@" >> "$OUT/steps.txt" && return 0
    sleep 1
  done
  echo "hidkey gave up: $*" >> "$OUT/steps.txt"; return 1
}
waitax() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do $AX dump $PID 14 | grep -q -E "$1" && return 0; sleep 1; done; return 1; }
waiturl() { local end=$(( $(date +%s) + $2 )); while [ $(date +%s) -lt $end ]; do pages | grep -q -F "$1" && return 0; sleep 1; done; return 1; }
RESULTS=(); record() { RESULTS+=("\"$1\": $2"); echo "$1 -> $2" >> "$OUT/steps.txt"; }
finish() {
  local joined; joined=$(IFS=,; echo "${RESULTS[*]}")
  local sep=""; [ -n "$joined" ] && sep=", "
  echo "{${joined}${1:+$sep\"setupFailed\": \"$1\"}}" | python3 -c 'import json,sys;d=json.load(sys.stdin);d["pass"]=("setupFailed" not in d) and all(v is True for k,v in d.items() if k!="setupFailed");print(json.dumps(d,indent=1))' > "$OUT/verdict.json"
  cat "$OUT/verdict.json"
}
fail_setup() { $AX dump $PID 14 > "$OUT/ax-setup-failure.txt"; finish "$1"; quit; exit 4; }
BAR="AXWindow \\| Suchen oder URL eingeben"
command_bar() { # ⌘T opens the command bar in the frontmost window
  for attempt in 1 2 3; do
    key 17 cmd; waitax "$BAR" 6 && { sleep 1; return 0; }
  done
  return 1
}
open_url() { # <url> <expected substring>
  command_bar || fail_setup "command bar did not open for $1"
  $AX type $PID "$1" >> "$OUT/steps.txt"; sleep 1; key 36
  waiturl "$2" 5 || { echo "info: Return repeated" >> "$OUT/steps.txt"; key 36; }
  waiturl "$2" 20 || fail_setup "did not load $1"; sleep 2
}
run_command() { # <label>; types the command's label and runs the top row
  command_bar || return 1
  $AX type $PID "$1" >> "$OUT/steps.txt"; sleep 2
  $AX dump $PID 14 | grep -q "AXStaticText | $1\$" || { key 53; return 1; }
  key 36; sleep 2
}
clip() { pbpaste; }
clip_types() {
  osascript -l JavaScript -e 'ObjC.import("AppKit"); ObjC.deepUnwrap($.NSPasteboard.generalPasteboard.types).join(",")'
}
COPY="Link der aktiven Seite kopieren"
COPY_MD="Link der aktiven Seite als Markdown kopieren"
READER="Aktive Seite im Lesemodus öffnen"

launch
open_url "$PAGE" "article.html"
printf 'sentinel' | pbcopy
run_command "$COPY" && record copyLinkOffered true || record copyLinkOffered false
clip > "$OUT/clip-url.txt"; echo "url: $(clip)" >> "$OUT/steps.txt"
[ "$(clip)" = "$SITE/article.html?x=(y)" ] && record copiedUrl true || record copiedUrl false
run_command "$COPY_MD" && record copyMarkdownOffered true || record copyMarkdownOffered false
clip > "$OUT/clip-markdown.txt"; echo "markdown: $(clip)" >> "$OUT/steps.txt"
[ "$(clip)" = '[Ahoi \*Test\* \[Artikel\] \(1\)](http://127.0.0.1:'$SITE_PORT'/article.html?x=\(y\))' ] \
  && record markdownEscaped true || record markdownEscaped false
# Credentials typed into the address never reach the copied link.
open_url "http://user:secret@127.0.0.1:$SITE_PORT/article.html" "article.html"
run_command "$COPY" >/dev/null; clip > "$OUT/clip-credential.txt"
! clip | grep -q -E 'user|secret' && clip | grep -q "127.0.0.1:$SITE_PORT/article.html" \
  && record credentialsStripped true || record credentialsStripped false
clip_types > "$OUT/clip-types-normal.txt"
! grep -q ConcealedType "$OUT/clip-types-normal.txt" && record normalCopyNotConcealed true \
  || record normalCopyNotConcealed false
# Reading mode opens Chromium's panel next to the unchanged page.
before=$(pages)
run_command "$READER" && record readerOffered true || record readerOffered false
sleep 3; targets > "$OUT/targets-reader.txt"
grep -q "read-anything" "$OUT/targets-reader.txt" && record readerPanelOpened true || record readerPanelOpened false
[ "$(pages)" = "$before" ] && record readerKeepsPage true || record readerKeepsPage false
$AX dump $PID 14 > "$OUT/ax-reader.txt"
# Incognito: the same copy is concealed from clipboard history.
key 45 cmd,shift; sleep 3
open_url "$SITE/article.html?private=1" "private=1"
printf 'sentinel' | pbcopy
run_command "$COPY" >/dev/null; clip > "$OUT/clip-incognito.txt"
clip_types > "$OUT/clip-types-incognito.txt"
clip | grep -q "private=1" && grep -q ConcealedType "$OUT/clip-types-incognito.txt" \
  && record incognitoCopyConcealed true || record incognitoCopyConcealed false
$AX dump $PID 14 > "$OUT/ax-final.txt"
finish; quit
