#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "${script_dir}/.." && pwd)"
master_icon="${1:-${repository_root}/assets/branding/ahoi-browser-icon-1024.png}"
mac_icon_mask="${repository_root}/assets/branding/macos-icon-mask-1024.png"
mobile_app_icon="${repository_root}/apps/AhoiMobile/Sources/AhoiMobileApp/Assets.xcassets/AppIcon.appiconset/AppIcon-1024.png"
theme_root="${repository_root}/overlay/chromium/src/chrome/app/theme"
chromium_theme="${theme_root}/chromium"
mac_theme="${chromium_theme}/mac"
app_icon_set="${mac_theme}/Assets.xcassets/AppIcon.appiconset"

if [[ ! -f "${master_icon}" ]]; then
  printf 'Branding master is missing: %s\n' "${master_icon}" >&2
  exit 1
fi
command -v magick >/dev/null
command -v xcrun >/dev/null
command -v iconutil >/dev/null

master_geometry="$(magick identify -format '%wx%h' "${master_icon}")"
if [[ "${master_geometry}" != "1024x1024" ]]; then
  printf 'Branding master must be exactly 1024x1024, got %s\n' \
    "${master_geometry}" >&2
  exit 1
fi
if [[ "$(magick identify -format '%[opaque]' "${master_icon}")" != "True" ]]; then
  printf 'The shared branding master must be opaque for the iOS app icon.\n' >&2
  exit 1
fi
if [[ ! -f "${mac_icon_mask}" ||
      "$(magick identify -format '%wx%h' "${mac_icon_mask}")" != "1024x1024" ]]; then
  printf 'The macOS icon mask must be an existing 1024x1024 image.\n' >&2
  exit 1
fi

staging_root="$(mktemp -d /private/tmp/ahoi-branding.XXXXXX)"
publishing=false
published_destinations=()
published_backups=()

rollback_publish() {
  local index
  local destination
  local backup
  for ((index = ${#published_destinations[@]} - 1; index >= 0; --index)); do
    destination="${published_destinations[index]}"
    backup="${published_backups[index]}"
    if [[ -n "${backup}" && -f "${backup}" ]]; then
      local restore_file
      restore_file="$(mktemp "${destination}.ahoi-restore.XXXXXX")"
      install -m 0644 "${backup}" "${restore_file}"
      mv -f -- "${restore_file}" "${destination}"
    else
      rm -f -- "${destination}"
    fi
  done
}

cleanup_and_exit() {
  local status="$?"
  trap - EXIT
  set +e
  if [[ "${status}" -ne 0 && "${publishing}" == true ]]; then
    rollback_publish
  fi
  case "${staging_root}" in
    /private/tmp/ahoi-branding.*) rm -rf -- "${staging_root}" ;;
    *) printf 'Refusing unsafe branding cleanup path: %s\n' \
         "${staging_root}" >&2 ;;
  esac
  exit "${status}"
}
trap cleanup_and_exit EXIT

staged_theme="${staging_root}/theme"
staged_mac="${staged_theme}/chromium/mac"
staged_app_icon_set="${staged_mac}/Assets.xcassets/AppIcon.appiconset"
staged_iconset="${staged_mac}/Assets.xcassets/Icon.iconset"
compiled_assets="${staging_root}/compiled"
mkdir -p \
  "${staged_theme}/chromium" \
  "${staged_theme}/default_100_percent/chromium" \
  "${staged_theme}/default_200_percent/chromium" \
  "${staged_app_icon_set}" \
  "${staged_iconset}" \
  "${compiled_assets}"
install -m 0644 "${app_icon_set}/Contents.json" \
  "${staged_app_icon_set}/Contents.json"

# Keep one opaque artwork master for iOS. Desktop retains the existing native
# icon footprint; baking that alpha into the iOS asset would create dark corners.
staged_mac_master="${staging_root}/macos-master.png"
staged_mobile_master="${staging_root}/ios-master.png"
magick "${master_icon}" "${mac_icon_mask}" -alpha off \
  -compose CopyOpacity -composite -strip "${staged_mac_master}"
magick "${master_icon}" -alpha off -strip "${staged_mobile_master}"

staged_files=()
destination_files=()

generate_icon() {
  local size="$1"
  local staged_destination="$2"
  local final_destination="$3"
  local source_icon="${4:-${staged_mac_master}}"
  magick "${source_icon}" -filter Lanczos -resize "${size}x${size}!" \
    -strip "${staged_destination}"
  if [[ "$(magick identify -format '%wx%h' "${staged_destination}")" != \
        "${size}x${size}" ]]; then
    printf 'Generated icon has an invalid size: %s\n' \
      "${staged_destination}" >&2
    exit 1
  fi
  staged_files+=("${staged_destination}")
  destination_files+=("${final_destination}")
}

for size in 16 24 48 64 128 256; do
  generate_icon "${size}" \
    "${staged_theme}/chromium/product_logo_${size}.png" \
    "${chromium_theme}/product_logo_${size}.png"
done

generate_icon 16 \
  "${staged_theme}/default_100_percent/chromium/product_logo_16.png" \
  "${theme_root}/default_100_percent/chromium/product_logo_16.png"
generate_icon 32 \
  "${staged_theme}/default_100_percent/chromium/product_logo_32.png" \
  "${theme_root}/default_100_percent/chromium/product_logo_32.png"
generate_icon 32 \
  "${staged_theme}/default_200_percent/chromium/product_logo_16.png" \
  "${theme_root}/default_200_percent/chromium/product_logo_16.png"
generate_icon 64 \
  "${staged_theme}/default_200_percent/chromium/product_logo_32.png" \
  "${theme_root}/default_200_percent/chromium/product_logo_32.png"

for size in 16 32 64 128 256 512 1024; do
  generate_icon "${size}" \
    "${staged_app_icon_set}/appicon_${size}.png" \
    "${app_icon_set}/appicon_${size}.png"
done

for point_size in 16 32 128 256 512; do
  generate_icon "${point_size}" \
    "${staged_iconset}/icon_${point_size}x${point_size}.png" \
    "${mac_theme}/Assets.xcassets/Icon.iconset/icon_${point_size}x${point_size}.png"
  pixel_size=$((point_size * 2))
  generate_icon "${pixel_size}" \
    "${staged_iconset}/icon_${point_size}x${point_size}@2x.png" \
    "${mac_theme}/Assets.xcassets/Icon.iconset/icon_${point_size}x${point_size}@2x.png"
done

generate_icon 1024 "${staging_root}/mobile-appicon.png" \
  "${mobile_app_icon}" "${staged_mobile_master}"

xcrun actool "${staged_mac}/Assets.xcassets" \
  --compile "${compiled_assets}" \
  --platform macosx \
  --minimum-deployment-target 12.0 \
  --app-icon AppIcon \
  --output-partial-info-plist "${compiled_assets}/partial.plist" \
  --warnings --notices >/dev/null
iconutil --convert icns --output "${compiled_assets}/app.icns" \
  "${staged_iconset}"

if [[ ! -f "${compiled_assets}/Assets.car" ||
      ! -f "${compiled_assets}/app.icns" ]]; then
  printf 'Native branding compilation did not produce complete outputs.\n' >&2
  exit 1
fi

backup_root="${staging_root}/backup"
mkdir -p "${backup_root}"

publish_file() {
  local source="$1"
  local destination="$2"
  local publish_file
  local backup=""
  publish_file="$(mktemp "${destination}.ahoi-publish.XXXXXX")"
  install -m 0644 "${source}" "${publish_file}"
  if [[ -f "${destination}" ]]; then
    backup="${backup_root}/${#published_destinations[@]}"
    install -m 0644 "${destination}" "${backup}"
  fi
  mv -f -- "${publish_file}" "${destination}"
  published_destinations+=("${destination}")
  published_backups+=("${backup}")
}

# Publish only after every raster and native asset has been generated and
# validated. Each replacement is an atomic same-directory rename, and any
# later failure rolls every already-published destination back to its previous
# generation.
publishing=true
for index in "${!staged_files[@]}"; do
  publish_file "${staged_files[index]}" "${destination_files[index]}"
done
publish_file "${compiled_assets}/Assets.car" "${mac_theme}/Assets.car"
publish_file "${compiled_assets}/app.icns" "${mac_theme}/app.icns"
publishing=false

printf 'Regenerated AhoiBrowser branding from %s\n' "${master_icon}"
