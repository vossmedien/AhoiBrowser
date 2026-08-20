#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

[ -d "${AHOI_CHROMIUM_SRC}/.git" ] || ahoi_die "Chromium checkout is missing"
ahoi_enable_depot_tools
ahoi_require_gclient_config
expected="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" commit)"
actual="$(git -C "${AHOI_CHROMIUM_SRC}" rev-parse HEAD)"
[ "${actual}" = "${expected}" ] || ahoi_die "expected ${expected}, found ${actual}"
ahoi_require_clean_git_checkout "${AHOI_CHROMIUM_SRC}"
python3 "${AHOI_REPO_ROOT}/tools/chromium_dependencies.py" \
  --expected-commit "${expected}" \
  --output "${AHOI_REPO_ROOT}/artifacts/build/chromium-dependencies.json"
ahoi_note "unmodified Chromium checkout and actual child revisions verified at ${actual}"
