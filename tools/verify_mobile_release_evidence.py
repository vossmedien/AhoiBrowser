#!/usr/bin/env python3
"""Fail-closed validator for candidate-bound AhoiBrowser Mobile evidence."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import pathlib
import re
import sys
from typing import Any, Callable, Optional

from mobile_evidence_artifacts import (
    EvidenceInspectionError,
    inspect_archive,
    inspect_xcresult,
    repository_state,
    sha256_path,
)


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCHEMA_VERSION = 1
SHA256_RE = re.compile(r"[0-9a-f]{64}")
COMMIT_RE = re.compile(r"[0-9a-f]{40}")
APPLE_BUILD_ID_RE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:-]{2,127}")
ALLOWED_STAGES = {
    "SOURCE_COMPLETE", "LOCAL_BUILD_PASS", "UNIT_PASS", "INTEGRATION_PASS",
    "SIMULATOR_VISIBLE", "DEVICE_VISIBLE", "ASSISTED_E2E_PASS", "ARCHIVE_PASS",
    "TESTFLIGHT_INTERNAL_PASS", "TESTFLIGHT_PUBLIC_PASS",
    "DEFAULT_BROWSER_REQUESTED", "DEFAULT_BROWSER_GRANTED",
    "DEFAULT_BROWSER_E2E_PASS", "RELEASE_PASS",
}
RELEASE_CONFIGURATIONS = {"TestFlightBootstrap", "ReleasePostGrant"}
POST_GRANT_STAGES = {
    "DEFAULT_BROWSER_GRANTED",
    "DEFAULT_BROWSER_E2E_PASS",
    "RELEASE_PASS",
}
TESTFLIGHT_STAGES = {
    "TESTFLIGHT_INTERNAL_PASS", "TESTFLIGHT_PUBLIC_PASS",
    "DEFAULT_BROWSER_REQUESTED", "DEFAULT_BROWSER_GRANTED",
    "DEFAULT_BROWSER_E2E_PASS", "RELEASE_PASS",
}
ARCHIVE_STAGES = {"ARCHIVE_PASS", *TESTFLIGHT_STAGES}
RESULT_KEYS = {"startedAt", "completedAt", "expectedResult", "actualResult"}


def object_at(value: Any, label: str, errors: list[str]) -> dict[str, Any]:
    if not isinstance(value, dict):
        errors.append(f"{label} must be an object")
        return {}
    return value


def array_at(value: Any, label: str, errors: list[str]) -> list[Any]:
    if not isinstance(value, list):
        errors.append(f"{label} must be an array")
        return []
    return value


def exact_keys(
    value: dict[str, Any], label: str, required: set[str], optional: set[str], errors: list[str]
) -> None:
    for key in sorted(required - value.keys()):
        errors.append(f"{label}.{key} is required")
    for key in sorted(value.keys() - required - optional):
        errors.append(f"unexpected field: {label}.{key}")


def string_at(
    value: dict[str, Any], key: str, label: str, errors: list[str]
) -> str:
    result = value.get(key)
    if not isinstance(result, str) or not result.strip():
        errors.append(f"{label}.{key} must be a non-empty string")
        return ""
    return result


def validate_result_context(
    value: dict[str, Any], label: str, errors: list[str]
) -> None:
    timestamps = []
    for key in ("startedAt", "completedAt"):
        raw = string_at(value, key, label, errors)
        try:
            parsed = dt.datetime.fromisoformat(raw.replace("Z", "+00:00"))
        except ValueError:
            errors.append(f"{label}.{key} must be an ISO-8601 timestamp")
            continue
        if parsed.tzinfo is None:
            errors.append(f"{label}.{key} must include a timezone")
        timestamps.append(parsed)
    for key in ("expectedResult", "actualResult"):
        string_at(value, key, label, errors)
    if len(timestamps) == 2 and timestamps[1] < timestamps[0]:
        errors.append(f"{label}.completedAt must not precede startedAt")


def validate_device(value: Any, label: str, errors: list[str]) -> dict[str, Any]:
    device = object_at(value, label, errors)
    exact_keys(
        device,
        label,
        {"identifier", "model", "platform", "osVersion", "osBuild", "physical"},
        set(),
        errors,
    )
    for key in ("identifier", "model", "platform", "osVersion", "osBuild"):
        string_at(device, key, label, errors)
    if not isinstance(device.get("physical"), bool):
        errors.append(f"{label}.physical must be a Boolean")
    return device


def artifact_path(
    manifest_path: pathlib.Path,
    value: Any,
    label: str,
    errors: list[str],
) -> Optional[pathlib.Path]:
    reference = object_at(value, label, errors)
    exact_keys(reference, label, {"path", "sha256"}, set(), errors)
    relative = string_at(reference, "path", label, errors)
    expected = string_at(reference, "sha256", label, errors)
    if expected and SHA256_RE.fullmatch(expected) is None:
        errors.append(f"{label}.sha256 must be lowercase SHA-256")
    pure = pathlib.PurePosixPath(relative)
    if not relative or pure.is_absolute() or ".." in pure.parts:
        errors.append(f"{label}.path must stay inside the candidate directory")
        return None
    base = manifest_path.resolve().parent
    resolved = (base / relative).resolve()
    try:
        resolved.relative_to(base)
    except ValueError:
        errors.append(f"{label}.path escapes the candidate directory")
        return None
    try:
        actual = sha256_path(resolved)
    except EvidenceInspectionError as error:
        errors.append(f"{label}: {error}")
        return None
    if expected and actual != expected:
        errors.append(f"{label}.sha256 does not match the local artifact")
    return resolved


def json_receipt(
    manifest_path: pathlib.Path,
    value: Any,
    label: str,
    expected: dict[str, Any],
    errors: list[str],
) -> dict[str, Any]:
    path = artifact_path(manifest_path, value, label, errors)
    if path is None:
        return {}
    if path.suffix != ".json" or not path.is_file():
        errors.append(f"{label} must reference a JSON receipt")
        return {}
    try:
        receipt = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        errors.append(f"{label} cannot be read as JSON: {error}")
        return {}
    if not isinstance(receipt, dict):
        errors.append(f"{label} JSON root must be an object")
        return {}
    if receipt.get("schemaVersion") != 1:
        errors.append(f"{label}.schemaVersion must be 1")
    captured = receipt.get("capturedAt")
    try:
        parsed = dt.datetime.fromisoformat(str(captured).replace("Z", "+00:00"))
    except ValueError:
        errors.append(f"{label}.capturedAt must be an ISO-8601 timestamp")
    else:
        if parsed.tzinfo is None:
            errors.append(f"{label}.capturedAt must include a timezone")
    for key, expected_value in expected.items():
        if receipt.get(key) != expected_value:
            errors.append(f"{label}.{key} does not match the candidate chain")
    return receipt


def receipt_identity(candidate: dict[str, Any]) -> dict[str, Any]:
    return {
        key: candidate.get(key)
        for key in (
            "sourceCommit", "bundleId", "teamId", "marketingVersion", "buildNumber"
        )
    }


def validate_identity(
    value: dict[str, Any], candidate: dict[str, Any], label: str, errors: list[str]
) -> None:
    mapping = {
        "sourceCommit": "sourceCommit",
        "bundleId": "bundleId",
        "teamId": "teamId",
        "marketingVersion": "marketingVersion",
        "buildNumber": "buildNumber",
    }
    for key, candidate_key in mapping.items():
        if value.get(key) != candidate.get(candidate_key):
            errors.append(f"{label}.{key} does not match candidate.{candidate_key}")


def validate_xcresults(
    manifest_path: pathlib.Path,
    values: Any,
    candidate: dict[str, Any],
    errors: list[str],
    inspector: Callable[[pathlib.Path], dict[str, Any]],
) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for index, raw in enumerate(array_at(values, "xcresults", errors)):
        label = f"xcresults[{index}]"
        item = object_at(raw, label, errors)
        exact_keys(
            item,
            label,
            {
                "kind", "resultBundle", "sourceCommit", "bundleId", "teamId",
                "marketingVersion", "buildNumber", "configuration", "device",
            } | RESULT_KEYS,
            set(),
            errors,
        )
        kind = string_at(item, "kind", label, errors)
        if kind not in {"unit", "integration", "ui"}:
            errors.append(f"{label}.kind must be unit, integration or ui")
        validate_identity(item, candidate, label, errors)
        validate_result_context(item, label, errors)
        string_at(item, "configuration", label, errors)
        device = validate_device(item.get("device"), f"{label}.device", errors)
        bundle_path = artifact_path(
            manifest_path, item.get("resultBundle"), f"{label}.resultBundle", errors
        )
        if bundle_path is not None:
            try:
                summary = inspector(bundle_path)
            except EvidenceInspectionError as error:
                errors.append(f"{label}.resultBundle: {error}")
            else:
                if summary.get("result") != "Passed":
                    errors.append(f"{label} xcresult is not Passed")
                if summary.get("failedTests") != 0:
                    errors.append(f"{label} xcresult contains failed tests")
                if not isinstance(summary.get("passedTests"), int) or summary.get(
                    "passedTests", 0
                ) <= 0:
                    errors.append(f"{label} xcresult contains no passed tests")
                configurations = summary.get("devicesAndConfigurations", [])
                if isinstance(configurations, dict):
                    configurations = [configurations]
                actual_devices = [
                    value.get("device", {}) for value in configurations
                    if isinstance(value, dict)
                ] if isinstance(configurations, list) else []
                comparison = {
                    "identifier": "deviceId",
                    "model": "modelName",
                    "platform": "platform",
                    "osVersion": "osVersion",
                    "osBuild": "osBuildNumber",
                }
                if not any(actual and all(
                    actual.get(actual_key) in (None, device.get(expected_key))
                    for expected_key, actual_key in comparison.items()
                ) for actual in actual_devices):
                    errors.append(f"{label}.device does not match an xcresult destination")
        results.append(item)
    return results


def validate_builds(
    manifest_path: pathlib.Path,
    values: Any,
    candidate: dict[str, Any],
    errors: list[str],
) -> list[dict[str, Any]]:
    builds: list[dict[str, Any]] = []
    for index, raw in enumerate(array_at(values, "builds", errors)):
        label = f"builds[{index}]"
        item = object_at(raw, label, errors)
        exact_keys(
            item,
            label,
            {
                "kind", "status", "sourceCommit", "bundleId", "teamId",
                "marketingVersion", "buildNumber", "configuration", "destination",
                "receipt",
            } | RESULT_KEYS,
            set(),
            errors,
        )
        if item.get("kind") not in {"simulator", "device"}:
            errors.append(f"{label}.kind must be simulator or device")
        if item.get("status") != "PASS":
            errors.append(f"{label}.status must be PASS")
        validate_identity(item, candidate, label, errors)
        validate_result_context(item, label, errors)
        string_at(item, "configuration", label, errors)
        string_at(item, "destination", label, errors)
        artifact_path(manifest_path, item.get("receipt"), f"{label}.receipt", errors)
        builds.append(item)
    return builds


def validate_executions(
    manifest_path: pathlib.Path,
    values: Any,
    candidate: dict[str, Any],
    errors: list[str],
) -> list[dict[str, Any]]:
    executions: list[dict[str, Any]] = []
    allowed_kinds = {
        "simulator-visible", "device-visible", "assisted-e2e", "default-browser-e2e"
    }
    for index, raw in enumerate(array_at(values, "executions", errors)):
        label = f"executions[{index}]"
        item = object_at(raw, label, errors)
        exact_keys(
            item,
            label,
            {
                "kind", "status", "sourceCommit", "bundleId", "teamId",
                "marketingVersion", "buildNumber", "configuration", "device",
                "installationSource", "testIds", "evidence",
            } | RESULT_KEYS,
            {"appStoreConnectBuildId"},
            errors,
        )
        kind = item.get("kind")
        if kind not in allowed_kinds:
            errors.append(f"{label}.kind is unsupported")
        if item.get("status") != "PASS":
            errors.append(f"{label}.status must be PASS")
        validate_identity(item, candidate, label, errors)
        validate_result_context(item, label, errors)
        string_at(item, "configuration", label, errors)
        string_at(item, "installationSource", label, errors)
        device = validate_device(item.get("device"), f"{label}.device", errors)
        test_ids = array_at(item.get("testIds"), f"{label}.testIds", errors)
        if not test_ids or any(not isinstance(value, str) or not value for value in test_ids):
            errors.append(f"{label}.testIds must contain test identifiers")
        evidence = array_at(item.get("evidence"), f"{label}.evidence", errors)
        if not evidence:
            errors.append(f"{label}.evidence must not be empty")
        for evidence_index, reference in enumerate(evidence):
            artifact_path(
                manifest_path,
                reference,
                f"{label}.evidence[{evidence_index}]",
                errors,
            )
        if kind != "simulator-visible" and device.get("physical") is not True:
            errors.append(f"{label} must target a physical device")
        if kind == "simulator-visible" and device.get("physical") is not False:
            errors.append(f"{label} must target a simulator")
        executions.append(item)
    return executions


def validate_archive(
    manifest_path: pathlib.Path,
    value: Any,
    candidate: dict[str, Any],
    errors: list[str],
    inspector: Callable[[pathlib.Path], dict[str, str]],
) -> dict[str, Any]:
    archive = object_at(value, "archive", errors)
    exact_keys(
        archive,
        "archive",
        {
            "bundle", "inspectionReceipt", "sourceCommit", "bundleId", "teamId",
            "marketingVersion", "buildNumber", "configuration",
        } | RESULT_KEYS,
        set(),
        errors,
    )
    validate_identity(archive, candidate, "archive", errors)
    validate_result_context(archive, "archive", errors)
    if archive.get("configuration") != candidate.get("configuration"):
        errors.append("archive.configuration does not match candidate.configuration")
    json_receipt(
        manifest_path,
        archive.get("inspectionReceipt"),
        "archive.inspectionReceipt",
        {
            "kind": "archive-inspection",
            **receipt_identity(candidate),
            "configuration": candidate.get("configuration"),
            "status": "PASS",
        },
        errors,
    )
    bundle_path = artifact_path(
        manifest_path, archive.get("bundle"), "archive.bundle", errors
    )
    if bundle_path is not None:
        try:
            actual = inspector(bundle_path)
        except EvidenceInspectionError as error:
            errors.append(f"archive.bundle: {error}")
        else:
            for key in (
                "sourceCommit", "bundleId", "teamId", "marketingVersion",
                "buildNumber", "configuration",
            ):
                if actual.get(key) != candidate.get(key):
                    errors.append(f"archive binary {key} does not match candidate")
    return archive


def validate_app_store_connect(
    manifest_path: pathlib.Path,
    value: Any,
    candidate: dict[str, Any],
    errors: list[str],
) -> dict[str, Any]:
    asc = object_at(value, "appStoreConnect", errors)
    exact_keys(
        asc,
        "appStoreConnect",
        {
            "buildId", "sourceCommit", "bundleId", "teamId", "marketingVersion",
            "buildNumber", "processingState", "uploadReceipt", "processingReceipt",
            "publicTestFlight",
        } | RESULT_KEYS,
        set(),
        errors,
    )
    validate_identity(asc, candidate, "appStoreConnect", errors)
    validate_result_context(asc, "appStoreConnect", errors)
    build_id = string_at(asc, "buildId", "appStoreConnect", errors)
    if build_id and APPLE_BUILD_ID_RE.fullmatch(build_id) is None:
        errors.append("appStoreConnect.buildId has an invalid format")
    if asc.get("processingState") != "VALID":
        errors.append("appStoreConnect.processingState must be VALID")
    json_receipt(
        manifest_path,
        asc.get("uploadReceipt"),
        "appStoreConnect.uploadReceipt",
        {"kind": "app-store-connect-upload", **receipt_identity(candidate),
         "buildId": build_id, "status": "UPLOADED"},
        errors,
    )
    json_receipt(
        manifest_path,
        asc.get("processingReceipt"),
        "appStoreConnect.processingReceipt",
        {"kind": "app-store-connect-processing", **receipt_identity(candidate),
         "buildId": build_id, "processingState": "VALID"},
        errors,
    )
    public = object_at(asc.get("publicTestFlight"), "appStoreConnect.publicTestFlight", errors)
    exact_keys(
        public,
        "appStoreConnect.publicTestFlight",
        {"enabled", "betaReviewState", "url", "receipt"},
        set(),
        errors,
    )
    if not isinstance(public.get("enabled"), bool):
        errors.append("appStoreConnect.publicTestFlight.enabled must be a Boolean")
    if public.get("enabled") is True:
        if public.get("betaReviewState") != "APPROVED":
            errors.append("public TestFlight beta review must be APPROVED")
        url = string_at(public, "url", "appStoreConnect.publicTestFlight", errors)
        if not re.fullmatch(r"https://testflight\.apple\.com/join/[A-Za-z0-9]+", url):
            errors.append("public TestFlight URL must be an Apple join URL")
        json_receipt(
            manifest_path,
            public.get("receipt"),
            "appStoreConnect.publicTestFlight.receipt",
            {
                "kind": "public-testflight-link", "buildId": build_id,
                "enabled": True, "betaReviewState": "APPROVED", "url": url,
            },
            errors,
        )
    return asc


def validate_installations(
    manifest_path: pathlib.Path,
    values: Any,
    candidate: dict[str, Any],
    build_id: str,
    errors: list[str],
) -> list[dict[str, Any]]:
    installations: list[dict[str, Any]] = []
    for index, raw in enumerate(array_at(values, "testFlightInstallations", errors)):
        label = f"testFlightInstallations[{index}]"
        item = object_at(raw, label, errors)
        exact_keys(
            item,
            label,
            {
                "source", "appStoreConnectBuildId", "sourceCommit", "bundleId", "teamId",
                "marketingVersion", "buildNumber", "device", "installationReceipt",
                "journeyReceipt",
            } | RESULT_KEYS,
            set(),
            errors,
        )
        if item.get("source") != "TestFlight":
            errors.append(f"{label}.source must be TestFlight")
        validate_identity(item, candidate, label, errors)
        validate_result_context(item, label, errors)
        if item.get("appStoreConnectBuildId") != build_id:
            errors.append(f"{label}.appStoreConnectBuildId does not match processed build")
        device = validate_device(item.get("device"), f"{label}.device", errors)
        if device.get("physical") is not True:
            errors.append(f"{label} must target a physical device")
        json_receipt(
            manifest_path,
            item.get("installationReceipt"),
            f"{label}.installationReceipt",
            {
                "kind": "testflight-installation", **receipt_identity(candidate),
                "buildId": build_id, "device": device, "status": "INSTALLED",
            },
            errors,
        )
        json_receipt(
            manifest_path,
            item.get("journeyReceipt"),
            f"{label}.journeyReceipt",
            {
                "kind": "testflight-device-journey", **receipt_identity(candidate),
                "buildId": build_id, "device": device, "status": "PASS",
            },
            errors,
        )
        installations.append(item)
    return installations


def device_families(items: list[dict[str, Any]]) -> set[str]:
    families: set[str] = set()
    for item in items:
        model = str(item.get("device", {}).get("model", "")).lower()
        platform = str(item.get("device", {}).get("platform", "")).lower()
        if "ipad" in model or "ipad" in platform:
            families.add("iPad")
        elif "iphone" in model or "iphone" in platform:
            families.add("iPhone")
    return families


def validate_default_browser(
    manifest_path: pathlib.Path,
    value: Any,
    candidate: dict[str, Any],
    build_id: str,
    errors: list[str],
) -> dict[str, Any]:
    boundary = object_at(value, "defaultBrowser", errors)
    exact_keys(
        boundary,
        "defaultBrowser",
        {"requestState", "requestReceipt", "grantState", "grantReceipt"} | RESULT_KEYS,
        set(),
        errors,
    )
    validate_result_context(boundary, "defaultBrowser", errors)
    if boundary.get("requestState") not in {"NOT_REQUESTED", "REQUESTED"}:
        errors.append("defaultBrowser.requestState is invalid")
    if boundary.get("grantState") not in {"NOT_GRANTED", "GRANTED"}:
        errors.append("defaultBrowser.grantState is invalid")
    expected_states = {"requestReceipt": "REQUESTED", "grantReceipt": "GRANTED"}
    for key, state in expected_states.items():
        if boundary.get(key) is not None:
            json_receipt(
                manifest_path,
                boundary.get(key),
                f"defaultBrowser.{key}",
                {
                    "kind": f"default-browser-{state.lower()}",
                    **receipt_identity(candidate),
                    "buildId": build_id,
                    "state": state,
                },
                errors,
            )
    return boundary


def validate_manifest(
    manifest_path: pathlib.Path,
    *,
    repository_root: pathlib.Path = ROOT,
    state_reader: Callable[[pathlib.Path], tuple[str, bool]] = repository_state,
    xcresult_inspector: Callable[[pathlib.Path], dict[str, Any]] = inspect_xcresult,
    archive_inspector: Callable[[pathlib.Path], dict[str, str]] = inspect_archive,
) -> list[str]:
    errors: list[str] = []
    try:
        data = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        return [f"cannot read Mobile evidence manifest: {error}"]
    root = object_at(data, "manifest", errors)
    exact_keys(
        root,
        "manifest",
        {
            "schemaVersion", "createdAt", "candidate", "claimedStages", "builds",
            "xcresults", "executions", "archive", "appStoreConnect",
            "testFlightInstallations", "defaultBrowser",
        },
        set(),
        errors,
    )
    if root.get("schemaVersion") != SCHEMA_VERSION:
        errors.append(f"schemaVersion must be {SCHEMA_VERSION}")
    created_at = string_at(root, "createdAt", "manifest", errors)
    if created_at and re.fullmatch(r"\d{4}-\d{2}-\d{2}T[^\s]+(?:Z|[+-]\d{2}:\d{2})", created_at) is None:
        errors.append("manifest.createdAt must be an ISO-8601 timestamp with timezone")

    candidate = object_at(root.get("candidate"), "candidate", errors)
    exact_keys(
        candidate,
        "candidate",
        {
            "id", "sourceCommit", "sourceDirty", "configuration", "bundleId",
            "teamId", "marketingVersion", "buildNumber",
        },
        set(),
        errors,
    )
    for key in (
        "id", "sourceCommit", "configuration", "bundleId", "teamId",
        "marketingVersion", "buildNumber",
    ):
        string_at(candidate, key, "candidate", errors)
    if COMMIT_RE.fullmatch(str(candidate.get("sourceCommit", ""))) is None:
        errors.append("candidate.sourceCommit must be an exact lowercase Git SHA")
    if candidate.get("sourceDirty") is not False:
        errors.append("candidate.sourceDirty must be false")
    if candidate.get("configuration") not in {
        "DebugLocal", "CloudKitDevelopment", "TestFlightBootstrap",
        "DefaultBrowserDevelopment", "ReleasePostGrant",
    }:
        errors.append("candidate.configuration is not a supported Mobile build mode")
    try:
        current_commit, current_dirty = state_reader(repository_root)
    except EvidenceInspectionError as error:
        errors.append(str(error))
    else:
        if candidate.get("sourceCommit") != current_commit:
            errors.append("candidate.sourceCommit does not match the checked-out repository")
        if current_dirty:
            errors.append("Mobile evidence validation requires a clean checkout")

    claimed_raw = array_at(root.get("claimedStages"), "claimedStages", errors)
    claimed = {value for value in claimed_raw if isinstance(value, str)}
    if len(claimed) != len(claimed_raw):
        errors.append("claimedStages must contain unique strings")
    for stage in sorted(claimed - ALLOWED_STAGES):
        errors.append(f"unsupported claimed stage: {stage}")
    if not claimed:
        errors.append("claimedStages must not be empty")
    if claimed & ARCHIVE_STAGES and candidate.get("configuration") not in RELEASE_CONFIGURATIONS:
        errors.append("archive/TestFlight stages require a release build mode")
    if claimed & POST_GRANT_STAGES and candidate.get("configuration") != "ReleasePostGrant":
        errors.append("post-grant stages require ReleasePostGrant")

    builds = validate_builds(
        manifest_path, root.get("builds"), candidate, errors
    )
    xcresults = validate_xcresults(
        manifest_path,
        root.get("xcresults"),
        candidate,
        errors,
        xcresult_inspector,
    )
    executions = validate_executions(
        manifest_path, root.get("executions"), candidate, errors
    )
    archive = validate_archive(
        manifest_path, root.get("archive"), candidate, errors, archive_inspector
    ) if claimed & ARCHIVE_STAGES else {}
    asc = validate_app_store_connect(
        manifest_path, root.get("appStoreConnect"), candidate, errors
    ) if claimed & TESTFLIGHT_STAGES else {}
    installations = validate_installations(
        manifest_path,
        root.get("testFlightInstallations"),
        candidate,
        str(asc.get("buildId", "")),
        errors,
    ) if claimed & TESTFLIGHT_STAGES else []
    default_browser = validate_default_browser(
        manifest_path, root.get("defaultBrowser"), candidate,
        str(asc.get("buildId", "")), errors
    ) if claimed & {
        "DEFAULT_BROWSER_REQUESTED", "DEFAULT_BROWSER_GRANTED",
        "DEFAULT_BROWSER_E2E_PASS", "RELEASE_PASS",
    } else {}

    if "LOCAL_BUILD_PASS" in claimed and not builds:
        errors.append("LOCAL_BUILD_PASS requires a hashed successful build receipt")
    if "UNIT_PASS" in claimed and not any(item.get("kind") == "unit" for item in xcresults):
        errors.append("UNIT_PASS requires a passing unit-test xcresult")
    if "INTEGRATION_PASS" in claimed and not any(
        item.get("kind") == "integration" for item in xcresults
    ):
        errors.append("INTEGRATION_PASS requires a passing integration-test xcresult")
    execution_stage_kinds = {
        "SIMULATOR_VISIBLE": "simulator-visible",
        "DEVICE_VISIBLE": "device-visible",
        "ASSISTED_E2E_PASS": "assisted-e2e",
        "DEFAULT_BROWSER_E2E_PASS": "default-browser-e2e",
    }
    for stage, kind in execution_stage_kinds.items():
        if stage in claimed and not any(item.get("kind") == kind for item in executions):
            errors.append(f"{stage} requires a matching visible execution")
    if claimed & TESTFLIGHT_STAGES:
        if not archive:
            errors.append("TestFlight evidence requires an inspected archive")
        if not installations:
            errors.append("TestFlight evidence requires a physical TestFlight installation")
        for item in executions:
            if item.get("installationSource") == "TestFlight" and (
                item.get("appStoreConnectBuildId") != asc.get("buildId")
            ):
                errors.append("TestFlight execution does not match App Store Connect build")
    if "TESTFLIGHT_PUBLIC_PASS" in claimed or claimed & {
        "DEFAULT_BROWSER_REQUESTED", "DEFAULT_BROWSER_GRANTED",
        "DEFAULT_BROWSER_E2E_PASS", "RELEASE_PASS",
    }:
        public = asc.get("publicTestFlight", {})
        if not isinstance(public, dict) or public.get("enabled") is not True:
            errors.append("this stage requires an active public TestFlight link")
    if "DEFAULT_BROWSER_REQUESTED" in claimed or claimed & POST_GRANT_STAGES:
        if default_browser.get("requestState") != "REQUESTED" or not default_browser.get(
            "requestReceipt"
        ):
            errors.append("default-browser request stage requires its hashed receipt")
    if claimed & POST_GRANT_STAGES:
        if default_browser.get("grantState") != "GRANTED" or not default_browser.get(
            "grantReceipt"
        ):
            errors.append("post-grant stage requires Apple's hashed grant receipt")
    if "DEFAULT_BROWSER_E2E_PASS" in claimed:
        matching = [
            item for item in executions if item.get("kind") == "default-browser-e2e"
        ]
        if device_families(matching) != {"iPhone", "iPad"}:
            errors.append("DEFAULT_BROWSER_E2E_PASS requires physical iPhone and iPad")
    if "RELEASE_PASS" in claimed:
        if device_families(installations) != {"iPhone", "iPad"}:
            errors.append("RELEASE_PASS requires TestFlight installs on iPhone and iPad")
        required_ids = {
            *(f"MOB-USER-{number:02d}" for number in range(1, 16)),
            *(f"IOS-{number:02d}" for number in range(1, 16)),
        }
        actual_ids = {
            test_id
            for item in executions
            for test_id in item.get("testIds", [])
            if isinstance(test_id, str)
        }
        missing = sorted(required_ids - actual_ids)
        if missing:
            errors.append("RELEASE_PASS is missing journeys: " + ", ".join(missing))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate candidate-bound AhoiBrowser Mobile release evidence."
    )
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("--repository-root", type=pathlib.Path, default=ROOT)
    args = parser.parse_args()
    errors = validate_manifest(
        args.manifest.resolve(), repository_root=args.repository_root.resolve()
    )
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"Mobile release evidence valid: {args.manifest.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
