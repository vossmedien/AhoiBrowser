#!/bin/bash
set -uo pipefail
task_repo=/private/tmp/ahoi-native-sync-recovery.DxdGLK/repo
task_evidence='/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/artifacts/build/native-sync-26b01b1-20260913'
export AHOI_WORK_ROOT='/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work'
export AHOI_JOBS=3
export AHOI_ALLOW_LOW_DISK=1
cd "$task_repo" || exit 1
test "$(git rev-parse HEAD)" = 26b01b17134a04e6d4e96bf9a837bbcf81ea06e3 || exit 1
./scripts/apply-overlay.sh --compatible-dev-xcode > "$task_evidence/overlay.log" 2>&1
task_exit=$?
printf '%s\n' "$task_exit" > "$task_evidence/overlay.exit"
if [ "$task_exit" -ne 0 ]; then exit "$task_exit"; fi
./scripts/build-ahoi.sh dev > "$task_evidence/build.log" 2>&1
task_exit=$?
printf '%s\n' "$task_exit" > "$task_evidence/build.exit"
if [ "$task_exit" -eq 0 ]; then
  cp "$task_repo/artifacts/build/ahoi-dev-build.json" "$task_evidence/build-receipt.json"
fi
exit "$task_exit"
