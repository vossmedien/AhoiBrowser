#!/usr/bin/env python3
"""Measure Lean Chromium bundle baselines deterministically and fail closed."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import plistlib
import re
import stat
import subprocess
import tempfile
from decimal import Decimal, ROUND_HALF_UP
from typing import Any, Optional

from evidence import bundle_hash


ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "config/lean-bundle-measurement.json"
MACH_O_MAGICS = {
    bytes.fromhex(value)
    for value in (
        "feedface",
        "cefaedfe",
        "feedfacf",
        "cffaedfe",
        "cafebabe",
        "bebafeca",
        "cafebabf",
        "bfbafeca",
    )
}
CATEGORY_IDS = ("resources", "frameworks-and-libraries", "mach-o")
SHA256_RE = re.compile(r"[0-9a-f]{64}")
EXPECTED_RECEIPT_BINDINGS = {
    "upstream-control": (
        "artifacts/build/upstream-build.json",
        "unmodified-upstream-control",
    ),
    "ahoi-full-release": (
        "artifacts/build/ahoi-full-release-build.json",
        "ahoi-full-release",
    ),
    "ahoi-release": (
        "artifacts/build/ahoi-release-build.json",
        "ahoi-release",
    ),
}
BUILD_TOOL_KEYS = ("gn", "ninja", "siso", "clang", "lld")


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def json_sha256(value: Any) -> str:
    encoded = json.dumps(
        value, ensure_ascii=False, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def git_output(*args: str, cwd: pathlib.Path) -> str:
    try:
        return subprocess.run(
            args,
            cwd=cwd,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"cannot inspect repository identity: {error}") from error


def require_object(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise SystemExit(f"{field} must be an object")
    return value


def require_nonempty_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise SystemExit(f"{field} must be a non-empty string")
    return value


def require_sha256(value: Any, field: str) -> str:
    if not isinstance(value, str) or SHA256_RE.fullmatch(value) is None:
        raise SystemExit(f"{field} must be a lowercase SHA-256")
    return value


def logical_repo_path(relative: pathlib.PurePosixPath) -> str:
    return f"<repo>/{relative.as_posix()}"


def provenance_logical_path(path: pathlib.Path, work_root: pathlib.Path) -> str:
    resolved = path.resolve()
    for root, label in (
        (ROOT.resolve(), "<repo>"),
        (work_root.resolve(), "<work-root>"),
    ):
        try:
            relative = resolved.relative_to(root)
        except ValueError:
            continue
        return label if str(relative) == "." else f"{label}/{relative.as_posix()}"
    raise SystemExit(f"path is outside the provenance roots: {resolved}")


def load_json(path: pathlib.Path) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise SystemExit(f"cannot read JSON {path}: {error}") from error
    if not isinstance(payload, dict):
        raise SystemExit(f"JSON root must be an object: {path}")
    return payload


def configured_work_root() -> pathlib.Path:
    raw = os.environ.get("AHOI_WORK_ROOT")
    work_root = pathlib.Path(raw) if raw else ROOT / ".work"
    if not work_root.is_absolute():
        raise SystemExit("AHOI_WORK_ROOT must be absolute")
    return work_root.resolve()


def relative_path(value: Any, field: str) -> pathlib.PurePosixPath:
    if not isinstance(value, str) or not value:
        raise SystemExit(f"{field} must be a non-empty relative POSIX path")
    parsed = pathlib.PurePosixPath(value)
    if parsed.is_absolute() or ".." in parsed.parts or "." in parsed.parts:
        raise SystemExit(f"{field} must not be absolute or contain dot components")
    return parsed


def resolve_beneath(
    root: pathlib.Path, value: Any, field: str, *, kind: str
) -> pathlib.Path:
    rel_path = relative_path(value, field)
    candidate = root.joinpath(*rel_path.parts)
    if candidate.is_symlink():
        raise SystemExit(f"{field} must not be a symlink: {value}")
    resolved = candidate.resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError as error:
        raise SystemExit(f"{field} escapes its configured root: {value}") from error
    if kind == "file" and not resolved.is_file():
        raise SystemExit(f"{field} is not a regular file: {value}")
    if kind == "directory" and not resolved.is_dir():
        raise SystemExit(f"{field} is not a directory: {value}")
    return resolved


def is_mach_o(path: pathlib.Path) -> bool:
    with path.open("rb") as handle:
        return handle.read(4) in MACH_O_MAGICS


def path_categories(relative: str, *, mach_o: bool) -> list[str]:
    parts = pathlib.PurePosixPath(relative).parts
    categories = []
    if "Resources" in parts:
        categories.append("resources")
    if "Frameworks" in parts or relative.endswith(".dylib"):
        categories.append("frameworks-and-libraries")
    if mach_o:
        categories.append("mach-o")
    return categories


def inventory_bundle(bundle: pathlib.Path) -> dict[str, Any]:
    entries: list[dict[str, Any]] = []
    category_metrics = {
        category: {"regularFileCount": 0, "logicalBytes": 0}
        for category in CATEGORY_IDS
    }
    regular_file_count = 0
    symlink_count = 0
    logical_bytes = 0

    def add_symlink(path: pathlib.Path) -> None:
        nonlocal symlink_count
        info = path.lstat()
        relative = path.relative_to(bundle).as_posix()
        target = os.readlink(path)
        entries.append(
            {
                "path": relative,
                "type": "symlink",
                "mode": f"{stat.S_IMODE(info.st_mode):04o}",
                "target": target,
            }
        )
        symlink_count += 1

    for current_raw, directory_names, file_names in os.walk(
        bundle, topdown=True, followlinks=False
    ):
        current = pathlib.Path(current_raw)
        directory_names.sort()
        file_names.sort()

        retained_directories = []
        for name in directory_names:
            child = current / name
            if child.is_symlink():
                add_symlink(child)
            else:
                if not child.is_dir():
                    raise SystemExit(f"unsupported bundle entry type: {child}")
                retained_directories.append(name)
        directory_names[:] = retained_directories

        for name in file_names:
            child = current / name
            if child.is_symlink():
                add_symlink(child)
                continue
            info = child.lstat()
            if not stat.S_ISREG(info.st_mode):
                raise SystemExit(f"unsupported bundle entry type: {child}")
            relative = child.relative_to(bundle).as_posix()
            digest = sha256_file(child)
            mach_o = is_mach_o(child)
            categories = path_categories(relative, mach_o=mach_o)
            size = info.st_size
            entry = {
                "path": relative,
                "type": "regular-file",
                "mode": f"{stat.S_IMODE(info.st_mode):04o}",
                "logicalBytes": size,
                "sha256": digest,
                "categories": categories,
            }
            entries.append(entry)
            regular_file_count += 1
            logical_bytes += size
            for category in categories:
                category_metrics[category]["regularFileCount"] += 1
                category_metrics[category]["logicalBytes"] += size

    entries.sort(key=lambda entry: (entry["path"], entry["type"]))
    tree_digest = hashlib.sha256()
    for entry in entries:
        digest_entry = {
            key: entry[key]
            for key in (
                "path",
                "type",
                "mode",
                "logicalBytes",
                "sha256",
                "target",
            )
            if key in entry
        }
        tree_digest.update(
            json.dumps(
                digest_entry,
                ensure_ascii=False,
                sort_keys=True,
                separators=(",", ":"),
            ).encode("utf-8")
        )
        tree_digest.update(b"\n")

    return {
        "regularFileCount": regular_file_count,
        "symlinkCount": symlink_count,
        "logicalBytes": logical_bytes,
        "treeSha256": tree_digest.hexdigest(),
        "categories": category_metrics,
        "entries": entries,
    }


def validate_manifest(manifest: dict[str, Any]) -> None:
    if manifest.get("schemaVersion") != 1:
        raise SystemExit("unsupported Lean bundle measurement schema")
    relative_path(manifest.get("matrixPath"), "matrixPath")
    relative_path(manifest.get("outputPath"), "outputPath")
    require_object(manifest.get("chromium"), "chromium")
    profiles = manifest.get("profiles")
    comparisons = manifest.get("comparisons")
    if not isinstance(profiles, list) or not profiles:
        raise SystemExit("measurement manifest profiles must be a non-empty list")
    if not isinstance(comparisons, list) or not comparisons:
        raise SystemExit("measurement manifest comparisons must be a non-empty list")
    profile_ids = [
        profile.get("id") for profile in profiles if isinstance(profile, dict)
    ]
    if len(profile_ids) != len(profiles) or any(
        not isinstance(profile_id, str) or not profile_id for profile_id in profile_ids
    ):
        raise SystemExit("every measurement profile must have a non-empty id")
    if len(profile_ids) != len(set(profile_ids)):
        raise SystemExit("measurement profile ids must be unique")
    if set(profile_ids) != set(EXPECTED_RECEIPT_BINDINGS):
        raise SystemExit(
            "measurement manifest must contain the three release baselines"
        )
    profile_paths: dict[str, set[pathlib.PurePosixPath]] = {
        key: set()
        for key in (
            "argsPath",
            "bundlePath",
            "generatedArgsPath",
            "receiptPath",
        )
    }
    expected_profiles = {
        "upstream-control": None,
        "ahoi-full-release": "full-release",
        "ahoi-release": "release",
    }
    for profile in profiles:
        assert isinstance(profile, dict)
        profile_id = profile["id"]
        expected_receipt_path, expected_receipt_kind = EXPECTED_RECEIPT_BINDINGS[
            profile_id
        ]
        if profile.get("receiptPath") != expected_receipt_path:
            raise SystemExit(f"{profile_id} receiptPath is not the canonical receipt")
        if profile.get("expectedReceiptKind") != expected_receipt_kind:
            raise SystemExit(f"{profile_id} expectedReceiptKind is not canonical")
        if profile.get("expectedBuildProfile") != expected_profiles[profile_id]:
            raise SystemExit(f"{profile_id} expectedBuildProfile is not canonical")
        require_sha256(
            profile.get("expectedArgsSha256"),
            f"profiles.{profile_id}.expectedArgsSha256",
        )
        for key in profile_paths:
            parsed = relative_path(
                profile.get(key), f"profiles.{profile_id}.{key}"
            )
            if parsed in profile_paths[key]:
                raise SystemExit(f"measurement profile {key} values must be unique")
            profile_paths[key].add(parsed)
        require_nonempty_string(
            profile.get("expectedBundleName"),
            f"profiles.{profile_id}.expectedBundleName",
        )
        require_nonempty_string(
            profile.get("expectedBundleIdentifier"),
            f"profiles.{profile_id}.expectedBundleIdentifier",
        )
    known_profiles = set(profile_ids)
    comparison_ids = []
    for comparison in comparisons:
        if not isinstance(comparison, dict):
            raise SystemExit("every comparison must be an object")
        comparison_id = comparison.get("id")
        if not isinstance(comparison_id, str) or not comparison_id:
            raise SystemExit("every comparison must have a non-empty id")
        comparison_ids.append(comparison_id)
        if comparison.get("operation") not in {"overhead", "savings"}:
            raise SystemExit(f"unsupported comparison operation: {comparison_id}")
        for key in ("subjectProfile", "referenceProfile"):
            if comparison.get(key) not in known_profiles:
                raise SystemExit(f"comparison {comparison_id} has unknown {key}")
    if len(comparison_ids) != len(set(comparison_ids)):
        raise SystemExit("comparison ids must be unique")
    gate = require_object(manifest.get("gate"), "gate")
    if gate.get("passWhen") != "all-comparisons-pass":
        raise SystemExit("gate.passWhen must be all-comparisons-pass")
    if gate.get("otherwise") != "product-decision-required":
        raise SystemExit("gate.otherwise must be product-decision-required")
    gate_test_ids = gate.get("testIds")
    if (
        not isinstance(gate_test_ids, list)
        or not gate_test_ids
        or any(not isinstance(item, str) or not item for item in gate_test_ids)
    ):
        raise SystemExit("gate.testIds must be a non-empty string array")


def read_identity(bundle: pathlib.Path) -> dict[str, Any]:
    plist_path = bundle / "Contents/Info.plist"
    try:
        with plist_path.open("rb") as handle:
            plist = plistlib.load(handle)
    except (OSError, plistlib.InvalidFileException) as error:
        raise SystemExit(f"cannot read bundle Info.plist: {plist_path}: {error}") from error
    return {
        "bundleName": plist.get("CFBundleName"),
        "bundleIdentifier": plist.get("CFBundleIdentifier"),
        "buildProfile": plist.get("AhoiBuildProfile"),
        "gnArgsSha256": plist.get("AhoiGNArgsSHA256"),
    }


def validate_identity(
    profile: dict[str, Any], identity: dict[str, Any], args_sha256: str
) -> None:
    checks = {
        "bundleName": profile.get("expectedBundleName"),
        "bundleIdentifier": profile.get("expectedBundleIdentifier"),
        "buildProfile": profile.get("expectedBuildProfile"),
    }
    for key, expected in checks.items():
        if identity.get(key) != expected:
            raise SystemExit(
                f"{profile['id']} {key} mismatch: expected {expected!r}, "
                f"got {identity.get(key)!r}"
            )
    if checks["buildProfile"] is None:
        if identity.get("gnArgsSha256") is not None:
            raise SystemExit("upstream control unexpectedly carries an Ahoi GN stamp")
    elif identity.get("gnArgsSha256") != args_sha256:
        raise SystemExit(f"{profile['id']} stamped GN args hash mismatch")


def expected_build_tool_identities(toolchain: dict[str, Any]) -> dict[str, Any]:
    pins = require_object(toolchain.get("buildTools"), "toolchain.buildTools")
    return {
        "gn": {
            "version": pins.get("gnVersionOutput"),
            "binarySha256": pins.get("gnBinarySha256"),
        },
        "ninja": {
            "version": pins.get("ninjaVersionOutput"),
            "binarySha256": pins.get("ninjaBinarySha256"),
        },
        "siso": {
            "enabled": False,
            "configuredRevision": pins.get("sisoRevision"),
        },
        "clang": {
            "version": pins.get("clangVersionLine"),
            "package": pins.get("clangPackage"),
            "archiveSha256": pins.get("clangArchiveSha256"),
            "binarySha256": pins.get("clangBinarySha256"),
        },
        "lld": {
            "version": pins.get("lldVersionLine"),
            "binarySha256": pins.get("lldBinarySha256"),
            "driver": "ld64.lld -> lld",
        },
    }


def expected_release_toolchain_identity(toolchain: dict[str, Any]) -> dict[str, Any]:
    xcode = require_object(toolchain.get("xcode"), "toolchain.xcode")
    sdks = require_object(toolchain.get("sdks"), "toolchain.sdks")
    macos = require_object(sdks.get("macOS"), "toolchain.sdks.macOS")
    ios = require_object(sdks.get("iOS"), "toolchain.sdks.iOS")
    return {
        "mode": "pinned-reference",
        "developerDirectory": xcode.get("developerDirectory"),
        "xcodeVersion": xcode.get("requiredVersion"),
        "xcodeBuild": xcode.get("requiredBuild"),
        "macOSSDKVersion": macos.get("testedVersion"),
        "macOSSDKBuild": macos.get("chromiumOfficialBuild"),
        "iOSSDKVersion": ios.get("testedVersion"),
        "iOSSDKBuild": ios.get("pinnedReferenceBuild"),
        "pins": toolchain,
    }


def validate_profile_receipt(
    profile: dict[str, Any],
    *,
    receipt_path: pathlib.Path,
    receipt: dict[str, Any],
    bundle: pathlib.Path,
    identity: dict[str, Any],
    args_path: pathlib.Path,
    args_sha256: str,
    generated_args_path: pathlib.Path,
    work_root: pathlib.Path,
) -> dict[str, Any]:
    profile_id = profile["id"]
    if receipt.get("schemaVersion") != 2:
        raise SystemExit(f"{profile_id} build receipt schema must be 2")
    if receipt.get("kind") != profile.get("expectedReceiptKind"):
        raise SystemExit(f"{profile_id} build receipt kind mismatch")
    app = require_object(receipt.get("app"), f"{profile_id}.receipt.app")
    source = require_object(receipt.get("source"), f"{profile_id}.receipt.source")
    build = require_object(receipt.get("build"), f"{profile_id}.receipt.build")
    require_object(receipt.get("toolchain"), f"{profile_id}.receipt.toolchain")

    relative_path(profile.get("bundlePath"), f"profiles.{profile_id}.bundlePath")
    relative_path(
        profile.get("generatedArgsPath"),
        f"profiles.{profile_id}.generatedArgsPath",
    )
    args_relative = relative_path(
        profile.get("argsPath"), f"profiles.{profile_id}.argsPath"
    )
    expected_app_path = provenance_logical_path(bundle, work_root)
    expected_generated_path = provenance_logical_path(generated_args_path, work_root)
    expected_out_path = provenance_logical_path(generated_args_path.parent, work_root)
    expected_args_path = logical_repo_path(args_relative)
    path_checks = {
        "app.path": (app.get("path"), expected_app_path),
        "build.outDirectory": (build.get("outDirectory"), expected_out_path),
        "build.gnArgsPath": (build.get("gnArgsPath"), expected_args_path),
        "build.generatedGnArgsPath": (
            build.get("generatedGnArgsPath"),
            expected_generated_path,
        ),
    }
    for field, (actual, expected) in path_checks.items():
        if actual != expected:
            raise SystemExit(
                f"{profile_id} receipt {field} mismatch: expected {expected!r}"
            )

    if app.get("bundleName") != identity.get("bundleName"):
        raise SystemExit(f"{profile_id} receipt bundle name mismatch")
    if app.get("bundleIdentifier") != identity.get("bundleIdentifier"):
        raise SystemExit(f"{profile_id} receipt bundle identifier mismatch")
    expected_profile = profile.get("expectedBuildProfile")
    if app.get("buildProfile") != expected_profile:
        raise SystemExit(f"{profile_id} receipt build profile mismatch")
    if build.get("gnArgsSha256") != args_sha256:
        raise SystemExit(f"{profile_id} receipt configured GN args hash mismatch")
    if expected_profile is None:
        if app.get("gnArgsSha256") is not None:
            raise SystemExit("upstream receipt unexpectedly carries an Ahoi GN hash")
    elif app.get("gnArgsSha256") != args_sha256:
        raise SystemExit(f"{profile_id} receipt stamped GN args hash mismatch")

    generated_args_sha256 = sha256_file(generated_args_path)
    if build.get("generatedGnArgsSha256") != generated_args_sha256:
        raise SystemExit(f"{profile_id} generated GN args receipt hash mismatch")
    try:
        configured_args = args_path.read_text(encoding="utf-8").strip()
        generated_args = generated_args_path.read_text(encoding="utf-8").strip()
    except (OSError, UnicodeDecodeError) as error:
        raise SystemExit(
            f"cannot compare {profile_id} generated GN args: {error}"
        ) from error
    if generated_args != configured_args:
        raise SystemExit(f"{profile_id} generated GN args differ from its profile")

    recorded_bundle_sha256 = require_sha256(
        app.get("bundleSha256"), f"{profile_id}.receipt.app.bundleSha256"
    )
    actual_bundle_sha256 = bundle_hash(bundle)
    if actual_bundle_sha256 != recorded_bundle_sha256:
        raise SystemExit(f"{profile_id} bundle differs from its build receipt")
    if source.get("repositoryDirty") is not False:
        raise SystemExit(f"{profile_id} receipt repositoryDirty must be false")

    return {
        "path": receipt_path.relative_to(ROOT).as_posix(),
        "sha256": sha256_file(receipt_path),
        "schemaVersion": 2,
        "kind": receipt["kind"],
        "bundleSha256": actual_bundle_sha256,
        "generatedGnArgsSha256": generated_args_sha256,
    }


def validate_shared_provenance(
    receipts: dict[str, dict[str, Any]],
    *,
    manifest_chromium: dict[str, Any],
    work_root: pathlib.Path,
    repository_commit: str,
) -> dict[str, Any]:
    chromium_pin = load_json(ROOT / "config/chromium.json")
    for key in ("milestone", "version", "commit"):
        if manifest_chromium.get(key) != chromium_pin.get(key):
            raise SystemExit(f"measurement Chromium {key} differs from the pin")
    depot_tools_pin = load_json(ROOT / "config/depot-tools.json")
    toolchain_pin = load_json(ROOT / "config/toolchain.json")
    canonical_gclient = ROOT / "config/gclient.py"
    expected_gclient_sha256 = sha256_file(canonical_gclient)
    chromium_root = work_root / "chromium"
    chromium_src = chromium_root / "src"
    deps_path = chromium_src / "DEPS"
    if not deps_path.is_file():
        raise SystemExit("current Chromium DEPS is missing")
    if git_output("git", "rev-parse", "HEAD", cwd=chromium_src) != chromium_pin.get(
        "commit"
    ):
        raise SystemExit("current Chromium checkout differs from the pin")
    depot_tools = work_root / "depot_tools"
    if git_output("git", "rev-parse", "HEAD", cwd=depot_tools) != depot_tools_pin.get(
        "commit"
    ):
        raise SystemExit("current depot_tools checkout differs from the pin")
    gclient_path = chromium_root / ".gclient"
    try:
        gclient_matches = gclient_path.read_bytes() == canonical_gclient.read_bytes()
    except OSError as error:
        raise SystemExit(
            f"cannot compare the current gclient config: {error}"
        ) from error
    if not gclient_matches:
        raise SystemExit("current Chromium .gclient differs from the canonical config")
    expected_deps_sha256 = sha256_file(deps_path)
    expected_build_tools = expected_build_tool_identities(toolchain_pin)
    expected_toolchain = expected_release_toolchain_identity(toolchain_pin)

    source_keys = (
        "repositoryCommit",
        "chromiumCommit",
        "chromiumVersion",
        "chromiumDepsSha256",
        "depotToolsCommit",
        "gclientConfigSha256",
        "expectedDependencyManifestSha256",
        "actualDependencyManifestSha256",
    )
    baseline_source: Optional[dict[str, Any]] = None
    baseline_toolchain: Optional[dict[str, Any]] = None
    baseline_build_tools: Optional[dict[str, Any]] = None
    for profile_id in EXPECTED_RECEIPT_BINDINGS:
        receipt = receipts[profile_id]
        source = require_object(receipt.get("source"), f"{profile_id}.receipt.source")
        build = require_object(receipt.get("build"), f"{profile_id}.receipt.build")
        toolchain = require_object(
            receipt.get("toolchain"), f"{profile_id}.receipt.toolchain"
        )
        if source.get("repositoryDirty") is not False:
            raise SystemExit(f"{profile_id} receipt repositoryDirty must be false")
        if source.get("repositoryCommit") != repository_commit:
            raise SystemExit(
                f"{profile_id} receipt was not built from the measured repository commit"
            )
        if source.get("chromiumCommit") != manifest_chromium.get("commit"):
            raise SystemExit(f"{profile_id} Chromium commit mismatch")
        if source.get("chromiumVersion") != manifest_chromium.get("version"):
            raise SystemExit(f"{profile_id} Chromium version mismatch")
        if source.get("chromiumDepsSha256") != expected_deps_sha256:
            raise SystemExit(f"{profile_id} Chromium DEPS hash mismatch")
        if source.get("depotToolsCommit") != depot_tools_pin.get("commit"):
            raise SystemExit(f"{profile_id} depot_tools commit mismatch")
        if source.get("gclientConfigSha256") != expected_gclient_sha256:
            raise SystemExit(f"{profile_id} gclient config hash mismatch")

        expected_revisions = require_object(
            source.get("expectedDependencyRevisions"),
            f"{profile_id}.source.expectedDependencyRevisions",
        )
        actual_revisions = require_object(
            source.get("actualDependencyRevisions"),
            f"{profile_id}.source.actualDependencyRevisions",
        )
        dependency_checks = (
            (
                "expected",
                expected_revisions,
                source.get("expectedDependencyCount"),
                source.get("expectedDependencyManifestSha256"),
            ),
            (
                "actual",
                actual_revisions,
                source.get("actualDependencyCount"),
                source.get("actualDependencyManifestSha256"),
            ),
        )
        for label, revisions, count, digest in dependency_checks:
            if type(count) is not int or count != len(revisions):
                raise SystemExit(f"{profile_id} {label} dependency count mismatch")
            require_sha256(
                digest,
                f"{profile_id}.source.{label}DependencyManifestSha256",
            )
            if digest != json_sha256(revisions):
                raise SystemExit(
                    f"{profile_id} {label} dependency manifest hash mismatch"
                )

        if toolchain != expected_toolchain:
            raise SystemExit(f"{profile_id} toolchain identity differs from the pins")
        build_tools = {
            key: require_object(build.get(key), f"{profile_id}.build.{key}")
            for key in BUILD_TOOL_KEYS
        }
        if build_tools != expected_build_tools:
            raise SystemExit(f"{profile_id} build-tool identities differ from the pins")

        if baseline_source is None:
            baseline_source = source
            baseline_toolchain = toolchain
            baseline_build_tools = build_tools
        else:
            assert baseline_toolchain is not None
            assert baseline_build_tools is not None
            for key in source_keys:
                if source.get(key) != baseline_source.get(key):
                    raise SystemExit(f"candidate receipts differ in source.{key}")
            if source.get("expectedDependencyRevisions") != baseline_source.get(
                "expectedDependencyRevisions"
            ):
                raise SystemExit("candidate expected dependency manifests differ")
            if source.get("actualDependencyRevisions") != baseline_source.get(
                "actualDependencyRevisions"
            ):
                raise SystemExit("candidate actual dependency manifests differ")
            if toolchain != baseline_toolchain:
                raise SystemExit("candidate pinned toolchain identities differ")
            if build_tools != baseline_build_tools:
                raise SystemExit("candidate build-tool identities differ")

        app = require_object(receipt.get("app"), f"{profile_id}.receipt.app")
        if profile_id == "upstream-control":
            if source.get("overlayApplied") is not False:
                raise SystemExit(
                    "upstream control receipt unexpectedly applied the overlay"
                )
            if "overlayFingerprint" in source or "checkoutDeltaFingerprint" in source:
                raise SystemExit(
                    "upstream control receipt carries Ahoi overlay fingerprints"
                )
        else:
            if source.get("overlayApplied") is not True:
                raise SystemExit(f"{profile_id} receipt did not verify the Ahoi overlay")
            if app.get("sourceCommit") != repository_commit:
                raise SystemExit(f"{profile_id} stamped source commit mismatch")
            if app.get("chromiumCommit") != manifest_chromium.get("commit"):
                raise SystemExit(f"{profile_id} stamped Chromium commit mismatch")
            if app.get("chromiumVersion") != manifest_chromium.get("version"):
                raise SystemExit(f"{profile_id} stamped Chromium version mismatch")
            require_sha256(
                source.get("overlayFingerprint"),
                f"{profile_id}.source.overlayFingerprint",
            )
            require_sha256(
                source.get("checkoutDeltaFingerprint"),
                f"{profile_id}.source.checkoutDeltaFingerprint",
            )

    full_source = receipts["ahoi-full-release"]["source"]
    lean_source = receipts["ahoi-release"]["source"]
    for key in ("overlayFingerprint", "checkoutDeltaFingerprint"):
        if full_source.get(key) != lean_source.get(key):
            raise SystemExit(f"full and lean Ahoi receipts differ in {key}")

    assert baseline_source is not None
    assert baseline_toolchain is not None
    assert baseline_build_tools is not None
    return {
        "repositoryCommit": repository_commit,
        "chromiumCommit": baseline_source["chromiumCommit"],
        "chromiumVersion": baseline_source["chromiumVersion"],
        "chromiumDepsSha256": baseline_source["chromiumDepsSha256"],
        "depotToolsCommit": baseline_source["depotToolsCommit"],
        "gclientConfigSha256": baseline_source["gclientConfigSha256"],
        "expectedDependencyManifestSha256": baseline_source[
            "expectedDependencyManifestSha256"
        ],
        "actualDependencyManifestSha256": baseline_source[
            "actualDependencyManifestSha256"
        ],
        "toolchainSha256": json_sha256(baseline_toolchain),
        "buildToolsSha256": json_sha256(baseline_build_tools),
        "overlayFingerprint": full_source["overlayFingerprint"],
        "checkoutDeltaFingerprint": full_source["checkoutDeltaFingerprint"],
    }


def format_percent(numerator: int, denominator: int) -> str:
    value = (Decimal(numerator) * Decimal(100)) / Decimal(denominator)
    return f"{value.quantize(Decimal('0.0001'), rounding=ROUND_HALF_UP):f}"


def format_basis_points(numerator: int, denominator: int) -> str:
    value = (Decimal(numerator) * Decimal(10000)) / Decimal(denominator)
    return f"{value.quantize(Decimal('0.01'), rounding=ROUND_HALF_UP):f}"


def compare_sizes(
    comparison: dict[str, Any], profile_results: dict[str, dict[str, Any]]
) -> dict[str, Any]:
    subject = profile_results[comparison["subjectProfile"]]["bundle"]["logicalBytes"]
    reference = profile_results[comparison["referenceProfile"]]["bundle"][
        "logicalBytes"
    ]
    if reference <= 0:
        raise SystemExit(f"comparison {comparison['id']} has an empty reference bundle")
    if comparison["operation"] == "overhead":
        numerator = subject - reference
        threshold = comparison.get("maximumBasisPoints")
        if type(threshold) is not int or threshold < 0:
            raise SystemExit(f"comparison {comparison['id']} has an invalid maximum")
        passed = numerator * 10000 <= threshold * reference
        threshold_payload = {"maximumBasisPoints": threshold}
    else:
        numerator = reference - subject
        threshold = comparison.get("minimumBasisPoints")
        if type(threshold) is not int or threshold < 0:
            raise SystemExit(f"comparison {comparison['id']} has an invalid minimum")
        passed = numerator * 10000 >= threshold * reference
        threshold_payload = {"minimumBasisPoints": threshold}
    return {
        "id": comparison["id"],
        "operation": comparison["operation"],
        "subjectProfile": comparison["subjectProfile"],
        "referenceProfile": comparison["referenceProfile"],
        "subjectBytes": subject,
        "referenceBytes": reference,
        "differenceBytes": numerator,
        "percent": format_percent(numerator, reference),
        "basisPoints": format_basis_points(numerator, reference),
        **threshold_payload,
        "status": "PASS" if passed else "PRODUCT_DECISION_REQUIRED",
        "testIds": comparison.get("testIds", []),
    }


def atomic_write_json(path: pathlib.Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = (
        json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")
    temporary: Optional[pathlib.Path] = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=f".{path.name}.", dir=path.parent, delete=False
        ) as handle:
            temporary = pathlib.Path(handle.name)
            handle.write(encoded)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=pathlib.Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()

    if args.manifest.is_symlink():
        raise SystemExit("measurement manifest must not be a symlink")
    manifest_path = args.manifest.resolve()
    try:
        manifest_relative = manifest_path.relative_to(ROOT)
    except ValueError as error:
        raise SystemExit("measurement manifest must be inside the repository") from error
    manifest = load_json(manifest_path)
    validate_manifest(manifest)
    matrix_path = resolve_beneath(
        ROOT, manifest.get("matrixPath"), "matrixPath", kind="file"
    )
    matrix = load_json(matrix_path)
    if matrix.get("schemaVersion") != 1:
        raise SystemExit("unsupported Lean Chromium component matrix schema")
    if matrix.get("chromium") != manifest.get("chromium"):
        raise SystemExit("measurement manifest Chromium pin differs from the matrix")
    matrix_profiles = {
        profile.get("id"): profile
        for profile in matrix.get("profiles", [])
        if isinstance(profile, dict) and isinstance(profile.get("id"), str)
    }
    for profile in manifest["profiles"]:
        matrix_profile = matrix_profiles.get(profile["id"])
        if matrix_profile is None:
            raise SystemExit(f"measurement profile is absent from matrix: {profile['id']}")
        expected_matrix_fields = {
            "argsPath": profile.get("argsPath"),
            "expectedGnArgsSha256": profile.get("expectedArgsSha256"),
        }
        for key, expected in expected_matrix_fields.items():
            if matrix_profile.get(key) != expected:
                raise SystemExit(
                    f"measurement profile {profile['id']} differs from matrix field {key}"
                )

    work_root = configured_work_root()
    repository_commit = git_output("git", "rev-parse", "HEAD", cwd=ROOT)
    if git_output("git", "status", "--porcelain", cwd=ROOT):
        raise SystemExit("measurement requires a clean Ahoi repository")
    profile_results: dict[str, dict[str, Any]] = {}
    bundles: dict[str, pathlib.Path] = {}
    receipts: dict[str, dict[str, Any]] = {}
    for profile in manifest["profiles"]:
        profile_id = profile["id"]
        args_path = resolve_beneath(
            ROOT,
            profile.get("argsPath"),
            f"profiles.{profile_id}.argsPath",
            kind="file",
        )
        args_sha256 = sha256_file(args_path)
        if args_sha256 != profile.get("expectedArgsSha256"):
            raise SystemExit(f"{profile_id} GN args differ from the manifest")
        bundle = resolve_beneath(
            work_root,
            profile.get("bundlePath"),
            f"profiles.{profile_id}.bundlePath",
            kind="directory",
        )
        generated_args_path = resolve_beneath(
            work_root,
            profile.get("generatedArgsPath"),
            f"profiles.{profile_id}.generatedArgsPath",
            kind="file",
        )
        receipt_path = resolve_beneath(
            ROOT,
            profile.get("receiptPath"),
            f"profiles.{profile_id}.receiptPath",
            kind="file",
        )
        receipt = load_json(receipt_path)
        identity = read_identity(bundle)
        validate_identity(profile, identity, args_sha256)
        receipt_summary = validate_profile_receipt(
            profile,
            receipt_path=receipt_path,
            receipt=receipt,
            bundle=bundle,
            identity=identity,
            args_path=args_path,
            args_sha256=args_sha256,
            generated_args_path=generated_args_path,
            work_root=work_root,
        )
        bundles[profile_id] = bundle
        receipts[profile_id] = receipt
        profile_results[profile_id] = {
            "id": profile_id,
            "argsPath": profile["argsPath"],
            "argsSha256": args_sha256,
            "generatedArgsPath": profile["generatedArgsPath"],
            "generatedArgsSha256": receipt_summary["generatedGnArgsSha256"],
            "bundlePath": profile["bundlePath"],
            "identity": identity,
            "receipt": receipt_summary,
        }

    shared_provenance = validate_shared_provenance(
        receipts,
        manifest_chromium=manifest["chromium"],
        work_root=work_root,
        repository_commit=repository_commit,
    )
    for profile_id, bundle in bundles.items():
        profile_results[profile_id]["bundle"] = inventory_bundle(bundle)

    comparison_results = [
        compare_sizes(comparison, profile_results)
        for comparison in manifest["comparisons"]
    ]
    gate_status = (
        "PASS"
        if all(result["status"] == "PASS" for result in comparison_results)
        else "PRODUCT_DECISION_REQUIRED"
    )
    report = {
        "schemaVersion": 1,
        "kind": "lean-chromium-bundle-measurement",
        "measurementId": manifest["measurementId"],
        "manifestPath": manifest_relative.as_posix(),
        "manifestSha256": sha256_file(manifest_path),
        "matrixPath": matrix_path.relative_to(ROOT).as_posix(),
        "matrixSha256": sha256_file(matrix_path),
        "chromium": manifest["chromium"],
        "byteBasis": manifest["byteBasis"],
        "treeDigest": manifest["treeDigest"],
        "sharedProvenance": shared_provenance,
        "profiles": [
            profile_results[profile["id"]] for profile in manifest["profiles"]
        ],
        "comparisons": comparison_results,
        "gate": {
            "passWhen": manifest["gate"]["passWhen"],
            "otherwise": manifest["gate"]["otherwise"],
            "status": gate_status,
            "testIds": manifest["gate"]["testIds"],
        },
    }
    output_path = args.output
    if output_path is None:
        output_path = ROOT.joinpath(
            *relative_path(manifest.get("outputPath"), "outputPath").parts
        )
    elif not output_path.is_absolute():
        output_path = ROOT / output_path
    atomic_write_json(output_path.resolve(), report)
    print(output_path.resolve())
    return 0 if gate_status == "PASS" else 2


if __name__ == "__main__":
    raise SystemExit(main())
