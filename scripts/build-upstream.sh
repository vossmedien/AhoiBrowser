#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

ahoi_require_build_free_space
ahoi_select_pinned_xcode
"${SCRIPT_DIR}/check-host.sh"
"${SCRIPT_DIR}/run-chromium-hooks.sh"
"${SCRIPT_DIR}/verify-upstream.sh"
ahoi_enable_depot_tools
ahoi_require_command gn
ahoi_require_command autoninja

out_dir="${AHOI_CHROMIUM_SRC}/out/AhoiUpstreamRelease"
args_file="${AHOI_REPO_ROOT}/config/build/upstream-release.gn"

ahoi_note "generating unmodified Chromium build in ${out_dir}"
"${SCRIPT_DIR}/build-chromium-with-dependency-workarounds.sh" \
  "${out_dir}" "${args_file}" chrome

"${SCRIPT_DIR}/verify-upstream.sh"

app_path="${out_dir}/Chromium.app"
[ -d "${app_path}" ] || ahoi_die "upstream app not found: ${app_path}"
binary="${app_path}/Contents/MacOS/Chromium"
[ -x "${binary}" ] || ahoi_die "upstream browser binary is missing"
file "${binary}" | grep -q 'arm64' || ahoi_die "upstream browser is not ARM64"
if file "${binary}" | grep -q 'x86_64'; then
  ahoi_die "upstream browser unexpectedly contains an x86_64 slice"
fi

mkdir -p "${AHOI_REPO_ROOT}/artifacts/build"
python3 "${AHOI_REPO_ROOT}/tools/build_provenance.py" \
  --kind upstream \
  --app "${app_path}" \
  --out-dir "${out_dir}" \
  --gn-args "${args_file}" \
  --output "${AHOI_REPO_ROOT}/artifacts/build/upstream-build.json"

ahoi_note "unmodified Chromium ARM64 control build complete"
