"""Strict structural validation for Ahoi E2E evidence payloads."""

from __future__ import annotations

import datetime as dt
import re
from typing import Optional, Set, Tuple


NOT_AVAILABLE = "NOT_AVAILABLE"


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
        "schemaVersion",
        "testId",
        "requirement",
        "testClass",
        "status",
        "executedBy",
        "source",
        "productVersion",
        "chromium",
        "bundle",
        "environment",
        "startedAt",
        "completedAt",
        "expectedResult",
        "actualResult",
        "steps",
        "assertions",
        "evidence",
        "artifacts",
    }
    errors = validate_object_shape(
        data, "result", top_required, top_required | {"blocker"}
    )
    if errors or not isinstance(data, dict):
        return errors

    for key in (
        "testId",
        "requirement",
        "testClass",
        "status",
        "executedBy",
        "productVersion",
        "expectedResult",
        "actualResult",
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
        elif not re.fullmatch(
            r"(?:[0-9a-f]{40}|NOT_AVAILABLE)", source["gitCommit"]
        ):
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
        "path",
        "sha256",
        "architecture",
        "name",
        "identifier",
        "marketingVersion",
        "buildNumber",
        "productVersion",
        "channel",
        "sourceCommit",
        "chromiumVersion",
        "chromiumCommit",
        "gnArgsSha256",
        "buildProfile",
        "teamIdentifier",
        "signingAuthority",
        "signatureVerified",
        "hardenedRuntimeVerified",
        "notarizationVerified",
    }
    errors.extend(validate_object_shape(data.get("bundle"), "bundle", bundle_keys))
    bundle = data.get("bundle")
    if isinstance(bundle, dict):
        for key in bundle_keys - {
            "signatureVerified",
            "hardenedRuntimeVerified",
            "notarizationVerified",
        }:
            if not isinstance(bundle.get(key), str) or not bundle[key]:
                errors.append(f"bundle.{key} must be a non-empty string")
        for key in (
            "signatureVerified",
            "hardenedRuntimeVerified",
            "notarizationVerified",
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
        "osVersion",
        "osBuild",
        "device",
        "hostArchitecture",
        "locale",
        "theme",
        "glass",
        "profileType",
        "startState",
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
            "fresh",
            "existing",
            "migration",
            "incognito",
            "not-applicable",
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
                    step,
                    f"steps[{index}]",
                    step_required,
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
        "screenshots",
        "videos",
        "redactedLogs",
        "fixtureReceipts",
        "networkCaptures",
        "fileHashes",
        "testReports",
        "linkedIssue",
        "repeatRun",
    }
    errors.extend(
        validate_object_shape(data.get("evidence"), "evidence", evidence_keys)
    )
    evidence_data = data.get("evidence")
    if isinstance(evidence_data, dict):
        for key in evidence_keys - {"fileHashes", "linkedIssue", "repeatRun"}:
            errors.extend(
                validate_string_array(evidence_data.get(key), f"evidence.{key}")
            )
        hashes = evidence_data.get("fileHashes")
        if not isinstance(hashes, dict):
            errors.append("evidence.fileHashes must be an object")
        else:
            for key, value in hashes.items():
                if not isinstance(key, str) or not re.fullmatch(
                    r"[0-9a-f]{64}", str(value)
                ):
                    errors.append(
                        "evidence.fileHashes entries must map paths to SHA-256"
                    )
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
