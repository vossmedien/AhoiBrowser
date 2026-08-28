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
ahoi_require_command codesign
ahoi_require_command file
ahoi_require_command find
ahoi_require_command lipo
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
sparkle_version="$(plutil -extract AhoiSparkleVersion raw "${plist}")"
sparkle_feed_configured="$(plutil -extract AhoiSparkleFeedConfigured raw "${plist}")"
binary="${app_path}/Contents/MacOS/${executable}"
expected_identifier="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/product.json" bundleId)"
expected_version="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" marketingVersion)"
expected_build_number="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" buildNumber)"
expected_product_version="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" displayVersion)"
expected_channel="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" channel)"
expected_source_commit="$(git -C "${AHOI_REPO_ROOT}" rev-parse HEAD)"
expected_chromium_version="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" version)"
expected_chromium_commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" commit)"
expected_sparkle_version="$(python3 - "${AHOI_REPO_ROOT}/config/third-party-pins.json" <<'PY'
import json
import pathlib
import sys
print(json.loads(pathlib.Path(sys.argv[1]).read_text())["dependencies"]["sparkle"]["version"])
PY
)"
case "${build_profile}" in
  dev) expected_args_file="${AHOI_REPO_ROOT}/config/build/ahoi-dev.gn" ;;
  full-dev) expected_args_file="${AHOI_REPO_ROOT}/config/build/ahoi-full-dev.gn" ;;
  release) expected_args_file="${AHOI_REPO_ROOT}/config/build/ahoi-release.gn" ;;
  full-release) expected_args_file="${AHOI_REPO_ROOT}/config/build/ahoi-full-release.gn" ;;
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
[ "${sparkle_version}" = "${expected_sparkle_version}" ] || \
  ahoi_die "unexpected Sparkle pin stamp: ${sparkle_version}"
[ "$(plutil -extract SURequireSignedFeed raw "${plist}")" = "true" ] || \
  ahoi_die "Sparkle signed-feed enforcement is disabled"
[ "$(plutil -extract SUVerifyUpdateBeforeExtraction raw "${plist}")" = "true" ] || \
  ahoi_die "Sparkle verification-before-extraction is disabled"
[ "$(plutil -extract SUSendProfileInfo raw "${plist}")" = "false" ] || \
  ahoi_die "Sparkle system-profile submission must remain disabled"
if [ "${build_profile}" = "release" ] || \
  [ "${build_profile}" = "full-release" ]; then
  [ "${sparkle_feed_configured}" = "true" ] || \
    ahoi_die "release bundle has no reviewed Sparkle feed/key configuration"
  sparkle_feed_url="$(plutil -extract SUFeedURL raw "${plist}")"
  sparkle_public_key="$(plutil -extract SUPublicEDKey raw "${plist}")"
  AHOI_RELEASE_POLICY="${AHOI_REPO_ROOT}/config/release-policy.json" \
    AHOI_RELEASE_CHANNEL="${channel}" \
    AHOI_SPARKLE_FEED_URL="${sparkle_feed_url}" \
    AHOI_SPARKLE_PUBLIC_ED_KEY="${sparkle_public_key}" \
    python3 - <<'PY'
import json
import os
import pathlib

policy = json.loads(pathlib.Path(os.environ["AHOI_RELEASE_POLICY"]).read_text())
configuration = policy["updates"]["channels"][os.environ["AHOI_RELEASE_CHANNEL"]]
if configuration.get("feedUrl") != os.environ["AHOI_SPARKLE_FEED_URL"]:
    raise SystemExit("bundled Sparkle feed URL differs from reviewed policy")
if configuration.get("publicEdKey") != os.environ["AHOI_SPARKLE_PUBLIC_ED_KEY"]:
    raise SystemExit("bundled Sparkle public key differs from reviewed policy")
if not configuration.get("artifactBaseUrl"):
    raise SystemExit("reviewed Sparkle artifact base URL is missing")
PY
fi

sparkle_framework="${app_path}/Contents/Frameworks/Sparkle.framework"
[ -d "${sparkle_framework}" ] || ahoi_die "bundled Sparkle.framework missing"
bundled_sparkle_version="$(plutil -extract CFBundleShortVersionString raw \
  "${sparkle_framework}/Resources/Info.plist")"
[ "${bundled_sparkle_version}" = "${expected_sparkle_version}" ] || \
  ahoi_die "bundled Sparkle framework version mismatch"
sparkle_required_code=(
  "${sparkle_framework}/Versions/B/Sparkle"
  "${sparkle_framework}/Versions/B/Autoupdate"
  "${sparkle_framework}/Versions/B/Updater.app/Contents/MacOS/Updater"
  "${sparkle_framework}/Versions/B/XPCServices/Downloader.xpc/Contents/MacOS/Downloader"
  "${sparkle_framework}/Versions/B/XPCServices/Installer.xpc/Contents/MacOS/Installer"
)
for sparkle_required in "${sparkle_required_code[@]}"; do
  [ -f "${sparkle_required}" ] && [ -x "${sparkle_required}" ] || \
    ahoi_die "required Sparkle code is missing: ${sparkle_required}"
done
while IFS= read -r -d '' sparkle_candidate; do
  case "$(file -b "${sparkle_candidate}")" in
    *Mach-O*)
      [ "$(lipo -archs "${sparkle_candidate}")" = "arm64" ] || \
        ahoi_die "bundled Sparkle code must be arm64-only: ${sparkle_candidate}"
      ;;
  esac
done < <(find "${sparkle_framework}" -type f -print0)
codesign --verify --deep --strict "${sparkle_framework}" || \
  ahoi_die "bundled Sparkle code signature is invalid"
[ -x "${binary}" ] || ahoi_die "main executable missing: ${binary}"
architecture="$(file "${binary}")"
echo "${architecture}" | grep -q 'arm64' || ahoi_die "main executable is not ARM64"
echo "${architecture}" | grep -q 'x86_64' && ahoi_die "unexpected x86_64 slice" || true

if [ "${build_profile}" = "dev" ] || \
  [ "${build_profile}" = "full-dev" ]; then
  component_runtime_dir="${app_path}/Contents/Frameworks"
  component_manifest="${app_path}/Contents/Resources/ahoi-component-runtime.sha256"
  component_framework="${component_runtime_dir}/AhoiBrowser Framework.framework"
  framework_resource_manifest="${app_path}/Contents/Resources/ahoi-component-framework-resources.sha256"
  [ -f "${component_runtime_dir}/libc++_chrome.dylib" ] || \
    ahoi_die "portable development bundle is missing libc++_chrome.dylib"
  [ -s "${component_manifest}" ] || \
    ahoi_die "portable development bundle is missing its component runtime manifest"
  component_manifest_count="$(wc -l <"${component_manifest}" | tr -d ' ')"
  component_dylib_count="$(find "${component_runtime_dir}" -maxdepth 1 \
    -type f -name '*.dylib' | wc -l | tr -d ' ')"
  [ "${component_manifest_count}" = "${component_dylib_count}" ] || \
    ahoi_die "component runtime manifest and staged dylib counts differ"
  (
    cd "${component_runtime_dir}"
    shasum -a 256 -s -c "${component_manifest}"
  ) || ahoi_die "a staged component runtime dylib differs from its build output"
  [ -L "${component_framework}/Versions/Current" ] || \
    ahoi_die "portable development bundle has no current component framework"
  framework_current_version="$(readlink "${component_framework}/Versions/Current")"
  [ -s "${framework_resource_manifest}" ] || \
    ahoi_die "portable development bundle is missing its framework resource manifest"
  framework_manifest_count="$(wc -l <"${framework_resource_manifest}" | tr -d ' ')"
  framework_resource_count="$(find \
    "${component_framework}/Versions/${framework_current_version}/Resources" \
    -type f | wc -l | tr -d ' ')"
  [ "${framework_manifest_count}" = "${framework_resource_count}" ] || \
    ahoi_die "component framework resource manifest and file counts differ"
  (
    cd "${component_framework}"
    shasum -a 256 -s -c "${framework_resource_manifest}"
  ) || ahoi_die "a staged component framework resource differs from its build output"
  codesign --verify --deep --strict "${app_path}" || \
    ahoi_die "portable development bundle has an invalid development code signature"
  ahoi_note "portable component runtime: ${component_dylib_count} verified dylibs; ${framework_resource_count} verified framework resources"
fi

ahoi_note "bundle: ${name} ${version} (${identifier})"
ahoi_note "Ahoi product/build/channel: ${product_version} (${build_number}, ${channel})"
ahoi_note "Ahoi source: ${source_commit}"
ahoi_note "Chromium: ${chromium_version} (${chromium_commit}); ${build_profile} GN args: ${gn_args_hash}"
ahoi_note "Sparkle: ${sparkle_version}; reviewed feed configured: ${sparkle_feed_configured}"
ahoi_note "binary: ${architecture}"
ahoi_note "binary SHA-256: $(ahoi_sha256 "${binary}")"
