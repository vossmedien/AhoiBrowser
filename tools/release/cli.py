#!/usr/bin/env python3
"""Production command line for the fail-closed AhoiBrowser release chain."""

from __future__ import annotations

import argparse
import os
import pathlib
import sys
from typing import Optional

from .assets import create_release_assets
from .chain import assemble_manifest, create_installed_receipt, validate_manifest
from .common import (
    ReleaseError,
    load_json,
    require_sha256,
    sha256_file,
    tree_sha256,
)
from .installation import install_release_app
from .materials import create_materials
from .packaging import notarize_and_package
from .signing import sign_app, verify_signed_app
from .sparkle import (
    PINNED_ARCHIVE_SHA256,
    PINNED_COMMIT,
    PINNED_VERSION,
    generate_appcast,
    validate_artifact_base_url,
    validate_component_inventory,
    validate_https_url,
    validate_public_ed_key,
)


ROOT = pathlib.Path(__file__).resolve().parents[2]
POLICY_PATH = ROOT / "config/release-policy.json"
ENTITLEMENTS_PATH = ROOT / "config/macos-entitlements.json"


def _config(name: str) -> dict:
    value = load_json(ROOT / "config" / name)
    if not isinstance(value, dict):
        raise ReleaseError(f"config/{name} must contain an object")
    return value


def _policy() -> dict:
    policy = load_json(POLICY_PATH)
    if not isinstance(policy, dict) or policy.get("schemaVersion") != 1:
        raise ReleaseError("release policy schema is unsupported")
    product = _config("product.json")
    expected_bundle = {
        "name": product["name"],
        "identifier": product["bundleId"],
        "architecture": "arm64",
        "buildProfile": "release",
    }
    if policy.get("bundle") != expected_bundle:
        raise ReleaseError("release bundle policy differs from product config")
    signing = policy.get("signing", {})
    if (
        signing.get("identityPrefix") != "Developer ID Application: "
        or signing.get("hardenedRuntimeRequired") is not True
        or signing.get("trustedTimestampRequired") is not True
    ):
        raise ReleaseError("release signing policy weakens required protections")
    installation = policy.get("installation", {})
    if (
        installation.get("target") != "/Applications/AhoiBrowser.app"
        or installation.get("atomicReplacement") is not True
        or installation.get("automaticRollbackOnVerificationFailure") is not True
    ):
        raise ReleaseError("release installation policy weakens atomic recovery")
    if policy.get("manifestSigning", {}).get("algorithm") != "Ed25519":
        raise ReleaseError("release manifest policy must use Ed25519")
    if set(policy.get("requiredMaterials", [])) != {
        "SPDX-2.3 SBOM",
        "Third-Party Notices",
        "corresponding source offer",
        "component license evidence",
        "separate debug-symbol archive",
        "SHA256SUMS",
    }:
        raise ReleaseError("release policy does not require the complete material set")
    reviews = policy.get("reviews")
    if reviews != {
        "policy": "config/release-review-policy.json",
        "externalGate": "trademark-and-distribution-policy",
    }:
        raise ReleaseError("release review policy binding is invalid")
    review_policy = _config("release-review-policy.json")
    required_reviews = review_policy.get("requiredReviews")
    if not isinstance(required_reviews, list) or any(
        not isinstance(item, dict) for item in required_reviews
    ):
        raise ReleaseError("release review inventory is invalid")
    if (
        review_policy.get("schemaVersion") != 1
        or review_policy.get("externalGate") != reviews["externalGate"]
        or {item.get("id") for item in required_reviews}
        != {"third-party-license-and-source", "trademark-and-branding"}
    ):
        raise ReleaseError("release review policy is incomplete")
    if not isinstance(review_policy.get("releasePassEnabled"), bool):
        raise ReleaseError("release review PASS switch must be boolean")
    reviewer_keys = review_policy.get("trustedReviewerKeyIds")
    if not isinstance(reviewer_keys, list):
        raise ReleaseError("trusted reviewer key IDs must be an array")
    for key_id in reviewer_keys:
        require_sha256(key_id, "trusted reviewer key ID")
    if len(reviewer_keys) != len(set(reviewer_keys)):
        raise ReleaseError("trusted reviewer key IDs contain duplicates")
    if review_policy.get("releasePassEnabled") is True and not reviewer_keys:
        raise ReleaseError("release review PASS requires a trusted reviewer key")
    updates = policy.get("updates", {})
    if (
        updates.get("implementation") != "Sparkle"
        or updates.get("frameworkVersion") != PINNED_VERSION
        or updates.get("frameworkCommit") != PINNED_COMMIT
        or updates.get("archiveSha256") != PINNED_ARCHIVE_SHA256
        or updates.get("algorithm") != "Ed25519"
        or updates.get("httpsOnly") is not True
        or updates.get("signedFeedRequired") is not True
        or updates.get("verifyBeforeExtraction") is not True
        or updates.get("systemProfileSubmission") is not False
        or updates.get("downgradePrevention")
        != "signed-appcast-version-policy"
        or set(updates.get("channels", {})) != {"nightly", "beta", "stable"}
    ):
        raise ReleaseError("update policy weakens channel/transport/rollback trust")
    manifest_ids = policy.get("manifestSigning", {}).get("trustedKeyIds", [])
    if not isinstance(manifest_ids, list):
        raise ReleaseError("release manifest key trust list must be an array")
    for key_id in manifest_ids:
        require_sha256(key_id, "configured release key ID")
    if len(manifest_ids) != len(set(manifest_ids)):
        raise ReleaseError("release key trust lists contain duplicates")
    for channel in ("nightly", "beta", "stable"):
        configuration = updates["channels"][channel]
        if not isinstance(configuration, dict) or set(configuration) != {
            "artifactBaseUrl",
            "feedUrl",
            "publicEdKey",
        }:
            raise ReleaseError(f"Sparkle {channel} configuration has an invalid shape")
        feed_url = configuration["feedUrl"]
        artifact_base_url = configuration["artifactBaseUrl"]
        public_key = configuration["publicEdKey"]
        if not all(
            isinstance(value, str)
            for value in (feed_url, artifact_base_url, public_key)
        ):
            raise ReleaseError(
                f"Sparkle {channel} feed/artifact/key values must be strings"
            )
        configured_values = (feed_url, artifact_base_url, public_key)
        if any(configured_values) and not all(configured_values):
            raise ReleaseError(
                f"Sparkle {channel} feed/artifact/key must be configured together"
            )
        if public_key:
            validate_https_url(feed_url, f"Sparkle {channel} feed URL")
            validate_artifact_base_url(
                artifact_base_url, f"Sparkle {channel} artifact base URL"
            )
            validate_public_ed_key(public_key)
    return policy


def _trusted_manifest_keys(policy: dict) -> set:
    keys = policy.get("manifestSigning", {}).get("trustedKeyIds")
    if not isinstance(keys, list) or not keys:
        raise ReleaseError(
            "release manifest key trust is not configured; keep release fail-closed"
        )
    return {require_sha256(key, "manifest trusted key ID") for key in keys}


def _required_environment(name: str) -> str:
    value = os.environ.get(name)
    if not value:
        raise ReleaseError(f"{name} must be supplied by secure release infrastructure")
    return value


def _path(value: str) -> pathlib.Path:
    return pathlib.Path(value).resolve()


def _require_common_parent(paths: list[pathlib.Path], purpose: str) -> pathlib.Path:
    parents = {path.resolve().parent for path in paths}
    if len(parents) != 1:
        raise ReleaseError(f"{purpose} files must share one evidence directory")
    return next(iter(parents))


def _sign(args: argparse.Namespace) -> None:
    _policy()
    _require_common_parent(
        [_path(args.build_provenance), _path(args.output)],
        "build/signing provenance",
    )
    sign_app(
        _path(args.app),
        identity=_required_environment("AHOI_CODESIGN_IDENTITY"),
        team_id=_required_environment("AHOI_TEAM_ID"),
        policy_path=ENTITLEMENTS_PATH,
        build_provenance_path=_path(args.build_provenance),
        output=_path(args.output),
    )


def _notarize(args: argparse.Namespace) -> None:
    _policy()
    signing_receipt_path = _path(args.signing_receipt)
    paths = [
        signing_receipt_path,
        _path(args.zip),
        _path(args.dmg),
        _path(args.notary_receipt),
        _path(args.package_receipt),
    ]
    _require_common_parent(paths, "notarization/package")
    signing_receipt = load_json(signing_receipt_path)
    if signing_receipt.get("kind") != "signed-package-provenance":
        raise ReleaseError("notarization requires a signed-package receipt")
    signing_policy = signing_receipt.get("signing", {})
    identity = _required_environment("AHOI_CODESIGN_IDENTITY")
    team_id = _required_environment("AHOI_TEAM_ID")
    if (
        signing_policy.get("authority") != identity
        or signing_policy.get("teamIdentifier") != team_id
    ):
        raise ReleaseError("signing receipt identity differs from release environment")
    app = _path(args.app)
    if tree_sha256(app) != signing_receipt.get("signedBundle", {}).get(
        "bundleTreeSha256"
    ):
        raise ReleaseError("notarization app differs from signed-package receipt")
    verify_signed_app(
        app,
        expected_team=team_id,
        expected_authority=identity,
        policy_path=ENTITLEMENTS_PATH,
    )
    notarize_and_package(
        app,
        keychain_profile=_required_environment("AHOI_NOTARY_KEYCHAIN_PROFILE"),
        zip_output=_path(args.zip),
        dmg_output=_path(args.dmg),
        volume_name=args.volume_name,
        notary_receipt_output=_path(args.notary_receipt),
        package_receipt_output=_path(args.package_receipt),
    )


def _materials(args: argparse.Namespace) -> None:
    _policy()
    paths = [
        _path(args.inventory),
        _path(args.source_offer),
        _path(args.notices),
        _path(args.sbom),
        _path(args.license_archive),
        _path(args.receipt),
    ]
    _require_common_parent(paths, "release materials")
    validate_component_inventory(
        _path(args.inventory), ROOT / "config/third-party-pins.json"
    )
    create_materials(
        _path(args.inventory),
        source_root=ROOT,
        source_offer=_path(args.source_offer),
        notices=_path(args.notices),
        sbom_output=_path(args.sbom),
        license_archive_output=_path(args.license_archive),
        receipt_output=_path(args.receipt),
        document_namespace=args.namespace,
        created_at=args.created_at,
    )


def _bind_installed(args: argparse.Namespace) -> None:
    policy = _policy()
    _require_common_parent(
        [
            _path(args.signing_receipt),
            _path(args.notary_receipt),
            _path(args.output),
        ],
        "installed binding",
    )
    create_installed_receipt(
        _path(args.app),
        signing_receipt_path=_path(args.signing_receipt),
        notarization_receipt_path=_path(args.notary_receipt),
        policy_path=ENTITLEMENTS_PATH,
        output=_path(args.output),
        required_install_path=pathlib.Path(policy["installation"]["target"]),
    )


def _install(args: argparse.Namespace) -> None:
    policy = _policy()
    signing_receipt = _path(args.signing_receipt)
    notary_receipt = _path(args.notary_receipt)
    output = _path(args.output)
    _require_common_parent(
        [signing_receipt, notary_receipt, output],
        "atomic installation evidence",
    )
    candidate = pathlib.Path(args.app)
    if not candidate.is_absolute():
        raise ReleaseError("install --app must be an absolute canonical path")
    install_release_app(
        candidate,
        signing_receipt_path=signing_receipt,
        notarization_receipt_path=notary_receipt,
        policy_path=ENTITLEMENTS_PATH,
        output=output,
        expected_bundle=policy["bundle"],
        required_install_path=pathlib.Path(policy["installation"]["target"]),
    )


def _release_assets(args: argparse.Namespace) -> None:
    _policy()
    paths = [
        _path(args.package_receipt),
        _path(args.materials_receipt),
        _path(args.symbol_archive),
        _path(args.checksums),
        _path(args.receipt),
    ]
    _require_common_parent(paths, "release assets")
    create_release_assets(
        symbols_root=_path(args.symbols_root),
        package_receipt_path=_path(args.package_receipt),
        materials_receipt_path=_path(args.materials_receipt),
        symbol_archive_output=_path(args.symbol_archive),
        checksums_output=_path(args.checksums),
        receipt_output=_path(args.receipt),
    )


def _assemble(args: argparse.Namespace) -> None:
    policy = _policy()
    assemble_manifest(
        root=_path(args.root),
        build_provenance_path=_path(args.build_provenance),
        signing_receipt_path=_path(args.signing_receipt),
        notarization_receipt_path=_path(args.notary_receipt),
        package_receipt_path=_path(args.package_receipt),
        installed_receipt_path=_path(args.installed_receipt),
        materials_receipt_path=_path(args.materials_receipt),
        release_assets_receipt_path=_path(args.release_assets_receipt),
        product=_config("product.json"),
        version=_config("version.json"),
        chromium=_config("chromium.json"),
        toolchain=_config("toolchain.json"),
        release_args_sha256=sha256_file(ROOT / "config/build/ahoi-release.gn"),
        private_key=_path(args.private_key),
        public_key=_path(args.public_key),
        trusted_key_ids=_trusted_manifest_keys(policy),
        output=_path(args.output),
    )


def _verify_chain(args: argparse.Namespace) -> None:
    policy = _policy()
    installed = _path(args.installed_app) if args.installed_app else None
    validate_manifest(
        _path(args.manifest),
        public_key=_path(args.public_key),
        trusted_key_ids=_trusted_manifest_keys(policy),
        product=_config("product.json"),
        version=_config("version.json"),
        chromium=_config("chromium.json"),
        toolchain=_config("toolchain.json"),
        release_args_sha256=sha256_file(ROOT / "config/build/ahoi-release.gn"),
        installed_app=installed,
        policy_path=ENTITLEMENTS_PATH if installed else None,
    )


def _verify_release_manifest(
    path: pathlib.Path,
    public_key: pathlib.Path,
    policy: dict,
    installed_app: Optional[pathlib.Path] = None,
) -> dict:
    return validate_manifest(
        path,
        public_key=public_key,
        trusted_key_ids=_trusted_manifest_keys(policy),
        product=_config("product.json"),
        version=_config("version.json"),
        chromium=_config("chromium.json"),
        toolchain=_config("toolchain.json"),
        release_args_sha256=sha256_file(ROOT / "config/build/ahoi-release.gn"),
        installed_app=installed_app,
        policy_path=ENTITLEMENTS_PATH if installed_app else None,
    )


def _sparkle_appcast(args: argparse.Namespace) -> None:
    policy = _policy()
    configured = policy["updates"]["channels"][args.channel]
    if not all(
        configured[name]
        for name in ("feedUrl", "artifactBaseUrl", "publicEdKey")
    ):
        raise ReleaseError(
            f"Sparkle {args.channel} production feed/key remain fail-closed"
        )
    release_manifest = _path(args.release_manifest)
    materials_receipt = _path(args.materials_receipt)
    receipt = _path(args.receipt)
    _require_common_parent(
        [release_manifest, materials_receipt, receipt], "Sparkle appcast evidence"
    )
    _verify_release_manifest(
        release_manifest,
        _path(args.manifest_public_key),
        policy,
    )
    generate_appcast(
        _path(args.archives),
        output_name=args.output_name,
        channel=args.channel,
        feed_url=configured["feedUrl"],
        download_url_prefix=configured["artifactBaseUrl"],
        public_ed_key=configured["publicEdKey"],
        keychain_account=_required_environment("AHOI_SPARKLE_KEY_ACCOUNT"),
        minimum_update_version=args.minimum_update_version,
        expected_build=args.expected_build,
        tool=ROOT / ".work/state/sparkle" / PINNED_VERSION / "bin/generate_appcast",
        pin_path=ROOT / "config/third-party-pins.json",
        framework=ROOT
        / ".work/chromium/src/third_party/sparkle/prebuilt/Sparkle.framework",
        release_manifest=release_manifest,
        materials_receipt=materials_receipt,
        receipt_output=receipt,
    )


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    commands = result.add_subparsers(dest="command", required=True)

    sign = commands.add_parser("sign", help="sign nested code and emit a receipt")
    sign.add_argument("--app", required=True)
    sign.add_argument("--build-provenance", required=True)
    sign.add_argument("--output", required=True)
    sign.set_defaults(handler=_sign)

    notarize = commands.add_parser(
        "notarize-package", help="notarize, staple, and package ZIP/DMG"
    )
    notarize.add_argument("--app", required=True)
    notarize.add_argument("--signing-receipt", required=True)
    notarize.add_argument("--zip", required=True)
    notarize.add_argument("--dmg", required=True)
    notarize.add_argument("--volume-name", default="AhoiBrowser")
    notarize.add_argument("--notary-receipt", required=True)
    notarize.add_argument("--package-receipt", required=True)
    notarize.set_defaults(handler=_notarize)

    materials = commands.add_parser("materials", help="build SBOM/license evidence")
    materials.add_argument("--inventory", required=True)
    materials.add_argument("--source-offer", required=True)
    materials.add_argument("--notices", required=True)
    materials.add_argument("--sbom", required=True)
    materials.add_argument("--license-archive", required=True)
    materials.add_argument("--receipt", required=True)
    materials.add_argument("--namespace", required=True)
    materials.add_argument("--created-at", required=True)
    materials.set_defaults(handler=_materials)

    install = commands.add_parser(
        "install", help="atomically install and verify a notarized release app"
    )
    install.add_argument("--app", required=True)
    install.add_argument("--signing-receipt", required=True)
    install.add_argument("--notary-receipt", required=True)
    install.add_argument("--output", required=True)
    install.set_defaults(handler=_install)

    bind = commands.add_parser(
        "bind-installed", help="diagnostically verify an already installed app"
    )
    bind.add_argument("--app", default="/Applications/AhoiBrowser.app")
    bind.add_argument("--signing-receipt", required=True)
    bind.add_argument("--notary-receipt", required=True)
    bind.add_argument("--output", required=True)
    bind.set_defaults(handler=_bind_installed)

    assets = commands.add_parser(
        "release-assets", help="archive debug symbols and publish checksums"
    )
    assets.add_argument("--symbols-root", required=True)
    assets.add_argument("--package-receipt", required=True)
    assets.add_argument("--materials-receipt", required=True)
    assets.add_argument("--symbol-archive", required=True)
    assets.add_argument("--checksums", required=True)
    assets.add_argument("--receipt", required=True)
    assets.set_defaults(handler=_release_assets)

    assemble = commands.add_parser("assemble", help="sign the complete release manifest")
    assemble.add_argument("--root", required=True)
    assemble.add_argument("--build-provenance", required=True)
    assemble.add_argument("--signing-receipt", required=True)
    assemble.add_argument("--notary-receipt", required=True)
    assemble.add_argument("--package-receipt", required=True)
    assemble.add_argument("--installed-receipt", required=True)
    assemble.add_argument("--materials-receipt", required=True)
    assemble.add_argument("--release-assets-receipt", required=True)
    assemble.add_argument("--private-key", required=True)
    assemble.add_argument("--public-key", required=True)
    assemble.add_argument("--output", required=True)
    assemble.set_defaults(handler=_assemble)

    verify = commands.add_parser("verify-chain", help="independently verify release chain")
    verify.add_argument("--manifest", required=True)
    verify.add_argument("--public-key", required=True)
    verify.add_argument("--installed-app")
    verify.set_defaults(handler=_verify_chain)

    appcast = commands.add_parser(
        "sparkle-appcast", help="generate signed full/delta appcast with Sparkle"
    )
    appcast.add_argument(
        "--channel",
        choices=sorted({"nightly", "beta", "stable"}),
        required=True,
    )
    appcast.add_argument("--archives", required=True)
    appcast.add_argument("--output-name", default="appcast.xml")
    appcast.add_argument("--minimum-update-version", type=int, required=True)
    appcast.add_argument("--expected-build", type=int, required=True)
    appcast.add_argument("--release-manifest", required=True)
    appcast.add_argument("--manifest-public-key", required=True)
    appcast.add_argument("--materials-receipt", required=True)
    appcast.add_argument("--receipt", required=True)
    appcast.set_defaults(handler=_sparkle_appcast)
    return result


def main(argv: Optional[list[str]] = None) -> int:
    args = parser().parse_args(argv)
    try:
        args.handler(args)
    except ReleaseError as error:
        print(f"release error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
