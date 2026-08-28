#!/usr/bin/env python3
"""Build a deterministic, fail-closed audit of every registered requirement."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import tempfile
from collections import Counter, defaultdict
from typing import Any, Iterable, Optional

import evidence


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_REGISTRY = ROOT / "config/test-registry.json"
DEFAULT_EXTERNAL_GATES = ROOT / "config/external-gates.json"
PRODUCT_VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
DEFAULT_EVIDENCE_ROOT = ROOT / "artifacts/e2e" / PRODUCT_VERSION

PASS, NOT_RUN = "PASS", "NOT_RUN"
VISIBLE_CLASSES = {"CU_E2E", "ASSISTED_E2E"}
STATUS_ORDER = (
    "PASS", "FAIL", "BLOCKED_USER_ASSISTANCE", "BLOCKED_CREDENTIAL", "BLOCKED_ENTITLEMENT",
    "BLOCKED_EXTERNAL_SERVICE", "NOT_RUN",
)
KNOWN_STATUSES = set(STATUS_ORDER)

# Journey prerequisites, not substitutes for result.json evidence.
ASSISTED_EXTERNAL_GATES = {
    "AUTH-24": ("password-manager-test-access",),
    "DEV-23": ("password-manager-test-access",),
    "DRM-01": ("widevine-mla",),
    "DRM-02": ("widevine-mla",),
    "EXT-06": ("onepassword-additional-browser", "password-manager-test-access"),
    "EXT-07": ("onepassword-additional-browser", "password-manager-test-access"),
    "MEDIA-07": ("physical-input-and-tcc",),
    "NAV-09": ("physical-input-and-tcc",),
    "PASS-03": ("password-manager-test-access",),
    "PERM-01": ("physical-input-and-tcc",),
    "PERM-02": ("physical-input-and-tcc",),
    "PERM-03": ("physical-input-and-tcc",),
    "SPLIT-26": ("physical-input-and-tcc",),
    "WS-03": ("physical-input-and-tcc",),
}


class AuditError(ValueError):
    """Raised when an audit input is internally inconsistent."""


def load_json(path: pathlib.Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise AuditError(f"cannot read JSON {path}: {error}") from error


def _require_nonempty_string(value: object, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise AuditError(f"{label} must be a non-empty string")
    return value


def load_registry(path: pathlib.Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    payload = load_json(path)
    if not isinstance(payload, dict) or payload.get("schemaVersion") != 1:
        raise AuditError("test registry schemaVersion must be 1")
    tests = payload.get("tests")
    if not isinstance(tests, list):
        raise AuditError("test registry tests must be an array")
    validated: list[dict[str, Any]] = []
    seen: set[str] = set()
    for index, entry in enumerate(tests):
        if not isinstance(entry, dict):
            raise AuditError(f"test registry entry {index} must be an object")
        test_id = _require_nonempty_string(entry.get("id"), f"tests[{index}].id")
        if test_id in seen:
            raise AuditError(f"duplicate registry test ID: {test_id}")
        seen.add(test_id)
        suite = _require_nonempty_string(entry.get("suite"), f"tests[{index}].suite")
        if test_id.rsplit("-", 1)[0] != suite:
            raise AuditError(f"registry suite does not match test ID: {test_id}")
        _require_nonempty_string(entry.get("description"), f"tests[{index}].description")
        _require_nonempty_string(entry.get("primaryClass"), f"tests[{index}].primaryClass")
        required_classes = entry.get("requiredEvidenceClasses")
        if (not isinstance(required_classes, list) or not required_classes or
                any(not isinstance(item, str) or not item for item in required_classes)):
            raise AuditError(f"tests[{index}].requiredEvidenceClasses must contain strings")
        if not isinstance(entry.get("releaseCritical"), bool):
            raise AuditError(f"tests[{index}].releaseCritical must be a boolean")
        validated.append(entry)
    return payload, sorted(validated, key=lambda item: item["id"])


def load_external_gates(path: pathlib.Path) -> tuple[
    dict[str, Any], list[dict[str, str]], dict[str, dict[str, str]]
]:
    payload = load_json(path)
    if not isinstance(payload, dict) or payload.get("schemaVersion") != 1:
        raise AuditError("external-gates schemaVersion must be 1")
    gates = payload.get("gates")
    if not isinstance(gates, list):
        raise AuditError("external-gates gates must be an array")
    normalized: list[dict[str, str]] = []
    by_id: dict[str, dict[str, str]] = {}
    for index, gate in enumerate(gates):
        if not isinstance(gate, dict):
            raise AuditError(f"external gate {index} must be an object")
        normalized_gate = {
            key: _require_nonempty_string(gate.get(key), f"gates[{index}].{key}")
            for key in ("id", "state", "owner", "condition")
        }
        gate_id = normalized_gate["id"]
        if gate_id in by_id:
            raise AuditError(f"duplicate external gate ID: {gate_id}")
        by_id[gate_id] = normalized_gate
        normalized.append(normalized_gate)
    normalized.sort(key=lambda item: item["id"])
    return payload, normalized, by_id


def _display_path(path: pathlib.Path, evidence_root: pathlib.Path) -> str:
    try:
        return path.resolve().relative_to(evidence_root.resolve()).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def _portable_output(value: Any, evidence_root: pathlib.Path,
                     release_manifest: Optional[pathlib.Path],
                     release_public_key: Optional[pathlib.Path]) -> Any:
    """Remove checkout- and host-specific absolute paths from audit output."""
    path_labels = ((ROOT, "."), (evidence_root, "<evidence-root>"),
                   (release_manifest, "<release-manifest>"),
                   (release_public_key, "<release-public-key>"))
    aliases: dict[str, str] = {}
    for path, external_label in path_labels:
        if path is None:
            continue
        resolved = path.resolve()
        try:
            alias = resolved.relative_to(ROOT.resolve()).as_posix() or "."
        except ValueError:
            alias = external_label
        for absolute in {path.absolute().as_posix(), resolved.as_posix()}:
            aliases[absolute] = alias
    replacements = sorted(aliases.items(), key=lambda item: len(item[0]), reverse=True)
    if isinstance(value, str):
        for absolute, portable in replacements:
            prefix = "" if portable == "." else portable + "/"
            value = value.replace(absolute + "/", prefix).replace(absolute, portable)
        return value
    recurse = lambda item: _portable_output(  # noqa: E731
        item, evidence_root, release_manifest, release_public_key
    )
    if isinstance(value, list):
        return [recurse(item) for item in value]
    if isinstance(value, dict):
        return {key: recurse(item) for key, item in value.items()}
    return value


def discover_evidence(
    evidence_root: pathlib.Path,
    registry_ids: set[str],
) -> tuple[dict[str, list[pathlib.Path]], list[dict[str, str]]]:
    """Map result files to requirements without trusting their claimed status."""
    candidates: dict[str, list[pathlib.Path]] = defaultdict(list)
    unmapped: list[dict[str, str]] = []
    if not evidence_root.is_dir():
        return {}, []
    for path in sorted(evidence_root.rglob("result.json")):
        directory_id = path.parent.name if path.parent.name in registry_ids else None
        payload_id: Optional[str] = None
        parse_error: Optional[str] = None
        try:
            payload = json.loads(path.read_text(encoding="utf-8"))
            if isinstance(payload, dict) and isinstance(payload.get("testId"), str):
                payload_id = payload["testId"]
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
            parse_error = str(error)
        mapped_id = directory_id
        if mapped_id is None and payload_id in registry_ids:
            mapped_id = payload_id
        if mapped_id is not None:
            candidates[mapped_id].append(path)
            continue
        reason = "result.json cannot be mapped to a registered test ID"
        if parse_error:
            reason += f": {parse_error}"
        elif payload_id:
            reason += f": claimed unknown testId {payload_id}"
        unmapped.append(
            {
                "path": _display_path(path, evidence_root),
                "reason": reason,
            }
        )
    return {test_id: sorted(paths)
            for test_id, paths in sorted(candidates.items())}, unmapped


def _read_declared_evidence(path: pathlib.Path) -> tuple[Optional[dict[str, Any]], str]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        return None, f"cannot read evidence JSON: {error}"
    if not isinstance(payload, dict):
        return None, "evidence JSON root must be an object"
    return payload, ""


def _attempted(payload: Optional[dict[str, Any]]) -> bool:
    if payload is None:
        return False
    if payload.get("status") not in (None, NOT_RUN):
        return True
    return bool(payload.get("steps") or payload.get("assertions"))


def _gate_ids_for(entry: dict[str, Any], *, release_chain_ready: bool,
                  include_assistance: bool) -> list[str]:
    gate_ids: list[str] = []
    test_id = entry["id"]
    # UBO-11 is an installed-app negative test against local fixture input;
    # it must retain CU_E2E evidence without depending on production signing.
    if (
        entry["primaryClass"] in VISIBLE_CLASSES
        and not release_chain_ready
        and test_id != "UBO-11"
    ):
        gate_ids.append("signed-release-provenance")
    if test_id.startswith(("SYNC-", "IOS-")):
        gate_ids.extend(("cloudkit-identifiers", "cloudkit-device-validation"))
    if test_id.startswith("PERF-"):
        gate_ids.append("disk-headroom")
    if test_id.startswith("UPDATE-"):
        gate_ids.extend(("release-manifest-and-update-keys",
                         "signed-release-provenance", "sparkle-feed-hosting"))
    ubo_production_gate_ids = {
        f"UBO-{number:02d}" for number in range(1, 11)
    } | {"UBO-12"}
    if test_id in ubo_production_gate_ids:
        gate_ids.extend(("ubo-catalog-hosting-and-signing",
                         "ubo-fixed-id-crx-publisher-provenance",
                         "ubo-redistribution"))
    if test_id == "UBO-12":
        gate_ids.append("chrome-web-store")
    if test_id.startswith("DRM-"):
        gate_ids.extend(("proprietary-codecs", "widevine-mla"))
    if test_id in {"EXT-01", "EXT-02", "EXT-03", "EXT-09", "EXT-10"}:
        gate_ids.append("chrome-web-store")
    specific_gate = {"PRIV-14": "safe-browsing-service",
                     "PRIV-18": "google-api-services",
                     "SEC-01": "signed-release-provenance"}.get(test_id)
    if specific_gate:
        gate_ids.append(specific_gate)
    if include_assistance and entry["primaryClass"] == "ASSISTED_E2E":
        gate_ids.extend(ASSISTED_EXTERNAL_GATES.get(test_id, ()))
    return sorted(set(gate_ids))


def _gate_condition(gate_ids: Iterable[str], gates: dict[str, dict[str, str]]) -> str:
    return " ".join(gates[gate_id]["condition"] for gate_id in gate_ids)


def _owner_for_gates(gate_ids: list[str], gates: dict[str, dict[str, str]],
                     fallback: str) -> str:
    return gates[gate_ids[0]]["owner"] if gate_ids else fallback


def _validate_gate_references(gate_ids: Iterable[str],
                              gates: dict[str, dict[str, str]]) -> None:
    unknown = sorted(set(gate_ids) - gates.keys())
    if unknown:
        raise AuditError(f"audit references unknown external gates: {', '.join(unknown)}")


def _evidence_payload(state: str, evidence_root: pathlib.Path,
                      paths: list[pathlib.Path], declared_status: Optional[str],
                      validation_errors: Iterable[str]) -> dict[str, Any]:
    return {
        "state": state,
        "path": _display_path(paths[0], evidence_root) if len(paths) == 1 else None,
        "candidatePaths": [_display_path(path, evidence_root) for path in paths],
        "declaredStatus": declared_status,
        "validationErrors": sorted(set(validation_errors)),
    }


def _base_record(entry: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": entry["id"],
        "suite": entry["suite"],
        "description": entry["description"],
        "primaryClass": entry["primaryClass"],
        "requiredEvidenceClasses": list(entry["requiredEvidenceClasses"]),
        "releaseCritical": entry["releaseCritical"],
    }


def _missing_record(entry: dict[str, Any], evidence_root: pathlib.Path,
                    release_chain_ready: bool,
                    gates: dict[str, dict[str, str]]) -> dict[str, Any]:
    record = _base_record(entry)
    gate_ids = _gate_ids_for(entry, release_chain_ready=release_chain_ready,
                             include_assistance=True)
    _validate_gate_references(gate_ids, gates)
    condition = "No result.json evidence was found for this registered requirement."
    if gate_ids:
        condition += " External prerequisites remain unresolved: " + _gate_condition(
            gate_ids, gates
        )
    next_action = "Execute the registered journey and record validated result.json evidence."
    if gate_ids:
        next_action = (
            f"Resolve external gates {', '.join(gate_ids)}, then execute the "
            "registered journey and record validated result.json evidence."
        )
    record.update(
        {
            "status": NOT_RUN,
            "condition": condition,
            "owner": _owner_for_gates(gate_ids, gates, "engineering"),
            "attempted": False,
            "locallyControllable": not gate_ids,
            "nextAction": next_action,
            "externalGateIds": gate_ids,
            "evidence": _evidence_payload("MISSING", evidence_root, [], None, []),
        }
    )
    return record


def _ambiguous_record(entry: dict[str, Any], evidence_root: pathlib.Path,
                      paths: list[pathlib.Path], release_chain_ready: bool,
                      gates: dict[str, dict[str, str]]) -> dict[str, Any]:
    record = _base_record(entry)
    gate_ids = _gate_ids_for(entry, release_chain_ready=release_chain_ready,
                             include_assistance=True)
    _validate_gate_references(gate_ids, gates)
    attempted = any(_attempted(_read_declared_evidence(path)[0]) for path in paths)
    condition = (
        "Multiple result.json candidates exist; no candidate was selected or "
        "treated as proof."
    )
    if gate_ids:
        condition += " External prerequisites remain unresolved: " + _gate_condition(
            gate_ids, gates
        )
    next_action = (
        "Keep one canonical result.json for the current product version, archive "
        "superseded evidence, and regenerate the audit."
    )
    if gate_ids:
        next_action = (
            f"Resolve external gates {', '.join(gate_ids)}, keep one canonical "
            "result.json, and regenerate the audit."
        )
    record.update(
        {
            "status": NOT_RUN,
            "condition": condition,
            "owner": _owner_for_gates(gate_ids, gates, "engineering"),
            "attempted": attempted,
            "locallyControllable": not gate_ids,
            "nextAction": next_action,
            "externalGateIds": gate_ids,
            "evidence": _evidence_payload(
                "AMBIGUOUS",
                evidence_root,
                paths,
                None,
                ["multiple result.json candidates for one requirement"],
            ),
        }
    )
    return record


def _audited_candidate_record(
    entry: dict[str, Any],
    evidence_root: pathlib.Path,
    path: pathlib.Path,
    *,
    release_manifest: Optional[pathlib.Path],
    release_public_key: Optional[pathlib.Path],
    release_chain_ready: bool,
    gates: dict[str, dict[str, str]],
) -> dict[str, Any]:
    record = _base_record(entry)
    payload, read_error = _read_declared_evidence(path)
    declared_status = payload.get("status") if payload is not None else None
    validation_errors: list[str] = []
    try:
        validation_errors.extend(
            evidence.validate_result(
                path,
                release_manifest=release_manifest,
                release_public_key=release_public_key,
            )
        )
    except Exception as error:  # The audit must fail closed on validator failure.
        validation_errors.append(
            f"evidence validator raised {type(error).__name__}: {error}"
        )
    if read_error and read_error not in validation_errors:
        validation_errors.append(read_error)
    ubo_11_local_fail_closed = evidence.is_ubo_11_local_fail_closed_pass(
        path,
        payload,
    )
    if declared_status == PASS and entry["primaryClass"] == "INTEGRATION":
        evidence_data = payload.get("evidence") if payload else None
        reports = evidence_data.get("testReports") if isinstance(evidence_data, dict) else None
        if not isinstance(reports, list) or not reports:
            validation_errors.append(
                "INTEGRATION PASS requires a concrete validated test report"
            )
        elif any(not isinstance(report, str) for report in reports):
            validation_errors.append("INTEGRATION PASS report paths must be strings")
        else:
            hashes = evidence_data.get("fileHashes")
            if not isinstance(hashes, dict):
                validation_errors.append("INTEGRATION PASS report hashes are missing")
            else:
                for report in reports:
                    try:
                        validation_errors.extend(
                            evidence.validate_artifact_reference(path, report, hashes)
                        )
                    except Exception as error:
                        validation_errors.append(f"cannot validate report {report}: {error}")
    if (
        declared_status == PASS
        and entry["primaryClass"] in VISIBLE_CLASSES
        and not release_chain_ready
        and not ubo_11_local_fail_closed
    ):
        validation_errors.append(
            "CU_E2E and ASSISTED_E2E PASS require a validated release chain"
        )
    accepted = not validation_errors and isinstance(declared_status, str)
    accepted = accepted and declared_status in KNOWN_STATUSES
    if accepted and declared_status == PASS:
        executed_by = payload.get("executedBy") if payload else None
        record.update(
            {
                "status": PASS,
                "condition": "Validated result.json evidence establishes PASS.",
                "owner": executed_by if isinstance(executed_by, str) else "engineering",
                "attempted": True,
                "locallyControllable": True,
                "nextAction": "No further action is required for this audited revision.",
                "externalGateIds": [],
                "evidence": _evidence_payload(
                    "VALID", evidence_root, [path], declared_status, []
                ),
            }
        )
        return record
    gate_ids = _gate_ids_for(
        entry,
        release_chain_ready=release_chain_ready,
        include_assistance=True,
    )
    _validate_gate_references(gate_ids, gates)
    if accepted:
        status = declared_status
        assert isinstance(status, str)
        # A FAIL from an unqualified visible build is useful diagnostic evidence,
        # but it is not a formal release-candidate disposition.
        if status == "FAIL" and entry["primaryClass"] in VISIBLE_CLASSES and gate_ids:
            status = NOT_RUN
        blocker = payload.get("blocker") if payload else None
        actual_result = payload.get("actualResult") if payload else None
        condition = (
            blocker.get("condition")
            if isinstance(blocker, dict) and isinstance(blocker.get("condition"), str)
            else (
                actual_result
                if isinstance(actual_result, str) and actual_result
                else f"Validated evidence records {declared_status}."
            )
        )
        owner = (
            blocker.get("owner")
            if isinstance(blocker, dict) and isinstance(blocker.get("owner"), str)
            else "engineering"
        )
        next_action = (
            blocker.get("nextAction")
            if isinstance(blocker, dict)
            and isinstance(blocker.get("nextAction"), str)
            else "Resolve the recorded condition and rerun the requirement."
        )
        if gate_ids:
            condition += " External prerequisites remain unresolved: " + _gate_condition(
                gate_ids, gates
            )
            owner = _owner_for_gates(gate_ids, gates, owner)
            next_action = (
                f"Resolve external gates {', '.join(gate_ids)}, then rerun the "
                "requirement and replace its evidence."
            )
        evidence_state = "VALID"
    else:
        status = NOT_RUN
        condition = (
            "Existing result.json evidence was rejected; its declared status was not "
            "accepted."
        )
        if gate_ids:
            condition += " External prerequisites remain unresolved: " + _gate_condition(
                gate_ids, gates
            )
        owner = _owner_for_gates(gate_ids, gates, "engineering")
        next_action = (
            "Repair or replace the evidence, rerun the requirement, and validate it "
            "with tools/evidence.py."
        )
        if gate_ids:
            next_action = (
                f"Resolve external gates {', '.join(gate_ids)}, repair or replace "
                "the evidence, and rerun the requirement."
            )
        evidence_state = "INVALID"
    record.update(
        {
            "status": status,
            "condition": condition,
            "owner": owner,
            "attempted": _attempted(payload),
            "locallyControllable": not gate_ids,
            "nextAction": next_action,
            "externalGateIds": gate_ids,
            "evidence": _evidence_payload(
                evidence_state,
                evidence_root,
                [path],
                declared_status if isinstance(declared_status, str) else None,
                validation_errors,
            ),
        }
    )
    return record


def _group_summary(requirements: list[dict[str, Any]], key: str) -> dict[str, dict[str, Any]]:
    groups: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for requirement in requirements:
        groups[requirement[key]].append(requirement)
    summary: dict[str, dict[str, Any]] = {}
    for group_name in sorted(groups):
        group = sorted(groups[group_name], key=lambda item: item["id"])
        statuses = Counter(item["status"] for item in group)
        summary[group_name] = {
            "count": len(group),
            "ids": [item["id"] for item in group],
            "byStatus": {
                status: statuses[status]
                for status in STATUS_ORDER
                if statuses[status]
            },
        }
    return summary


def _status_summary(requirements: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    return {
        status: {"count": len(ids), "ids": ids}
        for status in STATUS_ORDER
        if (ids := sorted(item["id"] for item in requirements
                          if item["status"] == status))
    }


def build_audit(
    *,
    registry_path: pathlib.Path = DEFAULT_REGISTRY,
    external_gates_path: pathlib.Path = DEFAULT_EXTERNAL_GATES,
    evidence_root: pathlib.Path = DEFAULT_EVIDENCE_ROOT,
    release_manifest: Optional[pathlib.Path] = None,
    release_public_key: Optional[pathlib.Path] = None,
) -> dict[str, Any]:
    registry_payload, entries = load_registry(registry_path)
    gates_payload, gates, gates_by_id = load_external_gates(external_gates_path)
    candidates, unmapped = discover_evidence(evidence_root,
                                             {entry["id"] for entry in entries})
    try:
        chain_errors = evidence.validate_release_evidence_chain(
            release_manifest, release_public_key
        )
    except Exception as error:  # The audit must fail closed on validator failure.
        chain_errors = [
            f"release-chain validator raised {type(error).__name__}: {error}"
        ]
    chain_errors = sorted(set(chain_errors))
    release_chain_ready = not chain_errors
    requirements: list[dict[str, Any]] = []
    for entry in entries:
        paths = candidates.get(entry["id"], [])
        if not paths:
            record = _missing_record(
                entry, evidence_root, release_chain_ready, gates_by_id
            )
        elif len(paths) > 1:
            record = _ambiguous_record(
                entry,
                evidence_root,
                paths,
                release_chain_ready,
                gates_by_id,
            )
        else:
            record = _audited_candidate_record(
                entry,
                evidence_root,
                paths[0],
                release_manifest=release_manifest,
                release_public_key=release_public_key,
                release_chain_ready=release_chain_ready,
                gates=gates_by_id,
            )
        requirements.append(record)
    ids = [item["id"] for item in requirements]
    if len(ids) != len(set(ids)) or set(ids) != {entry["id"] for entry in entries}:
        raise AuditError("audit did not cover every registry entry exactly once")
    audit = {
        "schemaVersion": 1,
        "registry": {
            "schemaVersion": registry_payload["schemaVersion"],
            "source": registry_payload.get("source"),
            "generatedAt": registry_payload.get("generatedAt"),
            "testCount": len(entries),
        },
        "evidenceRoot": evidence_root.as_posix(),
        "releaseChain": {
            "ready": release_chain_ready,
            "validationErrors": chain_errors,
        },
        "externalGates": {
            "schemaVersion": gates_payload["schemaVersion"],
            "updatedAt": gates_payload.get("updatedAt"),
            "items": gates,
        },
        "summary": {
            "total": len(requirements),
            "byPrimaryClass": _group_summary(requirements, "primaryClass"),
            "bySuite": _group_summary(requirements, "suite"),
            "byStatus": _status_summary(requirements),
        },
        "requirements": requirements,
        "unmappedEvidence": unmapped,
    }
    return _portable_output(audit, evidence_root, release_manifest, release_public_key)


def serialize_json(audit: dict[str, Any]) -> str:
    return json.dumps(audit, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def _markdown_cell(value: object) -> str:
    return str(value).replace("\n", " ").replace("|", "\\|")


def _summary_table(title: str, groups: dict[str, dict[str, Any]], label: str) -> list[str]:
    lines = [f"## {title}", "", f"| {label} | Count | Statuses | IDs |", "|---|---:|---|---|"]
    for name, item in groups.items():
        statuses = ", ".join(
            f"{status}={count}" for status, count in item["byStatus"].items()
        )
        ids = ", ".join(f"`{test_id}`" for test_id in item["ids"])
        lines.append(
            f"| {_markdown_cell(name)} | {item['count']} | "
            f"{_markdown_cell(statuses)} | {ids} |"
        )
    lines.append("")
    return lines


def render_markdown(audit: dict[str, Any]) -> str:
    summary = audit["summary"]
    release_chain = audit["releaseChain"]
    lines = [
        "# AhoiBrowser Requirement Audit",
        "",
        f"Registered requirements: **{summary['total']}**  ",
        f"Release chain ready: **{'yes' if release_chain['ready'] else 'no'}**  ",
        f"Evidence root: `{_markdown_cell(audit['evidenceRoot'])}`",
        "",
    ]
    if release_chain["validationErrors"]:
        lines.extend(["Release-chain validation errors:", ""])
        lines.extend(
            f"- {_markdown_cell(error)}"
            for error in release_chain["validationErrors"]
        )
        lines.append("")
    lines.extend(
        _summary_table(
            "Summary by primary class",
            summary["byPrimaryClass"],
            "Primary class",
        )
    )
    lines.extend(_summary_table("Summary by suite", summary["bySuite"], "Suite"))
    lines.extend(["## Summary by status", ""])
    for status, item in summary["byStatus"].items():
        ids = ", ".join(f"`{test_id}`" for test_id in item["ids"])
        lines.append(f"- {status} ({item['count']}): {ids}")
    lines.extend(
        [
            "",
            "## Requirement dispositions",
            "",
            "| ID | Suite | Class | Status | Attempted | Locally controllable | Evidence | External gates | Owner | Condition | Next action |",
            "|---|---|---|---|---|---|---|---|---|---|---|",
        ]
    )
    for item in audit["requirements"]:
        gates = ", ".join(item["externalGateIds"]) or "—"
        evidence_state = item["evidence"]["state"]
        evidence_path = item["evidence"]["path"]
        if evidence_path:
            evidence_state += f" ({evidence_path})"
        lines.append(
            "| "
            + " | ".join(
                _markdown_cell(value)
                for value in (
                    item["id"],
                    item["suite"],
                    item["primaryClass"],
                    item["status"],
                    "yes" if item["attempted"] else "no",
                    "yes" if item["locallyControllable"] else "no",
                    evidence_state,
                    gates,
                    item["owner"],
                    item["condition"],
                    item["nextAction"],
                )
            )
            + " |"
        )
    lines.append("")
    if audit["unmappedEvidence"]:
        lines.extend(["## Unmapped evidence", ""])
        for item in audit["unmappedEvidence"]:
            lines.append(
                f"- `{_markdown_cell(item['path'])}`: "
                f"{_markdown_cell(item['reason'])}"
            )
        lines.append("")
    return "\n".join(lines)


def _atomic_write(path: pathlib.Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary_path = pathlib.Path(temporary_name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(content)
        os.replace(temporary_path, path)
    finally:
        if temporary_path.exists():
            temporary_path.unlink()


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Generate a deterministic, fail-closed requirement audit.")
    parser.add_argument("--registry", type=pathlib.Path, default=DEFAULT_REGISTRY)
    parser.add_argument(
        "--external-gates", type=pathlib.Path, default=DEFAULT_EXTERNAL_GATES
    )
    parser.add_argument(
        "--evidence-root", type=pathlib.Path, default=DEFAULT_EVIDENCE_ROOT
    )
    parser.add_argument("--release-manifest", type=pathlib.Path)
    parser.add_argument("--release-public-key", type=pathlib.Path)
    parser.add_argument("--json-output", type=pathlib.Path, required=True)
    parser.add_argument("--markdown-output", type=pathlib.Path, required=True)
    args = parser.parse_args(argv)
    if args.json_output.resolve() == args.markdown_output.resolve():
        parser.error("JSON and Markdown outputs must be different files")
    try:
        audit = build_audit(
            registry_path=args.registry,
            external_gates_path=args.external_gates,
            evidence_root=args.evidence_root,
            release_manifest=args.release_manifest,
            release_public_key=args.release_public_key,
        )
    except AuditError as error:
        parser.error(str(error))
    _atomic_write(args.json_output, serialize_json(audit))
    _atomic_write(args.markdown_output, render_markdown(audit))
    print(args.json_output)
    print(args.markdown_output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
