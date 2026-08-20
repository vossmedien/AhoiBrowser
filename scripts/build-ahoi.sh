#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"
ahoi_require_build_free_space

profile="${1:-dev}"
case "${profile}" in
  dev)
    args_file="${AHOI_REPO_ROOT}/config/build/ahoi-dev.gn"
    out_name="AhoiDev"
    toolchain_mode="compatible-development"
    hook_xcode_option="--compatible-dev-xcode"
    ;;
  release)
    args_file="${AHOI_REPO_ROOT}/config/build/ahoi-release.gn"
    out_name="AhoiRelease"
    toolchain_mode="pinned-reference"
    hook_xcode_option=""
    ;;
  *) ahoi_die "usage: $0 [dev|release]" ;;
esac

ahoi_select_xcode "${toolchain_mode}"
if [ "${toolchain_mode}" = "compatible-development" ]; then
  "${SCRIPT_DIR}/check-host.sh" --compatible-dev-xcode
else
  "${SCRIPT_DIR}/check-host.sh"
fi

ahoi_require_overlay_state
hook_args=(--allow-source-overlay)
if [ -n "${hook_xcode_option}" ]; then
  hook_args+=("${hook_xcode_option}")
fi
"${SCRIPT_DIR}/run-chromium-hooks.sh" "${hook_args[@]}"
ahoi_require_overlay_state
ahoi_enable_depot_tools
ahoi_require_command gn
ahoi_require_command autoninja

out_dir="${AHOI_CHROMIUM_SRC}/out/${out_name}"
args="$(<"${args_file}")"
target="chrome"

ahoi_note "generating Ahoi ${profile} build"
(
  cd "${AHOI_CHROMIUM_SRC}"
  gn gen "${out_dir}" --args="${args}"
  if [ -n "${AHOI_JOBS:-}" ]; then
    autoninja -C "${out_dir}" -j "${AHOI_JOBS}" "${target}"
  else
    autoninja -C "${out_dir}" "${target}"
  fi
)

ahoi_require_overlay_state

app_path="${out_dir}/AhoiBrowser.app"
[ -d "${app_path}" ] || \
  ahoi_die "branded AhoiBrowser.app was not produced in ${out_dir}"
"${SCRIPT_DIR}/stamp-built-app.sh" "${app_path}" "${args_file}"
"${SCRIPT_DIR}/verify-built-app.sh" "${app_path}"
python3 "${AHOI_REPO_ROOT}/tools/build_provenance.py" \
  --kind "${profile}" \
  --app "${app_path}" \
  --out-dir "${out_dir}" \
  --gn-args "${args_file}" \
  --output "${AHOI_REPO_ROOT}/artifacts/build/ahoi-${profile}-build.json"
ahoi_note "Ahoi ${profile} build complete: ${app_path}"
