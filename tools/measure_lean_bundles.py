#!/usr/bin/env python3
"""Measure Lean Chromium bundle baselines deterministically and fail closed."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import plistlib
import stat
import tempfile
from decimal import Decimal, ROUND_HALF_UP
from typing import Any, Optional


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


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


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
    profile_results: dict[str, dict[str, Any]] = {}
    for profile in manifest["profiles"]:
        args_path = resolve_beneath(
            ROOT,
            profile.get("argsPath"),
            f"profiles.{profile['id']}.argsPath",
            kind="file",
        )
        args_sha256 = sha256_file(args_path)
        if args_sha256 != profile.get("expectedArgsSha256"):
            raise SystemExit(f"{profile['id']} GN args differ from the manifest")
        bundle = resolve_beneath(
            work_root,
            profile.get("bundlePath"),
            f"profiles.{profile['id']}.bundlePath",
            kind="directory",
        )
        identity = read_identity(bundle)
        validate_identity(profile, identity, args_sha256)
        profile_results[profile["id"]] = {
            "id": profile["id"],
            "argsPath": profile["argsPath"],
            "argsSha256": args_sha256,
            "bundlePath": profile["bundlePath"],
            "identity": identity,
            "bundle": inventory_bundle(bundle),
        }

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
        "profiles": [
            profile_results[profile["id"]] for profile in manifest["profiles"]
        ],
        "comparisons": comparison_results,
        "gate": {
            "status": gate_status,
            "testIds": manifest.get("gate", {}).get("testIds", []),
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
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
