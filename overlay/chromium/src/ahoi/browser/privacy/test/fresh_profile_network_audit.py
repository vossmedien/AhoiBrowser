#!/usr/bin/env python3
# Copyright 2026 The AhoiBrowser Authors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Fail-closed fresh-profile endpoint audit for AhoiBrowser.

The audit deliberately leaves Chromium background networking enabled. Output
is privacy-safe: endpoint identifiers, origins, counts and hashes only.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from typing import Any, Iterable
from urllib.parse import urlsplit


DEFAULT_POLICY = (
    Path(__file__).resolve().parents[1] / "config" / "endpoint_allowlist_v1.json"
)

REVIEW_FIELDS = {
    "owner",
    "payload_class",
    "retention",
    "disable_behavior",
    "failure_behavior",
}


class PolicyError(ValueError):
    """Raised when the policy is incomplete or unsafe."""


class NetLogError(ValueError):
    """Raised with a privacy-safe code when a net log cannot be audited."""

    def __init__(self, code: str):
        super().__init__(code)
        self.code = code


FAILURE_REASONS = {
    "browser_exited_early": "browser exited before the capture window completed",
    "browser_launch_failed": "browser could not be launched",
    "net_log_missing": "net log was not created",
    "net_log_unreadable": "net log could not be read",
    "net_log_empty": "net log is empty",
    "net_log_invalid_json": "net log is not valid JSON",
    "net_log_invalid_structure": "net log has an invalid structure",
}


def _validated_url(url: str, *, require_https: bool = True) -> dict[str, Any]:
    parsed = urlsplit(url)
    if require_https and parsed.scheme != "https":
        raise PolicyError("endpoint must use https")
    if not parsed.hostname or parsed.username or parsed.password:
        raise PolicyError("endpoint must have a host and no credentials")
    if parsed.query or parsed.fragment:
        raise PolicyError("configured endpoints must not contain query or fragment")
    try:
        port = parsed.port or (443 if parsed.scheme == "https" else 80)
    except ValueError as error:
        raise PolicyError("endpoint port is invalid") from error
    return {
        "scheme": parsed.scheme,
        "host": parsed.hostname.lower(),
        "port": port,
        "path_prefix": parsed.path or "/",
    }


def load_policy(path: Path, environment: dict[str, str] | None = None) -> dict:
    raw = path.read_bytes()
    policy = json.loads(raw)
    if policy.get("schema_version") != 1 or policy.get("default_action") != "deny":
        raise PolicyError("policy must use schema version 1 and default deny")

    endpoints = []
    endpoint_ids = set()
    for endpoint in policy.get("allowed_background_endpoints", []):
        required = {
            "id",
            "scheme",
            "host",
            "port",
            "path_prefix",
            "purpose",
        } | REVIEW_FIELDS
        if set(endpoint) != required:
            raise PolicyError("allowlisted endpoint fields do not match schema")
        normalized = _validated_url(
            f"{endpoint['scheme']}://{endpoint['host']}:{endpoint['port']}"
            f"{endpoint['path_prefix']}"
        )
        normalized.update(
            id=endpoint["id"],
            purpose=endpoint["purpose"],
            **{field: endpoint[field] for field in REVIEW_FIELDS},
        )
        if normalized["id"] in endpoint_ids:
            raise PolicyError("endpoint IDs must be unique")
        endpoint_ids.add(normalized["id"])
        endpoints.append(normalized)

    environment = os.environ if environment is None else environment
    for conditional in policy.get("conditional_endpoints", []):
        required = {"id", "environment_url", "purpose"} | REVIEW_FIELDS
        if set(conditional) != required:
            raise PolicyError("conditional endpoint fields do not match schema")
        value = environment.get(conditional["environment_url"])
        if not value:
            continue
        normalized = _validated_url(value)
        normalized.update(
            id=conditional["id"],
            purpose=conditional["purpose"],
            **{field: conditional[field] for field in REVIEW_FIELDS},
        )
        if normalized["id"] in endpoint_ids:
            raise PolicyError("endpoint IDs must be unique")
        endpoint_ids.add(normalized["id"])
        endpoints.append(normalized)

    prohibited = []
    for endpoint in policy.get("prohibited_endpoints", []):
        if set(endpoint) != {"id", "url"}:
            raise PolicyError("prohibited endpoint fields do not match schema")
        normalized = _validated_url(endpoint["url"])
        normalized["id"] = endpoint["id"]
        prohibited.append(normalized)

    return {
        "policy_id": policy.get("policy_id"),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "endpoints": endpoints,
        "prohibited": prohibited,
    }


def _normalized_request(url: str) -> dict[str, Any] | None:
    try:
        parsed = urlsplit(url)
        port = parsed.port or (443 if parsed.scheme == "https" else 80)
    except ValueError:
        return None
    if parsed.scheme not in ("http", "https") or not parsed.hostname:
        return None
    return {
        "scheme": parsed.scheme,
        "host": parsed.hostname.lower(),
        "port": port,
        "path": parsed.path or "/",
    }


def _matches(request: dict[str, Any], endpoint: dict[str, Any]) -> bool:
    return (
        request["scheme"] == endpoint["scheme"]
        and request["host"] == endpoint["host"]
        and request["port"] == endpoint["port"]
        and request["path"].startswith(endpoint["path_prefix"])
    )


def extract_urls(net_log: dict) -> Iterable[str]:
    for event in net_log.get("events", []):
        params = event.get("params")
        if isinstance(params, dict) and isinstance(params.get("url"), str):
            yield params["url"]


def load_net_log(path: Path) -> dict:
    """Loads a Chromium net log without echoing potentially sensitive input."""

    try:
        raw = path.read_bytes()
    except FileNotFoundError:
        raise NetLogError("net_log_missing") from None
    except OSError:
        raise NetLogError("net_log_unreadable") from None
    if not raw.strip():
        raise NetLogError("net_log_empty")
    try:
        net_log = json.loads(raw)
    except (json.JSONDecodeError, UnicodeDecodeError):
        raise NetLogError("net_log_invalid_json") from None
    if not isinstance(net_log, dict) or not isinstance(net_log.get("events"), list):
        raise NetLogError("net_log_invalid_structure")
    if any(not isinstance(event, dict) for event in net_log["events"]):
        raise NetLogError("net_log_invalid_structure")
    return net_log


def audit_urls(
    urls: Iterable[str],
    policy: dict,
    extra_allowed_origin: str | None = None,
) -> dict:
    extra_origin = None
    if extra_allowed_origin:
        extra_origin = _validated_url(extra_allowed_origin)
        extra_origin["path_prefix"] = "/"
        extra_origin["id"] = "controlled-navigation-fixture"

    counts: dict[tuple[str, str], int] = {}
    unknown: dict[tuple[str, str], int] = {}
    prohibited: dict[tuple[str, str], int] = {}
    for raw_url in urls:
        request = _normalized_request(raw_url)
        if not request:
            continue
        origin = f"{request['scheme']}://{request['host']}:{request['port']}"
        denied = next(
            (item for item in policy["prohibited"] if _matches(request, item)),
            None,
        )
        if denied:
            key = (denied["id"], origin)
            prohibited[key] = prohibited.get(key, 0) + 1
            continue
        allowed = next(
            (item for item in policy["endpoints"] if _matches(request, item)),
            None,
        )
        if not allowed and extra_origin and _matches(request, extra_origin):
            allowed = extra_origin
        if allowed:
            key = (allowed["id"], origin)
            counts[key] = counts.get(key, 0) + 1
            continue
        fingerprint = hashlib.sha256(
            f"{origin}{request['path']}".encode("utf-8")
        ).hexdigest()
        key = (origin, fingerprint)
        unknown[key] = unknown.get(key, 0) + 1

    def evidence(values: dict[tuple[str, str], int], names: tuple[str, str]):
        return [
            {names[0]: key[0], names[1]: key[1], "count": count}
            for key, count in sorted(values.items())
        ]

    return {
        "policy_id": policy["policy_id"],
        "policy_sha256": policy["sha256"],
        "passed": not unknown and not prohibited,
        "allowed": evidence(counts, ("endpoint_id", "origin")),
        "unknown": evidence(unknown, ("origin", "url_fingerprint")),
        "prohibited": evidence(prohibited, ("endpoint_id", "origin")),
    }


def capture_fresh_profile(
    browser: Path,
    net_log_path: Path,
    idle_seconds: float,
    navigate_url: str | None,
) -> int:
    """Captures one full idle window and returns zero only when it completes.

    The browser is intentionally terminated after a complete capture window, so
    that termination's process status is not a capture failure. Any earlier
    browser exit is returned as a failure; an early clean exit is normalized to
    one so callers cannot mistake it for a successful capture.
    """

    if idle_seconds <= 0:
        raise ValueError("idle_seconds must be positive")
    with tempfile.TemporaryDirectory(prefix="ahoi-fresh-profile-") as profile:
        command = [
            str(browser),
            f"--user-data-dir={profile}",
            "--no-first-run",
            "--disable-extensions",
            "--headless=new",
            f"--log-net-log={net_log_path}",
            "--net-log-capture-mode=Default",
            navigate_url or "about:blank",
        ]
        process = subprocess.Popen(
            command,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        completed_window = False
        early_return_code = 0
        try:
            try:
                early_return_code = process.wait(timeout=idle_seconds)
            except subprocess.TimeoutExpired:
                completed_window = True
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=10)
        if completed_window:
            return 0
        return early_return_code or 1


def failure_result(
    policy: dict,
    code: str,
    *,
    capture_return_code: int | None = None,
) -> dict:
    """Builds a machine-readable failure without including raw capture data."""

    failure: dict[str, Any] = {
        "code": code,
        "reason": FAILURE_REASONS[code],
    }
    if capture_return_code is not None:
        failure["capture_return_code"] = capture_return_code
    return {
        "policy_id": policy["policy_id"],
        "policy_sha256": policy["sha256"],
        "passed": False,
        "allowed": [],
        "unknown": [],
        "prohibited": [],
        "failure": failure,
    }


def write_result(path: Path, result: dict) -> None:
    path.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--browser", type=Path)
    source.add_argument("--net-log", type=Path)
    parser.add_argument("--policy", type=Path, default=DEFAULT_POLICY)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--idle-seconds", type=float, default=10.0)
    parser.add_argument("--navigate-url")
    parser.add_argument("--allow-navigation-origin")
    args = parser.parse_args()

    policy = load_policy(args.policy)
    if args.idle_seconds <= 0:
        parser.error("--idle-seconds must be positive")
    if args.navigate_url and not args.allow_navigation_origin:
        parser.error("--navigate-url requires --allow-navigation-origin")
    if args.navigate_url:
        navigation = _normalized_request(args.navigate_url)
        allowed_origin = _validated_url(args.allow_navigation_origin)
        if not navigation or not _matches(navigation, allowed_origin):
            parser.error("navigation URL must match the controlled origin")

    if args.browser:
        if not args.browser.is_file():
            parser.error("browser binary does not exist")
        with tempfile.TemporaryDirectory(prefix="ahoi-netlog-") as directory:
            net_log_path = Path(directory) / "netlog.json"
            try:
                capture_return_code = capture_fresh_profile(
                    args.browser, net_log_path, args.idle_seconds, args.navigate_url
                )
            except OSError:
                write_result(
                    args.output, failure_result(policy, "browser_launch_failed")
                )
                return 2
            if capture_return_code != 0:
                write_result(
                    args.output,
                    failure_result(
                        policy,
                        "browser_exited_early",
                        capture_return_code=capture_return_code,
                    ),
                )
                return 2
            try:
                net_log = load_net_log(net_log_path)
            except NetLogError as error:
                write_result(args.output, failure_result(policy, error.code))
                return 2
    else:
        try:
            net_log = load_net_log(args.net_log)
        except NetLogError as error:
            write_result(args.output, failure_result(policy, error.code))
            return 2

    result = audit_urls(extract_urls(net_log), policy, args.allow_navigation_origin)
    write_result(args.output, result)
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
