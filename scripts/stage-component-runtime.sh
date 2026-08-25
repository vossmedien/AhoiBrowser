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
runtime_dir="${app_path}/Contents/Frameworks"
manifest="${app_path}/Contents/Resources/ahoi-component-runtime.sha256"
source_names="$(mktemp -t ahoi-component-runtime-names.XXXXXX)"
new_manifest="$(mktemp -t ahoi-component-runtime-manifest.XXXXXX)"
cleanup() {
  rm -f "${source_names}" "${new_manifest}"
}
trap cleanup EXIT HUP INT TERM

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
ahoi_note "staged ${runtime_count} component runtime dylibs into the portable development app"
