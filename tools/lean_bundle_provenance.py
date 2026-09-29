"""Build receipt and shared provenance checks of the Lean Chromium bundle
measurement (split from measure_lean_bundles.py, source line budget)."""

from __future__ import annotations

import pathlib
from typing import Any, Optional

from evidence import bundle_hash
from lean_bundle_common import (
    ROOT,
    EXPECTED_RECEIPT_BINDINGS,
    BUILD_TOOL_KEYS,
    sha256_file,
    json_sha256,
    git_output,
    require_object,
    require_sha256,
    logical_repo_path,
    provenance_logical_path,
    load_json,
    relative_path,
)


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
