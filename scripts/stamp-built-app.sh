#!/bin/bash

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=scripts/lib/common.sh
source "${SCRIPT_DIR}/lib/common.sh"

app_path="${1:-}"
args_file="${2:-}"
[ -d "${app_path}" ] || ahoi_die "app bundle not found: ${app_path}"
[ -f "${args_file}" ] || ahoi_die "GN args file not found: ${args_file}"
[ -f "${app_path}/Contents/Info.plist" ] || ahoi_die "Info.plist missing"

ahoi_require_command git
ahoi_require_command plutil
ahoi_require_command shasum

source_commit="$(git -C "${AHOI_REPO_ROOT}" rev-parse HEAD)"
chromium_commit="$(git -C "${AHOI_CHROMIUM_SRC}" rev-parse HEAD)"
product_version="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" displayVersion)"
marketing_version="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" marketingVersion)"
build_number="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" buildNumber)"
channel="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/version.json" channel)"
chromium_version="$(ahoi_json_get "${AHOI_REPO_ROOT}/config/chromium.json" version)"
args_hash="$(ahoi_sha256 "${args_file}")"
case "$(basename "${args_file}")" in
  ahoi-dev.gn) build_profile="dev" ;;
  ahoi-full-dev.gn) build_profile="full-dev" ;;
  ahoi-release.gn) build_profile="release" ;;
  ahoi-full-release.gn) build_profile="full-release" ;;
  *) ahoi_die "unsupported Ahoi GN args file: ${args_file}" ;;
esac
plist="${app_path}/Contents/Info.plist"

set_plist_string() {
  local key="$1"
  local value="$2"
  if plutil -extract "${key}" raw "${plist}" >/dev/null 2>&1; then
    plutil -replace "${key}" -string "${value}" "${plist}"
  else
    plutil -insert "${key}" -string "${value}" "${plist}"
  fi
}

set_plist_bool() {
  local key="$1"
  local value="$2"
  if plutil -extract "${key}" raw "${plist}" >/dev/null 2>&1; then
    plutil -replace "${key}" -bool "${value}" "${plist}"
  else
    plutil -insert "${key}" -bool "${value}" "${plist}"
  fi
}

delete_plist_key() {
  local key="$1"
  if plutil -extract "${key}" raw "${plist}" >/dev/null 2>&1; then
    plutil -remove "${key}" "${plist}"
  fi
}

set_plist_string CFBundleShortVersionString "${marketing_version}"
set_plist_string CFBundleVersion "${build_number}"
set_plist_string AhoiProductVersion "${product_version}"
set_plist_string AhoiUpdateChannel "${channel}"
set_plist_string AhoiSourceCommit "${source_commit}"
set_plist_string AhoiChromiumVersion "${chromium_version}"
set_plist_string AhoiChromiumCommit "${chromium_commit}"
set_plist_string AhoiGNArgsSHA256 "${args_hash}"
set_plist_string AhoiBuildProfile "${build_profile}"

sparkle_version="$(python3 - "${AHOI_REPO_ROOT}/config/third-party-pins.json" <<'PY'
import json
import pathlib
import sys
print(json.loads(pathlib.Path(sys.argv[1]).read_text())["dependencies"]["sparkle"]["version"])
PY
)"
sparkle_feed_url="${AHOI_SPARKLE_FEED_URL:-}"
sparkle_public_key="${AHOI_SPARKLE_PUBLIC_ED_KEY:-}"
sparkle_require_policy_match=0
case "${build_profile}" in
  release|full-release) sparkle_require_policy_match=1 ;;
esac
if [ -n "${sparkle_feed_url}" ] || [ -n "${sparkle_public_key}" ]; then
  [ -n "${sparkle_feed_url}" ] && [ -n "${sparkle_public_key}" ] || \
    ahoi_die "Sparkle feed URL and public Ed25519 key must be supplied together"
  AHOI_SPARKLE_REQUIRE_POLICY_MATCH="${sparkle_require_policy_match}" \
    AHOI_RELEASE_POLICY="${AHOI_REPO_ROOT}/config/release-policy.json" \
    AHOI_RELEASE_CHANNEL="${channel}" \
    python3 - <<'PY'
import base64
import binascii
import json
import os
import pathlib
import urllib.parse

url = urllib.parse.urlsplit(os.environ["AHOI_SPARKLE_FEED_URL"])
if url.scheme != "https" or not url.hostname or url.username or url.password or url.fragment:
    raise SystemExit("AHOI_SPARKLE_FEED_URL must be credential-free HTTPS")
try:
    key = base64.b64decode(os.environ["AHOI_SPARKLE_PUBLIC_ED_KEY"], validate=True)
except (ValueError, binascii.Error) as error:
    raise SystemExit("AHOI_SPARKLE_PUBLIC_ED_KEY is not valid base64") from error
if len(key) != 32:
    raise SystemExit("AHOI_SPARKLE_PUBLIC_ED_KEY must decode to 32 bytes")

if os.environ.get("AHOI_SPARKLE_REQUIRE_POLICY_MATCH") == "1":
    policy = json.loads(pathlib.Path(os.environ["AHOI_RELEASE_POLICY"]).read_text())
    channel = os.environ["AHOI_RELEASE_CHANNEL"]
    configured = policy["updates"]["channels"][channel]
    if not all(
        isinstance(configured.get(name), str) and configured[name]
        for name in ("feedUrl", "artifactBaseUrl", "publicEdKey")
    ):
        raise SystemExit("reviewed Sparkle channel policy is incomplete")
    artifact_url = urllib.parse.urlsplit(configured["artifactBaseUrl"])
    if (
        artifact_url.scheme != "https"
        or not artifact_url.hostname
        or artifact_url.username
        or artifact_url.password
        or artifact_url.fragment
        or artifact_url.query
        or not artifact_url.path.endswith("/")
    ):
        raise SystemExit("reviewed Sparkle artifact base URL is not secure HTTPS")
    if configured.get("feedUrl") != os.environ["AHOI_SPARKLE_FEED_URL"]:
        raise SystemExit("AHOI_SPARKLE_FEED_URL differs from reviewed channel policy")
    if configured.get("publicEdKey") != os.environ["AHOI_SPARKLE_PUBLIC_ED_KEY"]:
        raise SystemExit("AHOI_SPARKLE_PUBLIC_ED_KEY differs from reviewed channel policy")
PY
  set_plist_string SUFeedURL "${sparkle_feed_url}"
  set_plist_string SUPublicEDKey "${sparkle_public_key}"
  set_plist_bool AhoiSparkleFeedConfigured true
else
  case "${build_profile}" in
    release|full-release)
      ahoi_die \
        "release-like stamping requires externally reviewed Sparkle feed/key values"
      ;;
  esac
  delete_plist_key SUFeedURL
  delete_plist_key SUPublicEDKey
  set_plist_bool AhoiSparkleFeedConfigured false
fi
set_plist_string AhoiSparkleVersion "${sparkle_version}"
set_plist_bool SURequireSignedFeed true
set_plist_bool SUVerifyUpdateBeforeExtraction true
set_plist_bool SUSendProfileInfo false
set_plist_bool SUEnableAutomaticChecks false
set_plist_bool SUAllowsAutomaticUpdates true

ahoi_note "stamped product/revision provenance and fail-closed Sparkle ${sparkle_version} configuration into Info.plist"
