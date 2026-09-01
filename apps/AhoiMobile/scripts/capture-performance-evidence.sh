#!/bin/bash

set -uo pipefail

readonly AHOI_EXPECTED_BUNDLE_ID='app.ahoibrowser.AhoiBrowser'
readonly AHOI_EXPECTED_TEAM_ID='248AJ5BN47'
readonly AHOI_MIN_DURATION_SECONDS=5
readonly AHOI_MAX_DURATION_SECONDS=60

usage() {
  cat <<'EOF'
Usage:
  capture-performance-evidence.sh \
    --app /absolute/path/AhoiMobile.app \
    --device-udid DEVICE_UDID \
    --output-dir /canonical/repository/path/output-run \
    --expected-source-sha 40_CHARACTER_LOWERCASE_SHA \
    --duration-seconds 5..60 \
    [--dry-run | --validation-only]

Record mode captures checksum-indexed trace bundles, TOC XML, schema XML/HAR,
privacy-bounded app markers, numeric host samples, and command receipts. A raw
capture is never a performance PASS. Only a physical Team 248AJ5BN47 candidate
built as optimized PerformanceDevelopment can become release evidence; simulator
and Debug captures are diagnostic-only. The repository evaluator derives samples
directly from these raw artifacts and accepts no caller-supplied measurements.
Cold runs reinstall the exact SHA-bound candidate; warm runs require a completed,
validated prelaunch marker while preserving that installed candidate and cache.
EOF
}

ahoi_die_early() {
  printf 'Ahoi Mobile performance evidence: ERROR: %s\n' "$*" >&2
  exit 64
}

ahoi_require_value() {
  [ "$#" -ge 2 ] && [ -n "$2" ] || ahoi_die_early "missing value for $1"
}

ahoi_now() { date -u '+%Y-%m-%dT%H:%M:%SZ'; }
ahoi_realpath() { python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$1"; }

AHOI_APP_PATH=''
AHOI_DEVICE_UDID=''
AHOI_OUTPUT_DIR=''
AHOI_EXPECTED_SOURCE_SHA=''
AHOI_DURATION_SECONDS=''
AHOI_MODE='record'

while [ "$#" -gt 0 ]; do
  case "$1" in
    --app) ahoi_require_value "$@"; AHOI_APP_PATH="$2"; shift 2 ;;
    --device-udid) ahoi_require_value "$@"; AHOI_DEVICE_UDID="$2"; shift 2 ;;
    --output-dir) ahoi_require_value "$@"; AHOI_OUTPUT_DIR="$2"; shift 2 ;;
    --expected-source-sha) ahoi_require_value "$@"; AHOI_EXPECTED_SOURCE_SHA="$2"; shift 2 ;;
    --duration-seconds) ahoi_require_value "$@"; AHOI_DURATION_SECONDS="$2"; shift 2 ;;
    --dry-run)
      [ "$AHOI_MODE" = record ] || ahoi_die_early 'capture modes are mutually exclusive'
      AHOI_MODE='dry-run'; shift ;;
    --validation-only)
      [ "$AHOI_MODE" = record ] || ahoi_die_early 'capture modes are mutually exclusive'
      AHOI_MODE='validation-only'; shift ;;
    -h|--help) usage; exit 0 ;;
    *) ahoi_die_early "unknown argument: $1" ;;
  esac
done

for variable in AHOI_APP_PATH AHOI_DEVICE_UDID AHOI_OUTPUT_DIR AHOI_EXPECTED_SOURCE_SHA AHOI_DURATION_SECONDS; do
  [ -n "${!variable}" ] || ahoi_die_early "required argument is absent: $variable"
done
printf '%s' "$AHOI_EXPECTED_SOURCE_SHA" | grep -Eq '^[0-9a-f]{40}$' ||
  ahoi_die_early '--expected-source-sha must be an exact lowercase 40-character SHA'
printf '%s' "$AHOI_DEVICE_UDID" | grep -Eq '^[A-Za-z0-9-]{8,128}$' ||
  ahoi_die_early '--device-udid contains unsupported characters'
printf '%s' "$AHOI_DURATION_SECONDS" | grep -Eq '^[0-9]+$' ||
  ahoi_die_early '--duration-seconds must be an integer'
[ "$AHOI_DURATION_SECONDS" -ge "$AHOI_MIN_DURATION_SECONDS" ] &&
  [ "$AHOI_DURATION_SECONDS" -le "$AHOI_MAX_DURATION_SECONDS" ] ||
  ahoi_die_early '--duration-seconds must be between 5 and 60'

AHOI_SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd -P)" || ahoi_die_early 'cannot resolve harness directory'
readonly AHOI_SCRIPT_DIR
AHOI_REPO_ROOT="$(cd "$AHOI_SCRIPT_DIR/../../.." && pwd -P)" || ahoi_die_early 'cannot resolve repository'
readonly AHOI_REPO_ROOT
readonly AHOI_HELPER="$AHOI_SCRIPT_DIR/mobile-performance-evidence-helper.py"
readonly AHOI_EVALUATOR="$AHOI_SCRIPT_DIR/evaluate-performance-evidence.py"
readonly AHOI_BUDGETS="$AHOI_REPO_ROOT/apps/AhoiMobile/performance-budgets.json"
readonly AHOI_XCRUN='/usr/bin/xcrun'
AHOI_APP_PATH="$(ahoi_realpath "$AHOI_APP_PATH")"
AHOI_OUTPUT_DIR="$(ahoi_realpath "$AHOI_OUTPUT_DIR")"
case "$AHOI_OUTPUT_DIR/" in "$AHOI_REPO_ROOT"/*) ;; *) ahoi_die_early '--output-dir must resolve below the canonical repository' ;; esac
[ "$AHOI_OUTPUT_DIR" != "$AHOI_REPO_ROOT" ] || ahoi_die_early '--output-dir cannot be the repository root'
[ ! -e "$AHOI_OUTPUT_DIR" ] || ahoi_die_early '--output-dir must be a new immutable path'

mkdir -p "$AHOI_OUTPUT_DIR"/{commands,exports,host-samples,markers,metadata/installed,plans,traces} ||
  ahoi_die_early 'could not create evidence output directory'
readonly AHOI_STARTED_AT="$(ahoi_now)"
readonly AHOI_CAPTURE_FILE="$AHOI_OUTPUT_DIR/metadata/captures.tsv"
readonly AHOI_PREPARATION_FILE="$AHOI_OUTPUT_DIR/metadata/preparations.tsv"
readonly AHOI_ERROR_FILE="$AHOI_OUTPUT_DIR/errors.log"
: >"$AHOI_CAPTURE_FILE"
: >"$AHOI_PREPARATION_FILE"
: >"$AHOI_ERROR_FILE"

AHOI_COMMAND_INDEX=0
AHOI_CAPTURE_FAILURES=0
AHOI_UNSUPPORTED_COUNT=0
AHOI_RELEASE_ELIGIBLE=0
AHOI_LAST_STDOUT=''
AHOI_LAST_STDERR=''
AHOI_LAST_LABEL=''
AHOI_EPHEMERAL_DEVICE_DIR=''
AHOI_DEVICECTL_DEVICES_JSON=''
AHOI_ACTIVE_INSTALLATION_ID=''
AHOI_CAPTURE_LAUNCH_TARGET=''

ahoi_relative() {
  python3 -c 'import os,sys; print(os.path.relpath(os.path.realpath(sys.argv[1]), os.path.realpath(sys.argv[2])))' "$1" "$AHOI_OUTPUT_DIR"
}

ahoi_run_logged() {
  local label="$1" padded command_file stdout_file stderr_file exit_file exit_code
  shift
  AHOI_COMMAND_INDEX=$((AHOI_COMMAND_INDEX + 1))
  padded="$(printf '%03d' "$AHOI_COMMAND_INDEX")"
  label="$(printf '%s' "$label" | tr -c 'A-Za-z0-9._-' '-')"
  command_file="$AHOI_OUTPUT_DIR/commands/${padded}-${label}.command.txt"
  stdout_file="$AHOI_OUTPUT_DIR/commands/${padded}-${label}.stdout.log"
  stderr_file="$AHOI_OUTPUT_DIR/commands/${padded}-${label}.stderr.log"
  exit_file="$AHOI_OUTPUT_DIR/commands/${padded}-${label}.exit-code.txt"
  { printf 'cwd='; printf '%q' "$AHOI_REPO_ROOT"; printf '\nargv='; printf '%q ' "$@"; printf '\n'; } >"$command_file"
  (cd "$AHOI_REPO_ROOT" && "$@") >"$stdout_file" 2>"$stderr_file"
  exit_code=$?
  printf '%s\n' "$exit_code" >"$exit_file"
  AHOI_LAST_STDOUT="$stdout_file"
  AHOI_LAST_STDERR="$stderr_file"
  AHOI_LAST_LABEL="${padded}-${label}"
  return "$exit_code"
}

ahoi_die() {
  printf '%s\n' "$*" >>"$AHOI_ERROR_FILE"
  printf 'Ahoi Mobile performance evidence: ERROR: %s\n' "$*" >&2
  exit 1
}

ahoi_require_command() { command -v "$1" >/dev/null 2>&1 || ahoi_die "required command is unavailable: $1"; }
ahoi_require_success() {
  local label="$1"; shift
  ahoi_run_logged "$label" "$@" ||
    ahoi_die "command failed ($AHOI_LAST_LABEL); inspect $(ahoi_relative "$AHOI_LAST_STDERR")"
}

ahoi_read_plist() {
  local key="$1"
  ahoi_require_success "plist-${key}" plutil -extract "$key" raw -o - "$AHOI_APP_PATH/Info.plist"
  AHOI_PLIST_VALUE="$(tr -d '\r\n' <"$AHOI_LAST_STDOUT")"
  [ -n "$AHOI_PLIST_VALUE" ] || ahoi_die "Info.plist key is empty: $key"
}

ahoi_record_capture() {
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$@" >>"$AHOI_CAPTURE_FILE"
}

ahoi_finalize() {
  local original_exit="$1" completed status manifest_exit checksum_exit
  completed="$(ahoi_now)"
  if [ "$original_exit" -ne 0 ]; then
    status='FAILED'
  elif [ "$AHOI_MODE" = dry-run ]; then
    status='DRY_RUN_NOT_PROFILED'
  elif [ "$AHOI_MODE" = validation-only ]; then
    status='VALIDATED_NOT_PROFILED'
  elif [ "$AHOI_RELEASE_ELIGIBLE" -eq 1 ]; then
    status='CAPTURED_RAW_EVIDENCE'
  else
    status='CAPTURED_DIAGNOSTIC_ONLY'
  fi
  python3 "$AHOI_HELPER" write-manifest \
    "$AHOI_OUTPUT_DIR" "$AHOI_STARTED_AT" "$AHOI_MODE" "$AHOI_APP_PATH" \
    "$AHOI_DEVICE_UDID" "$AHOI_EXPECTED_SOURCE_SHA" "$AHOI_DURATION_SECONDS" \
    "$status" "$original_exit" "$completed"
  manifest_exit=$?
  if [ "$manifest_exit" -eq 0 ]; then
    (cd "$AHOI_OUTPUT_DIR" && find . -type f ! -name artifacts.sha256 -print0 | LC_ALL=C sort -z | xargs -0 shasum -a 256) >"$AHOI_OUTPUT_DIR/artifacts.sha256"
    checksum_exit=$?
  else
    checksum_exit=1
  fi
  if [ "$manifest_exit" -ne 0 ] || [ "$checksum_exit" -ne 0 ]; then
    printf 'Ahoi Mobile performance evidence: ERROR: final indexing failed\n' >&2
    [ "$original_exit" -ne 0 ] || original_exit=70
  fi
  printf 'Ahoi Mobile performance evidence: %s (%s)\n' "$status" "$AHOI_OUTPUT_DIR"
  return "$original_exit"
}

ahoi_on_exit() {
  local original_exit="$1"
  trap - EXIT
  if [ -n "$AHOI_DEVICECTL_DEVICES_JSON" ] && [ -f "$AHOI_DEVICECTL_DEVICES_JSON" ]; then unlink "$AHOI_DEVICECTL_DEVICES_JSON" || original_exit=70; fi
  if [ -n "$AHOI_EPHEMERAL_DEVICE_DIR" ] && [ -d "$AHOI_EPHEMERAL_DEVICE_DIR" ]; then rmdir "$AHOI_EPHEMERAL_DEVICE_DIR" || original_exit=70; fi
  ahoi_finalize "$original_exit"
  exit $?
}
trap 'ahoi_on_exit $?' EXIT

for command in python3 plutil shasum file codesign cmp mktemp unlink rmdir xcodebuild xcode-select sw_vers uname; do ahoi_require_command "$command"; done
[ -x "$AHOI_XCRUN" ] || ahoi_die "required command is unavailable: $AHOI_XCRUN"
for source in "$AHOI_HELPER" "$AHOI_EVALUATOR" "$AHOI_BUDGETS"; do [ -f "$source" ] || ahoi_die "evidence source is unavailable: $source"; done
[ -d "$AHOI_APP_PATH" ] && [ -f "$AHOI_APP_PATH/Info.plist" ] || ahoi_die 'the exact .app bundle is incomplete'
case "$AHOI_APP_PATH" in *.app) ;; *) ahoi_die '--app must name an .app bundle' ;; esac
ahoi_require_success info-plist-lint plutil -lint "$AHOI_APP_PATH/Info.plist"

ahoi_read_plist CFBundleIdentifier; AHOI_BUNDLE_ID="$AHOI_PLIST_VALUE"
[ "$AHOI_BUNDLE_ID" = "$AHOI_EXPECTED_BUNDLE_ID" ] || ahoi_die "CFBundleIdentifier must equal $AHOI_EXPECTED_BUNDLE_ID"
ahoi_read_plist CFBundleExecutable; AHOI_EXECUTABLE_NAME="$AHOI_PLIST_VALUE"
case "$AHOI_EXECUTABLE_NAME" in ''|*/*|.|..) ahoi_die 'CFBundleExecutable is unsafe' ;; esac
AHOI_BINARY_PATH="$AHOI_APP_PATH/$AHOI_EXECUTABLE_NAME"
[ -x "$AHOI_BINARY_PATH" ] || ahoi_die 'the declared executable is absent or not executable'
ahoi_require_success binary-file-type file -b "$AHOI_BINARY_PATH"
grep -Fq Mach-O "$AHOI_LAST_STDOUT" || ahoi_die 'the declared executable is not Mach-O'
ahoi_read_plist AhoiSourceCommit; AHOI_EMBEDDED_SOURCE_SHA="$AHOI_PLIST_VALUE"
[ "$AHOI_EMBEDDED_SOURCE_SHA" = "$AHOI_EXPECTED_SOURCE_SHA" ] || ahoi_die 'embedded AhoiSourceCommit does not match --expected-source-sha'
ahoi_read_plist CFBundleShortVersionString; AHOI_MARKETING_VERSION="$AHOI_PLIST_VALUE"
ahoi_read_plist CFBundleVersion; AHOI_BUILD_NUMBER="$AHOI_PLIST_VALUE"
ahoi_read_plist AhoiBuildMode; AHOI_BUILD_MODE="$AHOI_PLIST_VALUE"
ahoi_read_plist AhoiOptimizationLevel; AHOI_OPTIMIZATION_LEVEL="$AHOI_PLIST_VALUE"
case "$AHOI_BUILD_MODE" in DebugLocal|CloudKitDevelopment|DefaultBrowserDevelopment|PerformanceDevelopment) ;; *) ahoi_die 'candidate lacks DEBUG performance workload support' ;; esac
case "$AHOI_OPTIMIZATION_LEVEL" in -Onone|-O) ;; *) ahoi_die 'AhoiOptimizationLevel must be -Onone or -O' ;; esac

ahoi_require_success plist-ios-platform-contract python3 "$AHOI_HELPER" validate-plist "$AHOI_APP_PATH/Info.plist"
AHOI_SUPPORTED_PLATFORM="$(sed -n '1p' "$AHOI_LAST_STDOUT")"
AHOI_DT_PLATFORM_NAME="$(sed -n '2p' "$AHOI_LAST_STDOUT")"
AHOI_DEVICE_FAMILIES="$(sed -n '3p' "$AHOI_LAST_STDOUT")"
ahoi_require_success binary-build-version "$AHOI_XCRUN" vtool -show-build "$AHOI_BINARY_PATH"
AHOI_BINARY_PLATFORM="$(sed -n 's/^[[:space:]]*platform[[:space:]]\{1,\}\([A-Z0-9_]\{1,\}\)$/\1/p' "$AHOI_LAST_STDOUT" | LC_ALL=C sort -u)"
case "$AHOI_BINARY_PLATFORM:$AHOI_SUPPORTED_PLATFORM:$AHOI_DT_PLATFORM_NAME" in IOS:iPhoneOS:iphoneos|IOSSIMULATOR:iPhoneSimulator:iphonesimulator) ;; *) ahoi_die 'binary and plist platform declarations do not match iOS' ;; esac
ahoi_require_success binary-architectures "$AHOI_XCRUN" lipo -archs "$AHOI_BINARY_PATH"
AHOI_BINARY_ARCHITECTURES="$(tr -d '\r\n' <"$AHOI_LAST_STDOUT")"
case " $AHOI_BINARY_ARCHITECTURES " in *' arm64 '*) ;; *) ahoi_die 'candidate must contain arm64' ;; esac

ahoi_require_success xcrun-find-xctrace "$AHOI_XCRUN" --find xctrace
AHOI_XCTRACE_PATH="$(tr -d '\r\n' <"$AHOI_LAST_STDOUT")"
[ -x "$AHOI_XCTRACE_PATH" ] || ahoi_die 'xcrun returned no executable xctrace'
ahoi_require_success xctrace-version "$AHOI_XCRUN" xctrace version; AHOI_XCTRACE_VERSION_LOG="$AHOI_LAST_STDOUT"
ahoi_require_success xcodebuild-version xcodebuild -version; AHOI_XCODE_VERSION_LOG="$AHOI_LAST_STDOUT"
ahoi_require_success xcode-select-path xcode-select -p; AHOI_XCODE_SELECT_LOG="$AHOI_LAST_STDOUT"
ahoi_require_success host-sw-vers sw_vers; AHOI_SW_VERS_LOG="$AHOI_LAST_STDOUT"
ahoi_require_success host-uname uname -a; AHOI_UNAME_LOG="$AHOI_LAST_STDOUT"
python3 "$AHOI_HELPER" write-toolchain \
  "$AHOI_OUTPUT_DIR/metadata/toolchain.json" "$0" "$AHOI_HELPER" "$AHOI_EVALUATOR" \
  "$AHOI_BUDGETS" "$AHOI_XCTRACE_PATH" "$AHOI_XCTRACE_VERSION_LOG" \
  "$AHOI_XCODE_VERSION_LOG" "$AHOI_XCODE_SELECT_LOG" "$AHOI_SW_VERS_LOG" "$AHOI_UNAME_LOG" ||
  ahoi_die 'toolchain metadata generation failed'

ahoi_require_success xctrace-list-templates "$AHOI_XCRUN" xctrace list templates; AHOI_XCTRACE_TEMPLATES_LOG="$AHOI_LAST_STDOUT"
ahoi_require_success xctrace-list-devices "$AHOI_XCRUN" xctrace list devices; AHOI_XCTRACE_DEVICES_LOG="$AHOI_LAST_STDOUT"
ahoi_require_success simctl-list-devices "$AHOI_XCRUN" simctl list devices --json; AHOI_SIMCTL_DEVICES_LOG="$AHOI_LAST_STDOUT"
AHOI_EPHEMERAL_DEVICE_DIR="$(mktemp -d /private/tmp/ahoi-mobile-device-inventory.XXXXXX)" || ahoi_die 'could not create device inventory directory'
AHOI_DEVICECTL_DEVICES_JSON="$AHOI_EPHEMERAL_DEVICE_DIR/devices.json"
ahoi_require_success devicectl-list-devices "$AHOI_XCRUN" devicectl list devices --timeout 15 --json-output "$AHOI_DEVICECTL_DEVICES_JSON" --quiet
python3 "$AHOI_HELPER" write-device "$AHOI_OUTPUT_DIR/metadata/device.json" "$AHOI_DEVICE_UDID" "$AHOI_XCTRACE_DEVICES_LOG" "$AHOI_SIMCTL_DEVICES_LOG" "$AHOI_DEVICECTL_DEVICES_JSON" || ahoi_die 'device metadata resolution failed'
AHOI_DEVICE_KIND="$(python3 "$AHOI_HELPER" device-kind "$AHOI_OUTPUT_DIR/metadata/device.json")" || ahoi_die 'could not read device kind'

ahoi_require_success codesign-verify-strict codesign --verify --deep --strict --verbose=4 "$AHOI_APP_PATH"
ahoi_require_success codesign-details codesign -d --verbose=4 "$AHOI_APP_PATH"
AHOI_CODESIGN_DETAILS="$AHOI_LAST_STDERR"
grep -Fxq "Identifier=$AHOI_EXPECTED_BUNDLE_ID" "$AHOI_CODESIGN_DETAILS" || ahoi_die 'code-signing identifier is wrong'
AHOI_TEAM_IDENTIFIER="$(sed -n 's/^TeamIdentifier=//p' "$AHOI_CODESIGN_DETAILS" | tail -n 1)"
[ -n "$AHOI_TEAM_IDENTIFIER" ] || ahoi_die 'code signature has no TeamIdentifier'
if grep -Fxq 'Signature=adhoc' "$AHOI_CODESIGN_DETAILS"; then AHOI_SIGNATURE_KIND='adhoc'; else AHOI_SIGNATURE_KIND='certificate'; fi
case "$AHOI_DEVICE_KIND:$AHOI_BINARY_PLATFORM" in
  simulator:IOSSIMULATOR) ;;
  physical:IOS)
    [ "$AHOI_SIGNATURE_KIND" = certificate ] || ahoi_die 'ad hoc signing is forbidden for a physical candidate'
    [ "$AHOI_TEAM_IDENTIFIER" = "$AHOI_EXPECTED_TEAM_ID" ] || ahoi_die "physical TeamIdentifier must equal $AHOI_EXPECTED_TEAM_ID" ;;
  *) ahoi_die 'candidate platform and target device classification disagree' ;;
esac
if [ "$AHOI_DEVICE_KIND" = physical ] && [ "$AHOI_BUILD_MODE" = PerformanceDevelopment ] && [ "$AHOI_OPTIMIZATION_LEVEL" = -O ]; then AHOI_RELEASE_ELIGIBLE=1; fi

python3 "$AHOI_HELPER" write-candidate \
  "$AHOI_OUTPUT_DIR/metadata/candidate.json" "$AHOI_APP_PATH" "$AHOI_BUNDLE_ID" \
  "$AHOI_EXECUTABLE_NAME" "$AHOI_BINARY_PATH" "$AHOI_EMBEDDED_SOURCE_SHA" \
  "$AHOI_MARKETING_VERSION" "$AHOI_BUILD_NUMBER" "$AHOI_BUILD_MODE" \
  "$AHOI_OPTIMIZATION_LEVEL" "$AHOI_BINARY_PLATFORM" "$AHOI_SUPPORTED_PLATFORM" \
  "$AHOI_DEVICE_FAMILIES" "$AHOI_BINARY_ARCHITECTURES" "$AHOI_SIGNATURE_KIND" \
  "$AHOI_TEAM_IDENTIFIER" "$AHOI_DEVICE_KIND" || ahoi_die 'candidate metadata generation failed'

readonly AHOI_FIXTURE='-AhoiUITestFixture'
readonly AHOI_NORMAL_COUNT='-AhoiUITestNormalTabCount'
readonly AHOI_PRIVATE_COUNT='-AhoiUITestPrivateTabCount'
readonly AHOI_SELECT_PRIVATE='-AhoiUITestSelectPrivate'
readonly AHOI_WORKLOAD='-AhoiPerformanceWorkload'
readonly AHOI_SCENARIO='-AhoiPerformanceEvidenceScenario'
readonly AHOI_NONCE='-AhoiPerformanceEvidenceNonce'
readonly AHOI_MARKER='-AhoiPerformanceEvidenceMarker'
readonly AHOI_REDUCE_MOTION='-AhoiPerformanceReduceMotionOverride'

ahoi_install_candidate() {
  local installation_id="$1" inventory installed_app install_label
  if [ "$AHOI_DEVICE_KIND" = simulator ]; then
    ahoi_require_success "candidate-install-${installation_id}" \
      "$AHOI_XCRUN" simctl install "$AHOI_DEVICE_UDID" "$AHOI_APP_PATH"
    install_label="$AHOI_LAST_LABEL"
    ahoi_require_success "candidate-container-${installation_id}" \
      "$AHOI_XCRUN" simctl get_app_container "$AHOI_DEVICE_UDID" "$AHOI_BUNDLE_ID" app
    installed_app="$(tr -d '\r\n' <"$AHOI_LAST_STDOUT")"
    [ -d "$installed_app" ] || ahoi_die 'simulator did not expose the installed candidate'
    ahoi_require_success "candidate-binary-${installation_id}" \
      cmp -s "$AHOI_BINARY_PATH" "$installed_app/$AHOI_EXECUTABLE_NAME"
    ahoi_require_success "candidate-plist-${installation_id}" \
      cmp -s "$AHOI_APP_PATH/Info.plist" "$installed_app/Info.plist"
    # Launch by the installed executable name so xctrace cannot reinstall the
    # source bundle and accidentally turn the warm run into another cold run.
    AHOI_CAPTURE_LAUNCH_TARGET="$AHOI_EXECUTABLE_NAME"
  else
    ahoi_require_success "candidate-install-${installation_id}" \
      "$AHOI_XCRUN" devicectl device install app --device "$AHOI_DEVICE_UDID" \
      "$AHOI_APP_PATH" --timeout 30 --quiet
    install_label="$AHOI_LAST_LABEL"
    inventory="$AHOI_OUTPUT_DIR/metadata/installed/${installation_id}.json"
    ahoi_require_success "candidate-inventory-${installation_id}" \
      "$AHOI_XCRUN" devicectl device info apps --device "$AHOI_DEVICE_UDID" \
      --bundle-id "$AHOI_BUNDLE_ID" --json-output "$inventory" --timeout 30 --quiet
    ahoi_require_success "candidate-identity-${installation_id}" python3 -c '
import json, sys
payload = json.load(open(sys.argv[1], encoding="utf-8"))
def objects(value):
    if isinstance(value, dict):
        yield value
        for child in value.values(): yield from objects(child)
    elif isinstance(value, list):
        for child in value: yield from objects(child)
matches = [item for item in objects(payload) if item.get("bundleIdentifier") == sys.argv[2]]
if len(matches) != 1: raise SystemExit("installed physical candidate identity is ambiguous")
item = matches[0]
if str(item.get("version")) != sys.argv[3] or str(item.get("bundleVersion")) != sys.argv[4]:
    raise SystemExit("installed physical candidate version does not match the exact bundle")
' "$inventory" "$AHOI_BUNDLE_ID" "$AHOI_MARKETING_VERSION" "$AHOI_BUILD_NUMBER"
    AHOI_CAPTURE_LAUNCH_TARGET="$AHOI_EXECUTABLE_NAME"
  fi
  AHOI_ACTIVE_INSTALLATION_ID="$installation_id"
  printf '%s\t%s\t%s\t%s\t%s\n' \
    install "$installation_id" "$AHOI_EXPECTED_SOURCE_SHA" "$install_label" \
    "$AHOI_CAPTURE_LAUNCH_TARGET" >>"$AHOI_PREPARATION_FILE"
}

ahoi_uninstall_candidate() {
  local capture_id="$1"
  if [ "$AHOI_DEVICE_KIND" = simulator ]; then
    ahoi_require_success "candidate-uninstall-${capture_id}" \
      "$AHOI_XCRUN" simctl uninstall "$AHOI_DEVICE_UDID" "$AHOI_BUNDLE_ID"
  else
    ahoi_require_success "candidate-uninstall-${capture_id}" \
      "$AHOI_XCRUN" devicectl device uninstall app --device "$AHOI_DEVICE_UDID" \
      "$AHOI_BUNDLE_ID" --timeout 30 --quiet
  fi
  AHOI_ACTIVE_INSTALLATION_ID=''
  AHOI_CAPTURE_LAUNCH_TARGET=''
}

ahoi_wait_for_preparation_marker() {
  local capture_id="$1" slug="$2" nonce="$3" workload="$4" source_name="$5"
  local destination="$AHOI_OUTPUT_DIR/markers/${capture_id}-preparation.json" container
  if [ "$AHOI_DEVICE_KIND" = simulator ]; then
    ahoi_require_success "warm-container-${capture_id}" \
      "$AHOI_XCRUN" simctl get_app_container "$AHOI_DEVICE_UDID" "$AHOI_BUNDLE_ID" data
    container="$(tr -d '\r\n' <"$AHOI_LAST_STDOUT")"
    ahoi_require_success "warm-marker-${capture_id}" /bin/bash -c '
for attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
  if cp "$1" "$2.tmp" 2>/dev/null && python3 "$3" validate-workload-marker "$2.tmp" "$4" "$5" "$6"; then mv "$2.tmp" "$2"; exit 0; fi
  rm -f "$2.tmp"; sleep 1
done
exit 1
' _ "$container/tmp/$source_name" "$destination" "$AHOI_HELPER" "$slug" "$nonce" "$workload"
  else
    ahoi_require_success "warm-marker-${capture_id}" /bin/bash -c '
for attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
  rm -f "$6.tmp"
  if "$1" devicectl device copy from --device "$2" --source "tmp/$3" --destination "$6.tmp" --domain-type appDataContainer --domain-identifier "$4" --timeout 15 --quiet >/dev/null 2>&1 && python3 "$5" validate-workload-marker "$6.tmp" "$7" "$8" "$9"; then mv "$6.tmp" "$6"; exit 0; fi
  sleep 1
done
exit 1
' _ "$AHOI_XCRUN" "$AHOI_DEVICE_UDID" "$source_name" "$AHOI_BUNDLE_ID" \
      "$AHOI_HELPER" "$destination" "$slug" "$nonce" "$workload"
  fi
}

ahoi_prepare_warm_launch() {
  local capture_id="$1" nonce="$2" workload="$3" source_name="$4"
  shift 4
  local pid='' launch_receipt
  [ -n "$AHOI_ACTIVE_INSTALLATION_ID" ] && [ -n "$AHOI_CAPTURE_LAUNCH_TARGET" ] ||
    ahoi_die 'warm preparation has no exact installed candidate'
  if [ "$AHOI_DEVICE_KIND" = simulator ]; then
    ahoi_require_success "warm-prelaunch-${capture_id}" \
      "$AHOI_XCRUN" simctl launch --terminate-running-process "$AHOI_DEVICE_UDID" \
      "$AHOI_BUNDLE_ID" "$@"
  else
    launch_receipt="$AHOI_OUTPUT_DIR/metadata/installed/${capture_id}-prelaunch.json"
    ahoi_require_success "warm-prelaunch-${capture_id}" \
      "$AHOI_XCRUN" devicectl device process launch --terminate-existing \
      --device "$AHOI_DEVICE_UDID" --json-output "$launch_receipt" --timeout 30 \
      "$AHOI_BUNDLE_ID" "$@"
    ahoi_require_success "warm-pid-${capture_id}" python3 -c '
import json, sys
payload = json.load(open(sys.argv[1], encoding="utf-8"))
def values(value):
    if isinstance(value, dict):
        for key, child in value.items():
            if key in {"processIdentifier", "pid"} and isinstance(child, int): yield child
            yield from values(child)
    elif isinstance(value, list):
        for child in value: yield from values(child)
pids = sorted(set(values(payload)))
if len(pids) != 1: raise SystemExit("warm physical prelaunch returned no unique process identifier")
print(pids[0])
' "$launch_receipt"
    pid="$(tr -d '\r\n' <"$AHOI_LAST_STDOUT")"
    printf '%s' "$pid" | grep -Eq '^[0-9]+$' || ahoi_die 'warm physical prelaunch returned no process identifier'
  fi
  ahoi_wait_for_preparation_marker "$capture_id" launch-warm-cache "$nonce" "$workload" "$source_name"
  if [ "$AHOI_DEVICE_KIND" = simulator ]; then
    ahoi_require_success "warm-terminate-${capture_id}" \
      "$AHOI_XCRUN" simctl terminate "$AHOI_DEVICE_UDID" "$AHOI_BUNDLE_ID"
  else
    ahoi_require_success "warm-terminate-${capture_id}" \
      "$AHOI_XCRUN" devicectl device process terminate --device "$AHOI_DEVICE_UDID" \
      --pid "$pid" --timeout 15 --quiet
  fi
  printf '%s\t%s\t%s\t%s\t%s\n' \
    warm-prelaunch "$capture_id" "$AHOI_EXPECTED_SOURCE_SHA" \
    "$AHOI_ACTIVE_INSTALLATION_ID" "$nonce" >>"$AHOI_PREPARATION_FILE"
}

ahoi_data_query() {
  case "$1" in
    life-cycle-period) printf '%s' '/trace-toc/run[@number="1"]/data/table[@schema="life-cycle-period"]' ;;
    sysmon-process) printf '%s' '/trace-toc/run[@number="1"]/data/table[@schema="sysmon-process"]' ;;
    hitches-summary) printf '%s' '/trace-toc/run[@number="1"]/data/table[@schema="hitches-summary"]' ;;
    har) printf '%s' 'HAR-v1' ;;
    *) return 1 ;;
  esac
}

ahoi_plan_capture() {
  local capture_id="$1" template="$2" seconds="$3" plan="$4" preparation="$5"; shift 5
  { printf 'preparation=%s\n' "$preparation"; printf 'candidateSourceSHA=%s\n' "$AHOI_EXPECTED_SOURCE_SHA"; printf 'cwd='; printf '%q' "$AHOI_REPO_ROOT"; printf '\nargv='; printf '%q ' "$AHOI_XCRUN" xctrace record --template "$template" --device "$AHOI_DEVICE_UDID" --time-limit "${seconds}s" --no-prompt --output "$AHOI_OUTPUT_DIR/traces/${capture_id}.trace" --launch -- "$AHOI_APP_PATH" "$@"; printf '\n'; } >"$plan"
}

ahoi_collect_marker() {
  local capture_id="$1" slug="$2" nonce="$3" workload="$4" source_name="$5"
  local destination="$AHOI_OUTPUT_DIR/markers/${capture_id}.json" container
  if [ "$AHOI_DEVICE_KIND" = simulator ]; then
    ahoi_run_logged "marker-container-${capture_id}" "$AHOI_XCRUN" simctl get_app_container "$AHOI_DEVICE_UDID" "$AHOI_BUNDLE_ID" data || return 1
    container="$(tr -d '\r\n' <"$AHOI_LAST_STDOUT")"
    ahoi_run_logged "marker-copy-${capture_id}" cp "$container/tmp/$source_name" "$destination" || return 1
  else
    ahoi_run_logged "marker-copy-${capture_id}" "$AHOI_XCRUN" devicectl device copy from --device "$AHOI_DEVICE_UDID" --source "tmp/$source_name" --destination "$destination" --domain-type appDataContainer --domain-identifier "$AHOI_BUNDLE_ID" --timeout 15 --quiet || return 1
  fi
  ahoi_run_logged "marker-validate-${capture_id}" python3 "$AHOI_HELPER" validate-workload-marker "$destination" "$slug" "$nonce" "$workload"
}

ahoi_process_capture() {
  local slug="$1" run="$2" template="$3" kind="$4" workload="$5" seconds="$6" motion="$7"
  shift 7
  local capture_id="${slug}-run-$(printf '%02d' "$run")"
  local nonce="${AHOI_EXPECTED_SOURCE_SHA}-$$-${capture_id}"
  local preparation prep_nonce
  local marker_source="ahoi-performance-${slug}.json"
  local trace_rel="traces/${capture_id}.trace" toc_rel="exports/${capture_id}.toc.xml"
  local data_rel="exports/${capture_id}.${kind}.raw" marker_rel="markers/${capture_id}.json"
  local host_rel="host-samples/${capture_id}.json" query plan command_label capture_exit
  case "$slug" in
    launch-cold) preparation='clean-install' ;;
    launch-warm-cache) preparation='completed-prelaunch' ;;
    *) preparation='retained-install' ;;
  esac
  query="$(ahoi_data_query "$kind")" || ahoi_die "unknown raw export kind: $kind"
  grep -Fxq "$template" "$AHOI_XCTRACE_TEMPLATES_LOG" || {
    AHOI_UNSUPPORTED_COUNT=$((AHOI_UNSUPPORTED_COUNT + 1))
    ahoi_record_capture "$slug" "$run" "$nonce" "$template" UNSUPPORTED '' '' "$kind" '' "$query" '' '' ''
    return
  }
  local launch_args=("$@" "$AHOI_WORKLOAD" "$workload" "$AHOI_SCENARIO" "$slug" "$AHOI_NONCE" "$nonce" "$AHOI_MARKER" "$marker_source")
  if [ -n "$motion" ]; then launch_args+=("$AHOI_REDUCE_MOTION" "$motion"); fi
  if [ "$AHOI_MODE" = validation-only ]; then
    ahoi_record_capture "$slug" "$run" "$nonce" "$template" SUPPORTED_NOT_CAPTURED '' '' "$kind" '' "$query" '' '' ''
    return
  fi
  if [ "$AHOI_MODE" = dry-run ]; then
    plan="$AHOI_OUTPUT_DIR/plans/${capture_id}.command.txt"
    ahoi_plan_capture "$capture_id" "$template" "$seconds" "$plan" "$preparation" "${launch_args[@]}"
    ahoi_record_capture "$slug" "$run" "$nonce" "$template" PLANNED_NOT_CAPTURED "$(ahoi_relative "$plan")" '' "$kind" '' "$query" '' '' ''
    return
  fi
  if [ "$slug" = launch-cold ]; then
    ahoi_uninstall_candidate "$capture_id"
    ahoi_install_candidate "$capture_id"
  elif [ "$slug" = launch-warm-cache ]; then
    prep_nonce="${nonce}-preparation"
    local prep_args=("$@" "$AHOI_WORKLOAD" "$workload" "$AHOI_SCENARIO" "$slug" "$AHOI_NONCE" "$prep_nonce" "$AHOI_MARKER" "$marker_source")
    if [ -n "$motion" ]; then prep_args+=("$AHOI_REDUCE_MOTION" "$motion"); fi
    ahoi_prepare_warm_launch "$capture_id" "$prep_nonce" "$workload" "$marker_source" "${prep_args[@]}"
  fi
  [ -n "$AHOI_ACTIVE_INSTALLATION_ID" ] && [ -n "$AHOI_CAPTURE_LAUNCH_TARGET" ] ||
    ahoi_die "$capture_id has no exact installed candidate binding"
  ahoi_run_logged "host-sample-${capture_id}" python3 "$AHOI_HELPER" write-host-sample "$AHOI_OUTPUT_DIR/$host_rel" || { AHOI_CAPTURE_FAILURES=$((AHOI_CAPTURE_FAILURES + 1)); return; }
  ahoi_run_logged "capture-${capture_id}" "$AHOI_XCRUN" xctrace record --template "$template" --device "$AHOI_DEVICE_UDID" --time-limit "${seconds}s" --no-prompt --output "$AHOI_OUTPUT_DIR/$trace_rel" --launch -- "$AHOI_CAPTURE_LAUNCH_TARGET" "${launch_args[@]}"
  capture_exit=$?; command_label="$AHOI_LAST_LABEL"
  if [ "$capture_exit" -ne 0 ] || [ ! -d "$AHOI_OUTPUT_DIR/$trace_rel" ]; then AHOI_CAPTURE_FAILURES=$((AHOI_CAPTURE_FAILURES + 1)); return; fi
  ahoi_run_logged "export-${capture_id}-toc" "$AHOI_XCRUN" xctrace export --input "$AHOI_OUTPUT_DIR/$trace_rel" --toc --output "$AHOI_OUTPUT_DIR/$toc_rel" || { AHOI_CAPTURE_FAILURES=$((AHOI_CAPTURE_FAILURES + 1)); return; }
  if [ "$kind" = har ]; then
    ahoi_run_logged "export-${capture_id}-har" "$AHOI_XCRUN" xctrace export --input "$AHOI_OUTPUT_DIR/$trace_rel" --har --output "$AHOI_OUTPUT_DIR/$data_rel" || { AHOI_CAPTURE_FAILURES=$((AHOI_CAPTURE_FAILURES + 1)); return; }
  else
    ahoi_run_logged "export-${capture_id}-${kind}" "$AHOI_XCRUN" xctrace export --input "$AHOI_OUTPUT_DIR/$trace_rel" --xpath "$query" --output "$AHOI_OUTPUT_DIR/$data_rel" || { AHOI_CAPTURE_FAILURES=$((AHOI_CAPTURE_FAILURES + 1)); return; }
  fi
  [ -s "$AHOI_OUTPUT_DIR/$toc_rel" ] && [ -s "$AHOI_OUTPUT_DIR/$data_rel" ] || { AHOI_CAPTURE_FAILURES=$((AHOI_CAPTURE_FAILURES + 1)); return; }
  ahoi_collect_marker "$capture_id" "$slug" "$nonce" "$workload" "$marker_source" || { AHOI_CAPTURE_FAILURES=$((AHOI_CAPTURE_FAILURES + 1)); return; }
  ahoi_record_capture "$slug" "$run" "$nonce" "$template" CAPTURED_RAW "$trace_rel" "$toc_rel" "$kind" "$data_rel" "$query" "$marker_rel" "$host_rel" "$command_label"
}

# Five process launches per launch class, one sampled Activity Monitor trace per
# tab scale, three independent HAR captures, and separate 30-second motion runs.
if [ "$AHOI_MODE" = record ]; then ahoi_install_candidate baseline; fi
for run in 1 2 3 4 5; do ahoi_process_capture launch-cold "$run" 'App Launch' life-cycle-period idle "$AHOI_DURATION_SECONDS" '' "$AHOI_FIXTURE" "$AHOI_NORMAL_COUNT" 1; done
for run in 1 2 3 4 5; do ahoi_process_capture launch-warm-cache "$run" 'App Launch' life-cycle-period idle "$AHOI_DURATION_SECONDS" '' "$AHOI_FIXTURE" "$AHOI_NORMAL_COUNT" 1; done
ahoi_process_capture memory-normal-1 1 'Activity Monitor' sysmon-process idle "$AHOI_DURATION_SECONDS" '' "$AHOI_FIXTURE" "$AHOI_NORMAL_COUNT" 1
ahoi_process_capture memory-normal-5 1 'Activity Monitor' sysmon-process idle "$AHOI_DURATION_SECONDS" '' "$AHOI_FIXTURE" "$AHOI_NORMAL_COUNT" 5
ahoi_process_capture memory-normal-20-discard-restore 1 'Activity Monitor' sysmon-process discard-restore "$AHOI_DURATION_SECONDS" '' "$AHOI_FIXTURE" "$AHOI_NORMAL_COUNT" 20
ahoi_process_capture memory-private-1 1 'Activity Monitor' sysmon-process idle "$AHOI_DURATION_SECONDS" '' "$AHOI_FIXTURE" "$AHOI_PRIVATE_COUNT" 1 "$AHOI_SELECT_PRIVATE"
ahoi_process_capture memory-private-5 1 'Activity Monitor' sysmon-process idle "$AHOI_DURATION_SECONDS" '' "$AHOI_FIXTURE" "$AHOI_PRIVATE_COUNT" 5 "$AHOI_SELECT_PRIVATE"
ahoi_process_capture memory-private-20-discard-restore 1 'Activity Monitor' sysmon-process discard-restore "$AHOI_DURATION_SECONDS" '' "$AHOI_FIXTURE" "$AHOI_PRIVATE_COUNT" 20 "$AHOI_SELECT_PRIVATE"
ahoi_process_capture idle-resources 1 'Activity Monitor' sysmon-process idle 30 '' "$AHOI_FIXTURE" "$AHOI_NORMAL_COUNT" 1
for run in 1 2 3; do ahoi_process_capture idle-network "$run" Network har idle "$AHOI_DURATION_SECONDS" '' "$AHOI_FIXTURE" "$AHOI_NORMAL_COUNT" 1; done
ahoi_process_capture controller-pressure-policy 1 'Activity Monitor' sysmon-process lifecycle-flush "$AHOI_DURATION_SECONDS" '' "$AHOI_FIXTURE" "$AHOI_NORMAL_COUNT" 20
ahoi_process_capture scroll-motion-standard 1 'Animation Hitches' hitches-summary scroll 30 false "$AHOI_FIXTURE" "$AHOI_NORMAL_COUNT" 1
ahoi_process_capture scroll-motion-reduced 1 'Animation Hitches' hitches-summary scroll 30 true "$AHOI_FIXTURE" "$AHOI_NORMAL_COUNT" 1

[ "$AHOI_UNSUPPORTED_COUNT" -eq 0 ] || ahoi_die "$AHOI_UNSUPPORTED_COUNT required xctrace stage(s) are unsupported"
[ "$AHOI_CAPTURE_FAILURES" -eq 0 ] || ahoi_die "$AHOI_CAPTURE_FAILURES raw capture(s) failed"
exit 0
