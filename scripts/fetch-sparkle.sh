#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

ahoi_require_command curl
ahoi_require_command codesign
ahoi_require_command file
ahoi_require_command install
ahoi_require_command lipo
ahoi_require_command plutil
ahoi_require_command python3
ahoi_require_command shasum
ahoi_require_command tar
ahoi_require_command wc

pin_file="${AHOI_REPO_ROOT}/config/third-party-pins.json"
read_pin() {
  python3 - "$pin_file" "$1" <<'PY'
import json
import pathlib
import sys

value = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
for component in sys.argv[2].split("."):
    value = value[component]
if not isinstance(value, (str, int)):
    raise SystemExit("Sparkle pin field is not scalar")
print(value)
PY
}

version="$(read_pin dependencies.sparkle.version)"
commit="$(read_pin dependencies.sparkle.commit)"
archive_url="$(read_pin dependencies.sparkle.archive.url)"
archive_sha256="$(read_pin dependencies.sparkle.archive.sha256)"
archive_size="$(read_pin dependencies.sparkle.archive.size)"
archive_name="Sparkle-${version}.tar.xz"
destination_root="${AHOI_CHROMIUM_SRC}/third_party/sparkle/prebuilt"
framework="${destination_root}/Sparkle.framework"
material_root="${AHOI_STATE_DIR}/sparkle/${version}"
tools_directory="${material_root}/bin"
material_license="${material_root}/LICENSE"
material_receipt="${material_root}/fetch-receipt.json"

if [ -f "${framework}/Resources/Info.plist" ] && \
   [ -f "${tools_directory}/generate_appcast" ] && \
   [ -f "${tools_directory}/sign_update" ] && \
   [ -f "${material_license}" ] && [ -f "${material_receipt}" ]; then
  installed_version="$(plutil -extract CFBundleShortVersionString raw \
    "${framework}/Resources/Info.plist")"
  installed_architectures="$(lipo -archs "${framework}/Versions/B/Sparkle")"
  if [ "${installed_version}" = "${version}" ] && \
     [ "${installed_architectures}" = "arm64" ]; then
    if PYTHONPATH="${AHOI_REPO_ROOT}/tools" python3 - \
      "${pin_file}" "${framework}" "${tools_directory}" \
      "${material_license}" "${material_receipt}" <<'PY'
import pathlib
import sys

from release.sparkle import validate_material_receipt

validate_material_receipt(*(pathlib.Path(value) for value in sys.argv[1:]))
PY
    then
      ahoi_note "Sparkle ${version} verified from fetched-material receipt (${commit})"
      exit 0
    fi
  fi
fi

mkdir -p "${AHOI_STATE_DIR}"
work_dir="$(mktemp -d "${AHOI_STATE_DIR}/sparkle-fetch.XXXXXX")"
cleanup() {
  rm -rf -- "${work_dir}"
}
trap cleanup EXIT
archive="${work_dir}/${archive_name}"

curl --fail --location --proto '=https' --tlsv1.2 \
  --output "${archive}" "${archive_url}"
actual_sha256="$(shasum -a 256 "${archive}" | awk '{print $1}')"
[ "${actual_sha256}" = "${archive_sha256}" ] || \
  ahoi_die "Sparkle archive SHA-256 mismatch"
actual_size="$(wc -c < "${archive}" | tr -d '[:space:]')"
[ "${actual_size}" = "${archive_size}" ] || \
  ahoi_die "Sparkle archive size mismatch"

mkdir -p "${work_dir}/extracted"
tar -xf "${archive}" -C "${work_dir}/extracted" \
  Sparkle.framework bin/generate_appcast bin/sign_update LICENSE
[ -d "${work_dir}/extracted/Sparkle.framework" ] || \
  ahoi_die "Sparkle archive has no framework"

# AhoiBrowser ships arm64-only. Thin the official universal binaries
# deterministically; the release signer later replaces these development
# ad-hoc signatures with leaf-to-root Developer ID signatures.
while IFS= read -r -d '' candidate; do
  case "$(file -b "${candidate}")" in
    *Mach-O*)
      architectures="$(lipo -archs "${candidate}")"
      case " ${architectures} " in
        *' arm64 '*) ;;
        *) ahoi_die "Sparkle binary has no arm64 slice: ${candidate}" ;;
      esac
      if [ "${architectures}" != "arm64" ]; then
        thinned="${candidate}.arm64"
        lipo "${candidate}" -thin arm64 -output "${thinned}"
        mv "${thinned}" "${candidate}"
      fi
      ;;
  esac
done < <(find "${work_dir}/extracted/Sparkle.framework" -type f -print0)

sparkle="${work_dir}/extracted/Sparkle.framework"
codesign --force --sign - --options runtime \
  "${sparkle}/Versions/B/XPCServices/Installer.xpc"
codesign --force --sign - --options runtime --preserve-metadata=entitlements \
  "${sparkle}/Versions/B/XPCServices/Downloader.xpc"
codesign --force --sign - --options runtime \
  "${sparkle}/Versions/B/Autoupdate"
codesign --force --sign - --options runtime \
  "${sparkle}/Versions/B/Updater.app"
codesign --force --sign - --options runtime "${sparkle}"

rm -rf -- "${destination_root}"
mkdir -p "${destination_root}"
mv "${work_dir}/extracted/Sparkle.framework" "${framework}"
mkdir -p "${tools_directory}"
for tool in generate_appcast sign_update; do
  install -m 0755 "${work_dir}/extracted/bin/${tool}" \
    "${tools_directory}/${tool}"
done
install -m 0644 "${work_dir}/extracted/LICENSE" \
  "${material_license}"

installed_version="$(plutil -extract CFBundleShortVersionString raw \
  "${framework}/Resources/Info.plist")"
[ "${installed_version}" = "${version}" ] || \
  ahoi_die "installed Sparkle framework version mismatch"
PYTHONPATH="${AHOI_REPO_ROOT}/tools" python3 - \
  "${pin_file}" "${framework}" "${tools_directory}" \
  "${material_license}" "${material_receipt}" <<'PY'
import pathlib
import sys

from release.sparkle import create_material_receipt

create_material_receipt(*(pathlib.Path(value) for value in sys.argv[1:]))
PY

ahoi_note "installed official Sparkle ${version} (${commit}, ${archive_sha256})"
