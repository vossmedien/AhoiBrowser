#!/usr/bin/env python3
"""Validate one signed Ahoi Mach-O against the role-specific entitlement policy."""

from __future__ import annotations

import argparse
import json
import pathlib
import plistlib
import re
import sys
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[1]


def load_policy(path: pathlib.Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schemaVersion") != 1:
        raise SystemExit("unsupported macOS entitlement policy schema")
    chromium = json.loads(
        (ROOT / "config/chromium.json").read_text(encoding="utf-8")
    )
    if value.get("chromiumCommit") != chromium["commit"]:
        raise SystemExit("entitlement policy is stale for the Chromium pin")
    rules = value.get("rules")
    if not isinstance(rules, list) or not rules:
        raise SystemExit("entitlement policy must contain rules")
    seen_ids = set()
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


def verify(policy: dict[str, Any], relative_path: str, actual: dict[str, Any]) -> str:
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
    expected = rule["entitlements"]
    if actual != expected:
        missing = sorted(set(expected) - set(actual))
        unexpected = sorted(set(actual) - set(expected))
        wrong = sorted(
            key for key in set(actual) & set(expected) if actual[key] != expected[key]
        )
        raise SystemExit(
            f"entitlement mismatch for role {rule['id']}: "
            f"missing={missing}, unexpected={unexpected}, wrongValues={wrong}"
        )
    return rule["id"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--policy",
        type=pathlib.Path,
        default=ROOT / "config/macos-entitlements.json",
    )
    parser.add_argument("--relative-path", required=True)
    args = parser.parse_args()
    policy = load_policy(args.policy)
    role = verify(policy, args.relative_path, parse_entitlements(sys.stdin.buffer.read()))
    print(role)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
