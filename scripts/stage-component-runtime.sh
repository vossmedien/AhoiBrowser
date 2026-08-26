#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

out_dir="${1:-}"
app_path="${2:-}"
[ -n "${out_dir}" ] && [ -n "${app_path}" ] || \
  ahoi_die "usage: $0 /absolute/path/out/AhoiDev /absolute/path/AhoiBrowser.app"
case "${out_dir}" in
  "${AHOI_CHROMIUM_SRC}"/out/*) ;;
  *) ahoi_die "output directory must be inside the pinned Chromium out directory" ;;
esac
[ "${app_path}" = "${out_dir}/AhoiBrowser.app" ] || \
  ahoi_die "app path must be the AhoiBrowser.app produced by the output directory"
[ -d "${app_path}/Contents/Frameworks" ] || \
  ahoi_die "AhoiBrowser Frameworks directory is missing: ${app_path}"

gn_binary="${AHOI_CHROMIUM_SRC}/buildtools/mac/gn"
[ -x "${gn_binary}" ] || ahoi_die "Chromium GN binary is missing: ${gn_binary}"
component_setting="$(
  cd "${AHOI_CHROMIUM_SRC}"
  "${gn_binary}" args "${out_dir}" --list=is_component_build --short
)"
case "${component_setting}" in
  *"is_component_build = true"*) ;;
  *)
    ahoi_note "non-component build needs no staged development dylibs"
    exit 0
    ;;
esac

ahoi_require_command find
ahoi_require_command shasum
ahoi_require_command cmp
ahoi_require_command ditto
ahoi_require_command readlink
runtime_dir="${app_path}/Contents/Frameworks"
manifest="${app_path}/Contents/Resources/ahoi-component-runtime.sha256"
framework_resource_manifest="${app_path}/Contents/Resources/ahoi-component-framework-resources.sha256"
source_names="$(mktemp -t ahoi-component-runtime-names.XXXXXX)"
new_manifest="$(mktemp -t ahoi-component-runtime-manifest.XXXXXX)"
new_framework_resource_manifest="$(mktemp -t ahoi-component-framework-resources.XXXXXX)"
source_framework_files="$(mktemp -t ahoi-component-framework-source-files.XXXXXX)"
staged_framework_files="$(mktemp -t ahoi-component-framework-staged-files.XXXXXX)"
source_framework_links="$(mktemp -t ahoi-component-framework-source-links.XXXXXX)"
staged_framework_links="$(mktemp -t ahoi-component-framework-staged-links.XXXXXX)"
framework_name="AhoiBrowser Framework.framework"
source_framework="${out_dir}/${framework_name}"
destination_framework="${runtime_dir}/${framework_name}"
framework_stage_root=""
framework_backup=""
cleanup() {
  status=$?
  trap - EXIT HUP INT TERM
  rm -f "${source_names}" "${new_manifest}" \
    "${new_framework_resource_manifest}" \
    "${source_framework_files}" "${staged_framework_files}" \
    "${source_framework_links}" "${staged_framework_links}"
  if [ -n "${framework_stage_root}" ]; then
    case "${framework_stage_root}" in
      "${runtime_dir}"/.ahoi-framework-stage.*) ;;
      *) ahoi_die "refusing to clean unexpected framework staging path" ;;
    esac
    if [ ! -e "${destination_framework}" ] && \
       [ -n "${framework_backup}" ] && [ -e "${framework_backup}" ]; then
      mv "${framework_backup}" "${destination_framework}"
    fi
    rm -rf "${framework_stage_root}"
  fi
  exit "${status}"
}
trap cleanup EXIT HUP INT TERM

[ -d "${source_framework}" ] || \
  ahoi_die "component build framework is missing: ${source_framework}"

validate_framework_layout() {
  framework_root="$1"
  [ -L "${framework_root}/Versions/Current" ] || \
    ahoi_die "component framework has no Current version symlink: ${framework_root}"
  current_version="$(readlink "${framework_root}/Versions/Current")"
  case "${current_version}" in
    ""|.*|*/*) ahoi_die "component framework Current version is unsafe" ;;
  esac
  version_root="${framework_root}/Versions/${current_version}"
  [ -d "${version_root}" ] || \
    ahoi_die "component framework Current version is missing"
  [ -f "${version_root}/AhoiBrowser Framework" ] || \
    ahoi_die "component framework binary is missing"
  [ -f "${version_root}/Resources/en.lproj/locale.pak" ] || \
    ahoi_die "component framework English locale pack is missing"
  [ -d "${version_root}/Helpers" ] || \
    ahoi_die "component framework helpers are missing"
}

validate_framework_layout "${source_framework}"
framework_stage_root="$(mktemp -d "${runtime_dir}/.ahoi-framework-stage.XXXXXX")"
case "${framework_stage_root}" in
  "${runtime_dir}"/.ahoi-framework-stage.*) ;;
  *) ahoi_die "framework staging directory escaped the app bundle" ;;
esac
staged_framework="${framework_stage_root}/${framework_name}"
framework_backup="${framework_stage_root}/previous.framework"
ditto "${source_framework}" "${staged_framework}"
validate_framework_layout "${staged_framework}"

(
  cd "${source_framework}"
  find . -type f -print | LC_ALL=C sort
) >"${source_framework_files}"
(
  cd "${staged_framework}"
  find . -type f -print | LC_ALL=C sort
) >"${staged_framework_files}"
cmp -s "${source_framework_files}" "${staged_framework_files}" || \
  ahoi_die "staged component framework file inventory differs from source"
while IFS= read -r relative_path; do
  cmp -s "${source_framework}/${relative_path}" \
    "${staged_framework}/${relative_path}" || \
    ahoi_die "staged component framework file differs: ${relative_path}"
done <"${source_framework_files}"

(
  cd "${source_framework}"
  find . -type l -print | LC_ALL=C sort
) >"${source_framework_links}"
(
  cd "${staged_framework}"
  find . -type l -print | LC_ALL=C sort
) >"${staged_framework_links}"
cmp -s "${source_framework_links}" "${staged_framework_links}" || \
  ahoi_die "staged component framework symlink inventory differs from source"
while IFS= read -r relative_path; do
  [ "$(readlink "${source_framework}/${relative_path}")" = \
    "$(readlink "${staged_framework}/${relative_path}")" ] || \
    ahoi_die "staged component framework symlink differs: ${relative_path}"
done <"${source_framework_links}"

if [ -e "${destination_framework}" ]; then
  mv "${destination_framework}" "${framework_backup}"
fi
if ! mv "${staged_framework}" "${destination_framework}"; then
  if [ -e "${framework_backup}" ] && [ ! -e "${destination_framework}" ]; then
    mv "${framework_backup}" "${destination_framework}"
  fi
  ahoi_die "failed to activate staged component framework"
fi
rm -rf "${framework_backup}"
framework_backup=""
rmdir "${framework_stage_root}"
framework_stage_root=""

framework_current_version="$(readlink "${destination_framework}/Versions/Current")"
(
  cd "${destination_framework}"
  find "Versions/${framework_current_version}/Resources" -type f -print | \
    LC_ALL=C sort | while IFS= read -r resource_path; do
      shasum -a 256 "${resource_path}"
    done
) >"${new_framework_resource_manifest}"
[ -s "${new_framework_resource_manifest}" ] || \
  ahoi_die "component framework resource manifest is empty"
mv "${new_framework_resource_manifest}" "${framework_resource_manifest}"

find "${out_dir}" -maxdepth 1 -type f -name '*.dylib' -print | \
  LC_ALL=C sort >"${source_names}"
runtime_count="$(wc -l <"${source_names}" | tr -d ' ')"
[ "${runtime_count}" -gt 0 ] || \
  ahoi_die "component build produced no top-level runtime dylibs"
grep -q '/libc++_chrome\.dylib$' "${source_names}" || \
  ahoi_die "component runtime is missing libc++_chrome.dylib"

while IFS= read -r existing; do
  existing_name="$(basename "${existing}")"
  if ! sed 's#.*/##' "${source_names}" | grep -Fqx "${existing_name}"; then
    rm -f "${existing}"
  fi
done < <(find "${runtime_dir}" -maxdepth 1 -type f -name '*.dylib' -print)

while IFS= read -r source; do
  name="$(basename "${source}")"
  destination="${runtime_dir}/${name}"
  if [ ! -f "${destination}" ] || ! cmp -s "${source}" "${destination}"; then
    if ! cp -c -p "${source}" "${destination}" 2>/dev/null; then
      cp -p "${source}" "${destination}"
    fi
  fi
  digest="$(shasum -a 256 "${source}" | awk '{print $1}')"
  printf '%s  %s\n' "${digest}" "${name}" >>"${new_manifest}"
done <"${source_names}"

mv "${new_manifest}" "${manifest}"
ahoi_note "staged current component framework and verified its resource manifest"
ahoi_note "staged ${runtime_count} component runtime dylibs into the portable development app"
