#!/bin/bash
set -euo pipefail
E=/Users/vossmedien/inhouse/evidence/ahoi-b6-browser-regression-20261009
N=/Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work/agent-queue/repo88-m155-f492d294
W=/Volumes/Daten/Inhouse/AhoiBrowser/work
OUT=/Users/vossmedien/inhouse/scratch/ahoi-chromium.fMMeXkCh/AhoiDev
LOCK=$W/state/desktop-build.lock
mkdir -p "$E"
phase() { printf '%s %s\n' "$(date -u +%FT%TZ)" "$*" >> "$E/phases.log"; }
cd "$N"
test "$(git rev-parse HEAD)" = b6bce96774f5440567f4d2f6d08d43526e403b13
test -z "$(git status --porcelain --untracked-files=no)"
test ! -e /Users/vossmedien/inhouse/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work/agent-queue/e2e.lock
/Users/vossmedien/.local/bin/inhouse-external-root >/dev/null
mkdir "$LOCK"
printf '{"owner":"%s","thread":"01a11179-cb6b-7dc1-9e53-f3d72fefc198","source":"b6bce967","pid":%s}\n' "$E" "$$" > "$LOCK/owner.json"
finish() { result=$?; printf '%s\n' "$result" > "$E/runner.exit"; phase "terminal exit=$result"; if test -f "$LOCK/owner.json" && grep -q "$E" "$LOCK/owner.json"; then rm "$LOCK/owner.json"; rmdir "$LOCK"; fi; }
trap finish EXIT
export AHOI_WORK_ROOT="$W" AHOI_CHROMIUM_OUT_ROOT=/Users/vossmedien/inhouse/scratch/ahoi-chromium.fMMeXkCh
export AHOI_JOBS=2 AHOI_NINJA_KEEP_GOING=0 AHOI_PLANNED_GROWTH_BYTES=8589934592 DEPOT_TOOLS_UPDATE=0
PB="$W/depot_tools/bootstrap-2@3.11.8.chromium.35_bin/python3/bin"
export PATH="/Users/vossmedien/inhouse/toolchains/git-lfs-v3.8.0/git-lfs-3.8.0:$PB:/usr/bin:/bin:/usr/sbin:/sbin"
source scripts/lib/common.sh
ahoi_select_xcode compatible-development
ahoi_require_build_free_space
ahoi_require_overlay_state
ahoi_require_hook_state overlay compatible-development
ahoi_enable_depot_tools
phase 'guarded existing BRT target compile only; no test run or app stamp'
nice -n 10 ./scripts/build-chromium-with-dependency-workarounds.sh "$OUT" "$N/config/build/ahoi-dev.gn" ahoi_arc_import_browsertests > "$E/build.log" 2>&1
phase 'browser regression binary compiled; visible E2E and focused execution remain open'
