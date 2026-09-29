#!/bin/bash
set -uo pipefail
task_repo=/private/tmp/ahoi-native-unified.1FqoG5/repo
task_evidence='/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/artifacts/build/native-m153-59a0bf5-20260920'
export AHOI_WORK_ROOT='/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser/.work'
export AHOI_JOBS=6
export AHOI_NINJA_KEEP_GOING=1
export AHOI_ALLOW_LOW_DISK=1
cd "$task_repo" || exit 1
test "$(git rev-parse HEAD)" = 59a0bf58121153d6357c69ec399819e9e263cdad || exit 1
test -z "$(git status --porcelain --untracked-files=no)" || exit 1
./scripts/build-ahoi.sh dev > "$task_evidence/build.log" 2>&1
task_exit=$?
printf '%s\n' "$task_exit" > "$task_evidence/build.exit"
if [ "$task_exit" -eq 0 ]; then
  cp "$task_repo/artifacts/build/ahoi-dev-build.json" "$task_evidence/build-receipt.json"
fi
exit "$task_exit"
