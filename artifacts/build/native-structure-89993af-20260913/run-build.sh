#!/bin/bash
set -uo pipefail
task_repo=/private/tmp/ahoi-native-structure.HoBjQO/repo
task_evidence='/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/artifacts/build/native-structure-89993af-20260913'
export AHOI_WORK_ROOT='/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work'
export AHOI_JOBS=2
export AHOI_ALLOW_LOW_DISK=1
export AHOI_NINJA_KEEP_GOING=1
cd "$task_repo" || exit 1
test "$(git rev-parse HEAD)" = 89993af004d3c73a202422fe9c950310e149ac92 || exit 1
test -z "$(git status --porcelain --untracked-files=no)" || exit 1
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
