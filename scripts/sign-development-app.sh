#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

app_path="${1:-}"
[ -n "${app_path}" ] || ahoi_die "usage: $0 /absolute/path/AhoiBrowser.app"
case "${app_path}" in
  /*/AhoiBrowser.app) ;;
  *) ahoi_die "development signing only accepts an absolute AhoiBrowser.app path" ;;
esac
[ -d "${app_path}" ] || ahoi_die "app bundle not found: ${app_path}"
plist="${app_path}/Contents/Info.plist"
[ -f "${plist}" ] || ahoi_die "Info.plist missing: ${plist}"
build_profile="$(plutil -extract AhoiBuildProfile raw "${plist}")"
case "${build_profile}" in
  dev|full-dev) ;;
  *) ahoi_die "development signing is restricted to development profiles" ;;
esac

ahoi_require_command codesign
ahoi_require_command security
framework="${app_path}/Contents/Frameworks/AhoiBrowser Framework.framework"
helpers="${framework}/Versions/Current/Helpers"
[ -d "${framework}" ] && [ -d "${helpers}" ] || \
  ahoi_die "AhoiBrowser framework or helper directory is missing"

identity="$(python3 "${AHOI_REPO_ROOT}/tools/development_signing.py")" || \
  ahoi_die "cannot resolve a safe development signing identity"
signing_arguments=(--force --sign "${identity}" --timestamp=none)
if [ "${identity}" != "-" ]; then
  # Development profiles are component builds. Their staged Chromium dylibs
  # intentionally keep their linker signatures. A stable identity fixes the
  # Keychain access requirement; it must not silently turn this non-release
  # bundle into a partial hardened-runtime package whose library validation
  # rejects those development components. The separate release signer owns full
  # hardened runtime signing for every nested code object.
  signing_arguments+=(--preserve-metadata=entitlements)
fi

while IFS= read -r helper; do
  codesign "${signing_arguments[@]}" "${helper}"
done < <(find "${helpers}" -maxdepth 1 -type d -name 'AhoiBrowser Helper*.app' \
  -print | LC_ALL=C sort)
codesign "${signing_arguments[@]}" "${framework}"
codesign "${signing_arguments[@]}" "${app_path}"
codesign --verify --deep --strict "${app_path}" || \
  ahoi_die "development signature verification failed"

if [ "${identity}" = "-" ]; then
  ahoi_note "warning: explicitly ad-hoc signed development app; its changing CDHash can trigger macOS Keychain authorization after every rebuild"
else
  signature_description="$(codesign -d --verbose=4 "${app_path}" 2>&1)"
  echo "${signature_description}" | grep -Fqx "Authority=${identity}" || \
    ahoi_die "development signature authority does not match the selected identity"
  echo "${signature_description}" | grep -Eq '^TeamIdentifier=[A-Z0-9]{10}$' || \
    ahoi_die "stable development signature has no valid TeamIdentifier"
  designated_requirement="$(codesign -d -r- "${app_path}" 2>&1)"
  echo "${designated_requirement}" | grep -q 'designated => cdhash' && \
    ahoi_die "stable development signature unexpectedly uses a CDHash requirement"
  ahoi_note "signed development app with stable identity: ${identity}"
fi
ahoi_note "verified nested development code"
