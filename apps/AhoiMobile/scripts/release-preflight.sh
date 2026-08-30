#!/bin/bash

set -euo pipefail

readonly AHOI_UNRESOLVED_PATTERN='(__AHOI_|\$\(|invalid\.ahoibrowser\.unconfigured)'
readonly AHOI_PUBLIC_CONFIG_RELATIVE='Configurations/AhoiMobile.Public.xcconfig'

ahoi_note() {
  printf 'Ahoi Mobile signing preflight: %s\n' "$*"
}

ahoi_die() {
  printf 'Ahoi Mobile signing preflight: ERROR: %s\n' "$*" >&2
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

ahoi_require_empty() {
  local name="$1"
  [ -z "${!name:-}" ] || ahoi_die "$name must be empty in ${AHOI_BUILD_MODE:-this mode}"
}

ahoi_require_resolved_setting() {
  local name="$1"
  local value="${!name:-}"
  [ -n "$value" ] || ahoi_die "$name is empty"
  if printf '%s' "$value" | grep -Eq "$AHOI_UNRESOLVED_PATTERN"; then
    ahoi_die "$name is unresolved"
  fi
}

ahoi_require_exact_setting() {
  local name="$1"
  local expected="$2"
  [ "${!name:-}" = "$expected" ] ||
    ahoi_die "$name must equal $expected for ${AHOI_BUILD_MODE:-this mode}"
}

ahoi_source_root() {
  if [ -n "${SRCROOT:-}" ]; then
    printf '%s\n' "$SRCROOT"
  else
    cd "$(dirname "$0")/.." && pwd
  fi
}

ahoi_public_value() {
  local source_root="$1"
  local key="$2"
  local config="$source_root/$AHOI_PUBLIC_CONFIG_RELATIVE"
  local value
  ahoi_require_file "$config"
  value="$(awk -F= -v requested="$key" '
    {
      lhs=$1
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", lhs)
      if (lhs == requested) {
        sub(/^[^=]*=/, "")
        gsub(/^[[:space:]]+|[[:space:]]+$/, "")
        print
        exit
      }
    }
  ' "$config")"
  [ -n "$value" ] || ahoi_die "missing public setting $key in $config"
  printf '%s\n' "$value"
}

ahoi_require_release_version() {
  printf '%s' "$1" | grep -Eq '^[0-9]+(\.[0-9]+){1,2}$' ||
    ahoi_die "AHOI_MOBILE_MARKETING_VERSION must contain two or three numeric components"
}

ahoi_require_release_build() {
  printf '%s' "$1" | grep -Eq '^[1-9][0-9]*$' ||
    ahoi_die "AHOI_MOBILE_BUILD_NUMBER must be a positive integer"
}

ahoi_require_source_commit() {
  local required="$1"
  local value="${AHOI_SOURCE_COMMIT:-}"
  if [ "$required" = "YES" ]; then
    printf '%s' "$value" | grep -Eq '^[0-9a-f]{40}$' ||
      ahoi_die "AHOI_SOURCE_COMMIT must be the exact lowercase 40-character commit SHA"
    return
  fi
  if [ "$value" != "NOT_AVAILABLE" ]; then
    printf '%s' "$value" | grep -Eq '^[0-9a-f]{40}$' ||
      ahoi_die "AHOI_SOURCE_COMMIT must be NOT_AVAILABLE or an exact commit SHA"
  fi
}

ahoi_require_export_classification() {
  case "${AHOI_APP_USES_NON_EXEMPT_ENCRYPTION:-}" in
    YES|NO) ;;
    *) ahoi_die "AHOI_APP_USES_NON_EXEMPT_ENCRYPTION must be explicitly YES or NO" ;;
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
  [ "$(plutil -type "$key_path" "$plist_path" 2>/dev/null || true)" = "bool" ] ||
    ahoi_die "$key_path must be a Boolean in $plist_path"
  ahoi_plist_raw "$plist_path" "$key_path"
}

ahoi_plist_key_absent() {
  local plist_path="$1"
  local key_path="$2"
  if plutil -extract "$key_path" raw -o - "$plist_path" >/dev/null 2>&1; then
    ahoi_die "$key_path must be absent from $plist_path"
  fi
}

ahoi_xml_raw() {
  local xml_value="$1"
  local key_name="$2"
  local escaped_key_name="${key_name//./\\.}"
  printf '%s\n' "$xml_value" |
    plutil -extract "$escaped_key_name" raw -o - - 2>/dev/null || true
}

ahoi_require_xml_key_true() {
  [ "$(ahoi_xml_raw "$1" "$2")" = "true" ] ||
    ahoi_die "required entitlement is absent or false: $2"
}

ahoi_require_xml_key_absent() {
  [ -z "$(ahoi_xml_raw "$1" "$2")" ] ||
    ahoi_die "forbidden entitlement is present: $2"
}

ahoi_require_xml_string() {
  local xml_value="$1"
  local key_name="$2"
  local expected="$3"
  local escaped_key_name="${key_name//./\\.}"
  local extracted
  extracted="$(printf '%s\n' "$xml_value" |
    plutil -extract "$escaped_key_name" xml1 -o - - 2>/dev/null || true)"
  printf '%s\n' "$extracted" | grep -Fq "<string>${expected}</string>" ||
    ahoi_die "$key_name does not contain $expected"
}

ahoi_require_xml_array_exact() {
  local xml_value="$1"
  local key_name="$2"
  shift 2
  printf '%s\n' "$xml_value" | python3 -c '
import plistlib, sys
payload = plistlib.loads(sys.stdin.buffer.read())
actual = payload.get(sys.argv[1])
expected = sys.argv[2:]
raise SystemExit(0 if actual == expected else 1)
' "$key_name" "$@" || ahoi_die "$key_name does not equal the expected ordered values"
}

ahoi_profile_authorizes_group() {
  local profile_entitlements="$1"
  local expected_group="$2"
  local prefix="$3"
  printf '%s\n' "$profile_entitlements" | python3 -c '
import plistlib, sys
payload = plistlib.loads(sys.stdin.buffer.read())
groups = payload.get("keychain-access-groups", [])
expected, wildcard = sys.argv[1], sys.argv[2] + ".*"
raise SystemExit(0 if expected in groups or wildcard in groups else 1)
' "$expected_group" "$prefix" ||
    ahoi_die "profile does not authorize Keychain group $expected_group"
}

ahoi_check_entitlement_template() {
  local path="$1"
  local requires_web_browser="$2"
  local xml
  ahoi_require_file "$path"
  plutil -lint "$path" >/dev/null
  xml="$(plutil -convert xml1 -o - "$path")"
  ahoi_require_xml_array_exact "$xml" \
    'com.apple.developer.icloud-container-identifiers' \
    "\$(AHOI_CLOUDKIT_CONTAINER_ID)"
  ahoi_require_xml_array_exact "$xml" \
    'com.apple.developer.icloud-services' CloudKit
  ahoi_require_xml_string "$xml" \
    'com.apple.developer.icloud-container-environment' \
    "\$(AHOI_CLOUDKIT_CONTAINER_ENVIRONMENT)"
  ahoi_require_xml_array_exact "$xml" keychain-access-groups \
    "\$(AHOI_SYNC_KEYCHAIN_ACCESS_GROUP)" \
    "\$(AHOI_COMMAND_KEYCHAIN_ACCESS_GROUP)"
  ahoi_require_xml_string "$xml" aps-environment "\$(AHOI_APS_ENVIRONMENT)"
  if [ "$requires_web_browser" = "YES" ]; then
    ahoi_require_xml_key_true "$xml" 'com.apple.developer.web-browser'
  else
    ahoi_require_xml_key_absent "$xml" 'com.apple.developer.web-browser'
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
    ahoi_die "Info.plist must source CFBundleShortVersionString from the build setting"
  [ "$(ahoi_plist_raw "$source_info" CFBundleVersion)" = "\$(AHOI_MOBILE_BUILD_NUMBER)" ] ||
    ahoi_die "Info.plist must source CFBundleVersion from the build setting"
  [ "$(ahoi_plist_raw "$source_info" AhoiSourceCommit)" = "\$(AHOI_SOURCE_COMMIT)" ] ||
    ahoi_die "Info.plist must source AhoiSourceCommit from the build setting"
  [ "$(ahoi_plist_raw "$source_info" AhoiBuildMode)" = "\$(AHOI_BUILD_MODE)" ] ||
    ahoi_die "Info.plist must source AhoiBuildMode from the build setting"
  local background_modes
  background_modes="$(plutil -extract UIBackgroundModes xml1 -o - "$source_info" 2>/dev/null || true)"
  printf '%s\n' "$background_modes" | grep -Fq '<string>remote-notification</string>' ||
    ahoi_die "Info.plist must declare UIBackgroundModes remote-notification"
  [ "$(sips -g hasAlpha "$source_icon" 2>/dev/null | awk '/hasAlpha:/ {print $2}')" = "no" ] ||
    ahoi_die "the 1024-point AppIcon source contains an alpha channel"
  ahoi_check_entitlement_template "$source_root/AhoiMobile.entitlements.template" NO
  ahoi_check_entitlement_template \
    "$source_root/AhoiMobile.DefaultBrowser.entitlements.template" YES
}

ahoi_check_signing_style() {
  local mode="$1"
  case "${CODE_SIGN_STYLE:-}" in
    Automatic)
      [ -z "${AHOI_PROVISIONING_PROFILE_SPECIFIER:-}" ] ||
        ahoi_die "Automatic signing must not pin a provisioning profile"
      [ "${AHOI_MANUAL_SIGNING_FALLBACK:-NO}" = "NO" ] ||
        ahoi_die "Automatic signing cannot declare a manual fallback"
      ;;
    Manual)
      [ "$mode" != "DebugLocal" ] || ahoi_die "DebugLocal cannot require manual signing"
      [ "${AHOI_MANUAL_SIGNING_FALLBACK:-NO}" = "YES" ] ||
        ahoi_die "Manual signing requires AHOI_MANUAL_SIGNING_FALLBACK=YES"
      ahoi_require_resolved_setting AHOI_PROVISIONING_PROFILE_SPECIFIER
      ahoi_require_resolved_setting CODE_SIGN_IDENTITY
      case "$mode" in
        CloudKitDevelopment|DefaultBrowserDevelopment)
          [ "$CODE_SIGN_IDENTITY" = "Apple Development" ] ||
            ahoi_die "$mode manual fallback must use Apple Development"
          ;;
        TestFlightBootstrap|ReleasePostGrant)
          [ "$CODE_SIGN_IDENTITY" = "Apple Distribution" ] ||
            ahoi_die "$mode manual fallback must use Apple Distribution"
          ;;
      esac
      ;;
    *) ahoi_die "CODE_SIGN_STYLE must be Automatic or an explicit reviewed Manual fallback" ;;
  esac
}

ahoi_check_public_identity() {
  local source_root="$1"
  local key
  for key in \
    AHOI_APPLE_TEAM_ID \
    AHOI_MOBILE_BUNDLE_ID \
    AHOI_MOBILE_CORE_BUNDLE_ID \
    AHOI_MOBILE_TEST_BUNDLE_ID \
    AHOI_MOBILE_UI_TEST_BUNDLE_ID \
    AHOI_CLOUDKIT_CONTAINER_ID \
    AHOI_SYNC_KEYCHAIN_ACCESS_GROUP \
    AHOI_SYNC_KEYCHAIN_SERVICE \
    AHOI_SYNC_KEYCHAIN_ACCOUNT \
    AHOI_SYNC_KEY_VERSION \
    AHOI_COMMAND_KEYCHAIN_ACCESS_GROUP \
    AHOI_COMMAND_KEYCHAIN_SERVICE \
    AHOI_COMMAND_KEYCHAIN_ACCOUNT; do
    ahoi_require_exact_setting "$key" "$(ahoi_public_value "$source_root" "$key")"
  done
}

ahoi_check_build_settings() {
  local source_root mode expected_entitlements expected_aps expected_cloud source_required
  source_root="$(ahoi_source_root)"
  mode="${AHOI_BUILD_MODE:-}"
  ahoi_check_source_contract "$source_root"
  case "$mode" in
    DebugLocal|CloudKitDevelopment|TestFlightBootstrap|DefaultBrowserDevelopment|ReleasePostGrant) ;;
    *) ahoi_die "AHOI_BUILD_MODE is missing or unsupported: $mode" ;;
  esac
  [ "${CONFIGURATION:-$mode}" = "$mode" ] ||
    ahoi_die "CONFIGURATION must match AHOI_BUILD_MODE ($mode)"
  ahoi_require_exact_setting AHOI_MOBILE_BUNDLE_ID \
    "$(ahoi_public_value "$source_root" AHOI_MOBILE_BUNDLE_ID)"
  ahoi_require_exact_setting AHOI_MOBILE_CORE_BUNDLE_ID \
    "$(ahoi_public_value "$source_root" AHOI_MOBILE_CORE_BUNDLE_ID)"
  ahoi_require_exact_setting AHOI_MOBILE_TEST_BUNDLE_ID \
    "$(ahoi_public_value "$source_root" AHOI_MOBILE_TEST_BUNDLE_ID)"
  ahoi_require_exact_setting AHOI_MOBILE_UI_TEST_BUNDLE_ID \
    "$(ahoi_public_value "$source_root" AHOI_MOBILE_UI_TEST_BUNDLE_ID)"
  ahoi_require_release_version "${AHOI_MOBILE_MARKETING_VERSION:-}"
  ahoi_require_release_build "${AHOI_MOBILE_BUILD_NUMBER:-}"
  ahoi_require_export_classification

  if [ "$mode" = "DebugLocal" ]; then
    local local_only_setting
    for local_only_setting in \
      AHOI_APPLE_TEAM_ID AHOI_APS_ENVIRONMENT AHOI_CLOUDKIT_CONTAINER_ENVIRONMENT \
      AHOI_CLOUDKIT_CONTAINER_ID AHOI_SYNC_KEYCHAIN_ACCESS_GROUP \
      AHOI_SYNC_KEYCHAIN_SERVICE AHOI_SYNC_KEYCHAIN_ACCOUNT AHOI_SYNC_KEY_VERSION \
      AHOI_COMMAND_KEYCHAIN_ACCESS_GROUP AHOI_COMMAND_KEYCHAIN_SERVICE \
      AHOI_COMMAND_KEYCHAIN_ACCOUNT CODE_SIGN_ENTITLEMENTS; do
      ahoi_require_empty "$local_only_setting"
    done
    ahoi_require_source_commit NO
    ahoi_check_signing_style "$mode"
    ahoi_note "DebugLocal is provider-free and contains no CloudKit, Push, Keychain-group or browser entitlement"
    return
  fi

  ahoi_check_public_identity "$source_root"
  expected_entitlements='AhoiMobile.entitlements.template'
  expected_aps=development
  expected_cloud=Development
  source_required=NO
  case "$mode" in
    TestFlightBootstrap)
      expected_aps=production
      expected_cloud=Production
      source_required=YES
      ;;
    DefaultBrowserDevelopment)
      expected_entitlements='AhoiMobile.DefaultBrowser.entitlements.template'
      ;;
    ReleasePostGrant)
      expected_entitlements='AhoiMobile.DefaultBrowser.entitlements.template'
      expected_aps=production
      expected_cloud=Production
      source_required=YES
      ;;
  esac
  ahoi_require_exact_setting CODE_SIGN_ENTITLEMENTS "$expected_entitlements"
  ahoi_require_exact_setting AHOI_APS_ENVIRONMENT "$expected_aps"
  ahoi_require_exact_setting AHOI_CLOUDKIT_CONTAINER_ENVIRONMENT "$expected_cloud"
  ahoi_require_source_commit "$source_required"
  ahoi_check_signing_style "$mode"
  ahoi_note "$mode settings match the exact public identity and entitlement contract"
}

ahoi_check_export_options() {
  local export_options="$1"
  local mode="$2"
  local source_root expected_bundle expected_team signing_style internal_only
  case "$mode" in
    TestFlightBootstrap|ReleasePostGrant) ;;
    *) ahoi_die "ExportOptions are valid only for TestFlightBootstrap or ReleasePostGrant" ;;
  esac
  source_root="$(ahoi_source_root)"
  expected_bundle="$(ahoi_public_value "$source_root" AHOI_MOBILE_BUNDLE_ID)"
  expected_team="$(ahoi_public_value "$source_root" AHOI_APPLE_TEAM_ID)"
  ahoi_require_file "$export_options"
  plutil -lint "$export_options" >/dev/null
  if grep -Eq "$AHOI_UNRESOLVED_PATTERN" "$export_options"; then
    ahoi_die "ExportOptions plist contains unresolved placeholders"
  fi
  [ "$(ahoi_plist_raw "$export_options" method)" = "app-store-connect" ] ||
    ahoi_die "ExportOptions method must be app-store-connect"
  [ "$(ahoi_plist_raw "$export_options" destination)" = "export" ] ||
    ahoi_die "ExportOptions destination must remain export for inspect-before-upload"
  [ "$(ahoi_plist_raw "$export_options" distributionBundleIdentifier)" = "$expected_bundle" ] ||
    ahoi_die "ExportOptions bundle identifier differs from the public Ahoi identity"
  [ "$(ahoi_plist_raw "$export_options" teamID)" = "$expected_team" ] ||
    ahoi_die "ExportOptions Team ID differs from the public Ahoi identity"
  [ "$(ahoi_plist_raw "$export_options" iCloudContainerEnvironment)" = "Production" ] ||
    ahoi_die "ExportOptions must use Production CloudKit"
  [ "$(ahoi_plist_bool "$export_options" manageAppVersionAndBuildNumber)" = "false" ] ||
    ahoi_die "ExportOptions must preserve the candidate-bound build number"
  [ "$(ahoi_plist_bool "$export_options" uploadSymbols)" = "true" ] ||
    ahoi_die "ExportOptions must upload symbols"
  internal_only="$(ahoi_plist_bool "$export_options" testFlightInternalTestingOnly)"
  [ "$internal_only" = "false" ] ||
    ahoi_die "$mode must remain eligible for external/public TestFlight"
  signing_style="$(ahoi_plist_raw "$export_options" signingStyle)"
  case "$signing_style" in
    automatic)
      ahoi_plist_key_absent "$export_options" signingCertificate
      ahoi_plist_key_absent "$export_options" provisioningProfiles
      ;;
    manual)
      [ "${AHOI_MANUAL_SIGNING_FALLBACK:-NO}" = "YES" ] ||
        ahoi_die "manual ExportOptions require the reviewed manual-fallback flag"
      [ "$(ahoi_plist_raw "$export_options" signingCertificate)" = "Apple Distribution" ] ||
        ahoi_die "manual App Store export must use Apple Distribution"
      plutil -extract provisioningProfiles xml1 -o - "$export_options" >/dev/null 2>&1 ||
        ahoi_die "manual ExportOptions require provisioningProfiles"
      ;;
    *) ahoi_die "ExportOptions signingStyle must be automatic or reviewed manual" ;;
  esac
  ahoi_note "$mode ExportOptions permit public TestFlight and preserve candidate identity"
}

ahoi_find_archive_app() {
  local applications="$1/Products/Applications"
  local selected="" count=0 candidate
  ahoi_require_directory "$applications"
  while IFS= read -r candidate; do
    selected="$candidate"
    count=$((count + 1))
  done < <(find "$applications" -maxdepth 1 -type d -name '*.app' -print)
  [ "$count" -eq 1 ] || ahoi_die "archive must contain exactly one top-level app"
  printf '%s\n' "$selected"
}

ahoi_check_archive() {
  local archive="$1"
  local export_options="$2"
  local mode="$3"
  local source_root app info profile bundle version build encryption commit build_mode
  local app_entitlements profile_xml profile_entitlements profile_identifier expiration expiration_epoch
  local expected_team expected_prefix expected_bundle expected_container sync_group command_group
  case "$mode" in
    TestFlightBootstrap|ReleasePostGrant) ;;
    *) ahoi_die "distribution archive inspection does not accept mode $mode" ;;
  esac
  source_root="$(ahoi_source_root)"
  expected_team="$(ahoi_public_value "$source_root" AHOI_APPLE_TEAM_ID)"
  expected_prefix="$(ahoi_public_value "$source_root" AHOI_APP_IDENTIFIER_PREFIX)"
  expected_bundle="$(ahoi_public_value "$source_root" AHOI_MOBILE_BUNDLE_ID)"
  expected_container="$(ahoi_public_value "$source_root" AHOI_CLOUDKIT_CONTAINER_ID)"
  sync_group="$(ahoi_public_value "$source_root" AHOI_SYNC_KEYCHAIN_ACCESS_GROUP)"
  command_group="$(ahoi_public_value "$source_root" AHOI_COMMAND_KEYCHAIN_ACCESS_GROUP)"
  ahoi_require_directory "$archive"
  app="$(ahoi_find_archive_app "$archive")"
  info="$app/Info.plist"
  profile="$app/embedded.mobileprovision"
  ahoi_require_file "$info"
  ahoi_require_file "$profile"
  plutil -lint "$info" >/dev/null
  bundle="$(ahoi_plist_raw "$info" CFBundleIdentifier)"
  version="$(ahoi_plist_raw "$info" CFBundleShortVersionString)"
  build="$(ahoi_plist_raw "$info" CFBundleVersion)"
  encryption="$(ahoi_plist_bool "$info" ITSAppUsesNonExemptEncryption)"
  commit="$(ahoi_plist_raw "$info" AhoiSourceCommit)"
  build_mode="$(ahoi_plist_raw "$info" AhoiBuildMode)"
  [ "$bundle" = "$expected_bundle" ] || ahoi_die "archive bundle identifier is not Ahoi Mobile"
  [ "$build_mode" = "$mode" ] || ahoi_die "archive AhoiBuildMode differs from requested mode"
  printf '%s' "$commit" | grep -Eq '^[0-9a-f]{40}$' ||
    ahoi_die "archive AhoiSourceCommit is not a concrete commit SHA"
  if [ -n "${AHOI_SOURCE_COMMIT:-}" ]; then
    [ "$commit" = "$AHOI_SOURCE_COMMIT" ] || ahoi_die "archive source commit differs from expected"
  fi
  ahoi_require_release_version "$version"
  ahoi_require_release_build "$build"
  if [ -n "${AHOI_MOBILE_MARKETING_VERSION:-}" ]; then
    [ "$version" = "$AHOI_MOBILE_MARKETING_VERSION" ] ||
      ahoi_die "archive marketing version differs from the expected candidate"
  fi
  if [ -n "${AHOI_MOBILE_BUILD_NUMBER:-}" ]; then
    [ "$build" = "$AHOI_MOBILE_BUILD_NUMBER" ] ||
      ahoi_die "archive build number differs from the expected candidate"
  fi
  ahoi_require_export_classification
  if [ "$AHOI_APP_USES_NON_EXEMPT_ENCRYPTION" = "YES" ]; then
    [ "$encryption" = "true" ] || ahoi_die "archive encryption declaration differs from YES"
  else
    [ "$encryption" = "false" ] || ahoi_die "archive encryption declaration differs from NO"
  fi
  codesign --verify --deep --strict "$app" >/dev/null 2>&1 ||
    ahoi_die "archive application signature verification failed"
  app_entitlements="$(codesign -d --entitlements :- "$app" 2>/dev/null || true)"
  [ -n "$app_entitlements" ] || ahoi_die "archive has no readable signed entitlements"
  ahoi_require_xml_string "$app_entitlements" application-identifier \
    "$expected_prefix.$expected_bundle"
  ahoi_require_xml_string "$app_entitlements" \
    'com.apple.developer.team-identifier' "$expected_team"
  ahoi_require_xml_array_exact "$app_entitlements" \
    'com.apple.developer.icloud-container-identifiers' "$expected_container"
  ahoi_require_xml_array_exact "$app_entitlements" \
    'com.apple.developer.icloud-services' CloudKit
  ahoi_require_xml_array_exact "$app_entitlements" keychain-access-groups \
    "$sync_group" "$command_group"
  ahoi_require_xml_string "$app_entitlements" aps-environment production
  ahoi_require_xml_string "$app_entitlements" \
    'com.apple.developer.icloud-container-environment' Production
  if [ "$mode" = "ReleasePostGrant" ]; then
    ahoi_require_xml_key_true "$app_entitlements" 'com.apple.developer.web-browser'
  else
    ahoi_require_xml_key_absent "$app_entitlements" 'com.apple.developer.web-browser'
  fi
  profile_xml="$(security cms -D -i "$profile" 2>/dev/null || true)"
  [ -n "$profile_xml" ] || ahoi_die "embedded provisioning profile cannot be decoded"
  profile_entitlements="$(printf '%s\n' "$profile_xml" |
    plutil -extract Entitlements xml1 -o - - 2>/dev/null || true)"
  [ -n "$profile_entitlements" ] || ahoi_die "profile has no entitlements"
  profile_identifier="$(printf '%s\n' "$profile_xml" |
    plutil -extract Entitlements.application-identifier raw -o - - 2>/dev/null || true)"
  [ "$profile_identifier" = "$expected_prefix.$expected_bundle" ] ||
    ahoi_die "profile App ID differs from the expected Ahoi identity"
  [ "$(printf '%s\n' "$profile_xml" |
    plutil -extract TeamIdentifier.0 raw -o - - 2>/dev/null || true)" = "$expected_team" ] ||
    ahoi_die "profile Team ID differs from the expected Ahoi Team"
  [ "$(printf '%s\n' "$profile_xml" |
    plutil -extract Entitlements.get-task-allow raw -o - - 2>/dev/null || true)" = "false" ] ||
    ahoi_die "archive profile is not a distribution profile"
  if printf '%s\n' "$profile_xml" |
    plutil -extract ProvisionedDevices xml1 -o - - >/dev/null 2>&1; then
    ahoi_die "archive profile contains development/ad-hoc devices"
  fi
  ahoi_require_xml_array_exact "$profile_entitlements" \
    'com.apple.developer.icloud-container-identifiers' "$expected_container"
  ahoi_require_xml_string "$profile_entitlements" aps-environment production
  ahoi_require_xml_string "$profile_entitlements" \
    'com.apple.developer.icloud-container-environment' Production
  ahoi_profile_authorizes_group "$profile_entitlements" "$sync_group" "$expected_prefix"
  ahoi_profile_authorizes_group "$profile_entitlements" "$command_group" "$expected_prefix"
  if [ "$mode" = "ReleasePostGrant" ]; then
    ahoi_require_xml_key_true "$profile_entitlements" 'com.apple.developer.web-browser'
  else
    ahoi_require_xml_key_absent "$profile_entitlements" 'com.apple.developer.web-browser'
  fi
  expiration="$(printf '%s\n' "$profile_xml" |
    plutil -extract ExpirationDate raw -o - - 2>/dev/null || true)"
  expiration_epoch="$(date -j -f '%Y-%m-%dT%H:%M:%SZ' "$expiration" '+%s' 2>/dev/null || true)"
  [ -n "$expiration_epoch" ] && [ "$expiration_epoch" -gt "$(date '+%s')" ] ||
    ahoi_die "embedded provisioning profile is expired or invalid"
  local privacy_count=0 privacy_manifest
  while IFS= read -r privacy_manifest; do
    privacy_count=$((privacy_count + 1))
    plutil -lint "$privacy_manifest" >/dev/null || ahoi_die "invalid archived Privacy Manifest"
  done < <(find "$app" -type f -name PrivacyInfo.xcprivacy -print)
  [ "$privacy_count" -gt 0 ] || ahoi_die "archive contains no Privacy Manifest"
  local asset_catalog="$app/Assets.car"
  ahoi_require_file "$asset_catalog"
  xcrun assetutil --info "$asset_catalog" 2>/dev/null | awk '
    /^[[:space:]]*\{/ { in_object=1; icon=0; app_icon=0; opaque=0 }
    in_object && /"AssetType" : "Icon Image"/ { icon=1 }
    in_object && /"Name" : "AppIcon"/ { app_icon=1 }
    in_object && /"Opaque" : true/ { opaque=1 }
    in_object && /^[[:space:]]*\},?[[:space:]]*$/ {
      if (icon && app_icon) { count += 1; if (!opaque) invalid=1 }
      in_object=0
    }
    END { if (count == 0 || invalid) exit 1 }
  ' || ahoi_die "compiled AppIcon renditions are missing or not opaque"
  ahoi_check_export_options "$export_options" "$mode"
  ahoi_note "$mode archive binds bundle/version/build/source/mode and exact signed capabilities"
}

ahoi_usage() {
  cat >&2 <<'USAGE'
Usage:
  release-preflight.sh --build-settings
  release-preflight.sh --export-options <ExportOptions.plist> --mode <mode>
  release-preflight.sh --archive <AhoiMobile.xcarchive> --export-options <plist> --mode <mode>

Archive inspection requires AHOI_APP_USES_NON_EXEMPT_ENCRYPTION=YES|NO.
AHOI_SOURCE_COMMIT may additionally bind the expected exact source commit.
USAGE
  exit 64
}

ahoi_require_command plutil
ahoi_require_command sips
ahoi_require_command python3

case "${1:-}" in
  --build-settings)
    [ "$#" -eq 1 ] || ahoi_usage
    ahoi_check_build_settings
    ;;
  --export-options)
    [ "$#" -eq 4 ] && [ "$3" = "--mode" ] || ahoi_usage
    ahoi_check_export_options "$2" "$4"
    ;;
  --archive)
    [ "$#" -eq 6 ] && [ "$3" = "--export-options" ] && [ "$5" = "--mode" ] ||
      ahoi_usage
    ahoi_require_command codesign
    ahoi_require_command security
    ahoi_require_command xcrun
    ahoi_check_archive "$2" "$4" "$6"
    ;;
  *) ahoi_usage ;;
esac
