#!/usr/bin/env python3
"""Derive Ahoi Mobile performance samples only from raw capture artifacts."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
import pathlib
import re
import subprocess
import sys
from typing import Any

VERSION = "2.0.0"
ROOT = pathlib.Path(__file__).resolve().parents[3]
MOBILE = ROOT / "apps" / "AhoiMobile"
DEFAULT_BUDGETS = MOBILE / "performance-budgets.json"
HARNESS = MOBILE / "scripts" / "capture-performance-evidence.sh"
HELPER = MOBILE / "scripts" / "mobile-performance-evidence-helper.py"
HELPER_SPEC = importlib.util.spec_from_file_location("ahoi_mobile_performance_helper", HELPER)
if HELPER_SPEC is None or HELPER_SPEC.loader is None:
    raise RuntimeError("could not load the raw performance evidence helper")
RAW_HELPER = importlib.util.module_from_spec(HELPER_SPEC)
HELPER_SPEC.loader.exec_module(RAW_HELPER)
HEX_64 = re.compile(r"^[0-9a-f]{64}$")
SHA_40 = re.compile(r"^[0-9a-f]{40}$")
CAPTURE_STATUS = "CAPTURED_RAW_EVIDENCE"
STAGE_STATUS = "CAPTURED_RAW"
STAGE_CONTRACTS = RAW_HELPER.STAGE_CONTRACTS
DATA_QUERIES = {
    "life-cycle-period": '/trace-toc/run[@number="1"]/data/table[@schema="life-cycle-period"]',
    "sysmon-process": '/trace-toc/run[@number="1"]/data/table[@schema="sysmon-process"]',
    "hitches-summary": '/trace-toc/run[@number="1"]/data/table[@schema="hitches-summary"]',
    "har": "HAR-v1",
}


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_path(path: pathlib.Path) -> str:
    if path.is_file():
        return sha256_file(path)
    digest = hashlib.sha256(b"ahoi-mobile-tree-sha256-v1\0")
    children = sorted(path.rglob("*"), key=lambda item: item.relative_to(path).as_posix())
    for child in children:
        relative = child.relative_to(path).as_posix().encode()
        if child.is_symlink():
            digest.update(b"L\0" + relative + b"\0" + os.readlink(child).encode() + b"\0")
        elif child.is_file():
            digest.update(b"F\0" + relative + b"\0" + bytes.fromhex(sha256_file(child)))
        elif child.is_dir():
            digest.update(b"D\0" + relative + b"\0")
    return digest.hexdigest()


def canonical_bytes(payload: Any) -> bytes:
    return (json.dumps(payload, indent=2, sort_keys=True) + "\n").encode()


def is_number(value: Any) -> bool:
    return type(value) in {int, float} and math.isfinite(float(value)) and value >= 0


def exact_keys(value: Any, expected: set[str], label: str, errors: list[str]) -> bool:
    if not isinstance(value, dict):
        errors.append(f"{label} must be an object")
        return False
    missing = sorted(expected - set(value))
    extra = sorted(set(value) - expected)
    if missing:
        errors.append(f"{label} is missing keys: {', '.join(missing)}")
    if extra:
        errors.append(f"{label} contains unsupported keys: {', '.join(extra)}")
    return not missing and not extra


def below_root(path: pathlib.Path) -> bool:
    try:
        path.resolve().relative_to(ROOT)
        return True
    except ValueError:
        return False


def read_json(path: pathlib.Path, label: str, errors: list[str]) -> Any:
    if not below_root(path) or path.is_symlink() or not path.is_file():
        errors.append(f"{label} must be a regular non-symlink file below the repository")
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        errors.append(f"{label} is not readable canonical JSON: {error}")
        return None


def safe_path(root: pathlib.Path, relative: Any, label: str, errors: list[str]):
    if not isinstance(relative, str) or not relative:
        errors.append(f"{label} must be a non-empty relative path")
        return None
    pure = pathlib.PurePosixPath(relative)
    if pure.is_absolute() or ".." in pure.parts:
        errors.append(f"{label} may not escape the capture root")
        return None
    path = root.joinpath(*pure.parts)
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError:
        errors.append(f"{label} resolves outside the capture root")
        return None
    return path


def validate_budgets(payload: Any, errors: list[str]) -> dict[str, dict[str, Any]]:
    keys = {
        "schemaVersion", "kind", "budgetVersion", "frozenBeforeFeatureCandidate",
        "frozenAtBaseCommit", "frozenOn", "amendmentPolicy", "statistics",
        "candidatePolicy", "normalizationBoundary", "metrics",
    }
    if not exact_keys(payload, keys, "budgets", errors):
        return {}
    if payload.get("schemaVersion") != 2 or payload.get("kind") != "mobile-performance-budgets":
        errors.append("budgets schemaVersion/kind is unsupported")
    if payload.get("frozenBeforeFeatureCandidate") is not True:
        errors.append("budgets must be frozen before the feature candidate")
    base = payload.get("frozenAtBaseCommit")
    if not isinstance(base, str) or not SHA_40.fullmatch(base):
        errors.append("budgets frozenAtBaseCommit is invalid")
    policy = payload.get("candidatePolicy", {})
    policy_keys = {
        "bundleId", "captureMode", "captureStatus", "cleanHostRequired", "deviceKind",
        "hostCleanliness", "platform", "requiredBuildMode", "requiredOptimizationLevel",
        "signingTeamIdentifier", "simulatorOnlyAccepted", "strictCodeSignatureRequired",
    }
    if exact_keys(policy, policy_keys, "budgets.candidatePolicy", errors):
        expected = {
            "bundleId": "app.ahoibrowser.AhoiBrowser", "captureMode": "record",
            "captureStatus": CAPTURE_STATUS, "cleanHostRequired": True,
            "deviceKind": "physical", "platform": "iOS",
            "requiredBuildMode": "PerformanceDevelopment", "requiredOptimizationLevel": "-O",
            "signingTeamIdentifier": "248AJ5BN47", "simulatorOnlyAccepted": False,
            "strictCodeSignatureRequired": True,
        }
        for key, value in expected.items():
            if policy.get(key) != value:
                errors.append(f"budgets candidatePolicy.{key} violates the release contract")
        clean_keys = {
            "maximumLoadPerLogicalCPU", "maximumSingleForeignProcessCPUPercent",
            "maximumTotalForeignCPUPercentPerLogicalCPU",
        }
        cleanliness = policy.get("hostCleanliness")
        if exact_keys(cleanliness, clean_keys, "budgets hostCleanliness", errors):
            if any(not is_number(cleanliness.get(key)) for key in clean_keys):
                errors.append("host cleanliness thresholds must be finite non-negative numbers")
    metrics = payload.get("metrics")
    if not isinstance(metrics, list) or not metrics:
        errors.append("budgets.metrics must be a non-empty array")
        return {}
    result: dict[str, dict[str, Any]] = {}
    metric_keys = {
        "id", "description", "unit", "condition", "minimumSamples",
        "sourceStageSlugs", "thresholds",
    }
    for index, metric in enumerate(metrics):
        label = f"budgets.metrics[{index}]"
        if not exact_keys(metric, metric_keys, label, errors):
            continue
        metric_id = metric.get("id")
        if not isinstance(metric_id, str) or not metric_id or metric_id in result:
            errors.append(f"{label}.id is empty or duplicate")
            continue
        minimum = metric.get("minimumSamples")
        if type(minimum) is not int or minimum < 2:
            errors.append(f"{label}.minimumSamples must be at least two")
        sources = metric.get("sourceStageSlugs")
        if not isinstance(sources, list) or not sources or len(sources) != len(set(sources)):
            errors.append(f"{label}.sourceStageSlugs must contain unique stages")
        elif any(source not in STAGE_CONTRACTS for source in sources):
            errors.append(f"{label} refers to an unsupported raw capture stage")
        thresholds = metric.get("thresholds")
        threshold_keys = {"maximumMean", "maximumP95", "maximumSample"}
        if exact_keys(thresholds, threshold_keys, f"{label}.thresholds", errors):
            values = [thresholds[key] for key in ("maximumMean", "maximumP95", "maximumSample")]
            if not all(is_number(value) for value in values) or not values[0] <= values[1] <= values[2]:
                errors.append(f"{label}.thresholds are invalid or unordered")
        result[metric_id] = metric
    return result


def validate_index(root: pathlib.Path, manifest: dict[str, Any], errors: list[str]):
    index = safe_path(root, manifest.get("artifactChecksumIndex"), "artifactChecksumIndex", errors)
    if index is None or index.is_symlink() or not index.is_file():
        errors.append("capture checksum index is absent or unsafe")
        return None, {}
    entries: dict[str, str] = {}
    try:
        lines = index.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError) as error:
        errors.append(f"capture checksum index is unreadable: {error}")
        return index, entries
    for number, line in enumerate(lines, 1):
        match = re.fullmatch(r"([0-9a-f]{64})  \./(.+)", line)
        if not match:
            errors.append(f"checksum index line {number} is malformed")
            continue
        expected, relative = match.groups()
        path = safe_path(root, relative, "checksum entry", errors)
        if path is None or path.is_symlink() or not path.is_file():
            errors.append(f"indexed artifact is absent or unsafe: {relative}")
            continue
        if relative in entries:
            errors.append(f"duplicate checksum entry: {relative}")
            continue
        entries[relative] = expected
        if sha256_file(path) != expected:
            errors.append(f"capture checksum mismatch: {relative}")
    actual: set[str] = set()
    for path in root.rglob("*"):
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            errors.append(f"capture contains forbidden symlink: {relative}")
        elif path.is_file() and path != index:
            actual.add(relative)
    for relative in sorted(actual - set(entries)):
        errors.append(f"capture contains unindexed artifact: {relative}")
    for relative in sorted(set(entries) - actual):
        errors.append(f"checksum index contains unexpected artifact: {relative}")
    if "manifest.json" not in entries:
        errors.append("checksum index does not bind manifest.json")
    return index, entries


def validate_ref(root, value, label, entries, errors, *, directory=False):
    if not exact_keys(value, {"path", "sha256"}, label, errors):
        return None
    path = safe_path(root, value.get("path"), label, errors)
    expected = value.get("sha256")
    valid_kind = path is not None and not path.is_symlink()
    valid_kind = valid_kind and (path.is_dir() if directory else path.is_file())
    if not valid_kind:
        errors.append(f"{label} is absent or unsafe")
        return None
    if not isinstance(expected, str) or not HEX_64.fullmatch(expected) or sha256_path(path) != expected:
        errors.append(f"{label} hash mismatch")
    if directory:
        files = {
            child.relative_to(root).as_posix() for child in path.rglob("*")
            if child.is_file() and not child.is_symlink()
        }
        if not files or not files.issubset(entries):
            errors.append(f"{label} tree is empty or not fully checksum-indexed")
    elif value["path"] not in entries:
        errors.append(f"{label} is not checksum-indexed")
    return path


def git_prefreeze(base: str, candidate: str, errors: list[str]) -> None:
    for commit, label in ((base, "budget base"), (candidate, "candidate source")):
        check = subprocess.run(
            ["git", "cat-file", "-e", f"{commit}^{{commit}}"],
            cwd=ROOT,
            capture_output=True,
        )
        if check.returncode:
            errors.append(f"{label} commit is absent from the canonical repository")
            return
    ancestor = subprocess.run(
        ["git", "merge-base", "--is-ancestor", base, candidate], cwd=ROOT
    )
    if ancestor.returncode:
        errors.append("budget pre-freeze commit is not an ancestor of the candidate source commit")


def validate_toolchain(toolchain: Any, budget_path: pathlib.Path, errors: list[str]) -> None:
    keys = {
        "harnessPath", "harnessSha256", "helperPath", "helperSha256",
        "evaluatorPath", "evaluatorSha256", "budgetsPath", "budgetsSha256",
        "xctracePath", "xctraceSha256", "xctraceVersion", "xcodeVersion",
        "developerDirectory", "hostSystem", "hostKernel",
    }
    if not exact_keys(toolchain, keys, "capture.toolchain", errors):
        return
    expected = (
        ("harness", HARNESS),
        ("helper", HELPER),
        ("evaluator", pathlib.Path(__file__).resolve()),
        ("budgets", budget_path.resolve()),
    )
    for prefix, path in expected:
        recorded = pathlib.Path(toolchain.get(prefix + "Path", ""))
        if recorded.resolve() != path or toolchain.get(prefix + "Sha256") != sha256_file(path):
            errors.append(f"capture toolchain {prefix} path/hash does not match current toolchain")
    xctrace = pathlib.Path(toolchain.get("xctracePath", ""))
    if xctrace.is_symlink() or not xctrace.is_file():
        errors.append("capture xctrace tool is absent or unsafe")
    elif toolchain.get("xctraceSha256") != sha256_file(xctrace):
        errors.append("capture xctrace tool hash is invalid")


def validate_host(path: pathlib.Path, policy: dict[str, Any], errors: list[str]) -> None:
    payload = read_json(path, "host sample", errors)
    keys = {
        "schemaVersion", "logicalCPUCount", "load1", "maxForeignCPUPercent",
        "totalForeignCPUPercent", "sampledProcessCount",
    }
    if not exact_keys(payload, keys, "host sample", errors):
        return
    if payload.get("schemaVersion") != 1:
        errors.append("host sample schema is unsupported")
        return
    cpu, count = payload.get("logicalCPUCount"), payload.get("sampledProcessCount")
    if type(cpu) is not int or cpu < 1 or type(count) is not int or count < 1:
        errors.append("host sample counts are invalid")
        return
    for key in ("load1", "maxForeignCPUPercent", "totalForeignCPUPercent"):
        if not is_number(payload.get(key)):
            errors.append(f"host sample {key} is invalid")
            return
    limits = policy["hostCleanliness"]
    if payload["load1"] / cpu > limits["maximumLoadPerLogicalCPU"]:
        errors.append("busy-host load sample is diagnostic only")
    if payload["maxForeignCPUPercent"] > limits["maximumSingleForeignProcessCPUPercent"]:
        errors.append("busy-host single-process CPU sample is diagnostic only")
    if payload["totalForeignCPUPercent"] / cpu > limits["maximumTotalForeignCPUPercentPerLogicalCPU"]:
        errors.append("busy-host aggregate CPU sample is diagnostic only")


def validate_capture(payload, manifest_path, budget_payload, budget_path, errors):
    root = manifest_path.parent
    if not isinstance(payload, dict):
        errors.append("capture manifest must be an object")
        return [], None
    if payload.get("schemaVersion") != 2 or payload.get("kind") != "mobile-performance-evidence-capture":
        errors.append("capture manifest schemaVersion/kind is unsupported")
    if payload.get("mode") != "record" or payload.get("status") != CAPTURE_STATUS or payload.get("exitCode") != 0:
        errors.append("only a completed record-mode raw capture can be evaluated")
    if payload.get("errors") != [] or payload.get("evaluation", {}).get("status") != "NOT_EVALUATED":
        errors.append("capture contains failures or a precomputed evaluation")
    if payload.get("releaseGate", {}).get("acceptedAsFeaturePass") is not False:
        errors.append("raw capture may not claim a feature or performance PASS")
    index, entries = validate_index(root, payload, errors)
    candidate = payload.get("candidate", {})
    policy = budget_payload.get("candidatePolicy", {})
    if candidate.get("bundleId") != policy.get("bundleId"):
        errors.append("candidate bundle ID is wrong")
    source = candidate.get("sourceCommit")
    if not isinstance(source, str) or not SHA_40.fullmatch(source):
        errors.append("candidate sourceCommit is invalid")
    else:
        git_prefreeze(budget_payload.get("frozenAtBaseCommit", ""), source, errors)
    request = payload.get("request", {})
    if request.get("expectedSourceCommit") != source:
        errors.append("capture request and candidate source SHA disagree")
    for key in ("executableSha256", "infoPlistSha256", "appBundleTreeSha256"):
        if not isinstance(candidate.get(key), str) or not HEX_64.fullmatch(candidate[key]):
            errors.append(f"candidate {key} is invalid")
    if candidate.get("buildMode") != policy.get("requiredBuildMode"):
        errors.append("Debug or non-performance build modes are diagnostic only")
    if candidate.get("optimizationLevel") != policy.get("requiredOptimizationLevel"):
        errors.append("non-optimized candidates are diagnostic only")
    if candidate.get("binaryPlatform") != "IOS" or candidate.get("supportedPlatform") != "iPhoneOS":
        errors.append("candidate is not an iPhoneOS device binary")
    signing = candidate.get("signing", {})
    signed = (
        signing.get("verifiedStrictly") is True
        and signing.get("kind") == "certificate"
        and signing.get("teamIdentifier") == policy.get("signingTeamIdentifier")
        and signing.get("boundDeviceKind") == "physical"
    )
    if not signed:
        errors.append("candidate lacks the required strict physical-device Team signature")
    device = payload.get("device", {})
    if device.get("kind") != "physical" or device.get("platform") != "iOS":
        errors.append("simulator or non-iOS captures are diagnostic only")
    if device.get("deviceFamily") not in {"iPhone", "iPad"}:
        errors.append("capture device is not a physical iPhone or iPad")
    udid = device.get("udid")
    if not isinstance(udid, str) or not 8 <= len(udid) <= 128:
        errors.append("capture device UDID is invalid")
    validate_toolchain(payload.get("toolchain"), budget_path, errors)
    captures = payload.get("captures")
    if not isinstance(captures, list) or not captures:
        errors.append("capture manifest contains no raw capture runs")
        return [], index
    expected_stages = {
        slug for metric in budget_payload.get("metrics", [])
        for slug in metric.get("sourceStageSlugs", [])
    }
    seen: set[tuple[str, int]] = set()
    nonces: set[str] = set()
    validated = []
    capture_keys = {
        "stageSlug", "runIndex", "scenarioNonce", "template", "status", "trace",
        "tocExport", "dataExport", "marker", "hostSample", "commandLabel",
    }
    for number, capture in enumerate(captures):
        label = f"capture.captures[{number}]"
        if not exact_keys(capture, capture_keys, label, errors):
            continue
        slug, run_index = capture.get("stageSlug"), capture.get("runIndex")
        if slug not in expected_stages or slug not in STAGE_CONTRACTS:
            errors.append(f"{label} has unbudgeted stage {slug}")
            continue
        if type(run_index) is not int or run_index < 1 or (slug, run_index) in seen:
            errors.append(f"{label} has invalid or duplicate runIndex")
            continue
        seen.add((slug, run_index))
        nonce = capture.get("scenarioNonce")
        if (
            not isinstance(nonce, str) or not nonce.startswith(f"{source}-")
            or nonce in nonces
        ):
            errors.append(f"{label} has an invalid, unbound, or duplicate scenario nonce")
        else:
            nonces.add(nonce)
        template, data_kind, _, _ = STAGE_CONTRACTS[slug]
        if capture.get("template") != template or capture.get("status") != STAGE_STATUS:
            errors.append(f"{label} template/status violates the stage contract")
        data = capture.get("dataExport")
        data_keys = {"kind", "path", "sha256", "query"}
        if not isinstance(data, dict) or set(data) != data_keys or data.get("kind") != data_kind:
            errors.append(f"{label}.dataExport violates the raw export contract")
            continue
        if data.get("query") != DATA_QUERIES[data_kind]:
            errors.append(f"{label}.dataExport query violates the raw export contract")
        trace = validate_ref(
            root, capture.get("trace"), f"{label}.trace", entries, errors, directory=True
        )
        toc = validate_ref(
            root, capture.get("tocExport"), f"{label}.tocExport", entries, errors
        )
        export = validate_ref(
            root,
            {"path": data.get("path"), "sha256": data.get("sha256")},
            f"{label}.dataExport",
            entries,
            errors,
        )
        marker = validate_ref(
            root, capture.get("marker"), f"{label}.marker", entries, errors
        )
        host = validate_ref(
            root, capture.get("hostSample"), f"{label}.hostSample", entries, errors
        )
        if host:
            validate_host(host, policy, errors)
        if all((trace, toc, export, marker, host)):
            validated.append(
                {
                    **capture,
                    "tracePath": trace,
                    "tocPath": toc,
                    "exportPath": export,
                    "markerPath": marker,
                }
            )
    present = {item["stageSlug"] for item in validated}
    for stage in sorted(expected_stages - present):
        errors.append(f"required raw capture stage is absent: {stage}")
    commands = payload.get("commands")
    if not isinstance(commands, list) or not commands:
        errors.append("capture command receipts are absent")
    else:
        command_keys = {
            "label", "command", "stdout", "stderr", "exitCode", "exitCodeReceipt",
        }
        labels: set[str] = set()
        for number, command in enumerate(commands):
            label = f"capture.commands[{number}]"
            if not exact_keys(command, command_keys, label, errors):
                continue
            command_label = command.get("label")
            if (
                not isinstance(command_label, str) or not command_label
                or command_label in labels
            ):
                errors.append(f"{label} has an invalid or duplicate label")
            else:
                labels.add(command_label)
            if type(command.get("exitCode")) is not int or command["exitCode"] != 0:
                errors.append(f"{label} contains a failed or invalid exit code")
            receipt_paths = {}
            for field in ("command", "stdout", "stderr", "exitCodeReceipt"):
                receipt_paths[field] = validate_ref(
                    root, command.get(field), f"{label}.{field}", entries, errors
                )
            exit_receipt = receipt_paths.get("exitCodeReceipt")
            if exit_receipt:
                try:
                    recorded_exit = int(exit_receipt.read_text(encoding="utf-8").strip())
                except (OSError, UnicodeDecodeError, ValueError):
                    errors.append(f"{label}.exitCodeReceipt is invalid")
                else:
                    if recorded_exit != command.get("exitCode"):
                        errors.append(f"{label}.exitCodeReceipt disagrees with exitCode")
        for number, capture in enumerate(captures):
            if capture.get("commandLabel") not in labels:
                errors.append(
                    f"capture.captures[{number}].commandLabel has no command receipt"
                )
    return validated, index



def nearest_rank_p95(values: list[float]) -> float:
    ordered = sorted(values)
    return ordered[math.ceil(0.95 * len(ordered)) - 1]


def metric_results(budgets, samples, errors):
    results, insufficient, exceeded = [], False, False
    unknown = sorted(set(samples) - set(budgets))
    if unknown:
        errors.append(f"derived samples contain unbudgeted metrics: {', '.join(unknown)}")
    for metric_id, budget in budgets.items():
        records = samples.get(metric_id, [])
        result = {
            "id": metric_id,
            "unit": budget["unit"],
            "condition": budget["condition"],
            "minimumSamples": budget["minimumSamples"],
            "thresholds": budget["thresholds"],
            "samples": records,
            "sampleCount": len(records),
        }
        wrong = [
            record for record in records
            if record["provenance"]["stageSlug"] not in budget["sourceStageSlugs"]
        ]
        if wrong:
            result["status"] = "INVALID_PROVENANCE"
            errors.append(f"{metric_id} has samples from an unauthorized capture stage")
            insufficient = True
        elif len(records) < budget["minimumSamples"]:
            result["status"] = "INSUFFICIENT_EVIDENCE"
            insufficient = True
        else:
            values = [record["value"] for record in records]
            computed = {
                "mean": math.fsum(values) / len(values),
                "p95": nearest_rank_p95(values),
                "maximumSample": max(values),
            }
            result["computed"] = computed
            pairs = (
                ("mean", "maximumMean"),
                ("p95", "maximumP95"),
                ("maximumSample", "maximumSample"),
            )
            violations = [
                name for name, threshold in pairs
                if computed[name] > budget["thresholds"][threshold]
            ]
            if violations:
                result["status"] = "BUDGET_EXCEEDED"
                result["violations"] = violations
                exceeded = True
            else:
                result["status"] = "WITHIN_BUDGET"
        results.append(result)
    return results, insufficient, exceeded


def evaluate(budget_path: pathlib.Path, manifest_path: pathlib.Path) -> dict[str, Any]:
    errors: list[str] = []
    budget_payload = read_json(budget_path, "budgets", errors)
    capture = read_json(manifest_path, "capture manifest", errors)
    budgets = validate_budgets(budget_payload, errors) if budget_payload is not None else {}
    if capture is not None and budget_payload is not None:
        captures, index = validate_capture(
            capture, manifest_path, budget_payload, budget_path, errors
        )
    else:
        captures, index = [], None
    samples = RAW_HELPER.derive_samples(captures, errors) if captures else {}
    results, insufficient, exceeded = metric_results(budgets, samples, errors)
    if errors:
        status = "RED_INVALID_EVIDENCE"
    elif insufficient:
        status = "RED_INSUFFICIENT_EVIDENCE"
    elif exceeded:
        status = "RED_BUDGET_EXCEEDED"
    else:
        status = "PASS"
    evaluator = pathlib.Path(__file__).resolve()
    return {
        "schemaVersion": 2,
        "kind": "mobile-performance-budget-evaluation",
        "evaluator": {
            "version": VERSION,
            "path": str(evaluator),
            "sha256": sha256_file(evaluator),
        },
        "status": status,
        "inputs": {
            "budgets": {
                "path": str(budget_path.resolve()),
                "sha256": sha256_file(budget_path) if budget_path.is_file() else None,
            },
            "captureManifest": {
                "path": str(manifest_path.resolve()),
                "sha256": sha256_file(manifest_path) if manifest_path.is_file() else None,
            },
            "artifactChecksumIndex": {
                "path": str(index.resolve()) if index else None,
                "sha256": sha256_file(index) if index and index.is_file() else None,
            },
        },
        "normalization": {
            "mode": "direct-raw-artifact-derivation",
            "callerSuppliedMeasurementsAccepted": False,
        },
        "metricResults": results,
        "errors": errors,
        "releaseGate": {
            "acceptedAsPerformancePass": status == "PASS",
            "blocking": status != "PASS",
            "visibleE2EStillRequired": True,
        },
    }


def write_exclusive(path: pathlib.Path, payload: dict[str, Any]) -> None:
    if not below_root(path):
        raise ValueError("output must resolve below the canonical repository")
    data = canonical_bytes(payload)
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o400)
    with os.fdopen(descriptor, "wb") as stream:
        stream.write(data)
    receipt = path.with_name(path.name + ".sha256")
    receipt_data = f"{hashlib.sha256(data).hexdigest()}  {path.name}\n".encode()
    descriptor = os.open(receipt, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o400)
    with os.fdopen(descriptor, "wb") as stream:
        stream.write(receipt_data)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Evaluate only checksum-indexed raw Mobile performance artifacts; "
            "caller-supplied measurements can never pass."
        )
    )
    parser.add_argument("--capture-manifest", required=True, type=pathlib.Path)
    parser.add_argument("--budgets", default=DEFAULT_BUDGETS, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    capture_root = arguments.capture_manifest.resolve().parent
    try:
        arguments.output.resolve().relative_to(capture_root)
        print("output must be outside the immutable capture root", file=sys.stderr)
        return 2
    except ValueError:
        pass
    receipt = arguments.output.with_name(arguments.output.name + ".sha256")
    if arguments.output.exists() or receipt.exists():
        print("evaluation output uses exclusive-create and already exists", file=sys.stderr)
        return 2
    payload = evaluate(arguments.budgets, arguments.capture_manifest)
    try:
        write_exclusive(arguments.output, payload)
    except (OSError, ValueError) as error:
        print(f"could not write exclusive evaluation: {error}", file=sys.stderr)
        return 2
    return 0 if payload["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
