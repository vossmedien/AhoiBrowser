#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

xcode_mode="pinned-reference"
case "$#" in
  0) ;;
  1)
    [ "$1" = "--compatible-dev-xcode" ] || \
      ahoi_die "usage: $0 [--compatible-dev-xcode]"
    xcode_mode="compatible-development"
    ;;
  *) ahoi_die "usage: $0 [--compatible-dev-xcode]" ;;
esac

ahoi_require_command uname
ahoi_require_command sw_vers
ahoi_require_command sysctl
ahoi_require_command xcodebuild
ahoi_require_command xcrun
ahoi_require_command git
ahoi_require_command python3
ahoi_require_command curl
ahoi_require_command shasum
ahoi_require_command diskutil
ahoi_require_command defaults
ahoi_select_xcode "${xcode_mode}"

minimum_python="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" python.minimumVersion)"
python3 - "${minimum_python}" <<'PY'
import re
import sys

minimum = sys.argv[1]
if not re.fullmatch(r"\d+\.\d+", minimum):
    raise SystemExit(f"invalid configured Python minimum: {minimum}")
required = tuple(int(component) for component in minimum.split("."))
if sys.version_info[:2] < required:
    raise SystemExit(
        f"Python {sys.version_info.major}.{sys.version_info.minor} is below {minimum}"
    )
PY

expected_arch="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" host.architecture)"
actual_arch="$(uname -m)"
[ "${actual_arch}" = "${expected_arch}" ] || \
  ahoi_die "unsupported architecture: expected ${expected_arch}, got ${actual_arch}"

minimum_os="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" host.minimumVersion)"
actual_os="$(sw_vers -productVersion)"
python3 - "${actual_os}" "${minimum_os}" <<'PY'
import re
import sys

def version(value: str) -> tuple[int, ...]:
    if not re.fullmatch(r"\d+(?:\.\d+)*", value):
        raise SystemExit(f"invalid macOS version: {value}")
    return tuple(int(component) for component in value.split("."))

actual, minimum = map(version, sys.argv[1:])
width = max(len(actual), len(minimum))
if actual + (0,) * (width - len(actual)) < minimum + (0,) * (width - len(minimum)):
    raise SystemExit(
        f"macOS {sys.argv[1]} is below the required minimum {sys.argv[2]}"
    )
PY

expected_sdk="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" sdks.macOS.testedVersion)"
actual_sdk="$(xcrun --sdk macosx --show-sdk-version)"
[ "${actual_sdk}" = "${expected_sdk}" ] || \
  ahoi_die "macOS SDK mismatch: expected ${expected_sdk}, got ${actual_sdk}"
sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
expected_sdk_build="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" sdks.macOS.chromiumOfficialBuild)"
actual_sdk_build="$(defaults read "${sdk_path}/System/Library/CoreServices/SystemVersion" ProductBuildVersion)"
[ "${actual_sdk_build}" = "${expected_sdk_build}" ] || \
  ahoi_die "macOS SDK build mismatch: expected ${expected_sdk_build}, got ${actual_sdk_build}"

expected_ios_sdk="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" sdks.iOS.testedVersion)"
actual_ios_sdk="$(xcrun --sdk iphoneos --show-sdk-version)"
[ "${actual_ios_sdk}" = "${expected_ios_sdk}" ] || \
  ahoi_die "iOS SDK mismatch: expected ${expected_ios_sdk}, got ${actual_ios_sdk}"
expected_ios_sdk_build="$(ahoi_expected_ios_sdk_build "${xcode_mode}")"
actual_ios_sdk_build="$(xcrun --sdk iphoneos --show-sdk-build-version)"
[ "${actual_ios_sdk_build}" = "${expected_ios_sdk_build}" ] || \
  ahoi_die "iOS SDK build mismatch: expected ${expected_ios_sdk_build}, got ${actual_ios_sdk_build}"

expected_xcode="$(ahoi_expected_xcode_version "${xcode_mode}")"
expected_xcode_build="$(ahoi_expected_xcode_build "${xcode_mode}")"
actual_xcode="$(xcodebuild -version | awk 'NR == 1 {print $2}')"
actual_xcode_build="$(xcodebuild -version | awk 'NR == 2 {print $3}')"
[ "${actual_xcode}" = "${expected_xcode}" ] || \
  ahoi_die "Xcode mismatch: expected ${expected_xcode}, got ${actual_xcode}"
[ "${actual_xcode_build}" = "${expected_xcode_build}" ] || \
  ahoi_die "Xcode build mismatch: expected ${expected_xcode_build}, got ${actual_xcode_build}"

work_parent="$(ahoi_existing_parent "${AHOI_WORK_ROOT}")"
work_device="$(df -P "${work_parent}" | awk 'NR == 2 {print $1}')"
[ -n "${work_device}" ] || ahoi_die "could not resolve work-root filesystem device"
filesystem="$(diskutil info "${work_device}" | awk -F: '/File System Personality/ {gsub(/^[ \t]+|[ \t]+$/, "", $2); print $2}')"
[ "${filesystem}" = "APFS" ] || ahoi_die "Chromium work root must be on APFS, found: ${filesystem}"

actual_memory="$(sysctl -n hw.memsize)"
minimum_memory="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" host.minimumMemoryBytes)"
[ "${actual_memory}" -ge "${minimum_memory}" ] || \
  ahoi_die "host memory is below the configured minimum"

available="$(ahoi_free_bytes "${AHOI_WORK_ROOT}")"
required="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/toolchain.json" host.minimumFreeWorkBytes)"

ahoi_note "host: $(sw_vers -productName) $(sw_vers -productVersion) ($(sw_vers -buildVersion)), ${actual_arch}"
ahoi_note "hardware: $(sysctl -n machdep.cpu.brand_string), $((actual_memory / 1073741824)) GiB RAM"
ahoi_note "toolchain (${xcode_mode}): $(xcodebuild -version | tr '\n' ' '), macOS SDK ${actual_sdk} (${actual_sdk_build}), iOS SDK ${actual_ios_sdk} (${actual_ios_sdk_build})"
ahoi_note "work root: ${AHOI_WORK_ROOT}"
python3 - "${available}" "${required}" <<'PY'
import sys
available, required = (int(item) for item in sys.argv[1:])
print(f"==> free disk: {available / 2**30:.1f} GiB; required: {required / 2**30:.1f} GiB")
PY

ahoi_require_build_free_space

ahoi_note "host check passed"
