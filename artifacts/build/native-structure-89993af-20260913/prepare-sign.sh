#!/bin/bash
set -euo pipefail
task_repo=/private/tmp/ahoi-native-structure.HoBjQO/repo
task_root='/Volumes/Macintosh HD - Daten/Cloud/Projekte/Apps/Plattformuebergreifend/AhoiBrowser'
task_evidence="$task_root/artifacts/build/native-structure-89993af-20260913"
task_source="$task_root/.work/chromium/src/out/AhoiDev/AhoiBrowser.app"
task_app="$task_evidence/cloudkit/AhoiBrowser.app"
task_scope="$task_root/artifacts/e2e/shared-sync-structure-development-scope-20260913.json"
task_profile='/Users/vossmedien/Library/Developer/Xcode/UserData/Provisioning Profiles/8f149b92-89cc-4d34-a0db-1b305d4e545c.provisionprofile'
export AHOI_WORK_ROOT="$task_root/.work"
export AHOI_TEAM_ID=248AJ5BN47
export AHOI_CODESIGN_IDENTITY='Apple Development: Christian Voss (2265UJB5KF)'
test "$(< "$task_evidence/build.exit")" = 0
test -f "$task_evidence/build-receipt.json"
test "$(plutil -extract AhoiSourceCommit raw "$task_source/Contents/Info.plist")" = 89993af004d3c73a202422fe9c950310e149ac92
test ! -e "$task_app"
test ! -e "$task_evidence/AhoiBrowser.app"
cp -cR "$task_source" "$task_evidence/AhoiBrowser.app"
mkdir -p "$task_evidence/cloudkit"
cp -cR "$task_source" "$task_app"
cd "$task_repo"
python3 scripts/release/ahoi-release.py prepare-macos-cloudkit \
  --app "$task_app" --signing-profile cloudkit-development \
  --provisioning-profile "$task_profile" --acceptance-scope "$task_scope" \
  --entitlements-output "$task_evidence/cloudkit/entitlements.plist" \
  --output "$task_evidence/cloudkit/preparation.json"
codesign --force --sign "$AHOI_CODESIGN_IDENTITY" --timestamp=none \
  --entitlements "$task_evidence/cloudkit/entitlements.plist" "$task_app"
codesign --verify --deep --strict "$task_app"
python3 scripts/release/ahoi-release.py verify-macos-cloudkit \
  --app "$task_app" --signing-profile cloudkit-development \
  --acceptance-scope "$task_scope" \
  --output "$task_evidence/cloudkit/verification.json"
