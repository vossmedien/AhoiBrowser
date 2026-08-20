#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

checkout_mode="clean"
toolchain_mode="pinned-reference"
for option in "$@"; do
  case "${option}" in
    --allow-source-overlay) checkout_mode="overlay" ;;
    --compatible-dev-xcode) toolchain_mode="compatible-development" ;;
    *) ahoi_die "usage: $0 [--allow-source-overlay] [--compatible-dev-xcode]" ;;
  esac
done

ahoi_require_build_free_space
ahoi_select_xcode "${toolchain_mode}"
if [ "${toolchain_mode}" = "compatible-development" ]; then
  "${SCRIPT_DIR}/check-host.sh" --compatible-dev-xcode
else
  "${SCRIPT_DIR}/check-host.sh"
fi
ahoi_enable_depot_tools

expected_commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" commit)"
mkdir -p "${AHOI_STATE_DIR}" "${AHOI_REPO_ROOT}/artifacts/build"

verify_hook_checkout() {
  if [ "${checkout_mode}" = "clean" ]; then
    "${SCRIPT_DIR}/verify-upstream.sh"
    return
  fi

  [ -d "${AHOI_CHROMIUM_SRC}/.git" ] || ahoi_die "Chromium checkout is missing"
  ahoi_require_gclient_config
  [ "$(git -C "${AHOI_CHROMIUM_SRC}" rev-parse HEAD)" = "${expected_commit}" ] || \
    ahoi_die "overlay hook run is not at the pinned Chromium commit"
  ahoi_require_overlay_state
  python3 "${AHOI_REPO_ROOT}/tools/chromium_dependencies.py" \
    --expected-commit "${expected_commit}" \
    --allow-source-overlay \
    --output "${AHOI_REPO_ROOT}/artifacts/build/chromium-dependencies.json"
}

verify_hook_checkout
ahoi_invalidate_hook_state
ahoi_note "running Chromium hooks with ${toolchain_mode} Xcode/SDK (${checkout_mode} checkout)"
(
  cd "${AHOI_CHROMIUM_ROOT}"
  gclient runhooks
)
verify_hook_checkout

deps_hash="$(ahoi_sha256 "${AHOI_CHROMIUM_SRC}/DEPS")"
checkout_delta="$(ahoi_checkout_delta_fingerprint "${AHOI_CHROMIUM_SRC}")"
state_file="$(ahoi_hook_state_file)"
artifact_file="$(ahoi_hook_artifact_file)"
python3 - "${state_file}" "${artifact_file}" "${expected_commit}" "${deps_hash}" \
  "${checkout_mode}" "${toolchain_mode}" "${checkout_delta}" \
  "$(xcodebuild -version | awk 'NR == 1 {print $2}')" \
  "$(xcodebuild -version | awk 'NR == 2 {print $3}')" \
  "$(xcrun --sdk macosx --show-sdk-version)" \
  "$(xcrun --sdk macosx --show-sdk-build-version)" \
  "$(xcrun --sdk iphoneos --show-sdk-version)" \
  "$(xcrun --sdk iphoneos --show-sdk-build-version)" <<'PY'
import datetime
import json
import os
import pathlib
import sys
import tempfile

(
    state_path,
    artifact_path,
    commit,
    deps_hash,
    checkout_mode,
    toolchain_mode,
    checkout_delta,
    xcode,
    xcode_build,
    mac_sdk,
    mac_sdk_build,
    ios_sdk,
    ios_sdk_build,
) = sys.argv[1:]
payload = {
    "schemaVersion": 3,
    "chromiumCommit": commit,
    "depsSha256": deps_hash,
    "checkoutMode": checkout_mode,
    "toolchainMode": toolchain_mode,
    "checkoutDeltaFingerprint": checkout_delta,
    "xcodeVersion": xcode,
    "xcodeBuild": xcode_build,
    "macOSSDKVersion": mac_sdk,
    "macOSSDKBuild": mac_sdk_build,
    "iOSSDKVersion": ios_sdk,
    "iOSSDKBuild": ios_sdk_build,
    "completedAt": datetime.datetime.now(datetime.timezone.utc).isoformat(),
}

encoded = (json.dumps(payload, indent=2, sort_keys=True) + "\n").encode("utf-8")
staged = []


def stage(destination: str) -> tuple[pathlib.Path, pathlib.Path]:
    path = pathlib.Path(destination)
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary_path = pathlib.Path(temporary)
    staged.append(temporary_path)
    with os.fdopen(descriptor, "wb") as handle:
        handle.write(encoded)
        handle.flush()
        os.fsync(handle.fileno())
    os.chmod(temporary_path, 0o644)
    return path, temporary_path


try:
    # Publish the diagnostic artifact first and the build-consumed state last.
    # Every individual replacement is atomic and no state exists on hook failure.
    artifact, artifact_temporary = stage(artifact_path)
    state, state_temporary = stage(state_path)
    os.replace(artifact_temporary, artifact)
    staged.remove(artifact_temporary)
    os.replace(state_temporary, state)
    staged.remove(state_temporary)
    for directory in {artifact.parent, state.parent}:
        descriptor = os.open(directory, os.O_RDONLY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
finally:
    for temporary_path in staged:
        temporary_path.unlink(missing_ok=True)
PY

ahoi_require_hook_state "${checkout_mode}" "${toolchain_mode}"
ahoi_note "pinned Chromium hooks verified: ${state_file}"
