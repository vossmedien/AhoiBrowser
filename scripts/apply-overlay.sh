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
ahoi_require_command shasum

[ -d "${AHOI_CHROMIUM_SRC}/.git" ] || ahoi_die "Chromium checkout is missing"
ahoi_require_hook_state "clean" "${toolchain_mode}"
expected_commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" commit)"
actual_commit="$(git -C "${AHOI_CHROMIUM_SRC}" rev-parse HEAD)"
[ "${actual_commit}" = "${expected_commit}" ] || \
  ahoi_die "overlay requires Chromium ${expected_commit}, found ${actual_commit}"

mkdir -p "${AHOI_STATE_DIR}"
fingerprint="$(ahoi_overlay_inputs_fingerprint)"
state_file="${AHOI_STATE_DIR}/overlay-${expected_commit}.json"

if [ -f "${state_file}" ]; then
  ahoi_require_overlay_state
  ahoi_note "overlay already applied and checkout delta verified"
  exit 0
fi

ahoi_require_clean_git_checkout "${AHOI_CHROMIUM_SRC}"
series="${AHOI_REPO_ROOT}/patches/chromium/series"
compose_dir="$(mktemp -d "${AHOI_STATE_DIR}/overlay-compose.XXXXXX")"
combined_patch="${compose_dir}/combined.patch"
cleanup_compose_dir() {
  rm -rf -- "${compose_dir}"
}
trap cleanup_compose_dir EXIT

# The helper builds overlay files plus the complete ordered series in a
# temporary Git index. The Chromium checkout is untouched until the full
# composition and the final whole-delta preflight have succeeded.
python3 "${AHOI_REPO_ROOT}/tools/compose_overlay.py" \
  --checkout "${AHOI_CHROMIUM_SRC}" \
  --overlay "${AHOI_REPO_ROOT}/overlay/chromium/src" \
  --series "${series}" \
  --patch-root "${AHOI_REPO_ROOT}/patches/chromium" \
  --output "${combined_patch}"

[ "$(git -C "${AHOI_CHROMIUM_SRC}" rev-parse HEAD)" = "${expected_commit}" ] || \
  ahoi_die "Chromium HEAD changed while composing the overlay"
ahoi_require_clean_git_checkout "${AHOI_CHROMIUM_SRC}"
git -C "${AHOI_CHROMIUM_SRC}" apply --check --whitespace=error-all \
  "${combined_patch}"
ahoi_require_clean_git_checkout "${AHOI_CHROMIUM_SRC}"
git -C "${AHOI_CHROMIUM_SRC}" apply --whitespace=error-all "${combined_patch}"

checkout_delta_fingerprint="$(
  ahoi_expected_overlay_delta_fingerprint "${AHOI_CHROMIUM_SRC}"
)"

python3 - "${state_file}" "${fingerprint}" "${checkout_delta_fingerprint}" \
  "${expected_commit}" <<'PY'
import datetime
import json
import sys

path, fingerprint, checkout_delta_fingerprint, commit = sys.argv[1:]
payload = {
    "schemaVersion": 2,
    "fingerprint": fingerprint,
    "checkoutDeltaFingerprint": checkout_delta_fingerprint,
    "chromiumCommit": commit,
    "appliedAt": datetime.datetime.now(datetime.timezone.utc).isoformat(),
}
with open(path, "w", encoding="utf-8") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
PY

ahoi_require_overlay_state
ahoi_note "Ahoi overlay applied: ${fingerprint}"
