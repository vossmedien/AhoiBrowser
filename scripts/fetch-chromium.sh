#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

usage() {
  cat <<'EOF'
Usage: ./scripts/fetch-chromium.sh [--prehydrate-target]

  --prehydrate-target  For an existing clean promisor checkout, resumably
                       fetch all missing blobs of the exact pinned target in
                       small guarded batches before gclient changes HEAD.
EOF
}

prehydrate_target=0
[ "$#" -le 1 ] || {
  usage >&2
  ahoi_die "fetch-chromium accepts at most one option"
}
case "${1:-}" in
  "") ;;
  --prehydrate-target) prehydrate_target=1 ;;
  -h|--help)
    usage
    exit 0
    ;;
  *)
    usage >&2
    ahoi_die "unsupported fetch option: $1"
    ;;
esac

ahoi_require_command git
ahoi_require_command python3
ahoi_require_command cmp
ahoi_require_command install
ahoi_require_free_space
ahoi_enable_depot_tools

source_url="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" source)"
pinned_commit="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" commit)"
pinned_version="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" version)"
gclient_jobs="${AHOI_GCLIENT_JOBS:-1}"
[[ "${gclient_jobs}" =~ ^[1-9][0-9]*$ ]] || \
  ahoi_die "AHOI_GCLIENT_JOBS must be a positive integer"

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

if [ "${prehydrate_target}" -eq 1 ]; then
  [ -d "${AHOI_CHROMIUM_SRC}/.git" ] || \
    ahoi_die "--prehydrate-target requires an existing Chromium checkout"
  [[ "${pinned_commit}" =~ ^[0-9a-f]{40}$ ]] || \
    ahoi_die "Chromium pin is not an exact lowercase SHA-1"

  # A normal blobless target fetch supplies commit/tree metadata without
  # changing a ref. Skip it when the complete target tree is already local.
  if ! GIT_NO_LAZY_FETCH=1 GIT_OPTIONAL_LOCKS=0 \
    git -C "${AHOI_CHROMIUM_SRC}" ls-tree -r --full-tree \
      "${pinned_commit}" >/dev/null 2>&1; then
    checkout_guard() {
      PYTHONPATH="${AHOI_REPO_ROOT}/tools" python3 - "${AHOI_CHROMIUM_SRC}" <<'PY'
import json
import pathlib
import sys

from chromium_checkout_state import checkout_snapshot, git_environment

print(json.dumps(checkout_snapshot(pathlib.Path(sys.argv[1]), git_environment()), sort_keys=True))
PY
    }
    metadata_guard_before="$(checkout_guard)"
    ahoi_note "fetching pinned Chromium commit/tree metadata before blob hydration"
    metadata_fetch_status=0
    git -C "${AHOI_CHROMIUM_SRC}" \
      -c http.version=HTTP/1.1 \
      -c http.maxRequests=1 \
      -c fetch.parallel=1 \
      -c maintenance.auto=false \
      -c gc.auto=0 \
      fetch --no-tags --no-write-fetch-head --no-recurse-submodules \
      --filter=blob:none origin --stdin <<<"${pinned_commit}" || \
      metadata_fetch_status=$?
    metadata_guard_after="$(checkout_guard)"
    [ "${metadata_guard_before}" = "${metadata_guard_after}" ] || \
      ahoi_die "Chromium checkout metadata changed during target metadata fetch"
    [ "${metadata_fetch_status}" -eq 0 ] || \
      ahoi_die "pinned target metadata fetch failed; rerun resumes safely"
  fi

  ahoi_note "prehydrating missing blobs for pinned Chromium ${pinned_version}"
  if ! python3 "${AHOI_REPO_ROOT}/tools/chromium_checkout_hydration.py" \
    --repository "${AHOI_REPO_ROOT}" \
    --checkout "${AHOI_CHROMIUM_SRC}" \
    --target "${pinned_commit}" \
    --output "${AHOI_REPO_ROOT}/artifacts/build/chromium-checkout-hydration.json"; then
    ahoi_die "Chromium target prehydration is incomplete; rerun the same command to resume"
  fi
fi

# A completed hook record describes the old checkout. Invalidate it before the
# sync starts, including when the sync later fails or is interrupted.
ahoi_invalidate_hook_state
ahoi_note "syncing Chromium ${pinned_version} at ${pinned_commit}"
(
  cd "${AHOI_CHROMIUM_ROOT}"
  # Chromium's anonymous Git hosts apply a shared short-term request quota.
  # One repository at a time is slower at peak throughput but avoids the
  # repeated 429/retry cycle; trusted environments may opt into more workers.
  gclient sync -j "${gclient_jobs}" --no-history --nohooks \
    --revision "src@${pinned_commit}"
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
