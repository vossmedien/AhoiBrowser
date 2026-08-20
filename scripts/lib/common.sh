#!/bin/bash

set -euo pipefail

AHOI_SCRIPT_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AHOI_REPO_ROOT="$(cd "${AHOI_SCRIPT_LIB_DIR}/../.." && pwd)"
AHOI_WORK_ROOT="${AHOI_WORK_ROOT:-${AHOI_REPO_ROOT}/.work}"

case "${AHOI_WORK_ROOT}" in
  /*) ;;
  *)
    echo "AHOI_WORK_ROOT must be an absolute path: ${AHOI_WORK_ROOT}" >&2
    exit 2
    ;;
esac

AHOI_DEPOT_TOOLS_DIR="${AHOI_WORK_ROOT}/depot_tools"
AHOI_CHROMIUM_ROOT="${AHOI_WORK_ROOT}/chromium"
AHOI_CHROMIUM_SRC="${AHOI_CHROMIUM_ROOT}/src"
AHOI_STATE_DIR="${AHOI_WORK_ROOT}/state"
export AHOI_REPO_ROOT AHOI_WORK_ROOT AHOI_DEPOT_TOOLS_DIR
export AHOI_CHROMIUM_ROOT AHOI_CHROMIUM_SRC AHOI_STATE_DIR

ahoi_die() {
  echo "error: $*" >&2
  exit 1
}

ahoi_note() {
  echo "==> $*"
}

ahoi_require_command() {
  command -v "$1" >/dev/null 2>&1 || ahoi_die "required command not found: $1"
}

ahoi_json_get() {
  local file="$1"
  local path="$2"
  python3 - "${file}" "${path}" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    value = json.load(handle)
for component in sys.argv[2].split("."):
    value = value[component]
if isinstance(value, bool):
    print("true" if value else "false")
elif isinstance(value, (dict, list)):
    print(json.dumps(value, separators=(",", ":")))
else:
    print(value)
PY
}

ahoi_existing_parent() {
  local candidate="$1"
  while [ ! -e "${candidate}" ]; do
    candidate="$(dirname "${candidate}")"
  done
  printf '%s\n' "${candidate}"
}

ahoi_free_bytes() {
  local parent
  parent="$(ahoi_existing_parent "$1")"
  df -Pk "${parent}" | awk 'NR == 2 { printf "%.0f\n", $4 * 1024 }'
}

ahoi_require_free_space() {
  local required
  local available
  required="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" host.minimumFreeWorkBytes)"
  available="$(ahoi_free_bytes "${AHOI_WORK_ROOT}")"
  if [ "${available}" -lt "${required}" ]; then
    if [ "${AHOI_ALLOW_LOW_DISK:-0}" = "1" ]; then
      local absolute_floor
      absolute_floor="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" host.absoluteMinimumFreeCheckoutBytes)"
      [ "${available}" -ge "${absolute_floor}" ] || \
        ahoi_die "low-disk override refused below the absolute checkout floor"
      python3 - "${available}" "${required}" <<'PY'
import sys
available, recommended = (int(item) for item in sys.argv[1:])
print(
    "warning: proceeding with explicit low-disk checkout override: "
    f"{available / 2**30:.1f} GiB available, {recommended / 2**30:.1f} GiB recommended",
    file=sys.stderr,
)
PY
      return 0
    fi
    python3 - "${available}" "${required}" <<'PY'
import sys
available, required = (int(item) for item in sys.argv[1:])
print(
    "error: insufficient free space for Chromium checkout/build: "
    f"{available / 2**30:.1f} GiB available, {required / 2**30:.1f} GiB required",
    file=sys.stderr,
)
PY
    exit 2
  fi
}

ahoi_require_build_free_space() {
  local required
  local available
  required="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" host.minimumFreeWorkBytes)"
  available="$(ahoi_free_bytes "${AHOI_WORK_ROOT}")"
  if [ "${available}" -lt "${required}" ]; then
    python3 - "${available}" "${required}" <<'PY'
import sys
available, required = (int(item) for item in sys.argv[1:])
print(
    "error: insufficient free space for a Chromium build: "
    f"{available / 2**30:.1f} GiB available, {required / 2**30:.1f} GiB required; "
    "the checkout-only override is not accepted for builds",
    file=sys.stderr,
)
PY
    exit 2
  fi
}

ahoi_export_depot_tools_environment() {
  case "${AHOI_DEPOT_TOOLS_DIR}" in
    /*) ;;
    *) ahoi_die "depot_tools directory must be absolute: ${AHOI_DEPOT_TOOLS_DIR}" ;;
  esac
  export DEPOT_TOOLS_UPDATE=0
  export DEPOT_TOOLS_DIR="${AHOI_DEPOT_TOOLS_DIR}"
  export PATH="${AHOI_DEPOT_TOOLS_DIR}:${PATH}"
}

ahoi_require_depot_tools_python() {
  local validation_error
  if ! validation_error="$(python3 - "${AHOI_DEPOT_TOOLS_DIR}" <<'PY'
import os
from pathlib import Path
import sys


def fail(message: str) -> None:
    raise SystemExit(message)


configured_root = Path(sys.argv[1])
if not configured_root.is_absolute():
    fail(f"depot_tools root is not absolute: {configured_root}")

try:
    root = configured_root.resolve(strict=True)
except OSError as error:
    fail(f"depot_tools root cannot be resolved: {error}")

pointer = root / "python3_bin_reldir.txt"
if pointer.is_symlink() or not pointer.is_file():
    fail(f"missing regular bootstrap pointer: {pointer}")

try:
    relative_text = pointer.read_text(encoding="utf-8")
except (OSError, UnicodeError) as error:
    fail(f"cannot read bootstrap pointer: {error}")

if not relative_text or relative_text != relative_text.strip():
    fail("bootstrap pointer must contain one non-empty path without surrounding whitespace")
if "\n" in relative_text or "\r" in relative_text:
    fail("bootstrap pointer must contain exactly one path")

relative_path = Path(relative_text)
if relative_path.is_absolute():
    fail(f"bootstrap pointer must be relative: {relative_text}")

try:
    python_directory = (root / relative_path).resolve(strict=True)
    python_binary = (python_directory / "python3").resolve(strict=True)
    python_directory.relative_to(root)
    python_binary.relative_to(root)
except (OSError, ValueError) as error:
    fail(f"bootstrap Python must resolve inside depot_tools: {error}")

if not python_directory.is_dir():
    fail(f"bootstrap Python directory is missing: {python_directory}")
if not python_binary.is_file() or not os.access(python_binary, os.X_OK):
    fail(f"bootstrap Python is not executable: {python_binary}")
PY
)"; then
    ahoi_die "invalid depot_tools Python bootstrap: ${validation_error}"
  fi
}

ahoi_enable_depot_tools() {
  [ -d "${AHOI_DEPOT_TOOLS_DIR}/.git" ] || \
    ahoi_die "depot_tools is not bootstrapped; run scripts/bootstrap-depot-tools.sh"
  local expected_origin
  local expected_commit
  local actual_origin
  local actual_commit
  expected_origin="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/depot-tools.json" source)"
  expected_commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/depot-tools.json" commit)"
  actual_origin="$(git -C "${AHOI_DEPOT_TOOLS_DIR}" remote get-url origin)"
  actual_commit="$(git -C "${AHOI_DEPOT_TOOLS_DIR}" rev-parse HEAD)"
  [ "${actual_origin}" = "${expected_origin}" ] || \
    ahoi_die "unexpected depot_tools origin: ${actual_origin}"
  [ "${actual_commit}" = "${expected_commit}" ] || \
    ahoi_die "depot_tools pin mismatch: expected ${expected_commit}, got ${actual_commit}"
  ahoi_require_clean_git_checkout "${AHOI_DEPOT_TOOLS_DIR}"
  ahoi_export_depot_tools_environment
  ahoi_require_depot_tools_python
}

ahoi_xcode_config_prefix() {
  case "$1" in
    pinned-reference) printf '%s\n' "xcode" ;;
    compatible-development) printf '%s\n' "xcode.compatibleDevelopment" ;;
    *) ahoi_die "unsupported Xcode toolchain mode: $1" ;;
  esac
}

ahoi_expected_xcode_version() {
  local prefix
  prefix="$(ahoi_xcode_config_prefix "$1")"
  if [ "${prefix}" = "xcode" ]; then
    ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" "${prefix}.requiredVersion"
  else
    ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" "${prefix}.version"
  fi
}

ahoi_expected_xcode_build() {
  local prefix
  prefix="$(ahoi_xcode_config_prefix "$1")"
  if [ "${prefix}" = "xcode" ]; then
    ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" "${prefix}.requiredBuild"
  else
    ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" "${prefix}.build"
  fi
}

ahoi_expected_ios_sdk_build() {
  case "$1" in
    pinned-reference)
      ahoi_json_get \
        "${AHOI_REPO_ROOT}/config/toolchain.json" \
        "sdks.iOS.pinnedReferenceBuild"
      ;;
    compatible-development)
      ahoi_json_get \
        "${AHOI_REPO_ROOT}/config/toolchain.json" \
        "sdks.iOS.compatibleDevelopmentBuild"
      ;;
    *) ahoi_die "unsupported Xcode toolchain mode: $1" ;;
  esac
}

ahoi_select_xcode() {
  local mode="$1"
  local prefix
  local configured
  prefix="$(ahoi_xcode_config_prefix "${mode}")"
  configured="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" "${prefix}.developerDirectory")"
  if [ "${mode}" = "pinned-reference" ]; then
    export DEVELOPER_DIR="${AHOI_XCODE_DEVELOPER_DIR:-${configured}}"
  else
    export DEVELOPER_DIR="${AHOI_DEV_XCODE_DEVELOPER_DIR:-${configured}}"
  fi
  [ -d "${DEVELOPER_DIR}" ] || \
    ahoi_die "${mode} Xcode developer directory is missing: ${DEVELOPER_DIR}"
}

ahoi_select_pinned_xcode() {
  ahoi_select_xcode "pinned-reference"
}

ahoi_select_compatible_dev_xcode() {
  ahoi_select_xcode "compatible-development"
}

ahoi_require_clean_git_checkout() {
  local checkout="$1"
  [ -d "${checkout}/.git" ] || ahoi_die "not a Git checkout: ${checkout}"
  if [ -n "$(git -C "${checkout}" status --porcelain)" ]; then
    ahoi_die "checkout has modifications or untracked files and will not be changed: ${checkout}"
  fi
}

ahoi_sha256() {
  shasum -a 256 "$1" | awk '{print $1}'
}

ahoi_require_gclient_config() {
  local expected="${AHOI_REPO_ROOT}/config/gclient.py"
  local actual="${AHOI_CHROMIUM_ROOT}/.gclient"
  [ -f "${actual}" ] || ahoi_die "canonical Chromium .gclient is missing"
  cmp -s "${expected}" "${actual}" || \
    ahoi_die "Chromium .gclient differs from config/gclient.py"
}

ahoi_hook_state_file() {
  local expected_commit
  expected_commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" commit)"
  printf '%s\n' "${AHOI_STATE_DIR}/hooks-${expected_commit}.json"
}

ahoi_hook_artifact_file() {
  printf '%s\n' "${AHOI_REPO_ROOT}/artifacts/build/chromium-hooks.json"
}

ahoi_invalidate_hook_state() {
  # A state record is evidence from one completed run, never permission to skip
  # the next run. Remove both copies before any operation that may change the
  # checkout or before starting a fresh hook execution.
  rm -f -- "$(ahoi_hook_state_file)" "$(ahoi_hook_artifact_file)"
}

ahoi_require_hook_state() {
  local expected_mode="${1:-clean}"
  local expected_toolchain_mode="${2:-pinned-reference}"
  local expected_commit
  local state_file
  local expected_deps_hash
  local recorded_deps_hash
  local expected_xcode
  local expected_xcode_build
  local expected_ios_sdk_build
  local actual_delta
  local recorded_delta
  case "${expected_mode}" in
    clean|overlay) ;;
    *) ahoi_die "unsupported Chromium hook checkout mode: ${expected_mode}" ;;
  esac
  ahoi_xcode_config_prefix "${expected_toolchain_mode}" >/dev/null
  expected_commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" commit)"
  state_file="$(ahoi_hook_state_file)"
  [ -f "${state_file}" ] || \
    ahoi_die "pinned Chromium hooks have not run; use scripts/run-chromium-hooks.sh"
  [ "$(ahoi_json_get "${state_file}" schemaVersion)" = "3" ] || \
    ahoi_die "unsupported or stale Chromium hook state schema"
  [ "$(ahoi_json_get "${state_file}" chromiumCommit)" = "${expected_commit}" ] || \
    ahoi_die "hook state Chromium commit mismatch"
  [ "$(ahoi_json_get "${state_file}" checkoutMode)" = "${expected_mode}" ] || \
    ahoi_die "hook state checkout mode mismatch"
  [ "$(ahoi_json_get "${state_file}" toolchainMode)" = "${expected_toolchain_mode}" ] || \
    ahoi_die "hook state Xcode toolchain mode mismatch"
  expected_deps_hash="$(ahoi_sha256 "${AHOI_CHROMIUM_SRC}/DEPS")"
  recorded_deps_hash="$(ahoi_json_get "${state_file}" depsSha256)"
  [ "${recorded_deps_hash}" = "${expected_deps_hash}" ] || \
    ahoi_die "Chromium DEPS changed after the pinned hook run"
  actual_delta="$(ahoi_checkout_delta_fingerprint "${AHOI_CHROMIUM_SRC}")"
  recorded_delta="$(ahoi_json_get "${state_file}" checkoutDeltaFingerprint)"
  [ "${recorded_delta}" = "${actual_delta}" ] || \
    ahoi_die "Chromium checkout changed after the pinned hook run"
  expected_xcode="$(ahoi_expected_xcode_version "${expected_toolchain_mode}")"
  expected_xcode_build="$(ahoi_expected_xcode_build "${expected_toolchain_mode}")"
  expected_ios_sdk_build="$(ahoi_expected_ios_sdk_build "${expected_toolchain_mode}")"
  [ "$(ahoi_json_get "${state_file}" xcodeVersion)" = "${expected_xcode}" ] || \
    ahoi_die "hook state Xcode version mismatch"
  [ "$(ahoi_json_get "${state_file}" xcodeBuild)" = "${expected_xcode_build}" ] || \
    ahoi_die "hook state Xcode build mismatch"
  [ "$(ahoi_json_get "${state_file}" macOSSDKVersion)" = \
    "$(ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" sdks.macOS.testedVersion)" ] || \
    ahoi_die "hook state macOS SDK version mismatch"
  [ "$(ahoi_json_get "${state_file}" macOSSDKBuild)" = \
    "$(ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" sdks.macOS.chromiumOfficialBuild)" ] || \
    ahoi_die "hook state macOS SDK build mismatch"
  [ "$(ahoi_json_get "${state_file}" iOSSDKVersion)" = \
    "$(ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" sdks.iOS.testedVersion)" ] || \
    ahoi_die "hook state iOS SDK version mismatch"
  [ "$(ahoi_json_get "${state_file}" iOSSDKBuild)" = \
    "${expected_ios_sdk_build}" ] || \
    ahoi_die "hook state iOS SDK build mismatch"
}

ahoi_overlay_inputs_fingerprint() {
  python3 "${AHOI_REPO_ROOT}/tools/overlay_fingerprint.py" \
    --repository "${AHOI_REPO_ROOT}"
}

ahoi_checkout_delta_fingerprint() {
  local checkout="$1"
  python3 - "${checkout}" <<'PY'
import hashlib
import os
import pathlib
import subprocess
import sys

root = pathlib.Path(sys.argv[1])
digest = hashlib.sha256()
diff = subprocess.run(
    ["git", "-C", str(root), "diff", "--binary", "--no-ext-diff", "HEAD", "--"],
    check=True,
    capture_output=True,
).stdout
digest.update(b"tracked\0")
digest.update(diff)
untracked = subprocess.run(
    ["git", "-C", str(root), "ls-files", "--others", "--exclude-standard", "-z"],
    check=True,
    capture_output=True,
).stdout.split(b"\0")
for encoded in sorted(item for item in untracked if item):
    relative = encoded.decode("utf-8", "surrogateescape")
    path = root / relative
    digest.update(b"untracked\0")
    digest.update(encoded)
    digest.update(b"\0")
    digest.update(str(os.lstat(path).st_mode).encode("ascii"))
    digest.update(b"\0")
    if path.is_symlink():
        digest.update(os.readlink(path).encode("utf-8", "surrogateescape"))
    else:
        digest.update(path.read_bytes())
    digest.update(b"\0")
print(digest.hexdigest())
PY
}

ahoi_expected_overlay_delta_fingerprint() {
  local checkout="$1"
  local expected_commit
  expected_commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" commit)"
  python3 "${AHOI_REPO_ROOT}/tools/overlay_state.py" expected-fingerprint \
    --repository "${AHOI_REPO_ROOT}" \
    --checkout "${checkout}" \
    --expected-commit "${expected_commit}"
}

ahoi_require_overlay_state() {
  local expected_commit
  local state_file
  expected_commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" commit)"
  state_file="${AHOI_STATE_DIR}/overlay-${expected_commit}.json"
  python3 "${AHOI_REPO_ROOT}/tools/overlay_state.py" verify \
    --repository "${AHOI_REPO_ROOT}" \
    --checkout "${AHOI_CHROMIUM_SRC}" \
    --state "${state_file}" \
    --expected-commit "${expected_commit}" >/dev/null
}
