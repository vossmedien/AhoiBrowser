#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

app_path="${1:-}"
[ -n "${app_path}" ] || ahoi_die "usage: $0 /absolute/path/AhoiBrowser.app"
case "${app_path}" in
  /*) ;;
  *) ahoi_die "app path must be absolute" ;;
esac
[ -d "${app_path}" ] || ahoi_die "app bundle not found: ${app_path}"

ahoi_require_command plutil
ahoi_require_command file
ahoi_require_command shasum

plist="${app_path}/Contents/Info.plist"
[ -f "${plist}" ] || ahoi_die "Info.plist missing"
name="$(plutil -extract CFBundleName raw "${plist}")"
identifier="$(plutil -extract CFBundleIdentifier raw "${plist}")"
executable="$(plutil -extract CFBundleExecutable raw "${plist}")"
version="$(plutil -extract CFBundleShortVersionString raw "${plist}")"
build_number="$(plutil -extract CFBundleVersion raw "${plist}")"
product_version="$(plutil -extract AhoiProductVersion raw "${plist}")"
channel="$(plutil -extract AhoiUpdateChannel raw "${plist}")"
source_commit="$(plutil -extract AhoiSourceCommit raw "${plist}")"
chromium_version="$(plutil -extract AhoiChromiumVersion raw "${plist}")"
chromium_commit="$(plutil -extract AhoiChromiumCommit raw "${plist}")"
gn_args_hash="$(plutil -extract AhoiGNArgsSHA256 raw "${plist}")"
build_profile="$(plutil -extract AhoiBuildProfile raw "${plist}")"
binary="${app_path}/Contents/MacOS/${executable}"
expected_identifier="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/product.json" bundleId)"
expected_version="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" marketingVersion)"
expected_build_number="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" buildNumber)"
expected_product_version="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" displayVersion)"
expected_channel="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" channel)"
expected_source_commit="$(git -C "${AHOI_REPO_ROOT}" rev-parse HEAD)"
expected_chromium_version="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" version)"
expected_chromium_commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" commit)"
case "${build_profile}" in
  dev) expected_args_file="${AHOI_REPO_ROOT}/config/build/ahoi-dev.gn" ;;
  release) expected_args_file="${AHOI_REPO_ROOT}/config/build/ahoi-release.gn" ;;
  *) ahoi_die "unexpected Ahoi build profile: ${build_profile}" ;;
esac
expected_gn_args_hash="$(ahoi_sha256 "${expected_args_file}")"

[ "${name}" = "AhoiBrowser" ] || ahoi_die "unexpected CFBundleName: ${name}"
[ "${identifier}" = "${expected_identifier}" ] || \
  ahoi_die "unexpected bundle ID: ${identifier}"
[ "${version}" = "${expected_version}" ] || \
  ahoi_die "unexpected marketing version: ${version}"
[ "${build_number}" = "${expected_build_number}" ] || \
  ahoi_die "unexpected build number: ${build_number}"
[ "${product_version}" = "${expected_product_version}" ] || \
  ahoi_die "unexpected Ahoi product version: ${product_version}"
[ "${channel}" = "${expected_channel}" ] || \
  ahoi_die "unexpected update channel: ${channel}"
[ "${source_commit}" = "${expected_source_commit}" ] || \
  ahoi_die "unexpected Ahoi source commit: ${source_commit}"
[ "${chromium_version}" = "${expected_chromium_version}" ] || \
  ahoi_die "unexpected Chromium version stamp: ${chromium_version}"
[ "${chromium_commit}" = "${expected_chromium_commit}" ] || \
  ahoi_die "unexpected Chromium commit stamp: ${chromium_commit}"
[ "${gn_args_hash}" = "${expected_gn_args_hash}" ] || \
  ahoi_die "GN args hash does not match the stamped ${build_profile} profile"
[ -x "${binary}" ] || ahoi_die "main executable missing: ${binary}"
architecture="$(file "${binary}")"
echo "${architecture}" | grep -q 'arm64' || ahoi_die "main executable is not ARM64"
echo "${architecture}" | grep -q 'x86_64' && ahoi_die "unexpected x86_64 slice" || true

ahoi_note "bundle: ${name} ${version} (${identifier})"
ahoi_note "Ahoi product/build/channel: ${product_version} (${build_number}, ${channel})"
ahoi_note "Ahoi source: ${source_commit}"
ahoi_note "Chromium: ${chromium_version} (${chromium_commit}); ${build_profile} GN args: ${gn_args_hash}"
ahoi_note "binary: ${architecture}"
ahoi_note "binary SHA-256: $(ahoi_sha256 "${binary}")"
