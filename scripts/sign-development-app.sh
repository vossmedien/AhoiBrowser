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
[ "$(plutil -extract AhoiBuildProfile raw "${plist}")" = "dev" ] || \
  ahoi_die "ad-hoc development signing is forbidden for release bundles"

ahoi_require_command codesign
framework="${app_path}/Contents/Frameworks/AhoiBrowser Framework.framework"
helpers="${framework}/Versions/Current/Helpers"
[ -d "${framework}" ] && [ -d "${helpers}" ] || \
  ahoi_die "AhoiBrowser framework or helper directory is missing"

while IFS= read -r helper; do
  codesign --force --sign - --timestamp=none "${helper}"
done < <(find "${helpers}" -maxdepth 1 -type d -name 'AhoiBrowser Helper*.app' \
  -print | LC_ALL=C sort)
codesign --force --sign - --timestamp=none "${framework}"
codesign --force --sign - --timestamp=none "${app_path}"
codesign --verify --deep --strict "${app_path}" || \
  ahoi_die "ad-hoc development signature verification failed"

ahoi_note "ad-hoc signed portable development app and verified nested code"
