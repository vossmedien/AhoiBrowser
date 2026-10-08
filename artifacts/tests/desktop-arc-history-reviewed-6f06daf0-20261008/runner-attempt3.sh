#!/bin/bash
set -euo pipefail
umask 077
BASE=/Users/vossmedien/inhouse/evidence/ahoi-arc-reviewed-6f06daf0-20261008
E=$BASE/attempt3
mkdir -p "$E"
APP=/Users/vossmedien/inhouse/scratch/ahoi-chromium.fMMeXkCh/AhoiDev/AhoiBrowser.app
S=$E/tools/desktop_e2e
export PATH=/Users/vossmedien/inhouse/toolchains/node-v22.23.1-darwin-arm64/bin:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export AHOI_E2E_GUI_LAUNCH=1
export AHOI_AXTOOL=/private/tmp/ahoi-axtool
export AHOI_E2E_CDP_PORT=9357
export AHOI_E2E_LOCK=/Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work/agent-queue/e2e.lock
node --version > "$E/node-version.txt"
RESULT=1
trap 'printf "%s\n" "$RESULT" > "$E/runner.exit"' EXIT
printf '%s\n' "$$" > "$E/runner.pid"
printf '%s\n' '01a113e8-cfc5-79b1-9a36-71e8e45f3a93 / worker-handoff:e5fbc1f3' > "$E/owner.txt"
shasum -a 256 "$S/arc-history-journey.sh" "$S/browser_launch.sh" "$S/run-journey-set.sh" > "$E/scripts.sha256"
[ "$(/usr/libexec/PlistBuddy -c 'Print AhoiSourceCommit' "$APP/Contents/Info.plist")" = 6f06daf0ca0e79091a2fc48f6bd79bb838588149 ]
[ "$(shasum -a 256 "$APP/Contents/MacOS/AhoiBrowser" | awk '{print $1}')" = bdd229591271f8d33e7b462f99b543a0b4afca213164b8d06049a80a4a6f463f ]
caffeinate -d -w "$$" &
MAIN_ROOT=$(mktemp -d /private/tmp/ahoi-arc-history-e2e-XXXXXX)
SEPARATED_ROOT=$(mktemp -d /private/tmp/ahoi-arc-history-e2e-XXXXXX)
printf '%s\n' "$MAIN_ROOT" "$SEPARATED_ROOT" > "$E/fixture-roots.txt"
for CASE in main separated rollback recovery; do
  ROOT=$MAIN_ROOT; [ "$CASE" = separated ] && ROOT=$SEPARATED_ROOT
  printf '%s %s\n' "$(date -u +%FT%TZ)" "$CASE START" >> "$E/phases.log"
  AHOI_E2E_ARC_CASE=$CASE AHOI_E2E_ARC_ROOT=$ROOT bash "$S/run-journey-set.sh" "$APP" "$E/$CASE" arc-history
  python3 - "$E/$CASE/summary.json" <<'PY'
import json,sys
assert json.load(open(sys.argv[1]))['pass'] is True
PY
  printf '%s %s\n' "$(date -u +%FT%TZ)" "$CASE PASS" >> "$E/phases.log"
done
RESULT=0
