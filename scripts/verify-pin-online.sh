#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

ahoi_require_command curl
ahoi_require_command python3

config="${AHOI_REPO_ROOT}/config/chromium.json"
commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" commit)"
version="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" version)"
branch_point="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" branchPoint)"
release_api="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" releaseApi)"
base="https://chromium.googlesource.com/chromium/src/+/${commit}"

temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/ahoi-pin-verification.XXXXXX")"
cleanup() {
  rm -rf "${temp_dir}"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

python3 "${AHOI_REPO_ROOT}/tools/verify_chromium_pin.py" \
  --config "${config}" --validate-config-only

curl_official() {
  curl --fail --silent --show-error \
    --connect-timeout 10 --max-time 45 \
    --retry 2 --retry-delay 1 --retry-max-time 120 --retry-all-errors "$@"
}

curl_official \
  "https://chromium.googlesource.com/chromium/src/+show/refs/tags/${version}?format=JSON" \
  >"${temp_dir}/tag-ref.json"
curl_official \
  "https://chromium.googlesource.com/chromium/src/+show/refs/branch-heads/$(ahoi_json_get "${config}" branchHead)?format=JSON" \
  >"${temp_dir}/branch-ref.json"
python3 "${AHOI_REPO_ROOT}/tools/verify_chromium_pin.py" \
  --config "${config}" \
  --tag-ref-json "${temp_dir}/tag-ref.json" \
  --branch-ref-json "${temp_dir}/branch-ref.json" \
  --resolve-gitiles-refs "${temp_dir}/remote-refs.txt"

curl_official \
  "${base}?format=JSON" >"${temp_dir}/commit.json"
curl_official \
  "https://chromium.googlesource.com/chromium/src/+/${branch_point}?format=JSON" \
  >"${temp_dir}/branch-point.json"
curl_official \
  "${base}/chrome/VERSION?format=TEXT" >"${temp_dir}/version.txt"
curl_official \
  "${release_api}" >"${temp_dir}/release.json"

python3 "${AHOI_REPO_ROOT}/tools/verify_chromium_pin.py" \
  --config "${config}" \
  --remote-refs "${temp_dir}/remote-refs.txt" \
  --commit-json "${temp_dir}/commit.json" \
  --branch-point-json "${temp_dir}/branch-point.json" \
  --version-text "${temp_dir}/version.txt" \
  --release-json "${temp_dir}/release.json"

depot_commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/depot-tools.json" commit)"
curl_official \
  "https://chromium.googlesource.com/chromium/tools/depot_tools/+/${depot_commit}?format=JSON" \
  >/dev/null

ahoi_note \
  "official tag, commit, branch metadata, and release feed verify Chromium ${version} at ${commit}"
ahoi_note "official depot_tools source verifies ${depot_commit}"
