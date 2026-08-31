#!/usr/bin/env python3
"""Create honest Ahoi E2E result skeletons and validate release evidence."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import pathlib
import platform
import plistlib
import re
import subprocess
import sys
from typing import Optional

from evidence_schema import validate_shape
from release import chain as release_chain
from release.common import ReleaseError


ROOT = pathlib.Path(__file__).resolve().parents[1]
INSTALLED_BUNDLE = pathlib.Path("/Applications/AhoiBrowser.app")
NOT_AVAILABLE = "NOT_AVAILABLE"
BLOCKED = {
    "BLOCKED_USER_ASSISTANCE",
    "BLOCKED_CREDENTIAL",
    "BLOCKED_ENTITLEMENT",
    "BLOCKED_EXTERNAL_SERVICE",
}
RELEASE_EVIDENCE_NOT_READY = (
    "Computer Use PASS requires an independently validated signed release "
    "manifest bound to the live installed bundle"
)
RELEASE_MANIFEST_REQUIRED = (
    "Computer Use PASS requires an explicit --release-manifest path"
)
RELEASE_PUBLIC_KEY_REQUIRED = (
    "Computer Use PASS requires an explicit --release-public-key path"
)
UBO_11_LOCAL_FAIL_CLOSED_RECEIPT = {
    "schemaVersion": 1,
    "kind": "ubo-11-local-foreign-mv2-fail-closed",
    "testId": "UBO-11",
    "staticBootstrapProvisioned": True,
    "signedCatalogTrustRootProvisioned": False,
    "foreignMv2Rejected": True,
    "networkRequestCount": 0,
    "positiveUboInstallAttempted": False,
}


def load(relative: str):
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def validate_release_evidence_chain(
    manifest_path: Optional[pathlib.Path],
    public_key_path: Optional[pathlib.Path],
) -> list[str]:
    """Validate the signed chain and its binding to the live installed app."""
    errors: list[str] = []
    if manifest_path is None:
        errors.append(RELEASE_MANIFEST_REQUIRED)
    elif not manifest_path.is_file():
        errors.append(f"release manifest is missing: {manifest_path}")
    if public_key_path is None:
        errors.append(RELEASE_PUBLIC_KEY_REQUIRED)
    elif not public_key_path.is_file():
        errors.append(f"release public key is missing: {public_key_path}")

    try:
        gate = load("config/release-evidence.json")
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        errors.append("release evidence gate cannot be read")
        return [RELEASE_EVIDENCE_NOT_READY, *errors]
    required = {
        "build-provenance",
        "signed-package-provenance",
        "notarization-receipt",
        "installed-bundle-binding",
    }
    if not isinstance(gate, dict) or gate.get("schemaVersion") != 1:
        errors.append("release evidence gate schema is unsupported")
    elif gate.get("releasePassEnabled") is not True:
        errors.append("release evidence gate is not enabled")
    elif (
        not isinstance(gate.get("requiredChain"), list)
        or len(gate["requiredChain"]) != len(required)
        or any(not isinstance(item, str) for item in gate["requiredChain"])
        or set(gate["requiredChain"]) != required
    ):
        errors.append("release evidence gate does not require the complete chain")

    try:
        policy = load("config/release-policy.json")
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        errors.append("release policy cannot be read")
        return [RELEASE_EVIDENCE_NOT_READY, *errors]
    manifest_signing = (
        policy.get("manifestSigning") if isinstance(policy, dict) else None
    )
    trusted_key_ids = (
        manifest_signing.get("trustedKeyIds")
        if isinstance(manifest_signing, dict)
        else None
    )
    if (
        not isinstance(trusted_key_ids, list)
        or not trusted_key_ids
        or any(
            not isinstance(key_id, str)
            or re.fullmatch(r"[0-9a-f]{64}", key_id) is None
            for key_id in trusted_key_ids
        )
        or len(trusted_key_ids) != len(set(trusted_key_ids))
    ):
        errors.append("release manifest key trust is not configured correctly")

    if errors:
        return [RELEASE_EVIDENCE_NOT_READY, *errors]

    assert manifest_path is not None
    assert public_key_path is not None
    try:
        release_chain.validate_manifest(
            manifest_path.resolve(),
            public_key=public_key_path.resolve(),
            trusted_key_ids=set(trusted_key_ids),
            product=load("config/product.json"),
            version=load("config/version.json"),
            chromium=load("config/chromium.json"),
            toolchain=load("config/toolchain.json"),
            release_args_sha256=sha256_file(ROOT / "config/build/ahoi-release.gn"),
            installed_app=INSTALLED_BUNDLE,
            policy_path=ROOT / "config/macos-entitlements.json",
        )
    except (
        ReleaseError,
        KeyError,
        TypeError,
        ValueError,
        OSError,
        UnicodeDecodeError,
        json.JSONDecodeError,
    ) as error:
        return [
            RELEASE_EVIDENCE_NOT_READY,
            f"release evidence chain validation failed: {error}",
        ]
    return []


def release_evidence_chain_ready(
    manifest_path: Optional[pathlib.Path] = None,
    public_key_path: Optional[pathlib.Path] = None,
) -> bool:
    """Return true only after independent live release-chain validation."""
    return not validate_release_evidence_chain(manifest_path, public_key_path)


def run_output(*args: str, merge_stderr: bool = False) -> str:
    completed = subprocess.run(
        args,
        check=True,
        capture_output=True,
        text=True,
    )
    if merge_stderr:
        return (completed.stdout + completed.stderr).strip()
    return completed.stdout.strip()


def optional_output(*args: str, merge_stderr: bool = False) -> str:
    try:
        return run_output(*args, merge_stderr=merge_stderr)
    except (FileNotFoundError, subprocess.CalledProcessError):
        return NOT_AVAILABLE


def succeeds(*args: str) -> bool:
    try:
        return subprocess.run(args, capture_output=True).returncode == 0
    except FileNotFoundError:
        return False


def plist_value(plist: dict, key: str) -> str:
    value = plist.get(key, NOT_AVAILABLE)
    return str(value) if value is not None else NOT_AVAILABLE


def codesign_field(description: str, key: str) -> str:
    prefix = f"{key}="
    for line in description.splitlines():
        if line.startswith(prefix):
            return line[len(prefix) :].strip()
    return NOT_AVAILABLE


def installed_bundle_checks(bundle: pathlib.Path) -> dict[str, object]:
    result: dict[str, object] = {
        "signed": False,
        "gatekeeper": False,
        "stapled": False,
        "notarized": False,
        "architecture": NOT_AVAILABLE,
        "identityVerified": False,
        "hardenedRuntime": False,
        "name": NOT_AVAILABLE,
        "identifier": NOT_AVAILABLE,
        "marketingVersion": NOT_AVAILABLE,
        "buildNumber": NOT_AVAILABLE,
        "productVersion": NOT_AVAILABLE,
        "channel": NOT_AVAILABLE,
        "sourceCommit": NOT_AVAILABLE,
        "chromiumVersion": NOT_AVAILABLE,
        "chromiumCommit": NOT_AVAILABLE,
        "gnArgsSha256": NOT_AVAILABLE,
        "buildProfile": NOT_AVAILABLE,
        "teamIdentifier": NOT_AVAILABLE,
        "signingAuthority": NOT_AVAILABLE,
    }
    if not bundle.is_dir():
        return result

    result["signed"] = succeeds(
        "codesign", "--verify", "--deep", "--strict", str(bundle)
    )
    result["gatekeeper"] = succeeds(
        "spctl", "--assess", "--type", "execute", str(bundle)
    )
    result["stapled"] = succeeds("xcrun", "stapler", "validate", str(bundle))
    result["notarized"] = bool(result["gatekeeper"] and result["stapled"])

    plist_path = bundle / "Contents/Info.plist"
    if not plist_path.is_file():
        return result
    try:
        with plist_path.open("rb") as handle:
            plist = plistlib.load(handle)
    except (OSError, plistlib.InvalidFileException):
        return result

    field_map = {
        "name": "CFBundleName",
        "identifier": "CFBundleIdentifier",
        "marketingVersion": "CFBundleShortVersionString",
        "buildNumber": "CFBundleVersion",
        "productVersion": "AhoiProductVersion",
        "channel": "AhoiUpdateChannel",
        "sourceCommit": "AhoiSourceCommit",
        "chromiumVersion": "AhoiChromiumVersion",
        "chromiumCommit": "AhoiChromiumCommit",
        "gnArgsSha256": "AhoiGNArgsSHA256",
        "buildProfile": "AhoiBuildProfile",
    }
    for destination, source in field_map.items():
        result[destination] = plist_value(plist, source)

    executable_name = plist_value(plist, "CFBundleExecutable")
    executable = bundle / "Contents/MacOS" / executable_name
    if executable.is_file():
        file_output = optional_output("file", str(executable))
        if "arm64" in file_output and "x86_64" not in file_output:
            result["architecture"] = "arm64"

    product = load("config/product.json")
    result["identityVerified"] = (
        result["name"] == product["name"]
        and result["identifier"] == product["bundleId"]
    )
    description = optional_output(
        "codesign", "-d", "--verbose=4", str(bundle), merge_stderr=True
    )
    result["teamIdentifier"] = codesign_field(description, "TeamIdentifier")
    result["signingAuthority"] = codesign_field(description, "Authority")
    result["hardenedRuntime"] = (
        "runtime" in codesign_field(description, "CodeDirectory")
        and "Runtime Version=" in description
    )
    return result


def bundle_hash(bundle: pathlib.Path) -> str:
    if not bundle.is_dir():
        return NOT_AVAILABLE
    digest = hashlib.sha256()
    for path in sorted(item for item in bundle.rglob("*") if item.is_file()):
        digest.update(str(path.relative_to(bundle)).encode("utf-8"))
        digest.update(b"\0")
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
    return digest.hexdigest()


def registry_entry(test_id: str):
    for entry in load("config/test-registry.json")["tests"]:
        if entry["id"] == test_id:
            return entry
    raise ValueError(f"unknown test ID: {test_id}")


def repository_state() -> dict[str, object]:
    commit = optional_output("git", "-C", str(ROOT), "rev-parse", "HEAD")
    dirty_output = optional_output(
        "git", "-C", str(ROOT), "status", "--porcelain", "--untracked-files=normal"
    )
    return {"gitCommit": commit, "gitDirty": dirty_output != ""}


def bundle_payload(
    checks: dict[str, object], sha256_value: Optional[str] = None
) -> dict[str, object]:
    return {
        "path": str(INSTALLED_BUNDLE),
        "sha256": sha256_value or bundle_hash(INSTALLED_BUNDLE),
        "architecture": checks["architecture"],
        "name": checks["name"],
        "identifier": checks["identifier"],
        "marketingVersion": checks["marketingVersion"],
        "buildNumber": checks["buildNumber"],
        "productVersion": checks["productVersion"],
        "channel": checks["channel"],
        "sourceCommit": checks["sourceCommit"],
        "chromiumVersion": checks["chromiumVersion"],
        "chromiumCommit": checks["chromiumCommit"],
        "gnArgsSha256": checks["gnArgsSha256"],
        "buildProfile": checks["buildProfile"],
        "teamIdentifier": checks["teamIdentifier"],
        "signingAuthority": checks["signingAuthority"],
        "signatureVerified": checks["signed"],
        "hardenedRuntimeVerified": checks["hardenedRuntime"],
        "notarizationVerified": checks["notarized"],
    }


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_artifact_reference(
    result_path: pathlib.Path,
    reference: str,
    hashes: dict[str, str],
) -> list[str]:
    if pathlib.PurePosixPath(reference).is_absolute() or ".." in pathlib.PurePosixPath(
        reference
    ).parts:
        return [f"evidence path must stay inside the test package: {reference}"]
    base = result_path.resolve().parent
    target = (base / reference).resolve()
    try:
        target.relative_to(base)
    except ValueError:
        return [f"evidence path escapes the test package: {reference}"]
    if not target.is_file():
        return [f"evidence file is missing: {reference}"]
    expected_hash = hashes.get(reference)
    if expected_hash is None:
        return [f"evidence file has no SHA-256 binding: {reference}"]
    if sha256_file(target) != expected_hash:
        return [f"evidence file SHA-256 mismatch: {reference}"]
    return []


def is_ubo_11_local_fail_closed_pass(
    result_path: pathlib.Path,
    data: object,
) -> bool:
    """Recognize the only CU PASS that does not need production provenance."""
    if not isinstance(data, dict) or {
        "testId": data.get("testId"),
        "testClass": data.get("testClass"),
        "status": data.get("status"),
    } != {
        "testId": "UBO-11",
        "testClass": "CU_E2E",
        "status": "PASS",
    }:
        return False
    evidence_data = data.get("evidence")
    if not isinstance(evidence_data, dict):
        return False
    references = evidence_data.get("fixtureReceipts")
    hashes = evidence_data.get("fileHashes")
    if (
        not isinstance(references, list)
        or len(references) != 1
        or not isinstance(references[0], str)
        or not isinstance(hashes, dict)
        or validate_artifact_reference(result_path, references[0], hashes)
    ):
        return False
    receipt_path = result_path.resolve().parent / references[0]
    try:
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        return False
    return receipt == UBO_11_LOCAL_FAIL_CLOSED_RECEIPT


def init_result(args: argparse.Namespace) -> int:
    entry = registry_entry(args.test_id)
    product_version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    chromium = load("config/chromium.json")
    now = dt.datetime.now(dt.timezone.utc).isoformat()
    destination = ROOT / "artifacts/e2e" / product_version / args.test_id
    destination.mkdir(parents=True, exist_ok=True)
    result_path = destination / "result.json"
    if result_path.exists() and not args.force:
        raise FileExistsError(
            f"refusing to overwrite {result_path}; pass --force explicitly"
        )
    checks = installed_bundle_checks(INSTALLED_BUNDLE)
    payload = {
        "schemaVersion": 2,
        "testId": args.test_id,
        "requirement": entry["description"],
        "testClass": entry["primaryClass"],
        "status": "NOT_RUN",
        "executedBy": args.executor,
        "source": repository_state(),
        "productVersion": product_version,
        "chromium": {
            "version": chromium["version"],
            "commit": chromium["commit"],
        },
        "bundle": bundle_payload(checks),
        "environment": {
            "osVersion": platform.mac_ver()[0],
            "osBuild": optional_output("sw_vers", "-buildVersion"),
            "device": optional_output("sysctl", "-n", "hw.model"),
            "hostArchitecture": platform.machine(),
            "locale": args.locale,
            "theme": args.theme,
            "glass": args.glass,
            "profileType": args.profile_type,
            "startState": args.start_state,
        },
        "startedAt": now,
        "completedAt": now,
        "expectedResult": entry["description"],
        "actualResult": "NOT_RUN: no visible user journey has been executed.",
        "steps": [],
        "assertions": [],
        "evidence": {
            "screenshots": [],
            "videos": [],
            "redactedLogs": [],
            "fixtureReceipts": [],
            "networkCaptures": [],
            "fileHashes": {},
            "testReports": [],
            "linkedIssue": None,
            "repeatRun": None,
        },
        "artifacts": [],
        "blocker": {
            "condition": "The test has not been executed against an installed RC.",
            "owner": "engineering",
            "nextAction": "Build, install, and execute the registered user journey.",
        },
    }
    result_path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(result_path)
    return 0


def validate_result(
    path: pathlib.Path,
    *,
    release_manifest: Optional[pathlib.Path] = None,
    release_public_key: Optional[pathlib.Path] = None,
) -> list[str]:
    errors: list[str] = []
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        return [f"cannot read evidence JSON: {error}"]
    errors.extend(validate_shape(data))
    if errors:
        return errors
    allowed = set(load("config/test-statuses.json")["allowed"])
    if data["schemaVersion"] != 2:
        errors.append("unsupported evidence schemaVersion")

    entry = None
    if not re.fullmatch(r"[A-Z]{2,10}-\d{2,3}", data["testId"]):
        errors.append("invalid testId")
    else:
        try:
            entry = registry_entry(data["testId"])
        except ValueError as exc:
            errors.append(str(exc))
    if entry is not None:
        if data["testClass"] != entry["primaryClass"]:
            errors.append(
                "testClass does not match registry: "
                f"expected {entry['primaryClass']}, got {data['testClass']}"
            )
        if data["requirement"] != entry["description"]:
            errors.append("requirement does not match the test registry")
        if data["expectedResult"] != entry["description"]:
            errors.append("expectedResult does not match the test registry")

    expected_product_version = (ROOT / "VERSION").read_text(
        encoding="utf-8"
    ).strip()
    if data["productVersion"] != expected_product_version:
        errors.append(
            "productVersion does not match repository: "
            f"expected {expected_product_version}, got {data['productVersion']}"
        )
    expected_chromium = load("config/chromium.json")
    if data["chromium"] != {
        "version": expected_chromium["version"],
        "commit": expected_chromium["commit"],
    }:
        errors.append("Chromium version/commit do not match the pinned repository baseline")
    if data["status"] not in allowed:
        errors.append(f"invalid status: {data['status']}")
    if data["status"] in BLOCKED | {"NOT_RUN"} and not data.get("blocker"):
        errors.append("NOT_RUN and blocked results require blocker details")
    if data["status"] == "PASS" and data.get("blocker") is not None:
        errors.append("PASS must not retain blocker details")

    if data["status"] == "PASS":
        if not re.fullmatch(r"[0-9a-f]{40}", str(data["source"].get("gitCommit"))):
            errors.append("PASS requires an exact Ahoi Git commit")
        if data["source"].get("gitDirty") is not False:
            errors.append("PASS requires a clean Ahoi source checkout")
        current_source = repository_state()
        if data["source"].get("gitCommit") != current_source["gitCommit"]:
            errors.append("PASS source commit does not match the checked-out repository")
        if current_source["gitDirty"] is not False:
            errors.append("PASS validation requires the checked-out repository to be clean")
        if not data["steps"]:
            errors.append("PASS requires recorded test steps")
        if not data["assertions"]:
            errors.append("PASS requires at least one assertion")
        if any(not assertion.get("passed") for assertion in data["assertions"]):
            errors.append("PASS cannot contain a failed assertion")
        if not data["evidence"].get("repeatRun"):
            errors.append("PASS requires evidence of a successful repeat run")
        if data["actualResult"].startswith("NOT_RUN"):
            errors.append("PASS requires an actual result from execution")
        if not data["evidence"].get("testReports"):
            errors.append("PASS requires at least one hashed test report")
        hashes = data["evidence"]["fileHashes"]
        evidence_references = []
        for key in (
            "screenshots", "videos", "redactedLogs", "fixtureReceipts",
            "networkCaptures", "testReports",
        ):
            evidence_references.extend(data["evidence"][key])
        repeat_reference = data["evidence"].get("repeatRun")
        if repeat_reference:
            evidence_references.append(repeat_reference)
        for reference in sorted(set(evidence_references)):
            errors.extend(validate_artifact_reference(path, reference, hashes))

    ubo_11_local_fail_closed = is_ubo_11_local_fail_closed_pass(path, data)
    if data["testClass"] in {"CU_E2E", "ASSISTED_E2E"}:
        bundle = data["bundle"]
        if bundle.get("path") != str(INSTALLED_BUNDLE):
            errors.append("Computer Use evidence must target /Applications/AhoiBrowser.app")
        if data["status"] == "PASS" and not (
            bundle.get("signatureVerified")
            and bundle.get("hardenedRuntimeVerified")
            and bundle.get("notarizationVerified")
        ):
            errors.append(
                "Computer Use PASS requires signature, Hardened Runtime, and notarization"
            )
        if data["status"] == "PASS" and bundle.get("sha256") == NOT_AVAILABLE:
            errors.append("Computer Use PASS requires an installed bundle hash")
        if data["status"] == "PASS" and bundle.get("architecture") != "arm64":
            errors.append("Computer Use PASS requires an ARM64-only bundle")
        visual_evidence = data["evidence"].get("screenshots", []) + data[
            "evidence"
        ].get("videos", [])
        if data["status"] == "PASS" and not visual_evidence:
            errors.append("Computer Use PASS requires screenshot or video evidence")
        if data["status"] == "PASS":
            if not ubo_11_local_fail_closed:
                errors.extend(
                    validate_release_evidence_chain(
                        release_manifest,
                        release_public_key,
                    )
                )
            actual = installed_bundle_checks(INSTALLED_BUNDLE)
            actual_hash = bundle_hash(INSTALLED_BUNDLE)
            expected_team = os.environ.get("AHOI_TEAM_ID")
            expected_authority = os.environ.get("AHOI_CODESIGN_IDENTITY")
            if not succeeds(str(ROOT / "scripts/verify-installed-app.sh")):
                errors.append(
                    "installed release verifier failed identity/helper/runtime checks"
                )
            if not actual["identityVerified"]:
                errors.append("installed Computer Use bundle identity is not AhoiBrowser")
            if not actual["signed"]:
                errors.append("installed Computer Use bundle signature is not valid")
            if not actual["hardenedRuntime"]:
                errors.append("installed Computer Use bundle lacks Hardened Runtime")
            if not actual["notarized"]:
                errors.append("installed Computer Use bundle has no valid notarization staple")
            if actual["architecture"] != "arm64":
                errors.append("installed Computer Use bundle is not ARM64-only")
            if actual_hash == NOT_AVAILABLE:
                errors.append("installed Computer Use bundle is not available for validation")
            elif bundle.get("sha256") != actual_hash:
                errors.append("evidence bundle hash does not match the installed bundle")
            for key, value in bundle_payload(actual, actual_hash).items():
                if key == "path":
                    continue
                if bundle.get(key) != value:
                    errors.append(f"evidence bundle field does not match installed app: {key}")
            if not expected_team:
                errors.append("AHOI_TEAM_ID is required to validate Computer Use PASS")
            elif actual["teamIdentifier"] != expected_team:
                errors.append("installed bundle TeamIdentifier is not the expected team")
            if not expected_authority:
                errors.append(
                    "AHOI_CODESIGN_IDENTITY is required to validate Computer Use PASS"
                )
            elif actual["signingAuthority"] != expected_authority:
                errors.append("installed bundle signing authority is not expected")

            version = load("config/version.json")
            product = load("config/product.json")
            expected_bundle = {
                "name": product["name"],
                "identifier": product["bundleId"],
                "marketingVersion": version["marketingVersion"],
                "buildNumber": version["buildNumber"],
                "productVersion": version["displayVersion"],
                "channel": version["channel"],
                "sourceCommit": data["source"].get("gitCommit"),
                "chromiumVersion": expected_chromium["version"],
                "chromiumCommit": expected_chromium["commit"],
                "gnArgsSha256": sha256_file(ROOT / "config/build/ahoi-release.gn"),
                "buildProfile": "release",
            }
            for key, expected in expected_bundle.items():
                if actual[key] != expected:
                    errors.append(
                        f"installed bundle provenance mismatch for {key}: "
                        f"expected {expected}, got {actual[key]}"
                    )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command_name", required=True)
    init = sub.add_parser("init")
    init.add_argument("test_id")
    init.add_argument("--locale", choices=("de", "en"), default="de")
    init.add_argument("--theme", choices=("system", "light", "dark"), default="system")
    init.add_argument("--glass", action="store_true")
    init.add_argument("--executor", default=os.environ.get("AHOI_E2E_EXECUTOR", "Codex"))
    init.add_argument(
        "--profile-type",
        choices=("fresh", "existing", "migration", "incognito", "not-applicable"),
        default="fresh",
    )
    init.add_argument(
        "--start-state",
        default="AhoiBrowser not launched; test not executed.",
    )
    init.add_argument("--force", action="store_true")
    validate = sub.add_parser("validate")
    validate.add_argument(
        "--release-manifest",
        type=pathlib.Path,
        help="signed release-manifest JSON required for CU/assisted PASS",
    )
    validate.add_argument(
        "--release-public-key",
        type=pathlib.Path,
        help="public Ed25519 key required to verify the release manifest",
    )
    validate.add_argument("paths", nargs="+", type=pathlib.Path)
    args = parser.parse_args()
    if args.command_name == "init":
        return init_result(args)
    failed = False
    for path in args.paths:
        errors = validate_result(
            path,
            release_manifest=args.release_manifest,
            release_public_key=args.release_public_key,
        )
        if errors:
            failed = True
            print(f"{path}:", file=sys.stderr)
            for error in errors:
                print(f"  - {error}", file=sys.stderr)
        else:
            print(f"{path}: valid")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
