#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

ahoi_require_command git
ahoi_require_command python3
ahoi_require_command cmp
ahoi_require_command install
ahoi_require_free_space
ahoi_enable_depot_tools

source_url="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" source)"
pinned_commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" commit)"
pinned_version="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" version)"

mkdir -p "${AHOI_CHROMIUM_ROOT}" "${AHOI_STATE_DIR}" "${AHOI_REPO_ROOT}/artifacts/build"

if [ ! -f "${AHOI_CHROMIUM_ROOT}/.gclient" ]; then
  [ ! -e "${AHOI_CHROMIUM_SRC}" ] || \
    ahoi_die "src exists without a managed .gclient file: ${AHOI_CHROMIUM_SRC}"
  install -m 0644 "${AHOI_REPO_ROOT}/config/gclient.py" \
    "${AHOI_CHROMIUM_ROOT}/.gclient"
elif [ -d "${AHOI_CHROMIUM_SRC}/.git" ]; then
  actual_origin="$(git -C "${AHOI_CHROMIUM_SRC}" remote get-url origin)"
  [ "${actual_origin}" = "${source_url}" ] || \
    ahoi_die "unexpected Chromium origin: ${actual_origin}"
  ahoi_require_clean_git_checkout "${AHOI_CHROMIUM_SRC}"
fi
ahoi_require_gclient_config

# A completed hook record describes the old checkout. Invalidate it before the
# sync starts, including when the sync later fails or is interrupted.
ahoi_invalidate_hook_state
ahoi_note "syncing Chromium ${pinned_version} at ${pinned_commit}"
(
  cd "${AHOI_CHROMIUM_ROOT}"
  gclient sync --no-history --nohooks --revision "src@${pinned_commit}"
)
ahoi_require_gclient_config

actual_commit="$(git -C "${AHOI_CHROMIUM_SRC}" rev-parse HEAD)"
[ "${actual_commit}" = "${pinned_commit}" ] || \
  ahoi_die "Chromium commit mismatch: ${actual_commit}"

actual_version="$(python3 - "${AHOI_CHROMIUM_SRC}/chrome/VERSION" <<'PY'
import sys
parts = {}
with open(sys.argv[1], encoding="utf-8") as handle:
    for line in handle:
        key, value = line.strip().split("=", 1)
        parts[key] = value
print(".".join(parts[key] for key in ("MAJOR", "MINOR", "BUILD", "PATCH")))
PY
)"
[ "${actual_version}" = "${pinned_version}" ] || \
  ahoi_die "Chromium version mismatch: expected ${pinned_version}, got ${actual_version}"

python3 "${AHOI_REPO_ROOT}/tools/chromium_dependencies.py" \
  --expected-commit "${pinned_commit}" \
  --output "${AHOI_REPO_ROOT}/artifacts/build/chromium-dependencies.json"

python3 - "${AHOI_REPO_ROOT}/artifacts/build/chromium-checkout.json" \
  "${actual_version}" "${actual_commit}" \
  "$(ahoi_sha256 "${AHOI_REPO_ROOT}/config/gclient.py")" <<'PY'
import datetime
import json
import os
import sys

path, version, commit, gclient_hash = sys.argv[1:]
payload = {
    "schemaVersion": 1,
    "version": version,
    "commit": commit,
    "sourcePath": "<work-root>/chromium/src",
    "gclientConfigSha256": gclient_hash,
    "verifiedAt": datetime.datetime.now(datetime.timezone.utc).isoformat(),
}
with open(path, "w", encoding="utf-8") as handle:
    json.dump(payload, handle, indent=2, sort_keys=True)
    handle.write("\n")
PY

ahoi_note "Chromium checkout verified: ${actual_version} (${actual_commit})"
