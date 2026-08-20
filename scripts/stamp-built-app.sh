#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

app_path="${1:-}"
args_file="${2:-}"
[ -d "${app_path}" ] || ahoi_die "app bundle not found: ${app_path}"
[ -f "${args_file}" ] || ahoi_die "GN args file not found: ${args_file}"
[ -f "${app_path}/Contents/Info.plist" ] || ahoi_die "Info.plist missing"

ahoi_require_command git
ahoi_require_command plutil
ahoi_require_command shasum

source_commit="$(git -C "${AHOI_REPO_ROOT}" rev-parse HEAD)"
chromium_commit="$(git -C "${AHOI_CHROMIUM_SRC}" rev-parse HEAD)"
product_version="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" displayVersion)"
marketing_version="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" marketingVersion)"
build_number="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" buildNumber)"
channel="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" channel)"
chromium_version="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" version)"
args_hash="$(ahoi_sha256 "${args_file}")"
case "$(basename "${args_file}")" in
  ahoi-dev.gn) build_profile="dev" ;;
  ahoi-release.gn) build_profile="release" ;;
  *) ahoi_die "unsupported Ahoi GN args file: ${args_file}" ;;
esac
plist="${app_path}/Contents/Info.plist"

set_plist_string() {
  local key="$1"
  local value="$2"
  if plutil -extract "${key}" raw "${plist}" >/dev/null 2>&1; then
    plutil -replace "${key}" -string "${value}" "${plist}"
  else
    plutil -insert "${key}" -string "${value}" "${plist}"
  fi
}

set_plist_string CFBundleShortVersionString "${marketing_version}"
set_plist_string CFBundleVersion "${build_number}"
set_plist_string AhoiProductVersion "${product_version}"
set_plist_string AhoiUpdateChannel "${channel}"
set_plist_string AhoiSourceCommit "${source_commit}"
set_plist_string AhoiChromiumVersion "${chromium_version}"
set_plist_string AhoiChromiumCommit "${chromium_commit}"
set_plist_string AhoiGNArgsSHA256 "${args_hash}"
set_plist_string AhoiBuildProfile "${build_profile}"

ahoi_note "stamped product, channel, source, Chromium, build profile and GN provenance into Info.plist"
