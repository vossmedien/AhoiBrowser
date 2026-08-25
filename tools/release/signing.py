#!/usr/bin/env python3
"""Nested macOS signing order and independent signature verification."""

from __future__ import annotations

import pathlib
import plistlib
import re
import tempfile
from typing import Callable, Optional

from .common import (
    ReleaseError,
    atomic_write_json,
    bundle_identity,
    load_json,
    legacy_bundle_sha256,
    read_bundle_plist,
    require_string,
    run,
    sha256_file,
    tree_sha256,
)


CONTAINER_SUFFIXES = {".app", ".framework", ".xpc"}


def _relative(app: pathlib.Path, path: pathlib.Path) -> str:
    return path.relative_to(app).as_posix()


def _is_macho(path: pathlib.Path) -> bool:
    if not path.is_file() or path.is_symlink():
        return False
    return b"Mach-O" in run(["file", "-b", str(path)]).stdout


def discover_macho(app: pathlib.Path) -> list[pathlib.Path]:
    if not app.is_dir() or app.suffix != ".app":
        raise ReleaseError(f"AhoiBrowser app bundle is missing: {app}")
    return [
        path
        for path in sorted(app.rglob("*"), key=lambda item: str(item))
        if _is_macho(path)
    ]


def signing_plan(
    app: pathlib.Path, macho_paths: Optional[list[pathlib.Path]] = None
) -> list[pathlib.Path]:
    """Return leaves then nested wrappers, with the outer app always last."""
    macho = macho_paths if macho_paths is not None else discover_macho(app)
    if not macho:
        raise ReleaseError("release bundle contains no Mach-O code")
    candidates = set(macho)
    for path in macho:
        for parent in path.parents:
            if parent == app.parent:
                break
            if parent.suffix in CONTAINER_SUFFIXES:
                candidates.add(parent)
            if parent == app:
                break
    if app not in candidates:
        candidates.add(app)
    ordered = sorted(
        (path for path in candidates if path != app),
        key=lambda path: (
            -len(path.relative_to(app).parts),
            0 if path.is_file() else 1,
            path.relative_to(app).as_posix(),
        ),
    )
    ordered.append(app)
    positions = {item: index for index, item in enumerate(ordered)}
    for child in ordered:
        for parent in child.parents:
            if parent in positions and positions[parent] < positions[child]:
                raise ReleaseError("nested signing plan would sign a parent before a child")
            if parent == app:
                break
    return ordered


def _compiled_rules(policy_path: pathlib.Path) -> list[tuple[str, re.Pattern, dict]]:
    policy = load_json(policy_path)
    if not isinstance(policy, dict) or policy.get("schemaVersion") != 1:
        raise ReleaseError("macOS entitlement policy schema is unsupported")
    rules = policy.get("rules")
    if not isinstance(rules, list) or not rules:
        raise ReleaseError("macOS entitlement policy has no rules")
    compiled = []
    for rule in rules:
        if not isinstance(rule, dict) or set(rule) != {
            "id",
            "pathPattern",
            "entitlements",
        }:
            raise ReleaseError("macOS entitlement rule has an invalid shape")
        role = require_string(rule["id"], "entitlement rule id")
        pattern = re.compile(require_string(rule["pathPattern"], "pathPattern"))
        if not isinstance(rule["entitlements"], dict):
            raise ReleaseError(f"entitlements for {role} must be an object")
        compiled.append((role, pattern, rule["entitlements"]))
    return compiled


def role_for_relative(
    relative: str, rules: list[tuple[str, re.Pattern, dict]]
) -> tuple[str, dict]:
    matches = [
        (role, entitlements)
        for role, pattern, entitlements in rules
        if pattern.fullmatch(relative)
    ]
    if len(matches) != 1:
        raise ReleaseError(
            f"nested code must match exactly one entitlement role: {relative} "
            f"(matched {len(matches)})"
        )
    return matches[0]


def _container_executable(app: pathlib.Path, container: pathlib.Path) -> Optional[pathlib.Path]:
    if container.suffix not in {".app", ".xpc"}:
        return None
    plist = read_bundle_plist(container)
    executable_name = require_string(
        plist.get("CFBundleExecutable"), f"{container.name} CFBundleExecutable"
    )
    executable = container / "Contents/MacOS" / executable_name
    if not executable.is_file():
        raise ReleaseError(f"nested bundle executable is missing: {executable}")
    try:
        executable.relative_to(app)
    except ValueError as error:
        raise ReleaseError("nested bundle executable escapes the outer app") from error
    return executable


def _role_for_signable(
    app: pathlib.Path,
    signable: pathlib.Path,
    rules: list[tuple[str, re.Pattern, dict]],
) -> Optional[tuple[str, dict]]:
    executable = _container_executable(app, signable)
    policy_target = executable or (signable if signable.is_file() else None)
    if policy_target is None:
        return None
    return role_for_relative(_relative(app, policy_target), rules)


def _description(path: pathlib.Path) -> str:
    completed = run(["codesign", "-d", "--verbose=4", str(path)])
    return (completed.stdout + completed.stderr).decode("utf-8", "replace")


def _description_field(description: str, key: str) -> str:
    prefix = key + "="
    for line in description.splitlines():
        if line.startswith(prefix):
            return line[len(prefix) :].strip()
    raise ReleaseError(f"codesign description is missing {key}")


def _read_entitlements(path: pathlib.Path) -> dict:
    completed = run(["codesign", "-d", "--entitlements", ":-", str(path)])
    try:
        value = plistlib.loads(completed.stdout)
    except plistlib.InvalidFileException as error:
        raise ReleaseError(f"cannot parse signed entitlements: {path}") from error
    if not isinstance(value, dict):
        raise ReleaseError(f"signed entitlements are not a dictionary: {path}")
    return value


def verify_signed_app(
    app: pathlib.Path,
    *,
    expected_team: str,
    expected_authority: str,
    policy_path: pathlib.Path,
    require_notarization: bool = False,
    macho_paths: Optional[list[pathlib.Path]] = None,
) -> dict:
    if not re.fullmatch(r"[A-Z0-9]{10}", expected_team):
        raise ReleaseError("Apple Team ID must contain ten uppercase letters/digits")
    if not expected_authority.startswith("Developer ID Application: "):
        raise ReleaseError("release authority must be a Developer ID Application identity")
    rules = _compiled_rules(policy_path)
    macho = macho_paths if macho_paths is not None else discover_macho(app)
    plan = signing_plan(app, macho)
    verified = []
    for signable in plan:
        run(["codesign", "--verify", "--strict", "--verbose=4", str(signable)])
        description = _description(signable)
        if _description_field(description, "TeamIdentifier") != expected_team:
            relative = _relative(app, signable) if signable != app else "."
            raise ReleaseError(f"unexpected TeamIdentifier: {relative}")
        if _description_field(description, "Authority") != expected_authority:
            relative = _relative(app, signable) if signable != app else "."
            raise ReleaseError(f"unexpected signing authority: {relative}")
        if "Timestamp=" not in description and "Signed Time=" not in description:
            raise ReleaseError(f"trusted signing timestamp is missing: {signable}")
        role = _role_for_signable(app, signable, rules)
        role_name = None
        if role is not None:
            role_name, expected_entitlements = role
            actual_entitlements = _read_entitlements(signable)
            if actual_entitlements != expected_entitlements:
                raise ReleaseError(f"signed entitlements differ from role {role_name}: {signable}")
            if actual_entitlements.get("com.apple.security.get-task-allow") is not None:
                raise ReleaseError(f"get-task-allow is forbidden in release code: {signable}")
        if signable in macho and "runtime" not in _description_field(description, "CodeDirectory"):
            raise ReleaseError(f"Hardened Runtime is missing: {signable}")
        architecture = None
        if signable in macho:
            architecture = run(["lipo", "-archs", str(signable)]).stdout.decode(
                "utf-8", "replace"
            ).strip()
            if architecture != "arm64":
                raise ReleaseError(
                    f"release Mach-O must be ARM64-only, got {architecture}: {signable}"
                )
        verified.append(
            {
                "path": "." if signable == app else _relative(app, signable),
                "role": role_name,
                "cdHash": _description_field(description, "CDHash"),
                "architecture": architecture,
            }
        )
    run(["codesign", "--verify", "--deep", "--strict", "--verbose=4", str(app)])
    gatekeeper = False
    stapled = False
    if require_notarization:
        run(["spctl", "--assess", "--type", "execute", "--verbose=4", str(app)])
        run(["xcrun", "stapler", "validate", str(app)])
        gatekeeper = True
        stapled = True
    return {
        "bundle": bundle_identity(app),
        "teamIdentifier": expected_team,
        "authority": expected_authority,
        "hardenedRuntime": True,
        "trustedTimestamp": True,
        "gatekeeperAccepted": gatekeeper,
        "staplerValidated": stapled,
        "nestedCode": verified,
    }


def sign_app(
    app: pathlib.Path,
    *,
    identity: str,
    team_id: str,
    policy_path: pathlib.Path,
    build_provenance_path: pathlib.Path,
    output: pathlib.Path,
) -> dict:
    if not identity.startswith("Developer ID Application: "):
        raise ReleaseError("AHOI_CODESIGN_IDENTITY must be Developer ID Application")
    if not re.fullmatch(r"[A-Z0-9]{10}", team_id):
        raise ReleaseError("AHOI_TEAM_ID must be a ten-character Apple Team ID")
    if not identity.endswith(f"({team_id})"):
        raise ReleaseError("codesign identity does not end with the configured Team ID")
    build_provenance_hash = sha256_file(build_provenance_path)
    unsigned = bundle_identity(app)
    unsigned["buildProvenanceBundleSha256"] = legacy_bundle_sha256(app)
    build_provenance = load_json(build_provenance_path)
    if (
        not isinstance(build_provenance, dict)
        or build_provenance.get("schemaVersion") != 2
        or build_provenance.get("kind") != "ahoi-release"
    ):
        raise ReleaseError("signing requires schema-v2 ahoi-release provenance")
    build_app = build_provenance.get("app")
    if not isinstance(build_app, dict):
        raise ReleaseError("build provenance app section is missing")
    if build_app.get("bundleSha256") != unsigned["buildProvenanceBundleSha256"]:
        raise ReleaseError("unsigned candidate differs from build-provenance bundle hash")
    if build_app.get("binarySha256") != unsigned["executableSha256"]:
        raise ReleaseError("unsigned main binary differs from build provenance")
    identity_binding = {
        "bundleName": "name",
        "bundleIdentifier": "identifier",
        "marketingVersion": "marketingVersion",
        "buildNumber": "buildNumber",
        "productVersion": "productVersion",
        "channel": "channel",
        "sourceCommit": "sourceCommit",
        "chromiumVersion": "chromiumVersion",
        "chromiumCommit": "chromiumCommit",
        "gnArgsSha256": "gnArgsSha256",
        "buildProfile": "buildProfile",
    }
    for build_field, identity_field in identity_binding.items():
        if build_app.get(build_field) != unsigned[identity_field]:
            raise ReleaseError(
                f"unsigned candidate {identity_field} differs from build provenance"
            )
    rules = _compiled_rules(policy_path)
    plan = signing_plan(app)
    signed_order = []
    with tempfile.TemporaryDirectory(prefix="ahoi-entitlements-") as directory:
        entitlement_root = pathlib.Path(directory)
        for index, signable in enumerate(plan):
            arguments = [
                "codesign",
                "--force",
                "--sign",
                identity,
                "--timestamp",
                "--options",
                "runtime",
            ]
            role = _role_for_signable(app, signable, rules)
            role_name = None
            if role is not None:
                role_name, entitlements = role
                entitlement_path = entitlement_root / f"{index:04d}-{role_name}.plist"
                entitlement_path.write_bytes(plistlib.dumps(entitlements, sort_keys=True))
                arguments.extend(["--entitlements", str(entitlement_path)])
            arguments.append(str(signable))
            run(arguments)
            signed_order.append(
                {
                    "path": "." if signable == app else _relative(app, signable),
                    "role": role_name,
                }
            )
    verification = verify_signed_app(
        app,
        expected_team=team_id,
        expected_authority=identity,
        policy_path=policy_path,
    )
    receipt = {
        "schemaVersion": 1,
        "kind": "signed-package-provenance",
        "buildProvenance": {
            "file": build_provenance_path.name,
            "sha256": build_provenance_hash,
        },
        "unsignedBundle": unsigned,
        "signedBundle": verification["bundle"],
        "signing": {
            "teamIdentifier": team_id,
            "authority": identity,
            "hardenedRuntime": True,
            "trustedTimestamp": True,
            "order": signed_order,
        },
        "verification": {
            "nestedCode": verification["nestedCode"],
            "deepStrict": True,
        },
    }
    atomic_write_json(output, receipt)
    return receipt
