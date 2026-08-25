#!/usr/bin/env python3
"""Build-sign-notary-package-install release attestation chain."""

from __future__ import annotations

import pathlib
import zipfile
from typing import Callable, Optional

from .common import (
    ReleaseError,
    atomic_write_json,
    bundle_identity,
    canonical_json,
    load_json,
    require_sha256,
    require_string,
    resolved_child,
    safe_relative,
    sha256_bytes,
    sha256_file,
    tree_sha256,
)
from .crypto import signature_object, verify_signature_object
from .signing import verify_signed_app


RECEIPT_KINDS = {
    "buildProvenance": None,
    "signingReceipt": "signed-package-provenance",
    "notarizationReceipt": "notarization-receipt",
    "packageReceipt": "package-provenance",
    "installedReceipt": "installed-bundle-binding",
    "materialsReceipt": "release-materials",
}

IDENTITY_FIELDS = {
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
    "executable",
    "executableSha256",
}


def _identity_subset(value: object, name: str) -> dict:
    if not isinstance(value, dict):
        raise ReleaseError(f"{name} identity is missing")
    missing = IDENTITY_FIELDS - set(value)
    if missing:
        raise ReleaseError(f"{name} identity is missing: {', '.join(sorted(missing))}")
    return {field: value[field] for field in IDENTITY_FIELDS}


def _sibling_reference(path: pathlib.Path, root: pathlib.Path, name: str) -> dict:
    if path.resolve().parent != root.resolve():
        raise ReleaseError(f"{name} must be stored beside the release manifest")
    return {"file": path.name, "sha256": sha256_file(path)}


def _receipt(root: pathlib.Path, reference: object, name: str, kind: Optional[str]) -> dict:
    if not isinstance(reference, dict) or set(reference) != {"file", "sha256"}:
        raise ReleaseError(f"{name} reference has an invalid shape")
    path = resolved_child(root, reference["file"], f"{name}.file")
    expected_hash = require_sha256(reference["sha256"], f"{name}.sha256")
    if sha256_file(path) != expected_hash:
        raise ReleaseError(f"{name} SHA-256 mismatch")
    value = load_json(path)
    if not isinstance(value, dict):
        raise ReleaseError(f"{name} must contain an object")
    if kind is not None and value.get("kind") != kind:
        raise ReleaseError(f"{name} has the wrong receipt kind")
    return value


def validate_build_provenance(
    build: dict,
    product: dict,
    version: dict,
    chromium: dict,
    toolchain: dict,
    release_args_sha256: str,
) -> None:
    if build.get("schemaVersion") != 2 or build.get("kind") != "ahoi-release":
        raise ReleaseError("release requires schema-v2 ahoi-release build provenance")
    app = build.get("app")
    source = build.get("source")
    build_data = build.get("build")
    toolchain_evidence = build.get("toolchain")
    if not all(
        isinstance(item, dict)
        for item in (app, source, build_data, toolchain_evidence)
    ):
        raise ReleaseError("build provenance sections are missing")
    expected_app = {
        "bundleName": product["name"],
        "bundleIdentifier": product["bundleId"],
        "marketingVersion": version["marketingVersion"],
        "buildNumber": version["buildNumber"],
        "productVersion": version["displayVersion"],
        "channel": version["channel"],
        "chromiumVersion": chromium["version"],
        "chromiumCommit": chromium["commit"],
        "buildProfile": "release",
    }
    for key, expected in expected_app.items():
        if app.get(key) != expected:
            raise ReleaseError(f"build provenance app.{key} does not match release config")
    if source.get("repositoryDirty") is not False:
        raise ReleaseError("build provenance does not prove a clean source repository")
    if source.get("repositoryCommit") != app.get("sourceCommit"):
        raise ReleaseError("source commit stamp differs from build repository commit")
    if source.get("chromiumCommit") != chromium["commit"]:
        raise ReleaseError("build provenance Chromium commit is not pinned")
    expected_gn_hash = require_sha256(
        release_args_sha256, "configured release GN args SHA-256"
    )
    if (
        app.get("gnArgsSha256") != expected_gn_hash
        or build_data.get("gnArgsSha256") != expected_gn_hash
    ):
        raise ReleaseError("app/build GN hash differs from configured release arguments")
    if build_data.get("generatedGnArgsSha256") != expected_gn_hash:
        raise ReleaseError("generated GN arguments differ from configured release arguments")
    if toolchain_evidence.get("mode") != "pinned-reference":
        raise ReleaseError("release provenance does not use the pinned-reference toolchain")
    if toolchain_evidence.get("pins") != toolchain:
        raise ReleaseError("release provenance toolchain pins differ from config")
    pins = toolchain.get("buildTools")
    if not isinstance(pins, dict):
        raise ReleaseError("configured build tool pins are missing")
    expected_tools = {
        "gn": {
            "version": pins["gnVersionOutput"],
            "binarySha256": pins["gnBinarySha256"],
        },
        "ninja": {
            "version": pins["ninjaVersionOutput"],
            "binarySha256": pins["ninjaBinarySha256"],
        },
        "siso": {
            "enabled": False,
            "configuredRevision": pins["sisoRevision"],
        },
        "clang": {
            "version": pins["clangVersionLine"],
            "package": pins["clangPackage"],
            "archiveSha256": pins["clangArchiveSha256"],
            "binarySha256": pins["clangBinarySha256"],
        },
        "lld": {
            "version": pins["lldVersionLine"],
            "binarySha256": pins["lldBinarySha256"],
            "driver": "ld64.lld -> lld",
        },
    }
    for tool, expected in expected_tools.items():
        if build_data.get(tool) != expected:
            raise ReleaseError(f"release provenance {tool} identity differs from pins")
    require_sha256(app.get("bundleSha256"), "build provenance app.bundleSha256")
    require_sha256(app.get("binarySha256"), "build provenance app.binarySha256")


def create_installed_receipt(
    app: pathlib.Path,
    *,
    signing_receipt_path: pathlib.Path,
    notarization_receipt_path: pathlib.Path,
    policy_path: pathlib.Path,
    output: pathlib.Path,
    required_install_path: pathlib.Path = pathlib.Path("/Applications/AhoiBrowser.app"),
) -> dict:
    if app.resolve() != required_install_path.resolve():
        raise ReleaseError(f"installed release proof requires {required_install_path}")
    signing = load_json(signing_receipt_path)
    notary = load_json(notarization_receipt_path)
    if signing.get("kind") != "signed-package-provenance":
        raise ReleaseError("signing receipt kind is invalid")
    if notary.get("kind") != "notarization-receipt":
        raise ReleaseError("notarization receipt kind is invalid")
    verification = verify_signed_app(
        app,
        expected_team=signing["signing"]["teamIdentifier"],
        expected_authority=signing["signing"]["authority"],
        policy_path=policy_path,
        require_notarization=True,
    )
    installed_hash = tree_sha256(app)
    expected_hash = notary["bundle"].get("postStapleTreeSha256")
    if installed_hash != expected_hash:
        raise ReleaseError("installed bundle is not the exact notarized/stapled bundle")
    receipt = {
        "schemaVersion": 1,
        "kind": "installed-bundle-binding",
        "installPath": str(required_install_path),
        "bundle": verification["bundle"],
        "bundleTreeSha256": installed_hash,
        "signingReceipt": {
            "file": signing_receipt_path.name,
            "sha256": sha256_file(signing_receipt_path),
        },
        "notarizationReceipt": {
            "file": notarization_receipt_path.name,
            "sha256": sha256_file(notarization_receipt_path),
        },
        "verification": {
            "identity": True,
            "nestedCode": True,
            "hardenedRuntime": True,
            "gatekeeper": True,
            "stapling": True,
        },
    }
    atomic_write_json(output, receipt)
    return receipt


def _validate_material_files(root: pathlib.Path, materials: dict) -> None:
    fields = (
        "componentInventory",
        "sbom",
        "licenseArchive",
        "thirdPartyNotices",
        "correspondingSourceOffer",
    )
    resolved = {}
    for field in fields:
        reference = materials.get(field)
        if not isinstance(reference, dict):
            raise ReleaseError(f"materials receipt is missing {field}")
        path = resolved_child(root, reference.get("file"), f"materials.{field}.file")
        if sha256_file(path) != require_sha256(
            reference.get("sha256"), f"materials.{field}.sha256"
        ):
            raise ReleaseError(f"materials {field} SHA-256 mismatch")
        resolved[field] = path
    license_components = materials.get("licenses")
    if not isinstance(license_components, list):
        raise ReleaseError("materials licenses must be an array")
    expected_license_files = {}
    for component in license_components:
        if not isinstance(component, dict) or not isinstance(component.get("files"), list):
            raise ReleaseError("materials component license evidence is malformed")
        for item in component["files"]:
            if not isinstance(item, dict):
                raise ReleaseError("materials license file evidence is malformed")
            name = safe_relative(item.get("file"), "materials license file").as_posix()
            expected_license_files[name] = require_sha256(
                item.get("sha256"), "materials license file SHA-256"
            )
    if not expected_license_files:
        raise ReleaseError("materials receipt contains no component license evidence")
    try:
        archive = zipfile.ZipFile(resolved["licenseArchive"])
    except zipfile.BadZipFile as error:
        raise ReleaseError("component license archive is invalid") from error
    with archive:
        actual_names = {item.filename for item in archive.infolist() if not item.is_dir()}
        if actual_names != set(expected_license_files):
            raise ReleaseError("component license archive inventory mismatch")
        for name, expected_hash in expected_license_files.items():
            if sha256_bytes(archive.read(name)) != expected_hash:
                raise ReleaseError(f"component license archive hash mismatch: {name}")


def _validate_manifest_summary(
    manifest: dict,
    build: dict,
    product: dict,
    version: dict,
    chromium: dict,
    release_args_sha256: str,
) -> None:
    expected_release = {
        "name": product["name"],
        "bundleIdentifier": product["bundleId"],
        "marketingVersion": version["marketingVersion"],
        "buildNumber": version["buildNumber"],
        "productVersion": version["displayVersion"],
        "channel": version["channel"],
    }
    if manifest.get("release") != expected_release:
        raise ReleaseError("release manifest summary differs from release config")
    expected_source = {
        "repositoryCommit": build["source"]["repositoryCommit"],
        "chromiumVersion": chromium["version"],
        "chromiumCommit": chromium["commit"],
        "gnArgsSha256": release_args_sha256,
        "buildProvenanceSha256": manifest["evidence"]["buildProvenance"]["sha256"],
    }
    if manifest.get("source") != expected_source:
        raise ReleaseError("release manifest source summary differs from provenance")


def _validate_bindings(
    root: pathlib.Path,
    manifest: dict,
    receipts: dict[str, dict],
) -> None:
    build = receipts["buildProvenance"]
    signing = receipts["signingReceipt"]
    notary = receipts["notarizationReceipt"]
    package = receipts["packageReceipt"]
    installed = receipts["installedReceipt"]
    materials = receipts["materialsReceipt"]
    for name, receipt in receipts.items():
        if name != "buildProvenance" and receipt.get("schemaVersion") != 1:
            raise ReleaseError(f"{name} schema is unsupported")
    signing_policy = signing.get("signing")
    signing_verification = signing.get("verification")
    if not isinstance(signing_policy, dict) or not all(
        signing_policy.get(field) is True
        for field in ("hardenedRuntime", "trustedTimestamp")
    ):
        raise ReleaseError("signing receipt lacks Hardened Runtime/timestamp proof")
    if (
        not isinstance(signing_verification, dict)
        or signing_verification.get("deepStrict") is not True
    ):
        raise ReleaseError("signing receipt lacks deep strict verification")
    if not isinstance(
        signing_verification.get("nestedCode"), list
    ) or not signing_verification["nestedCode"]:
        raise ReleaseError("signing receipt has no verified nested code inventory")
    signing_order = signing_policy.get("order")
    if (
        not isinstance(signing_order, list)
        or not signing_order
        or signing_order[-1].get("path") != "."
    ):
        raise ReleaseError("signing receipt does not prove outer-app-last order")
    notary_bundle = notary.get("bundle")
    if (
        not isinstance(notary_bundle, dict)
        or notary_bundle.get("staplerValidated") is not True
        or notary_bundle.get("gatekeeperAccepted") is not True
    ):
        raise ReleaseError("notarization receipt lacks app stapling/Gatekeeper proof")
    notary_dmg = notary.get("dmg")
    if not isinstance(notary_dmg, dict) or notary_dmg.get("staplerValidated") is not True:
        raise ReleaseError("notarization receipt lacks DMG stapling proof")
    submissions = notary.get("submissions")
    if not isinstance(submissions, list) or len(submissions) != 2:
        raise ReleaseError("notarization receipt must bind app and DMG submissions")
    for index, submission in enumerate(submissions):
        if not isinstance(submission, dict) or submission.get("status") != "Accepted":
            raise ReleaseError("notarization submission was not accepted")
        subject = resolved_child(
            root, submission.get("subject"), f"submissions[{index}].subject"
        )
        log_path = resolved_child(
            root, submission.get("log"), f"submissions[{index}].log"
        )
        if sha256_file(subject) != require_sha256(
            submission.get("subjectSha256"), "notary subject hash"
        ):
            raise ReleaseError("notarization submission artifact SHA-256 mismatch")
        if sha256_file(log_path) != require_sha256(
            submission.get("logSha256"), "notary log hash"
        ):
            raise ReleaseError("notarization response log SHA-256 mismatch")
        log = load_json(log_path)
        if (
            not isinstance(log, dict)
            or log.get("id") != submission.get("submissionId")
            or log.get("status") != "Accepted"
        ):
            raise ReleaseError("notarization response log does not match its receipt")
    if submissions[1].get("subjectSha256") != notary_dmg.get("preStapleSha256"):
        raise ReleaseError("notarized DMG input differs from the pre-staple DMG")
    pre_staple = require_sha256(
        notary_bundle.get("preStapleTreeSha256"), "notary pre-staple app hash"
    )
    post_staple = require_sha256(
        notary_bundle.get("postStapleTreeSha256"), "notary post-staple app hash"
    )
    if pre_staple == post_staple:
        raise ReleaseError("notarization receipt does not prove app stapling mutation")
    pre_dmg = require_sha256(notary_dmg.get("preStapleSha256"), "notary pre-staple DMG")
    post_dmg = require_sha256(notary_dmg.get("postStapleSha256"), "notary post-staple DMG")
    if pre_dmg == post_dmg:
        raise ReleaseError("notarization receipt does not prove DMG stapling mutation")
    if signing.get("buildProvenance", {}).get("sha256") != manifest["evidence"][
        "buildProvenance"
    ]["sha256"]:
        raise ReleaseError("signing receipt is not bound to build provenance")
    if signing.get("buildProvenance", {}).get("file") != manifest["evidence"][
        "buildProvenance"
    ]["file"]:
        raise ReleaseError("signing receipt build-provenance filename differs")
    if signing.get("unsignedBundle", {}).get(
        "buildProvenanceBundleSha256"
    ) != build.get("app", {}).get("bundleSha256"):
        raise ReleaseError("unsigned bundle hash is not bound to build provenance")
    unsigned_identity = _identity_subset(signing.get("unsignedBundle"), "unsigned")
    signed_identity = _identity_subset(signing.get("signedBundle"), "signed")
    notary_identity = _identity_subset(notary_bundle.get("identity"), "notarized")
    installed_identity = _identity_subset(installed.get("bundle"), "installed")
    require_sha256(
        signing.get("unsignedBundle", {}).get("bundleTreeSha256"),
        "unsigned bundle tree hash",
    )
    if unsigned_identity["executable"] != build.get("app", {}).get("bundleName"):
        raise ReleaseError("unsigned app executable name differs from build product")
    metadata_fields = IDENTITY_FIELDS - {"executableSha256"}
    if any(unsigned_identity[field] != signed_identity[field] for field in metadata_fields):
        raise ReleaseError("signing changed immutable app identity metadata")
    if signed_identity != notary_identity:
        raise ReleaseError("notarized app identity differs from the signed app")
    if notary_identity != installed_identity:
        raise ReleaseError("installed app identity differs from the notarized app")
    build_identity = {
        "name": build["app"].get("bundleName"),
        "identifier": build["app"].get("bundleIdentifier"),
        "marketingVersion": build["app"].get("marketingVersion"),
        "buildNumber": build["app"].get("buildNumber"),
        "productVersion": build["app"].get("productVersion"),
        "channel": build["app"].get("channel"),
        "sourceCommit": build["app"].get("sourceCommit"),
        "chromiumVersion": build["app"].get("chromiumVersion"),
        "chromiumCommit": build["app"].get("chromiumCommit"),
        "gnArgsSha256": build["app"].get("gnArgsSha256"),
        "buildProfile": build["app"].get("buildProfile"),
        "executableSha256": build["app"].get("binarySha256"),
    }
    for field, expected in build_identity.items():
        if unsigned_identity[field] != expected:
            raise ReleaseError(f"unsigned app {field} differs from build provenance")
    signed_tree = signing.get("signedBundle", {}).get("bundleTreeSha256")
    if signed_tree != notary.get("bundle", {}).get("preStapleTreeSha256"):
        raise ReleaseError("notarization input is not the signed bundle")
    stapled_tree = notary.get("bundle", {}).get("postStapleTreeSha256")
    if stapled_tree != package.get("stapledBundleTreeSha256"):
        raise ReleaseError("package does not contain the stapled bundle")
    if stapled_tree != installed.get("bundleTreeSha256"):
        raise ReleaseError("installed bundle hash differs from the stapled package")
    if notary_dmg.get("postStapleSha256") != package.get("artifacts", {}).get(
        "dmg", {}
    ).get("sha256"):
        raise ReleaseError("package DMG differs from the notarized/stapled DMG")
    if installed.get("installPath") != "/Applications/AhoiBrowser.app":
        raise ReleaseError("installed receipt does not target /Applications/AhoiBrowser.app")
    for receipt_name in ("signingReceipt", "notarizationReceipt"):
        if installed.get(receipt_name, {}).get("sha256") != manifest["evidence"][
            receipt_name
        ]["sha256"]:
            raise ReleaseError(f"installed receipt is not bound to {receipt_name}")
    verification = installed.get("verification")
    verification_fields = {
        "identity",
        "nestedCode",
        "hardenedRuntime",
        "gatekeeper",
        "stapling",
    }
    if (
        not isinstance(verification, dict)
        or set(verification) != verification_fields
        or not all(verification.values())
    ):
        raise ReleaseError(
            "installed receipt does not contain all required verification PASS values"
        )
    for artifact_name in ("zip", "dmg"):
        package_reference = package.get("artifacts", {}).get(artifact_name)
        manifest_reference = manifest.get("artifacts", {}).get(artifact_name)
        if package_reference != manifest_reference:
            raise ReleaseError(f"manifest {artifact_name} differs from package receipt")
        artifact_path = resolved_child(
            root, package_reference.get("file"), f"artifacts.{artifact_name}.file"
        )
        if sha256_file(artifact_path) != package_reference.get("sha256"):
            raise ReleaseError(f"release {artifact_name} SHA-256 mismatch")
        if artifact_path.stat().st_size != package_reference.get("size"):
            raise ReleaseError(f"release {artifact_name} size mismatch")
    _validate_material_files(root, materials)


def assemble_manifest(
    *,
    root: pathlib.Path,
    build_provenance_path: pathlib.Path,
    signing_receipt_path: pathlib.Path,
    notarization_receipt_path: pathlib.Path,
    package_receipt_path: pathlib.Path,
    installed_receipt_path: pathlib.Path,
    materials_receipt_path: pathlib.Path,
    product: dict,
    version: dict,
    chromium: dict,
    toolchain: dict,
    release_args_sha256: str,
    private_key: pathlib.Path,
    public_key: pathlib.Path,
    trusted_key_ids: set,
    output: pathlib.Path,
) -> dict:
    if output.resolve().parent != root.resolve():
        raise ReleaseError("release manifest output must be inside its evidence root")
    paths = {
        "buildProvenance": build_provenance_path,
        "signingReceipt": signing_receipt_path,
        "notarizationReceipt": notarization_receipt_path,
        "packageReceipt": package_receipt_path,
        "installedReceipt": installed_receipt_path,
        "materialsReceipt": materials_receipt_path,
    }
    evidence = {
        name: _sibling_reference(path, root, name) for name, path in paths.items()
    }
    receipts = {
        name: _receipt(root, evidence[name], name, RECEIPT_KINDS[name])
        for name in paths
    }
    validate_build_provenance(
        receipts["buildProvenance"],
        product,
        version,
        chromium,
        toolchain,
        release_args_sha256,
    )
    package_artifacts = receipts["packageReceipt"].get("artifacts")
    if not isinstance(package_artifacts, dict):
        raise ReleaseError("package receipt has no artifacts")
    unsigned_manifest = {
        "schemaVersion": 1,
        "kind": "ahoi-release-manifest",
        "release": {
            "name": product["name"],
            "bundleIdentifier": product["bundleId"],
            "marketingVersion": version["marketingVersion"],
            "buildNumber": version["buildNumber"],
            "productVersion": version["displayVersion"],
            "channel": version["channel"],
        },
        "source": {
            "repositoryCommit": receipts["buildProvenance"]["source"]["repositoryCommit"],
            "chromiumVersion": chromium["version"],
            "chromiumCommit": chromium["commit"],
            "gnArgsSha256": receipts["buildProvenance"]["build"]["gnArgsSha256"],
            "buildProvenanceSha256": evidence["buildProvenance"]["sha256"],
        },
        "artifacts": package_artifacts,
        "evidence": evidence,
    }
    _validate_bindings(root, unsigned_manifest, receipts)
    _validate_manifest_summary(
        unsigned_manifest,
        receipts["buildProvenance"],
        product,
        version,
        chromium,
        release_args_sha256,
    )
    signature = signature_object(
        canonical_json(unsigned_manifest), private_key, public_key
    )
    if signature["keyId"] not in trusted_key_ids:
        raise ReleaseError("release manifest key is not trusted by release policy")
    manifest = dict(unsigned_manifest)
    manifest["signature"] = signature
    atomic_write_json(output, manifest)
    return manifest


def validate_manifest(
    manifest_path: pathlib.Path,
    *,
    public_key: pathlib.Path,
    trusted_key_ids: set,
    product: dict,
    version: dict,
    chromium: dict,
    toolchain: dict,
    release_args_sha256: str,
    installed_app: Optional[pathlib.Path] = None,
    policy_path: Optional[pathlib.Path] = None,
    live_verifier: Optional[Callable[[pathlib.Path, dict], None]] = None,
) -> dict:
    manifest = load_json(manifest_path)
    if not isinstance(manifest, dict) or set(manifest) != {
        "schemaVersion",
        "kind",
        "release",
        "source",
        "artifacts",
        "evidence",
        "signature",
    }:
        raise ReleaseError("release manifest has an invalid shape")
    if manifest["schemaVersion"] != 1 or manifest["kind"] != "ahoi-release-manifest":
        raise ReleaseError("release manifest schema/kind is unsupported")
    unsigned = dict(manifest)
    signature = unsigned.pop("signature")
    verify_signature_object(
        canonical_json(unsigned), signature, public_key, trusted_key_ids
    )
    root = manifest_path.resolve().parent
    evidence = manifest["evidence"]
    if not isinstance(evidence, dict) or set(evidence) != set(RECEIPT_KINDS):
        raise ReleaseError("release manifest evidence set is incomplete")
    receipts = {
        name: _receipt(root, evidence[name], name, kind)
        for name, kind in RECEIPT_KINDS.items()
    }
    validate_build_provenance(
        receipts["buildProvenance"],
        product,
        version,
        chromium,
        toolchain,
        release_args_sha256,
    )
    _validate_bindings(root, manifest, receipts)
    _validate_manifest_summary(
        manifest,
        receipts["buildProvenance"],
        product,
        version,
        chromium,
        release_args_sha256,
    )
    if installed_app is not None:
        installed = receipts["installedReceipt"]
        if tree_sha256(installed_app) != installed["bundleTreeSha256"]:
            raise ReleaseError("live installed bundle differs from release manifest")
        if live_verifier is not None:
            live_verifier(installed_app, receipts["signingReceipt"])
        else:
            if policy_path is None:
                raise ReleaseError("live validation requires the entitlement policy")
            signing = receipts["signingReceipt"]["signing"]
            verify_signed_app(
                installed_app,
                expected_team=signing["teamIdentifier"],
                expected_authority=signing["authority"],
                policy_path=policy_path,
                require_notarization=True,
            )
        identity = bundle_identity(installed_app)
        if identity != receipts["installedReceipt"].get("bundle"):
            raise ReleaseError("live installed bundle identity/hash payload differs")
    return manifest
