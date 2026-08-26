#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

[ "$#" -eq 0 ] || ahoi_die "usage: $0"
ahoi_require_command git
ahoi_require_command python3

[ -d "${AHOI_CHROMIUM_SRC}/.git" ] || ahoi_die "Chromium checkout is missing"
expected_commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" commit)"
actual_commit="$(git -C "${AHOI_CHROMIUM_SRC}" rev-parse HEAD)"
[ "${actual_commit}" = "${expected_commit}" ] || \
  ahoi_die "overlay restore requires Chromium ${expected_commit}, found ${actual_commit}"
state_file="${AHOI_STATE_DIR}/overlay-${expected_commit}.json"

restore_result="$(
  python3 "${AHOI_REPO_ROOT}/tools/overlay_state.py" restore \
    --repository "${AHOI_REPO_ROOT}" \
    --checkout "${AHOI_CHROMIUM_SRC}" \
    --state "${state_file}" \
    --expected-commit "${expected_commit}"
)"
case "${restore_result}" in
  checkout-restored\ *) ;;
  *) ahoi_die "unexpected overlay restore result: ${restore_result}" ;;
esac

# Hook evidence is bound to the former overlay tree and cannot survive its
# exact restoration to the pinned upstream tree.
ahoi_invalidate_hook_state
ahoi_require_clean_git_checkout "${AHOI_CHROMIUM_SRC}"
ahoi_note "Ahoi overlay restored to pinned Chromium: ${expected_commit}"
