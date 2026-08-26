#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

[ "$#" -eq 3 ] || \
  ahoi_die "usage: $0 /absolute/out-dir /absolute/args.gn chrome"
out_dir="$1"
args_file="$2"
target="$3"
case "${out_dir}" in
  "${AHOI_CHROMIUM_SRC}/out/"*) ;;
  *) ahoi_die "output directory must be below ${AHOI_CHROMIUM_SRC}/out" ;;
esac
[ -f "${args_file}" ] || ahoi_die "GN args file is missing: ${args_file}"
[ "${target}" = "chrome" ] || ahoi_die "unsupported Chromium build target: ${target}"

config="${AHOI_REPO_ROOT}/config/dependency-build-workarounds.json"
config_key="v8InspectorProtocolRelativeDepfilePaths"
workaround_id="$(ahoi_json_get "${config}" "${config_key}.id")"
dependency_path="$(ahoi_json_get "${config}" "${config_key}.dependencyPath")"
expected_commit="$(ahoi_json_get "${config}" "${config_key}.upstreamCommit")"
target_relative="$(ahoi_json_get "${config}" "${config_key}.targetPath")"
patch_relative="$(ahoi_json_get "${config}" "${config_key}.patchPath")"
expected_patch_sha="$(ahoi_json_get "${config}" "${config_key}.patchSha256")"
upstream_fix_commit="$(ahoi_json_get "${config}" "${config_key}.upstreamFixCommit")"
receipt_name="$(ahoi_json_get "${config}" "${config_key}.receiptName")"

dependency_root="${AHOI_CHROMIUM_SRC}/${dependency_path}"
target_path="${dependency_root}/${target_relative}"
patch_path="${AHOI_REPO_ROOT}/${patch_relative}"
[ -d "${dependency_root}/.git" ] || ahoi_die "V8 checkout is missing"
[ -f "${target_path}" ] || ahoi_die "V8 workaround target is missing"
[ -f "${patch_path}" ] || ahoi_die "V8 workaround patch is missing"
[ "$(git -C "${dependency_root}" rev-parse HEAD)" = "${expected_commit}" ] || \
  ahoi_die "V8 workaround commit mismatch"
[ "$(ahoi_sha256 "${patch_path}")" = "${expected_patch_sha}" ] || \
  ahoi_die "V8 workaround patch SHA-256 mismatch"
ahoi_require_clean_git_checkout "${dependency_root}"

backup="$(mktemp -t ahoi-v8-inspector-protocol.XXXXXX)"
cp -p -- "${target_path}" "${backup}"
original_sha="$(ahoi_sha256 "${backup}")"

restore_v8() {
  [ -n "${backup}" ] || return 0
  cp -p -- "${backup}" "${target_path}"
  cmp -s "${backup}" "${target_path}" || {
    echo "error: failed to restore V8 workaround target byte-for-byte" >&2
    return 1
  }
  [ -z "$(git -C "${dependency_root}" status --porcelain)" ] || {
    echo "error: V8 is not clean after dependency workaround restore" >&2
    return 1
  }
  rm -f -- "${backup}"
  backup=""
}

restore_on_exit() {
  status="$?"
  trap - EXIT HUP INT TERM
  restore_v8 || status=1
  exit "${status}"
}
trap restore_on_exit EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

git -C "${dependency_root}" apply --check --whitespace=error-all "${patch_path}"
git -C "${dependency_root}" apply --whitespace=error-all "${patch_path}"
expected_status=" M ${target_relative}"
[ "$(git -C "${dependency_root}" status --porcelain)" = "${expected_status}" ] || \
  ahoi_die "V8 workaround changed an unexpected path"
patched_sha="$(ahoi_sha256 "${target_path}")"

args="$(<"${args_file}")"
ahoi_note "applying ${workaround_id} only for gn gen and autoninja"
(
  cd "${AHOI_CHROMIUM_SRC}"
  gn gen "${out_dir}" --args="${args}"
  if [ -n "${AHOI_JOBS:-}" ]; then
    autoninja -C "${out_dir}" -j "${AHOI_JOBS}" "${target}"
  else
    autoninja -C "${out_dir}" "${target}"
  fi
)

restore_v8
trap - EXIT HUP INT TERM
mkdir -p "${out_dir}"
receipt="${out_dir}/${receipt_name}"
python3 - "${receipt}" "${workaround_id}" "${expected_commit}" \
  "${upstream_fix_commit}" "${patch_relative}" "${expected_patch_sha}" \
  "${target_relative}" "${original_sha}" "${patched_sha}" <<'PY'
import json
import os
from pathlib import Path
import sys
import tempfile

(
    output,
    workaround_id,
    v8_commit,
    upstream_fix_commit,
    patch_path,
    patch_sha256,
    target_path,
    original_sha256,
    patched_sha256,
) = sys.argv[1:]
payload = {
    "schemaVersion": 1,
    "workaroundId": workaround_id,
    "v8Commit": v8_commit,
    "upstreamFixCommit": upstream_fix_commit,
    "patchPath": patch_path,
    "patchSha256": patch_sha256,
    "targetPath": target_path,
    "originalTargetSha256": original_sha256,
    "patchedTargetSha256": patched_sha256,
    "restoredByteForByte": True,
    "phases": ["gn gen", "autoninja"],
}
destination = Path(output)
fd, temporary = tempfile.mkstemp(prefix=f".{destination.name}.", dir=destination.parent)
try:
    with os.fdopen(fd, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, destination)
except BaseException:
    try:
        os.unlink(temporary)
    except FileNotFoundError:
        pass
    raise
PY

ahoi_note "restored clean V8 checkout after ${workaround_id}"
