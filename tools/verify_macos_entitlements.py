#!/usr/bin/env python3
"""Validate signed Ahoi macOS entitlements and provisioning profiles."""

from __future__ import annotations

import argparse
import copy
import datetime as dt
import hashlib
import json
import pathlib
import plistlib
import re
import subprocess
import sys
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[1]
SIGNING_PROFILES = {
    "provider-free",
    "cloudkit-development",
    "cloudkit-production",
}
PROFILE_KEYS = {
    "signingAuthorityPrefix",
    "embeddedProvisioningProfile",
    "cloudKitEnvironment",
    "apsEnvironment",
    "getTaskAllow",
}
PUBLIC_IDENTITY_KEYS = {
    "teamIdentifier",
    "appIdentifierPrefix",
    "bundleIdentifier",
    "cloudKitContainerIdentifier",
    "keychainAccessGroups",
}
RUNTIME_CONFIGURATION_KEYS = {
    "zoneName",
    "subscriptionIdentifier",
    "syncKeychainService",
    "syncKeychainAccount",
    "syncKeyVersion",
    "commandKeychainService",
    "commandKeychainAccount",
}
CLOUDKIT_ENTITLEMENT_KEYS = {
    "com.apple.application-identifier",
    "com.apple.developer.team-identifier",
    "com.apple.developer.icloud-container-identifiers",
    "com.apple.developer.icloud-services",
    "com.apple.developer.icloud-container-environment",
    "com.apple.developer.aps-environment",
    "keychain-access-groups",
}
CLOUDKIT_RUNTIME_KEYS = {
    "AHOI_CLOUDKIT_CONTAINER_ID",
    "AHOI_CLOUDKIT_ZONE_NAME",
    "AHOI_CLOUDKIT_SUBSCRIPTION_ID",
    "AHOI_CLOUDKIT_CONTAINER_ENVIRONMENT",
    "AHOI_APS_ENVIRONMENT",
    "AHOI_SYNC_KEYCHAIN_SERVICE",
    "AHOI_SYNC_KEYCHAIN_ACCOUNT",
    "AHOI_SYNC_KEYCHAIN_ACCESS_GROUP",
    "AHOI_SYNC_KEY_VERSION",
    "AHOI_COMMAND_KEYCHAIN_SERVICE",
    "AHOI_COMMAND_KEYCHAIN_ACCOUNT",
    "AHOI_COMMAND_KEYCHAIN_ACCESS_GROUP",
    "AhoiMacCloudKitSigningProfile",
    "AhoiProvisioningProfileUUID",
}


def _require_string(value: Any, name: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise SystemExit(f"{name} must be a non-empty string")
    return value


def _reject_unsafe_string(value: str, name: str) -> None:
    if (
        "DisplayPilot" in value
        or "$(" in value
        or "__AHOI_" in value
        or "*" in value
    ):
        raise SystemExit(f"{name} contains a placeholder, wildcard or foreign value")


def signing_profile(policy: dict[str, Any], name: str) -> dict[str, Any]:
    profiles = policy["signingProfiles"]
    if name not in profiles:
        raise SystemExit(f"unsupported macOS signing profile: {name}")
    return profiles[name]


def expected_rule_entitlements(
    policy: dict[str, Any], profile_name: str, rule: dict[str, Any]
) -> dict[str, Any]:
    expected = copy.deepcopy(rule["entitlements"])
    if rule["id"] != "browser-app" or profile_name == "provider-free":
        return expected
    identity = policy["publicIdentity"]
    profile = signing_profile(policy, profile_name)
    expected.update(
        {
            "com.apple.application-identifier": (
                f"{identity['appIdentifierPrefix']}.{identity['bundleIdentifier']}"
            ),
            "com.apple.developer.team-identifier": identity["teamIdentifier"],
            "com.apple.developer.icloud-container-identifiers": [
                identity["cloudKitContainerIdentifier"]
            ],
            "com.apple.developer.icloud-services": ["CloudKit"],
            "com.apple.developer.icloud-container-environment": profile[
                "cloudKitEnvironment"
            ],
            "com.apple.developer.aps-environment": profile["apsEnvironment"],
            "keychain-access-groups": identity["keychainAccessGroups"],
        }
    )
    return expected


def _validate_policy_contract(value: dict[str, Any]) -> None:
    if value.get("schemaVersion") != 2:
        raise SystemExit("unsupported macOS entitlement policy schema")
    if value.get("defaultSigningProfile") != "provider-free":
        raise SystemExit("provider-free must remain the default macOS signing profile")
    identity = value.get("publicIdentity")
    if not isinstance(identity, dict) or set(identity) != PUBLIC_IDENTITY_KEYS:
        raise SystemExit("macOS public signing identity has an invalid shape")
    team = _require_string(identity["teamIdentifier"], "team identifier")
    prefix = _require_string(identity["appIdentifierPrefix"], "App ID prefix")
    bundle = _require_string(identity["bundleIdentifier"], "bundle identifier")
    container = _require_string(
        identity["cloudKitContainerIdentifier"], "CloudKit container"
    )
    groups = identity["keychainAccessGroups"]
    if not re.fullmatch(r"[A-Z0-9]{10}", team):
        raise SystemExit("macOS Team ID must contain ten uppercase letters/digits")
    if not re.fullmatch(r"[A-Z0-9]{10}", prefix):
        raise SystemExit("macOS App ID prefix must contain ten uppercase letters/digits")
    if bundle != json.loads((ROOT / "config/product.json").read_text())["bundleId"]:
        raise SystemExit("macOS entitlement bundle differs from product config")
    if container != f"iCloud.{bundle}":
        raise SystemExit("macOS CloudKit container must be dedicated to the Ahoi bundle")
    if groups != [
        f"{prefix}.app.ahoibrowser.sync",
        f"{prefix}.app.ahoibrowser.commands",
    ]:
        raise SystemExit("macOS Keychain groups must be the two exact Ahoi groups")
    for name, item in (
        ("bundle identifier", bundle),
        ("CloudKit container", container),
        ("sync Keychain group", groups[0]),
        ("command Keychain group", groups[1]),
    ):
        _reject_unsafe_string(item, name)

    runtime = value.get("runtimeConfiguration")
    if not isinstance(runtime, dict) or set(runtime) != RUNTIME_CONFIGURATION_KEYS:
        raise SystemExit("macOS CloudKit runtime configuration has an invalid shape")
    for key, item in runtime.items():
        _reject_unsafe_string(_require_string(item, key), key)

    profiles = value.get("signingProfiles")
    if not isinstance(profiles, dict) or set(profiles) != SIGNING_PROFILES:
        raise SystemExit("macOS signing profile inventory must be exact")
    expected_profiles = {
        "provider-free": (
            "Developer ID Application: ",
            "forbidden",
            None,
            None,
            None,
        ),
        "cloudkit-development": (
            "Apple Development: ",
            "required",
            "Development",
            "development",
            True,
        ),
        "cloudkit-production": (
            "Developer ID Application: ",
            "required",
            "Production",
            "production",
            False,
        ),
    }
    for name, expected in expected_profiles.items():
        profile = profiles[name]
        if not isinstance(profile, dict) or set(profile) != PROFILE_KEYS:
            raise SystemExit(f"macOS signing profile {name} has an invalid shape")
        actual = (
            profile["signingAuthorityPrefix"],
            profile["embeddedProvisioningProfile"],
            profile["cloudKitEnvironment"],
            profile["apsEnvironment"],
            profile["getTaskAllow"],
        )
        if actual != expected:
            raise SystemExit(f"macOS signing profile {name} weakens the fixed contract")


def load_policy(path: pathlib.Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise SystemExit("macOS entitlement policy must contain an object")
    _validate_policy_contract(value)
    chromium = json.loads((ROOT / "config/chromium.json").read_text(encoding="utf-8"))
    if value.get("chromiumCommit") != chromium["commit"]:
        raise SystemExit("entitlement policy is stale for the Chromium pin")
    if value.get("chromiumVersion") != chromium["version"]:
        raise SystemExit("entitlement policy is stale for the Chromium version")
    if value.get("frameworkVersion") != chromium["version"]:
        raise SystemExit("entitlement policy is stale for the Chromium framework version")
    rules = value.get("rules")
    if not isinstance(rules, list) or not rules:
        raise SystemExit("entitlement policy must contain rules")
    seen_ids: set[str] = set()
    for rule in rules:
        if not isinstance(rule, dict) or set(rule) != {
            "id",
            "pathPattern",
            "entitlements",
        }:
            raise SystemExit("malformed entitlement policy rule")
        if not isinstance(rule["id"], str) or rule["id"] in seen_ids:
            raise SystemExit("entitlement policy rule IDs must be unique")
        seen_ids.add(rule["id"])
        try:
            re.compile(rule["pathPattern"])
        except (TypeError, re.error) as error:
            raise SystemExit(f"invalid path pattern in {rule['id']}") from error
        if not isinstance(rule["entitlements"], dict):
            raise SystemExit(f"entitlements must be an object in {rule['id']}")
        if rule["id"] == "browser-app" and (
            set(rule["entitlements"]) & CLOUDKIT_ENTITLEMENT_KEYS
        ):
            raise SystemExit("provider-free browser rule contains provider entitlements")
    if "browser-app" not in seen_ids:
        raise SystemExit("entitlement policy has no browser-app rule")
    return value


def parse_entitlements(raw: bytes) -> dict[str, Any]:
    if not raw.strip():
        return {}
    try:
        value = plistlib.loads(raw)
    except Exception as error:
        raise SystemExit("codesign returned malformed entitlement plist") from error
    if not isinstance(value, dict):
        raise SystemExit("codesign entitlement payload must be a dictionary")
    return value


def verify(
    policy: dict[str, Any],
    relative_path: str,
    actual: dict[str, Any],
    profile_name: str = "provider-free",
) -> str:
    signing_profile(policy, profile_name)
    path = pathlib.PurePosixPath(relative_path)
    if path.is_absolute() or ".." in path.parts or path.as_posix() != relative_path:
        raise SystemExit(f"unsafe signed-code path: {relative_path}")
    matches = [
        rule
        for rule in policy["rules"]
        if re.fullmatch(rule["pathPattern"], relative_path)
    ]
    if len(matches) != 1:
        raise SystemExit(
            f"signed-code path must match exactly one policy role: {relative_path}"
        )
    rule = matches[0]
    expected = expected_rule_entitlements(policy, profile_name, rule)
    if actual != expected:
        missing = sorted(set(expected) - set(actual))
        unexpected = sorted(set(actual) - set(expected))
        wrong = sorted(
            key for key in set(actual) & set(expected) if actual[key] != expected[key]
        )
        raise SystemExit(
            f"entitlement mismatch for profile {profile_name}, role {rule['id']}: "
            f"missing={missing}, unexpected={unexpected}, wrongValues={wrong}"
        )
    return rule["id"]


def decode_provisioning_profile(path: pathlib.Path) -> dict[str, Any]:
    if not path.is_file() or path.is_symlink():
        raise SystemExit(f"concrete provisioning profile is missing: {path}")
    try:
        completed = subprocess.run(
            ["security", "cms", "-D", "-i", str(path)],
            check=True,
            capture_output=True,
        )
        value = plistlib.loads(completed.stdout)
    except (OSError, subprocess.CalledProcessError, plistlib.InvalidFileException) as error:
        raise SystemExit(f"cannot decode provisioning profile: {path}") from error
    if not isinstance(value, dict):
        raise SystemExit("decoded provisioning profile must contain a dictionary")
    return value


def _utc(value: dt.datetime) -> dt.datetime:
    if value.tzinfo is None:
        return value.replace(tzinfo=dt.timezone.utc)
    return value.astimezone(dt.timezone.utc)


def validate_provisioning_profile(
    policy: dict[str, Any],
    profile_name: str,
    decoded: dict[str, Any],
    *,
    now: dt.datetime | None = None,
) -> dict[str, Any]:
    contract = signing_profile(policy, profile_name)
    if contract["embeddedProvisioningProfile"] != "required":
        raise SystemExit(f"{profile_name} must not use a provisioning profile")
    identity = policy["publicIdentity"]
    uuid = _require_string(decoded.get("UUID"), "provisioning profile UUID")
    name = _require_string(decoded.get("Name"), "provisioning profile name")
    _reject_unsafe_string(name, "provisioning profile name")
    if re.fullmatch(
        r"[0-9A-Fa-f]{8}(?:-[0-9A-Fa-f]{4}){3}-[0-9A-Fa-f]{12}", uuid
    ) is None:
        raise SystemExit("provisioning profile UUID is malformed")
    if decoded.get("TeamIdentifier") != [identity["teamIdentifier"]]:
        raise SystemExit("provisioning profile TeamIdentifier is not exact")
    if decoded.get("ApplicationIdentifierPrefix") != [identity["appIdentifierPrefix"]]:
        raise SystemExit("provisioning profile App ID prefix is not exact")
    if decoded.get("Platform") != ["OSX"]:
        raise SystemExit("provisioning profile is not an exact macOS profile")
    certificates = decoded.get("DeveloperCertificates")
    if not isinstance(certificates, list) or not certificates or any(
        not isinstance(certificate, bytes) or not certificate for certificate in certificates
    ):
        raise SystemExit("provisioning profile contains no concrete developer certificate")
    expiration = decoded.get("ExpirationDate")
    creation = decoded.get("CreationDate")
    if not isinstance(expiration, dt.datetime) or not isinstance(creation, dt.datetime):
        raise SystemExit("provisioning profile dates are missing or malformed")
    current = _utc(now or dt.datetime.now(tz=dt.timezone.utc))
    if _utc(creation) >= _utc(expiration) or _utc(expiration) <= current:
        raise SystemExit("provisioning profile is expired or has an invalid lifetime")

    devices = decoded.get("ProvisionedDevices")
    if profile_name == "cloudkit-development":
        if not isinstance(devices, list) or not devices or any(
            not isinstance(device, str) or not device for device in devices
        ):
            raise SystemExit("CloudKit Development profile has no provisioned Mac")
        if decoded.get("ProvisionsAllDevices") is True:
            raise SystemExit("CloudKit Development profile cannot provision all devices")
    else:
        if "ProvisionedDevices" in decoded:
            raise SystemExit("Developer ID Production profile contains development devices")
        if decoded.get("ProvisionsAllDevices") is not True:
            raise SystemExit("Developer ID Production profile must provision all devices")

    entitlements = decoded.get("Entitlements")
    if not isinstance(entitlements, dict):
        raise SystemExit("provisioning profile entitlements are missing")
    browser_rule = next(rule for rule in policy["rules"] if rule["id"] == "browser-app")
    expected = expected_rule_entitlements(policy, profile_name, browser_rule)
    for key in CLOUDKIT_ENTITLEMENT_KEYS:
        if entitlements.get(key) != expected[key]:
            raise SystemExit(f"provisioning profile entitlement is not exact: {key}")
    if entitlements.get("get-task-allow") is not contract["getTaskAllow"]:
        raise SystemExit("provisioning profile get-task-allow differs from its mode")
    relevant_values: list[Any] = [name, uuid]
    for key in CLOUDKIT_ENTITLEMENT_KEYS:
        item = entitlements[key]
        relevant_values.extend(item if isinstance(item, list) else [item])
    for item in relevant_values:
        if isinstance(item, str):
            _reject_unsafe_string(item, "provisioning profile contract")
    return {
        "uuid": uuid,
        "name": name,
        "teamIdentifier": identity["teamIdentifier"],
        "appIdentifierPrefix": identity["appIdentifierPrefix"],
        "bundleIdentifier": identity["bundleIdentifier"],
        "cloudKitContainerIdentifier": identity["cloudKitContainerIdentifier"],
        "keychainAccessGroups": identity["keychainAccessGroups"],
        "cloudKitEnvironment": contract["cloudKitEnvironment"],
        "apsEnvironment": contract["apsEnvironment"],
        "developerCertificateSha256": sorted(
            hashlib.sha256(certificate).hexdigest() for certificate in certificates
        ),
        "expiration": _utc(expiration).isoformat().replace("+00:00", "Z"),
    }


def runtime_build_settings(
    policy: dict[str, Any], profile_name: str, profile_uuid: str | None = None
) -> dict[str, str]:
    if profile_name == "provider-free":
        return {}
    contract = signing_profile(policy, profile_name)
    identity = policy["publicIdentity"]
    runtime = policy["runtimeConfiguration"]
    values = {
        "AHOI_CLOUDKIT_CONTAINER_ID": identity["cloudKitContainerIdentifier"],
        "AHOI_CLOUDKIT_ZONE_NAME": runtime["zoneName"],
        "AHOI_CLOUDKIT_SUBSCRIPTION_ID": runtime["subscriptionIdentifier"],
        "AHOI_CLOUDKIT_CONTAINER_ENVIRONMENT": contract["cloudKitEnvironment"],
        "AHOI_APS_ENVIRONMENT": contract["apsEnvironment"],
        "AHOI_SYNC_KEYCHAIN_SERVICE": runtime["syncKeychainService"],
        "AHOI_SYNC_KEYCHAIN_ACCOUNT": runtime["syncKeychainAccount"],
        "AHOI_SYNC_KEYCHAIN_ACCESS_GROUP": identity["keychainAccessGroups"][0],
        "AHOI_SYNC_KEY_VERSION": runtime["syncKeyVersion"],
        "AHOI_COMMAND_KEYCHAIN_SERVICE": runtime["commandKeychainService"],
        "AHOI_COMMAND_KEYCHAIN_ACCOUNT": runtime["commandKeychainAccount"],
        "AHOI_COMMAND_KEYCHAIN_ACCESS_GROUP": identity["keychainAccessGroups"][1],
        "AhoiMacCloudKitSigningProfile": profile_name,
    }
    if profile_uuid is not None:
        values["AhoiProvisioningProfileUUID"] = profile_uuid
    return values


def verify_app_runtime_configuration(
    app: pathlib.Path,
    policy: dict[str, Any],
    profile_name: str,
    profile_metadata: dict[str, Any] | None = None,
) -> dict[str, str]:
    info_path = app / "Contents/Info.plist"
    try:
        info = plistlib.loads(info_path.read_bytes())
    except (OSError, plistlib.InvalidFileException) as error:
        raise SystemExit(f"cannot read app Info.plist: {info_path}") from error
    if not isinstance(info, dict):
        raise SystemExit("app Info.plist must contain a dictionary")
    if info.get("CFBundleIdentifier") != policy["publicIdentity"]["bundleIdentifier"]:
        raise SystemExit("app bundle identifier differs from the macOS signing policy")
    if profile_name == "provider-free":
        present = sorted(CLOUDKIT_RUNTIME_KEYS & set(info))
        if present:
            raise SystemExit(f"provider-free app contains CloudKit runtime keys: {present}")
        return {}
    if profile_metadata is None:
        raise SystemExit("CloudKit runtime readback requires provisioning profile metadata")
    expected = runtime_build_settings(policy, profile_name, profile_metadata["uuid"])
    for key, value in expected.items():
        if info.get(key) != value:
            raise SystemExit(f"app Info.plist CloudKit value is not exact: {key}")
    return expected


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--policy",
        type=pathlib.Path,
        default=ROOT / "config/macos-entitlements.json",
    )
    parser.add_argument("--relative-path", required=True)
    parser.add_argument(
        "--signing-profile",
        choices=sorted(SIGNING_PROFILES),
        default="provider-free",
    )
    parser.add_argument("--provisioning-profile", type=pathlib.Path)
    parser.add_argument("--app", type=pathlib.Path)
    args = parser.parse_args()
    policy = load_policy(args.policy)
    role = verify(
        policy,
        args.relative_path,
        parse_entitlements(sys.stdin.buffer.read()),
        args.signing_profile,
    )
    profile_metadata = None
    if args.signing_profile == "provider-free":
        if args.provisioning_profile is not None:
            raise SystemExit("provider-free validation forbids a provisioning profile")
    else:
        if args.provisioning_profile is None:
            raise SystemExit("CloudKit validation requires a provisioning profile")
        profile_metadata = validate_provisioning_profile(
            policy,
            args.signing_profile,
            decode_provisioning_profile(args.provisioning_profile),
        )
    if args.app is not None:
        verify_app_runtime_configuration(
            args.app, policy, args.signing_profile, profile_metadata
        )
    print(role)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
