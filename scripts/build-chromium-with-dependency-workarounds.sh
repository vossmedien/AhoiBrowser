#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

[ "$#" -ge 3 ] || \
  ahoi_die "usage: $0 /absolute/out-dir /absolute/args.gn target [target ...]"
out_dir="$1"
args_file="$2"
shift 2
targets=("$@")
ahoi_require_command python3
if ! out_dir="$(python3 - "${AHOI_CHROMIUM_SRC}" "${out_dir}" <<'PY'
import os
from pathlib import Path
import stat
import sys


configured_source = Path(sys.argv[1])
raw = sys.argv[2]
candidate = Path(raw)
if not candidate.is_absolute():
    raise SystemExit("output directory must be absolute")
if any(ord(character) < 32 or ord(character) == 127 for character in raw):
    raise SystemExit("output directory contains control characters")
if any(component in {".", ".."} for component in raw.split(os.sep)):
    raise SystemExit("output directory must not contain dot components")

try:
    relative = Path(os.path.normpath(raw)).relative_to(configured_source / "out")
except ValueError as error:
    raise SystemExit(
        f"output directory must be below {configured_source / 'out'}"
    ) from error
if not relative.parts:
    raise SystemExit("output directory must name a child below Chromium out")

try:
    source = configured_source.resolve(strict=True)
except OSError as error:
    raise SystemExit(f"Chromium source root cannot be resolved: {error}")
out_root = source / "out"
lexical = out_root.joinpath(*relative.parts)

cursor = source
for component in ("out", *relative.parts):
    cursor = cursor / component
    try:
        metadata = cursor.lstat()
    except FileNotFoundError:
        break
    if stat.S_ISLNK(metadata.st_mode):
        raise SystemExit(f"output directory component is a symlink: {cursor}")
    if not stat.S_ISDIR(metadata.st_mode):
        raise SystemExit(f"output directory component is not a directory: {cursor}")

resolved = lexical.resolve(strict=False)
try:
    resolved.relative_to(out_root.resolve(strict=False))
except ValueError as error:
    raise SystemExit(f"resolved output directory escapes {out_root}") from error
print(resolved)
PY
)"; then
  ahoi_die "unsafe Chromium output directory"
fi
[ -f "${args_file}" ] || ahoi_die "GN args file is missing: ${args_file}"
for target in "${targets[@]}"; do
  case "${target}" in
    ""|-*|*[!A-Za-z0-9_./:+-]*)
      ahoi_die "unsafe Chromium build target: ${target}"
      ;;
  esac
done

case "${AHOI_NINJA_KEEP_GOING:-0}" in
  0) ninja_failure_limit=1 ;;
  1) ninja_failure_limit=0 ;;
  *) ahoi_die "AHOI_NINJA_KEEP_GOING must be 0 or 1" ;;
esac

config="${AHOI_REPO_ROOT}/config/dependency-build-workarounds.json"
receipt_name="$(ahoi_json_get "${config}" "chromiumRustDepfileSpacePaths.receiptName")"
[ "${receipt_name}" = "$(ahoi_json_get "${config}" "v8InspectorProtocolRelativeDepfilePaths.receiptName")" ] || \
  ahoi_die "dependency workaround receipt names do not match"
receipt="${out_dir}/${receipt_name}"
if [ -e "${receipt}" ] || [ -L "${receipt}" ]; then
  [ -f "${receipt}" ] || ahoi_die "refusing to replace non-file receipt: ${receipt}"
  unlink "${receipt}"
fi

chromium_key="chromiumRustDepfileSpacePaths"
chromium_id="$(ahoi_json_get "${config}" "${chromium_key}.id")"
chromium_commit="$(ahoi_json_get "${config}" "${chromium_key}.upstreamCommit")"
chromium_target_relative="$(ahoi_json_get "${config}" "${chromium_key}.targetPath")"
chromium_target_expected_sha="$(ahoi_json_get "${config}" "${chromium_key}.targetSha256")"
chromium_patch_relative="$(ahoi_json_get "${config}" "${chromium_key}.patchPath")"
chromium_patch_expected_sha="$(ahoi_json_get "${config}" "${chromium_key}.patchSha256")"
chromium_patch_path="${AHOI_REPO_ROOT}/${chromium_patch_relative}"
chromium_target_path="${AHOI_CHROMIUM_SRC}/${chromium_target_relative}"

v8_key="v8InspectorProtocolRelativeDepfilePaths"
v8_id="$(ahoi_json_get "${config}" "${v8_key}.id")"
v8_dependency_path="$(ahoi_json_get "${config}" "${v8_key}.dependencyPath")"
v8_commit="$(ahoi_json_get "${config}" "${v8_key}.upstreamCommit")"
v8_target_relative="$(ahoi_json_get "${config}" "${v8_key}.targetPath")"
v8_target_expected_sha="$(ahoi_json_get "${config}" "${v8_key}.targetSha256")"
v8_patch_relative="$(ahoi_json_get "${config}" "${v8_key}.patchPath")"
v8_patch_expected_sha="$(ahoi_json_get "${config}" "${v8_key}.patchSha256")"
v8_upstream_fix_commit="$(ahoi_json_get "${config}" "${v8_key}.upstreamFixCommit")"
v8_root="${AHOI_CHROMIUM_SRC}/${v8_dependency_path}"
v8_target_path="${v8_root}/${v8_target_relative}"
v8_patch_path="${AHOI_REPO_ROOT}/${v8_patch_relative}"

[ "$(git -C "${AHOI_CHROMIUM_SRC}" rev-parse HEAD)" = "${chromium_commit}" ] || \
  ahoi_die "Chromium Rust workaround commit mismatch"
[ -f "${chromium_target_path}" ] || ahoi_die "Chromium Rust workaround target is missing"
[ -f "${chromium_patch_path}" ] || ahoi_die "Chromium Rust workaround patch is missing"
[ "$(ahoi_sha256 "${chromium_target_path}")" = "${chromium_target_expected_sha}" ] || \
  ahoi_die "Chromium Rust workaround target SHA-256 mismatch"
[ "$(ahoi_sha256 "${chromium_patch_path}")" = "${chromium_patch_expected_sha}" ] || \
  ahoi_die "Chromium Rust workaround patch SHA-256 mismatch"
[ -z "$(git -C "${AHOI_CHROMIUM_SRC}" status --porcelain -- "${chromium_target_relative}")" ] || \
  ahoi_die "Chromium Rust workaround target is already modified"

[ -d "${v8_root}/.git" ] || ahoi_die "V8 checkout is missing"
[ -f "${v8_target_path}" ] || ahoi_die "V8 workaround target is missing"
[ -f "${v8_patch_path}" ] || ahoi_die "V8 workaround patch is missing"
[ "$(git -C "${v8_root}" rev-parse HEAD)" = "${v8_commit}" ] || \
  ahoi_die "V8 workaround commit mismatch"
[ "$(ahoi_sha256 "${v8_target_path}")" = "${v8_target_expected_sha}" ] || \
  ahoi_die "V8 workaround target SHA-256 mismatch"
[ "$(ahoi_sha256 "${v8_patch_path}")" = "${v8_patch_expected_sha}" ] || \
  ahoi_die "V8 workaround patch SHA-256 mismatch"
ahoi_require_clean_git_checkout "${v8_root}"

chromium_backup="$(mktemp -t ahoi-chromium-rust-depfile.XXXXXX)"
v8_backup="$(mktemp -t ahoi-v8-inspector-protocol.XXXXXX)"
cp -p -- "${chromium_target_path}" "${chromium_backup}"
cp -p -- "${v8_target_path}" "${v8_backup}"
chromium_original_sha="$(ahoi_sha256 "${chromium_backup}")"
v8_original_sha="$(ahoi_sha256 "${v8_backup}")"

preserve_original_mtime() {
  reference="$1"
  target_path="$2"
  python3 - "${reference}" "${target_path}" <<'PY'
import os
from pathlib import Path
import sys

reference = Path(sys.argv[1])
target = Path(sys.argv[2])
reference_stat = reference.stat(follow_symlinks=False)
target_stat = target.stat(follow_symlinks=False)
os.utime(
    target,
    ns=(target_stat.st_atime_ns, reference_stat.st_mtime_ns),
    follow_symlinks=False,
)
if target.stat(follow_symlinks=False).st_mtime_ns != reference_stat.st_mtime_ns:
    raise SystemExit(f"failed to preserve original mtime for {target}")
PY
}

restore_workarounds() {
  restore_status=0
  if [ -n "${v8_backup}" ]; then
    v8_restore_ok=1
    cp -p -- "${v8_backup}" "${v8_target_path}" || v8_restore_ok=0
    cmp -s "${v8_backup}" "${v8_target_path}" || {
      echo "error: failed to restore V8 workaround target byte-for-byte" >&2
      v8_restore_ok=0
    }
    if [ -n "$(git -C "${v8_root}" status --porcelain)" ]; then
      echo "error: V8 is not clean after dependency workaround restore" >&2
      v8_restore_ok=0
    fi
    if [ "${v8_restore_ok}" -eq 1 ]; then
      unlink "${v8_backup}"
      v8_backup=""
    else
      echo "error: retained V8 recovery backup: ${v8_backup}" >&2
      restore_status=1
    fi
  fi
  if [ -n "${chromium_backup}" ]; then
    chromium_restore_ok=1
    cp -p -- "${chromium_backup}" "${chromium_target_path}" || chromium_restore_ok=0
    cmp -s "${chromium_backup}" "${chromium_target_path}" || {
      echo "error: failed to restore Chromium Rust workaround target byte-for-byte" >&2
      chromium_restore_ok=0
    }
    if [ -n "$(git -C "${AHOI_CHROMIUM_SRC}" status --porcelain -- "${chromium_target_relative}")" ]; then
      echo "error: Chromium Rust workaround target is not clean after restore" >&2
      chromium_restore_ok=0
    fi
    if [ "${chromium_restore_ok}" -eq 1 ]; then
      unlink "${chromium_backup}"
      chromium_backup=""
    else
      echo "error: retained Chromium recovery backup: ${chromium_backup}" >&2
      restore_status=1
    fi
  fi
  return "${restore_status}"
}

restore_on_exit() {
  status="$?"
  trap - EXIT HUP INT TERM
  restore_workarounds || status=1
  exit "${status}"
}
trap restore_on_exit EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

git -C "${AHOI_CHROMIUM_SRC}" apply --check --whitespace=error-all "${chromium_patch_path}"
git -C "${AHOI_CHROMIUM_SRC}" apply --whitespace=error-all "${chromium_patch_path}"
chromium_expected_status=" M ${chromium_target_relative}"
[ "$(git -C "${AHOI_CHROMIUM_SRC}" status --porcelain -- "${chromium_target_relative}")" = "${chromium_expected_status}" ] || \
  ahoi_die "Chromium Rust workaround changed an unexpected path"
preserve_original_mtime "${chromium_backup}" "${chromium_target_path}"
chromium_patched_sha="$(ahoi_sha256 "${chromium_target_path}")"

git -C "${v8_root}" apply --check --whitespace=error-all "${v8_patch_path}"
git -C "${v8_root}" apply --whitespace=error-all "${v8_patch_path}"
v8_expected_status=" M ${v8_target_relative}"
[ "$(git -C "${v8_root}" status --porcelain)" = "${v8_expected_status}" ] || \
  ahoi_die "V8 workaround changed an unexpected path"
preserve_original_mtime "${v8_backup}" "${v8_target_path}"
v8_patched_sha="$(ahoi_sha256 "${v8_target_path}")"

args="$(<"${args_file}")"
ahoi_note "applying ${chromium_id} and ${v8_id} only for gn gen and autoninja"
(
  cd "${AHOI_CHROMIUM_SRC}"
  gn gen "${out_dir}" --args="${args}"
  if [ -n "${AHOI_JOBS:-}" ]; then
    autoninja -C "${out_dir}" -k "${ninja_failure_limit}" -j "${AHOI_JOBS}" "${targets[@]}"
  else
    autoninja -C "${out_dir}" -k "${ninja_failure_limit}" "${targets[@]}"
  fi
)

restore_workarounds
trap - EXIT HUP INT TERM
mkdir -p "${out_dir}"
python3 - "${receipt}" \
  "${chromium_id}" "${chromium_commit}" "${chromium_patch_relative}" \
  "${chromium_patch_expected_sha}" "${chromium_target_relative}" \
  "${chromium_original_sha}" "${chromium_patched_sha}" \
  "${v8_id}" "${v8_commit}" "${v8_upstream_fix_commit}" \
  "${v8_patch_relative}" "${v8_patch_expected_sha}" "${v8_target_relative}" \
  "${v8_original_sha}" "${v8_patched_sha}" <<'PY'
import json
import os
from pathlib import Path
import sys
import tempfile

(
    output,
    chromium_id,
    chromium_commit,
    chromium_patch_path,
    chromium_patch_sha256,
    chromium_target_path,
    chromium_original_sha256,
    chromium_patched_sha256,
    v8_id,
    v8_commit,
    v8_upstream_fix_commit,
    v8_patch_path,
    v8_patch_sha256,
    v8_target_path,
    v8_original_sha256,
    v8_patched_sha256,
) = sys.argv[1:]
payload = {
    "schemaVersion": 2,
    "phases": ["gn gen", "autoninja"],
    "workarounds": [
        {
            "workaroundId": chromium_id,
            "dependency": "chromium",
            "upstreamCommit": chromium_commit,
            "upstreamFixCommit": None,
            "patchPath": chromium_patch_path,
            "patchSha256": chromium_patch_sha256,
            "targetPath": chromium_target_path,
            "originalTargetSha256": chromium_original_sha256,
            "patchedTargetSha256": chromium_patched_sha256,
            "restoredByteForByte": True,
            "sourceMtimePreserved": True,
        },
        {
            "workaroundId": v8_id,
            "dependency": "v8",
            "upstreamCommit": v8_commit,
            "upstreamFixCommit": v8_upstream_fix_commit,
            "patchPath": v8_patch_path,
            "patchSha256": v8_patch_sha256,
            "targetPath": v8_target_path,
            "originalTargetSha256": v8_original_sha256,
            "patchedTargetSha256": v8_patched_sha256,
            "restoredByteForByte": True,
            "sourceMtimePreserved": True,
        },
    ],
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

ahoi_note "restored Chromium and V8 sources after timestamp-stable temporary build workarounds"
