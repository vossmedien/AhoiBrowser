#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

ahoi_require_command git
ahoi_require_command python3

source_url="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/depot-tools.json" source)"
pinned_commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/depot-tools.json" commit)"
mkdir -p "${AHOI_WORK_ROOT}"

if [ ! -d "${AHOI_DEPOT_TOOLS_DIR}/.git" ]; then
  ahoi_note "cloning pinned depot_tools into ${AHOI_DEPOT_TOOLS_DIR}"
  git clone --filter=blob:none --no-checkout "${source_url}" "${AHOI_DEPOT_TOOLS_DIR}"
else
  actual_origin="$(git -C "${AHOI_DEPOT_TOOLS_DIR}" remote get-url origin)"
  [ "${actual_origin}" = "${source_url}" ] || \
    ahoi_die "unexpected depot_tools origin: ${actual_origin}"
  ahoi_require_clean_git_checkout "${AHOI_DEPOT_TOOLS_DIR}"
fi

if ! git -C "${AHOI_DEPOT_TOOLS_DIR}" cat-file -e "${pinned_commit}^{commit}" 2>/dev/null; then
  ahoi_note "fetching depot_tools commit ${pinned_commit}"
  git -C "${AHOI_DEPOT_TOOLS_DIR}" fetch --filter=blob:none origin "${pinned_commit}"
fi

git -C "${AHOI_DEPOT_TOOLS_DIR}" checkout --detach "${pinned_commit}"
(
  cd "${AHOI_DEPOT_TOOLS_DIR}"
  ./update_depot_tools_toggle.py --disable
)

ahoi_export_depot_tools_environment
"${AHOI_DEPOT_TOOLS_DIR}/ensure_bootstrap"
ahoi_enable_depot_tools

actual_commit="$(git -C "${AHOI_DEPOT_TOOLS_DIR}" rev-parse HEAD)"
[ "${actual_commit}" = "${pinned_commit}" ] || ahoi_die "depot_tools pin verification failed"
ahoi_note "depot_tools ready at ${actual_commit}"
gclient metrics --opt-out >/dev/null
gclient --help >/dev/null
