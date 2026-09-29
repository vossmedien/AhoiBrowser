#!/bin/bash
set -uo pipefail
task_repo=/private/tmp/ahoi-native-sync-recovery.DxdGLK/repo
task_root='/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser'
task_evidence="$task_root/artifacts/tests/receive-boundary-48f7d71-20260913"
export AHOI_WORK_ROOT="$task_root/.work"
export AHOI_JOBS=2
cd "$task_repo" || exit 1
./scripts/apply-overlay.sh --compatible-dev-xcode > "$task_evidence/overlay.log" 2>&1
task_exit=$?
printf '%s\n' "$task_exit" > "$task_evidence/overlay.exit"
if [ "$task_exit" -ne 0 ]; then exit "$task_exit"; fi
./scripts/build-ahoi.sh dev ahoi_sync_unittests > "$task_evidence/build.log" 2>&1
task_exit=$?
printf '%s\n' "$task_exit" > "$task_evidence/build.exit"
if [ "$task_exit" -ne 0 ]; then exit "$task_exit"; fi
"$AHOI_WORK_ROOT/chromium/src/out/AhoiDev/ahoi_sync_unittests" \
  --gtest_filter='SyncReceiveBoundaryTest.*' \
  --test-launcher-jobs=1 --test-launcher-retry-limit=0 \
  --test-launcher-summary-output="$task_evidence/summary.json" \
  > "$task_evidence/run.log" 2>&1
task_exit=$?
printf '%s\n' "$task_exit" > "$task_evidence/run.exit"
exit "$task_exit"
