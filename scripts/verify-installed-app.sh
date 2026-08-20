#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

app_path="/Applications/AhoiBrowser.app"
ahoi_require_command plutil
expected_team="${AHOI_TEAM_ID:-}"
expected_authority="${AHOI_CODESIGN_IDENTITY:-}"
[ -n "${expected_team}" ] || \
  ahoi_die "AHOI_TEAM_ID is required for installed release verification"
echo "${expected_team}" | grep -Eq '^[A-Z0-9]{10}$' || \
  ahoi_die "AHOI_TEAM_ID must be a ten-character Apple Team ID"
[ -n "${expected_authority}" ] || \
  ahoi_die "AHOI_CODESIGN_IDENTITY is required for installed release verification"
case "${expected_authority}" in
  'Developer ID Application: '*) ;;
  *) ahoi_die "AHOI_CODESIGN_IDENTITY must name a Developer ID Application identity" ;;
esac

"${SCRIPT_DIR}/verify-built-app.sh" "${app_path}"
installed_profile="$(plutil -extract AhoiBuildProfile raw "${app_path}/Contents/Info.plist")"
[ "${installed_profile}" = "release" ] || \
  ahoi_die "installed release evidence requires AhoiBuildProfile=release"

ahoi_require_command codesign
ahoi_require_command file
ahoi_require_command lipo
ahoi_require_command spctl
ahoi_require_command xcrun

codesign --verify --deep --strict --verbose=2 "${app_path}"
spctl --assess --type execute --verbose=2 "${app_path}"
xcrun stapler validate "${app_path}"

verify_signed_code() {
  local candidate="$1"
  local requires_runtime="$2"
  local description
  local actual_team
  local actual_authority
  local entitlements
  local relative_path
  local entitlement_role
  codesign --verify --strict --verbose=2 "${candidate}"
  description="$(codesign -d --verbose=4 "${candidate}" 2>&1)" || \
    ahoi_die "cannot read signature metadata: ${candidate}"
  actual_team="$(printf '%s\n' "${description}" | awk -F= '/^TeamIdentifier=/{print $2; exit}')"
  actual_authority="$(printf '%s\n' "${description}" | awk -F= '/^Authority=/{sub(/^Authority=/, ""); print; exit}')"
  [ "${actual_team}" = "${expected_team}" ] || \
    ahoi_die "unexpected TeamIdentifier for ${candidate}: ${actual_team:-missing}"
  [ "${actual_authority}" = "${expected_authority}" ] || \
    ahoi_die "unexpected signing authority for ${candidate}: ${actual_authority:-missing}"
  if [ "${requires_runtime}" = "1" ]; then
    printf '%s\n' "${description}" | grep -Eq '^CodeDirectory .*flags=.*\([^)]*runtime[^)]*\)' || \
      ahoi_die "Hardened Runtime flag missing: ${candidate}"
    printf '%s\n' "${description}" | grep -q '^Runtime Version=' || \
      ahoi_die "Hardened Runtime version missing: ${candidate}"
  fi
  if ! entitlements="$(codesign -d --entitlements :- "${candidate}" 2>/dev/null)"; then
    ahoi_die "cannot read entitlements fail-closed: ${candidate}"
  fi
  if printf '%s\n' "${entitlements}" | grep -q 'com.apple.security.get-task-allow'; then
    ahoi_die "release code contains get-task-allow: ${candidate}"
  fi
  relative_path="${candidate#"${app_path}"/}"
  entitlement_role="$(printf '%s' "${entitlements}" | \
    python3 "${AHOI_REPO_ROOT}/tools/verify_macos_entitlements.py" \
      --relative-path "${relative_path}")" || \
    ahoi_die "role-specific entitlement policy failed: ${candidate}"
  ahoi_note "entitlement role ${entitlement_role}: ${relative_path}"
}

verified_macho=0
verified_runtime_executables=0
while IFS= read -r -d '' candidate; do
  file_description="$(file "${candidate}")"
  if printf '%s\n' "${file_description}" | grep -q 'Mach-O'; then
    architectures="$(lipo -archs "${candidate}")" || \
      ahoi_die "cannot read Mach-O architectures: ${candidate}"
    [ "${architectures}" = "arm64" ] || \
      ahoi_die "nested Mach-O must be ARM64-only, found ${architectures}: ${candidate}"
    requires_runtime=0
    if printf '%s\n' "${file_description}" | grep -q 'executable'; then
      requires_runtime=1
      verified_runtime_executables=$((verified_runtime_executables + 1))
    fi
    verify_signed_code "${candidate}" "${requires_runtime}"
    verified_macho=$((verified_macho + 1))
  fi
done < <(find "${app_path}" -type f -print0)
[ "${verified_macho}" -gt 0 ] || ahoi_die "no Mach-O code found in installed bundle"
[ "${verified_runtime_executables}" -gt 0 ] || \
  ahoi_die "no Hardened Runtime executable found in installed bundle"

ahoi_note "installed identity, ${verified_runtime_executables} Hardened Runtime executables, entitlements, Gatekeeper, stapling and ${verified_macho} Mach-O signatures verified"
