#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

toolchain_mode="pinned-reference"
case "$#" in
  0) ;;
  1)
    [ "$1" = "--compatible-dev-xcode" ] || \
      ahoi_die "usage: $0 [--compatible-dev-xcode]"
    toolchain_mode="compatible-development"
    ;;
  *) ahoi_die "usage: $0 [--compatible-dev-xcode]" ;;
esac

ahoi_require_command git
ahoi_require_command python3

[ -d "${AHOI_CHROMIUM_SRC}/.git" ] || ahoi_die "Chromium checkout is missing"
expected_commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" commit)"
actual_commit="$(git -C "${AHOI_CHROMIUM_SRC}" rev-parse HEAD)"
[ "${actual_commit}" = "${expected_commit}" ] || \
  ahoi_die "overlay requires Chromium ${expected_commit}, found ${actual_commit}"

mkdir -p "${AHOI_STATE_DIR}"
state_file="${AHOI_STATE_DIR}/overlay-${expected_commit}.json"

if [ -f "${state_file}" ]; then
  refresh_result="$(
    python3 "${AHOI_REPO_ROOT}/tools/overlay_state.py" refresh \
      --repository "${AHOI_REPO_ROOT}" \
      --checkout "${AHOI_CHROMIUM_SRC}" \
      --state "${state_file}" \
      --expected-commit "${expected_commit}"
  )"
  refresh_status="${refresh_result%% *}"
  case "${refresh_status}" in
    checkout-refreshed)
      # Hooks are checkout-bound evidence. A semantic source refresh must make
      # the old record unusable so the next build reruns them for the new tree.
      ahoi_invalidate_hook_state
      ;;
    state-updated|unchanged) ;;
    *) ahoi_die "unexpected overlay refresh result: ${refresh_result}" ;;
  esac
  ahoi_note "overlay ${refresh_status} and checkout delta verified"
  exit 0
fi

ahoi_require_hook_state "clean" "${toolchain_mode}"
ahoi_require_clean_git_checkout "${AHOI_CHROMIUM_SRC}"
apply_result="$(
  python3 "${AHOI_REPO_ROOT}/tools/overlay_state.py" apply \
    --repository "${AHOI_REPO_ROOT}" \
    --checkout "${AHOI_CHROMIUM_SRC}" \
    --state "${state_file}" \
    --expected-commit "${expected_commit}"
)"
case "${apply_result}" in
  checkout-applied\ *) ;;
  *) ahoi_die "unexpected initial overlay result: ${apply_result}" ;;
esac

ahoi_require_overlay_state
ahoi_note "Ahoi overlay applied atomically: ${apply_result#checkout-applied }"
