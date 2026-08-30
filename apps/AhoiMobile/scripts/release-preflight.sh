#!/bin/bash

set -euo pipefail

readonly AHOI_UNRESOLVED_PATTERN='(__AHOI_|\$\(|invalid\.ahoibrowser\.unconfigured)'

ahoi_note() {
  printf 'Ahoi Mobile release preflight: %s\n' "$*"
}

ahoi_die() {
  printf 'Ahoi Mobile release preflight: ERROR: %s\n' "$*" >&2
  exit 1
}

ahoi_require_command() {
  command -v "$1" >/dev/null 2>&1 || ahoi_die "required command is unavailable: $1"
}

ahoi_require_file() {
  [ -f "$1" ] || ahoi_die "required file is missing: $1"
}

ahoi_require_directory() {
  [ -d "$1" ] || ahoi_die "required directory is missing: $1"
}

ahoi_require_resolved_setting() {
  local setting_name="$1"
  local setting_value="${!setting_name:-}"
  [ -n "$setting_value" ] || ahoi_die "$setting_name is empty"
  if printf '%s' "$setting_value" | grep -Eq "$AHOI_UNRESOLVED_PATTERN"; then
    ahoi_die "$setting_name is unresolved"
  fi
}

ahoi_require_release_version() {
  local version_value="$1"
  printf '%s' "$version_value" | grep -Eq '^[0-9]+(\.[0-9]+){1,2}$' ||
    ahoi_die "AHOI_MOBILE_MARKETING_VERSION must contain two or three numeric components"
}

ahoi_require_release_build() {
  local build_value="$1"
  printf '%s' "$build_value" | grep -Eq '^[1-9][0-9]*$' ||
    ahoi_die "AHOI_MOBILE_BUILD_NUMBER must be a positive integer"
}

ahoi_require_export_classification() {
  case "${AHOI_APP_USES_NON_EXEMPT_ENCRYPTION:-}" in
    YES|NO) ;;
    *) ahoi_die "AHOI_APP_USES_NON_EXEMPT_ENCRYPTION must be explicitly classified as YES or NO" ;;
  esac
}

ahoi_plist_raw() {
  local plist_path="$1"
  local key_path="$2"
  plutil -extract "$key_path" raw -o - "$plist_path" 2>/dev/null ||
    ahoi_die "missing or invalid plist key $key_path in $plist_path"
}

ahoi_plist_bool() {
  local plist_path="$1"
  local key_path="$2"
  local key_type
  key_type="$(plutil -type "$key_path" "$plist_path" 2>/dev/null || true)"
  [ "$key_type" = "bool" ] || ahoi_die "$key_path must be a Boolean in $plist_path"
  ahoi_plist_raw "$plist_path" "$key_path"
}

ahoi_require_xml_key_true() {
  local xml_value="$1"
  local key_name="$2"
  local escaped_key_name="${key_name//./\\.}"
  local raw_value
  raw_value="$(
    printf '%s\n' "$xml_value" |
      plutil -extract "$escaped_key_name" raw -o - - 2>/dev/null || true
  )"
  [ "$raw_value" = "true" ] ||
    ahoi_die "required entitlement is absent or false: $key_name"
}

ahoi_require_xml_string() {
  local xml_value="$1"
  local key_name="$2"
  local expected_value="$3"
  local description="$4"
  local escaped_key_name="${key_name//./\\.}"
  local extracted_xml
  extracted_xml="$(
    printf '%s\n' "$xml_value" |
      plutil -extract "$escaped_key_name" xml1 -o - - 2>/dev/null || true
  )"
  printf '%s\n' "$extracted_xml" | grep -Fq "<string>${expected_value}</string>" ||
    ahoi_die "$description is absent from entitlements"
}

ahoi_source_root() {
  if [ -n "${SRCROOT:-}" ]; then
    printf '%s\n' "$SRCROOT"
  else
    cd "$(dirname "$0")/.." && pwd
  fi
}

ahoi_check_source_contract() {
  local source_root="$1"
  local source_info="$source_root/Info.plist"
  local source_icon="$source_root/Sources/AhoiMobileApp/Assets.xcassets/AppIcon.appiconset/AppIcon-1024.png"

  ahoi_require_file "$source_info"
  ahoi_require_file "$source_icon"
  plutil -lint "$source_info" >/dev/null

  [ "$(ahoi_plist_raw "$source_info" CFBundleShortVersionString)" = "\$(AHOI_MOBILE_MARKETING_VERSION)" ] ||
    ahoi_die "Info.plist must source CFBundleShortVersionString from AHOI_MOBILE_MARKETING_VERSION"
  [ "$(ahoi_plist_raw "$source_info" CFBundleVersion)" = "\$(AHOI_MOBILE_BUILD_NUMBER)" ] ||
    ahoi_die "Info.plist must source CFBundleVersion from AHOI_MOBILE_BUILD_NUMBER"

  local background_modes
  background_modes="$(plutil -extract UIBackgroundModes xml1 -o - "$source_info" 2>/dev/null || true)"
  printf '%s\n' "$background_modes" | grep -Fq '<string>remote-notification</string>' ||
    ahoi_die "Info.plist must declare UIBackgroundModes remote-notification"

  local alpha_state
  alpha_state="$(sips -g hasAlpha "$source_icon" 2>/dev/null | awk '/hasAlpha:/ {print $2}')"
  [ "$alpha_state" = "no" ] || ahoi_die "the 1024-point AppIcon source still contains an alpha channel"
}

ahoi_check_build_settings() {
  local source_root
  source_root="$(ahoi_source_root)"
  ahoi_check_source_contract "$source_root"

  if [ "${CONFIGURATION:-Debug}" != "Release" ]; then
    ahoi_note "source contract valid; non-Release configuration remains buildable"
    return
  fi

  local required_setting
  for required_setting in \
    AHOI_APPLE_TEAM_ID \
    AHOI_MOBILE_BUNDLE_ID \
    AHOI_MOBILE_CORE_BUNDLE_ID \
    AHOI_MOBILE_MARKETING_VERSION \
    AHOI_MOBILE_BUILD_NUMBER \
    AHOI_MOBILE_TEST_BUNDLE_ID \
    AHOI_MOBILE_UI_TEST_BUNDLE_ID \
    AHOI_PROVISIONING_PROFILE_SPECIFIER \
    AHOI_CLOUDKIT_CONTAINER_ENVIRONMENT \
    AHOI_CLOUDKIT_CONTAINER_ID \
    AHOI_SYNC_KEYCHAIN_ACCESS_GROUP \
    AHOI_SYNC_KEYCHAIN_SERVICE \
    AHOI_SYNC_KEYCHAIN_ACCOUNT \
    AHOI_SYNC_KEY_VERSION \
    AHOI_COMMAND_KEYCHAIN_ACCESS_GROUP \
    AHOI_COMMAND_KEYCHAIN_SERVICE \
    AHOI_COMMAND_KEYCHAIN_ACCOUNT; do
    ahoi_require_resolved_setting "$required_setting"
  done

  ahoi_require_release_version "$AHOI_MOBILE_MARKETING_VERSION"
  ahoi_require_release_build "$AHOI_MOBILE_BUILD_NUMBER"
  ahoi_require_export_classification
  [ "${AHOI_APS_ENVIRONMENT:-}" = "production" ] ||
    ahoi_die "AHOI_APS_ENVIRONMENT must be production for Release"
  [ "${AHOI_CLOUDKIT_CONTAINER_ENVIRONMENT:-}" = "Production" ] ||
    ahoi_die "AHOI_CLOUDKIT_CONTAINER_ENVIRONMENT must be Production for Release"
  printf '%s' "$AHOI_CLOUDKIT_CONTAINER_ID" | grep -Eq '^iCloud\.[A-Za-z0-9.-]+$' ||
    ahoi_die "AHOI_CLOUDKIT_CONTAINER_ID must be a concrete iCloud container"
  printf '%s' "$AHOI_APPLE_TEAM_ID" | grep -Eq '^[A-Z0-9]{10}$' ||
    ahoi_die "AHOI_APPLE_TEAM_ID must be a ten-character Apple Team ID"
  printf '%s' "$AHOI_MOBILE_BUNDLE_ID" | grep -Eq '^[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+$' ||
    ahoi_die "AHOI_MOBILE_BUNDLE_ID is not a valid explicit bundle identifier"

  local entitlements_path="${CODE_SIGN_ENTITLEMENTS:-}"
  [ "$entitlements_path" = "AhoiMobile.DefaultBrowser.entitlements.template" ] ||
    ahoi_die "Release must use AhoiMobile.DefaultBrowser.entitlements.template"
  ahoi_require_file "$source_root/$entitlements_path"
  ahoi_note "Release settings are resolved and explicitly export-classified"
}

ahoi_find_archive_app() {
  local archive_path="$1"
  local applications_path="$archive_path/Products/Applications"
  ahoi_require_directory "$applications_path"

  local selected_app=""
  local app_count=0
  while IFS= read -r app_candidate; do
    selected_app="$app_candidate"
    app_count=$((app_count + 1))
  done < <(find "$applications_path" -maxdepth 1 -type d -name '*.app' -print)

  [ "$app_count" -eq 1 ] || ahoi_die "archive must contain exactly one top-level application"
  printf '%s\n' "$selected_app"
}

ahoi_check_export_options() {
  local export_options="$1"
  local bundle_identifier="$2"
  ahoi_require_file "$export_options"
  plutil -lint "$export_options" >/dev/null

  if grep -Eq "$AHOI_UNRESOLVED_PATTERN" "$export_options"; then
    ahoi_die "ExportOptions plist still contains unresolved placeholders"
  fi

  [ "$(ahoi_plist_raw "$export_options" method)" = "app-store-connect" ] ||
    ahoi_die "ExportOptions method must be app-store-connect"
  [ "$(ahoi_plist_raw "$export_options" destination)" = "export" ] ||
    ahoi_die "ExportOptions destination must remain export for inspect-before-upload"
  [ "$(ahoi_plist_raw "$export_options" signingStyle)" = "manual" ] ||
    ahoi_die "ExportOptions signingStyle must be manual"
  [ "$(ahoi_plist_raw "$export_options" signingCertificate)" = "Apple Distribution" ] ||
    ahoi_die "ExportOptions must use Apple Distribution"
  [ "$(ahoi_plist_raw "$export_options" distributionBundleIdentifier)" = "$bundle_identifier" ] ||
    ahoi_die "ExportOptions distribution bundle identifier does not match the archive"
  [ "$(ahoi_plist_raw "$export_options" iCloudContainerEnvironment)" = "Production" ] ||
    ahoi_die "ExportOptions must use the Production iCloud environment"
  [ "$(ahoi_plist_bool "$export_options" manageAppVersionAndBuildNumber)" = "false" ] ||
    ahoi_die "ExportOptions must preserve the candidate-bound build number"
  [ "$(ahoi_plist_bool "$export_options" uploadSymbols)" = "true" ] ||
    ahoi_die "ExportOptions must upload symbols"
  [ "$(ahoi_plist_bool "$export_options" testFlightInternalTestingOnly)" = "true" ] ||
    ahoi_die "this template must remain restricted to internal TestFlight testing"

  local profiles_xml
  profiles_xml="$(plutil -extract provisioningProfiles xml1 -o - "$export_options" 2>/dev/null || true)"
  printf '%s\n' "$profiles_xml" | grep -Fq "<key>${bundle_identifier}</key>" ||
    ahoi_die "ExportOptions has no provisioning profile mapping for the archived app"
}

ahoi_check_archive() {
  local archive_path="$1"
  local export_options="$2"
  ahoi_require_directory "$archive_path"

  local app_path
  app_path="$(ahoi_find_archive_app "$archive_path")"
  local app_info="$app_path/Info.plist"
  local embedded_profile="$app_path/embedded.mobileprovision"
  ahoi_require_file "$app_info"
  ahoi_require_file "$embedded_profile"
  plutil -lint "$app_info" >/dev/null

  local bundle_identifier marketing_version build_number encryption_value
  bundle_identifier="$(ahoi_plist_raw "$app_info" CFBundleIdentifier)"
  marketing_version="$(ahoi_plist_raw "$app_info" CFBundleShortVersionString)"
  build_number="$(ahoi_plist_raw "$app_info" CFBundleVersion)"
  encryption_value="$(ahoi_plist_bool "$app_info" ITSAppUsesNonExemptEncryption)"

  printf '%s' "$bundle_identifier" | grep -Eq "$AHOI_UNRESOLVED_PATTERN" &&
    ahoi_die "archived bundle identifier is unresolved"
  ahoi_require_release_version "$marketing_version"
  ahoi_require_release_build "$build_number"
  ahoi_require_export_classification
  if [ "$AHOI_APP_USES_NON_EXEMPT_ENCRYPTION" = "YES" ]; then
    [ "$encryption_value" = "true" ] || ahoi_die "archive encryption declaration disagrees with the explicit YES classification"
  else
    [ "$encryption_value" = "false" ] || ahoi_die "archive encryption declaration disagrees with the explicit NO classification"
  fi
  if [ -n "${AHOI_MOBILE_MARKETING_VERSION:-}" ]; then
    [ "$marketing_version" = "$AHOI_MOBILE_MARKETING_VERSION" ] || ahoi_die "archive marketing version differs from the expected candidate"
  fi
  if [ -n "${AHOI_MOBILE_BUILD_NUMBER:-}" ]; then
    [ "$build_number" = "$AHOI_MOBILE_BUILD_NUMBER" ] || ahoi_die "archive build number differs from the expected candidate"
  fi
  if [ -n "${AHOI_MOBILE_BUNDLE_ID:-}" ]; then
    [ "$bundle_identifier" = "$AHOI_MOBILE_BUNDLE_ID" ] || ahoi_die "archive bundle identifier differs from the expected candidate"
  fi

  local background_modes
  background_modes="$(plutil -extract UIBackgroundModes xml1 -o - "$app_info" 2>/dev/null || true)"
  printf '%s\n' "$background_modes" | grep -Fq '<string>remote-notification</string>' ||
    ahoi_die "archived Info.plist lacks UIBackgroundModes remote-notification"

  codesign --verify --deep --strict "$app_path" >/dev/null 2>&1 ||
    ahoi_die "archived application signature verification failed"
  local app_entitlements
  app_entitlements="$(codesign -d --entitlements :- "$app_path" 2>/dev/null || true)"
  [ -n "$app_entitlements" ] || ahoi_die "archived application has no readable signed entitlements"
  ahoi_require_xml_key_true "$app_entitlements" 'com.apple.developer.web-browser'
  ahoi_require_xml_string "$app_entitlements" 'com.apple.developer.icloud-services' 'CloudKit' 'CloudKit service'
  ahoi_require_xml_string "$app_entitlements" 'aps-environment' 'production' 'production APNs environment'
  ahoi_require_xml_string "$app_entitlements" 'com.apple.developer.icloud-container-environment' 'Production' 'production iCloud environment'

  local profile_xml profile_entitlements profile_app_identifier profile_bundle_identifier
  local profile_expiration profile_expiration_epoch current_epoch
  profile_xml="$(security cms -D -i "$embedded_profile" 2>/dev/null || true)"
  [ -n "$profile_xml" ] || ahoi_die "embedded provisioning profile cannot be decoded"
  profile_entitlements="$(printf '%s\n' "$profile_xml" | plutil -extract Entitlements xml1 -o - - 2>/dev/null || true)"
  [ -n "$profile_entitlements" ] || ahoi_die "embedded provisioning profile has no entitlements"
  profile_app_identifier="$(printf '%s\n' "$profile_xml" | plutil -extract Entitlements.application-identifier raw -o - - 2>/dev/null || true)"
  profile_bundle_identifier="${profile_app_identifier#*.}"
  [ "$profile_bundle_identifier" = "$bundle_identifier" ] ||
    ahoi_die "embedded profile App ID does not match the archived bundle identifier"
  [ "$(printf '%s\n' "$profile_xml" | plutil -extract Entitlements.get-task-allow raw -o - - 2>/dev/null || true)" = "false" ] ||
    ahoi_die "embedded profile is not a distribution profile"
  if printf '%s\n' "$profile_xml" | plutil -extract ProvisionedDevices xml1 -o - - >/dev/null 2>&1; then
    ahoi_die "embedded profile contains development/ad-hoc devices"
  fi
  ahoi_require_xml_key_true "$profile_entitlements" 'com.apple.developer.web-browser'
  ahoi_require_xml_string "$profile_entitlements" 'com.apple.developer.icloud-services' 'CloudKit' 'profile CloudKit service'
  ahoi_require_xml_string "$profile_entitlements" 'aps-environment' 'production' 'profile production APNs environment'
  ahoi_require_xml_string "$profile_entitlements" 'com.apple.developer.icloud-container-environment' 'Production' 'profile production iCloud environment'

  profile_expiration="$(printf '%s\n' "$profile_xml" | plutil -extract ExpirationDate raw -o - - 2>/dev/null || true)"
  [ -n "$profile_expiration" ] || ahoi_die "embedded profile has no expiration date"
  profile_expiration_epoch="$(date -j -f '%Y-%m-%dT%H:%M:%SZ' "$profile_expiration" '+%s' 2>/dev/null || true)"
  [ -n "$profile_expiration_epoch" ] || ahoi_die "embedded profile expiration date is invalid"
  current_epoch="$(date '+%s')"
  [ "$profile_expiration_epoch" -gt "$current_epoch" ] || ahoi_die "embedded profile is expired"

  if [ -n "${AHOI_APPLE_TEAM_ID:-}" ]; then
    [ "$(printf '%s\n' "$profile_xml" | plutil -extract TeamIdentifier.0 raw -o - - 2>/dev/null || true)" = "$AHOI_APPLE_TEAM_ID" ] ||
      ahoi_die "embedded profile Team ID differs from the expected release team"
    ahoi_require_xml_string "$app_entitlements" 'com.apple.developer.team-identifier' "$AHOI_APPLE_TEAM_ID" 'application Team ID'
  fi

  local expected_container
  expected_container="${AHOI_CLOUDKIT_CONTAINER_ID:-}"
  [ -n "$expected_container" ] || ahoi_die "AHOI_CLOUDKIT_CONTAINER_ID is required to inspect an archive"
  ahoi_require_xml_string "$app_entitlements" 'com.apple.developer.icloud-container-identifiers' "$expected_container" 'application CloudKit container'
  ahoi_require_xml_string "$profile_entitlements" 'com.apple.developer.icloud-container-identifiers' "$expected_container" 'profile CloudKit container'

  local keychain_group
  for keychain_group in "${AHOI_SYNC_KEYCHAIN_ACCESS_GROUP:-}" "${AHOI_COMMAND_KEYCHAIN_ACCESS_GROUP:-}"; do
    [ -n "$keychain_group" ] || ahoi_die "both expected Keychain access groups are required to inspect an archive"
    ahoi_require_xml_string "$app_entitlements" 'keychain-access-groups' "$keychain_group" 'application Keychain access group'
    ahoi_require_xml_string "$profile_entitlements" 'keychain-access-groups' "$keychain_group" 'profile Keychain access group'
  done

  local privacy_count=0 privacy_manifest
  while IFS= read -r privacy_manifest; do
    privacy_count=$((privacy_count + 1))
    plutil -lint "$privacy_manifest" >/dev/null || ahoi_die "invalid Privacy Manifest in archive"
    grep -Eq "$AHOI_UNRESOLVED_PATTERN" "$privacy_manifest" &&
      ahoi_die "Privacy Manifest contains an unresolved placeholder"
  done < <(find "$app_path" -type f -name PrivacyInfo.xcprivacy -print)
  [ "$privacy_count" -gt 0 ] || ahoi_die "archive contains no PrivacyInfo.xcprivacy"

  local asset_catalog="$app_path/Assets.car"
  ahoi_require_file "$asset_catalog"
  xcrun assetutil --info "$asset_catalog" 2>/dev/null | awk '
    /^[[:space:]]*\{/ {
      in_object = 1
      is_icon = 0
      is_app_icon = 0
      is_opaque = 0
    }
    in_object && /"AssetType" : "Icon Image"/ { is_icon = 1 }
    in_object && /"Name" : "AppIcon"/ { is_app_icon = 1 }
    in_object && /"Opaque" : true/ { is_opaque = 1 }
    in_object && /^[[:space:]]*\},?[[:space:]]*$/ {
      if (is_icon && is_app_icon) {
        icon_count += 1
        if (!is_opaque) invalid_icon = 1
      }
      in_object = 0
    }
    END {
      if (icon_count == 0 || invalid_icon) exit 1
    }
  ' || ahoi_die "compiled AppIcon asset renditions are missing or not opaque"

  ahoi_check_export_options "$export_options" "$bundle_identifier"
  ahoi_note "archive, profile, signed entitlements, icons, Privacy Manifests, version and ExportOptions passed"
}

ahoi_usage() {
  cat >&2 <<'USAGE'
Usage:
  release-preflight.sh --build-settings
  release-preflight.sh --archive <AhoiMobile.xcarchive> --export-options <ExportOptions.plist>

Archive inspection requires these explicit expected values in the environment:
  AHOI_APP_USES_NON_EXEMPT_ENCRYPTION=YES|NO
  AHOI_CLOUDKIT_CONTAINER_ID=...
  AHOI_SYNC_KEYCHAIN_ACCESS_GROUP=...
  AHOI_COMMAND_KEYCHAIN_ACCESS_GROUP=...

Optional expected values tighten candidate binding:
  AHOI_MOBILE_BUNDLE_ID, AHOI_MOBILE_MARKETING_VERSION,
  AHOI_MOBILE_BUILD_NUMBER
USAGE
  exit 64
}

ahoi_require_command plutil
ahoi_require_command sips

case "${1:-}" in
  --build-settings)
    [ "$#" -eq 1 ] || ahoi_usage
    ahoi_check_build_settings
    ;;
  --archive)
    [ "$#" -eq 4 ] || ahoi_usage
    [ "$3" = "--export-options" ] || ahoi_usage
    ahoi_require_command codesign
    ahoi_require_command security
    ahoi_require_command xcrun
    ahoi_check_archive "$2" "$4"
    ;;
  *)
    ahoi_usage
    ;;
esac
