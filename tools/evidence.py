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
from typing import Optional, Set, Tuple


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
    "Computer Use PASS is disabled until build-sign-package-install "
    "provenance is implemented and independently validated"
)


def load(relative: str):
    return json.loads((ROOT / relative).read_text(encoding="utf-8"))


def release_evidence_chain_ready() -> bool:
    """Fail closed until a real release-attestation validator exists."""
    try:
        gate = load("config/release-evidence.json")
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        return False
    required = {
        "build-provenance",
        "signed-package-provenance",
        "notarization-receipt",
        "installed-bundle-binding",
    }
    return (
        gate.get("schemaVersion") == 1
        and gate.get("releasePassEnabled") is True
        and set(gate.get("requiredChain", [])) == required
    )


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


def validate_object_shape(
    value: object,
    path: str,
    required: set[str],
    allowed: Optional[Set[str]] = None,
) -> list[str]:
    if not isinstance(value, dict):
        return [f"{path} must be an object"]
    allowed = allowed or required
    errors = [
        f"missing field: {path}.{name}" for name in sorted(required - value.keys())
    ]
    errors.extend(
        f"unexpected field: {path}.{name}" for name in sorted(value.keys() - allowed)
    )
    return errors


def validate_string_array(value: object, path: str) -> list[str]:
    if not isinstance(value, list):
        return [f"{path} must be an array"]
    return [
        f"{path}[{index}] must be a string"
        for index, item in enumerate(value)
        if not isinstance(item, str)
    ]


def validate_timestamp(
    value: object, path: str
) -> Tuple[list[str], Optional[dt.datetime]]:
    if not isinstance(value, str):
        return [f"{path} must be a date-time string"], None
    try:
        parsed = dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return [f"{path} is not a valid date-time"], None
    if parsed.tzinfo is None:
        return [f"{path} must include a timezone"], None
    return [], parsed


def validate_shape(data: object) -> list[str]:
    top_required = {
        "schemaVersion", "testId", "requirement", "testClass", "status",
        "executedBy", "source", "productVersion", "chromium", "bundle",
        "environment", "startedAt", "completedAt", "expectedResult",
        "actualResult", "steps", "assertions", "evidence", "artifacts",
    }
    errors = validate_object_shape(
        data, "result", top_required, top_required | {"blocker"}
    )
    if errors or not isinstance(data, dict):
        return errors

    for key in (
        "testId", "requirement", "testClass", "status", "executedBy",
        "productVersion", "expectedResult", "actualResult",
    ):
        if not isinstance(data.get(key), str) or not data[key]:
            errors.append(f"{key} must be a non-empty string")
    if not isinstance(data.get("schemaVersion"), int):
        errors.append("schemaVersion must be an integer")

    source_keys = {"gitCommit", "gitDirty"}
    errors.extend(validate_object_shape(data.get("source"), "source", source_keys))
    source = data.get("source")
    if isinstance(source, dict):
        if not isinstance(source.get("gitCommit"), str):
            errors.append("source.gitCommit must be a string")
        elif not re.fullmatch(r"(?:[0-9a-f]{40}|NOT_AVAILABLE)", source["gitCommit"]):
            errors.append("source.gitCommit is invalid")
        if not isinstance(source.get("gitDirty"), bool):
            errors.append("source.gitDirty must be a boolean")

    chromium_keys = {"version", "commit"}
    errors.extend(
        validate_object_shape(data.get("chromium"), "chromium", chromium_keys)
    )
    chromium = data.get("chromium")
    if isinstance(chromium, dict):
        for key in chromium_keys:
            if not isinstance(chromium.get(key), str):
                errors.append(f"chromium.{key} must be a string")
        if isinstance(chromium.get("version"), str) and not re.fullmatch(
            r"\d+\.0\.\d+\.\d+", chromium["version"]
        ):
            errors.append("chromium.version is invalid")
        if isinstance(chromium.get("commit"), str) and not re.fullmatch(
            r"[0-9a-f]{40}", chromium["commit"]
        ):
            errors.append("chromium.commit is invalid")

    bundle_keys = {
        "path", "sha256", "architecture", "name", "identifier",
        "marketingVersion", "buildNumber", "productVersion", "channel",
        "sourceCommit", "chromiumVersion", "chromiumCommit", "gnArgsSha256",
        "buildProfile", "teamIdentifier", "signingAuthority",
        "signatureVerified", "hardenedRuntimeVerified", "notarizationVerified",
    }
    errors.extend(validate_object_shape(data.get("bundle"), "bundle", bundle_keys))
    bundle = data.get("bundle")
    if isinstance(bundle, dict):
        for key in bundle_keys - {
            "signatureVerified", "hardenedRuntimeVerified", "notarizationVerified"
        }:
            if not isinstance(bundle.get(key), str) or not bundle[key]:
                errors.append(f"bundle.{key} must be a non-empty string")
        for key in (
            "signatureVerified", "hardenedRuntimeVerified", "notarizationVerified"
        ):
            if not isinstance(bundle.get(key), bool):
                errors.append(f"bundle.{key} must be a boolean")
        if isinstance(bundle.get("sha256"), str) and not re.fullmatch(
            r"(?:[0-9a-f]{64}|NOT_AVAILABLE)", bundle["sha256"]
        ):
            errors.append("bundle.sha256 is invalid")
        if isinstance(bundle.get("sourceCommit"), str) and not re.fullmatch(
            r"(?:[0-9a-f]{40}|NOT_AVAILABLE)", bundle["sourceCommit"]
        ):
            errors.append("bundle.sourceCommit is invalid")
        if isinstance(bundle.get("chromiumCommit"), str) and not re.fullmatch(
            r"(?:[0-9a-f]{40}|NOT_AVAILABLE)", bundle["chromiumCommit"]
        ):
            errors.append("bundle.chromiumCommit is invalid")
        if isinstance(bundle.get("gnArgsSha256"), str) and not re.fullmatch(
            r"(?:[0-9a-f]{64}|NOT_AVAILABLE)", bundle["gnArgsSha256"]
        ):
            errors.append("bundle.gnArgsSha256 is invalid")
        if bundle.get("architecture") not in {"arm64", NOT_AVAILABLE}:
            errors.append("bundle.architecture is invalid")
        if bundle.get("buildProfile") not in {"dev", "release", NOT_AVAILABLE}:
            errors.append("bundle.buildProfile is invalid")

    environment_keys = {
        "osVersion", "osBuild", "device", "hostArchitecture", "locale",
        "theme", "glass", "profileType", "startState",
    }
    errors.extend(
        validate_object_shape(
            data.get("environment"), "environment", environment_keys
        )
    )
    environment = data.get("environment")
    if isinstance(environment, dict):
        for key in environment_keys - {"glass"}:
            if not isinstance(environment.get(key), str) or not environment[key]:
                errors.append(f"environment.{key} must be a non-empty string")
        if not isinstance(environment.get("glass"), bool):
            errors.append("environment.glass must be a boolean")
        if environment.get("hostArchitecture") != "arm64":
            errors.append("environment.hostArchitecture must be arm64")
        if environment.get("locale") not in {"de", "en"}:
            errors.append("environment.locale is invalid")
        if environment.get("theme") not in {"system", "light", "dark"}:
            errors.append("environment.theme is invalid")
        if environment.get("profileType") not in {
            "fresh", "existing", "migration", "incognito", "not-applicable"
        }:
            errors.append("environment.profileType is invalid")

    started_errors, started = validate_timestamp(data.get("startedAt"), "startedAt")
    completed_errors, completed = validate_timestamp(
        data.get("completedAt"), "completedAt"
    )
    errors.extend(started_errors + completed_errors)
    if started is not None and completed is not None and completed < started:
        errors.append("completedAt cannot precede startedAt")

    steps = data.get("steps")
    if not isinstance(steps, list):
        errors.append("steps must be an array")
    else:
        step_required = {"index", "action", "expected", "observed"}
        for index, step in enumerate(steps):
            errors.extend(
                validate_object_shape(
                    step, f"steps[{index}]", step_required,
                    step_required | {"artifact"},
                )
            )
            if isinstance(step, dict):
                if not isinstance(step.get("index"), int) or step["index"] < 1:
                    errors.append(f"steps[{index}].index must be a positive integer")
                for key in ("action", "expected", "observed"):
                    if not isinstance(step.get(key), str):
                        errors.append(f"steps[{index}].{key} must be a string")
                for key in ("action", "expected"):
                    if isinstance(step.get(key), str) and not step[key]:
                        errors.append(f"steps[{index}].{key} must be non-empty")
                if "artifact" in step and not isinstance(step["artifact"], str):
                    errors.append(f"steps[{index}].artifact must be a string")

    assertions = data.get("assertions")
    if not isinstance(assertions, list):
        errors.append("assertions must be an array")
    else:
        assertion_keys = {"name", "passed", "evidence"}
        for index, assertion in enumerate(assertions):
            errors.extend(
                validate_object_shape(
                    assertion, f"assertions[{index}]", assertion_keys
                )
            )
            if isinstance(assertion, dict):
                if not isinstance(assertion.get("name"), str) or not assertion["name"]:
                    errors.append(f"assertions[{index}].name must be non-empty")
                if not isinstance(assertion.get("passed"), bool):
                    errors.append(f"assertions[{index}].passed must be a boolean")
                if not isinstance(assertion.get("evidence"), str):
                    errors.append(f"assertions[{index}].evidence must be a string")

    evidence_keys = {
        "screenshots", "videos", "redactedLogs", "fixtureReceipts",
        "networkCaptures", "fileHashes", "testReports", "linkedIssue",
        "repeatRun",
    }
    errors.extend(
        validate_object_shape(data.get("evidence"), "evidence", evidence_keys)
    )
    evidence_data = data.get("evidence")
    if isinstance(evidence_data, dict):
        for key in evidence_keys - {"fileHashes", "linkedIssue", "repeatRun"}:
            errors.extend(validate_string_array(evidence_data.get(key), f"evidence.{key}"))
        hashes = evidence_data.get("fileHashes")
        if not isinstance(hashes, dict):
            errors.append("evidence.fileHashes must be an object")
        else:
            for key, value in hashes.items():
                if not isinstance(key, str) or not re.fullmatch(r"[0-9a-f]{64}", str(value)):
                    errors.append("evidence.fileHashes entries must map paths to SHA-256")
        for key in ("linkedIssue", "repeatRun"):
            if evidence_data.get(key) is not None and not isinstance(
                evidence_data.get(key), str
            ):
                errors.append(f"evidence.{key} must be a string or null")

    errors.extend(validate_string_array(data.get("artifacts"), "artifacts"))
    blocker = data.get("blocker")
    if blocker is not None:
        blocker_keys = {"condition", "owner", "nextAction"}
        errors.extend(validate_object_shape(blocker, "blocker", blocker_keys))
        if isinstance(blocker, dict):
            for key in blocker_keys:
                if not isinstance(blocker.get(key), str) or not blocker[key]:
                    errors.append(f"blocker.{key} must be a non-empty string")
    return errors


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


def validate_result(path: pathlib.Path) -> list[str]:
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
            if not release_evidence_chain_ready():
                errors.append(RELEASE_EVIDENCE_NOT_READY)
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
    validate.add_argument("paths", nargs="+", type=pathlib.Path)
    args = parser.parse_args()
    if args.command_name == "init":
        return init_result(args)
    failed = False
    for path in args.paths:
        errors = validate_result(path)
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
