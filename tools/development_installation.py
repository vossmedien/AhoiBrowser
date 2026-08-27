#!/usr/bin/env python3
"""Install a verified Ahoi development bundle for installed-app E2E work."""

from __future__ import annotations

import argparse
import os
import pathlib
import sys
from typing import Callable, Optional

from release.common import (
    ReleaseError,
    bundle_identity,
    load_json,
    sha256_file,
    run,
)
from release.installation import (
    CopyBundle,
    RenameBundle,
    ProcessInspector,
    install_verified_app_transaction,
    reserve_installer_artifact_output,
)


ROOT = pathlib.Path(__file__).resolve().parents[1]
INSTALL_PATH = pathlib.Path("/Applications/AhoiBrowser.app")
VERIFY_SCRIPT = ROOT / "scripts/verify-built-app.sh"

BundleCheck = Callable[[pathlib.Path], None]


def _development_bundle_policy() -> dict:
    product = load_json(ROOT / "config/product.json")
    if not isinstance(product, dict):
        raise ReleaseError("config/product.json must contain an object")
    name = product.get("name")
    identifier = product.get("bundleId")
    if not isinstance(name, str) or not name:
        raise ReleaseError("product name is missing")
    if not isinstance(identifier, str) or not identifier:
        raise ReleaseError("product bundle ID is missing")
    return {
        "name": name,
        "identifier": identifier,
        "buildProfile": "dev",
    }


def _require_verifier_script(path: pathlib.Path) -> str:
    if (
        not path.is_absolute()
        or path.is_symlink()
        or not path.is_file()
        or path.resolve(strict=True) != path
        or not os.access(path, os.X_OK)
    ):
        raise ReleaseError(f"development verifier must be a canonical executable: {path}")
    return sha256_file(path)


def verify_development_bundle(app: pathlib.Path) -> None:
    """Apply the repository's complete stamped/signed build verification contract."""
    run([str(VERIFY_SCRIPT), str(app)], cwd=ROOT)


def install_development_app(
    app: pathlib.Path,
    *,
    output: pathlib.Path,
    required_install_path: pathlib.Path = INSTALL_PATH,
    verifier: BundleCheck = verify_development_bundle,
    copy_bundle: Optional[CopyBundle] = None,
    exchange_bundle: Optional[RenameBundle] = None,
    move_exclusive: Optional[RenameBundle] = None,
    process_inspector: Optional[ProcessInspector] = None,
) -> dict:
    """Verify, atomically activate, reverify, and receipt one AhoiDev bundle."""
    verifier_sha256 = _require_verifier_script(VERIFY_SCRIPT)
    artifact_reservation = reserve_installer_artifact_output(
        output,
        app=app,
        required_install_path=required_install_path,
        name="development installation receipt",
    )

    def pinned_verifier(target: pathlib.Path) -> None:
        if _require_verifier_script(VERIFY_SCRIPT) != verifier_sha256:
            raise ReleaseError("development verifier changed before execution")
        verifier(target)
        if _require_verifier_script(VERIFY_SCRIPT) != verifier_sha256:
            raise ReleaseError("development verifier changed during execution")

    def finalize(target: pathlib.Path, installation: dict) -> dict:
        pinned_verifier(target)
        installed_identity = bundle_identity(target)
        receipt = {
            "schemaVersion": 1,
            "kind": "development-installation-receipt",
            "releaseEvidenceEligible": False,
            "bundle": installed_identity,
            "verification": {
                "semantics": "scripts/verify-built-app.sh",
                "scriptSha256": verifier_sha256,
                "candidateVerifiedBeforeStaging": True,
                "sameVolumeCopyVerified": True,
                "installedBundleVerifiedAfterActivation": True,
            },
            "installation": installation,
        }
        if _require_verifier_script(VERIFY_SCRIPT) != verifier_sha256:
            raise ReleaseError(
                "development verifier changed before receipt publication"
            )
        artifact_reservation.publish_json(receipt)
        return receipt

    transaction_arguments = {}
    for name, value in (
        ("copy_bundle", copy_bundle),
        ("exchange_bundle", exchange_bundle),
        ("move_exclusive", move_exclusive),
        ("process_inspector", process_inspector),
    ):
        if value is not None:
            transaction_arguments[name] = value

    try:
        return install_verified_app_transaction(
            app,
            expected_bundle=_development_bundle_policy(),
            required_install_path=required_install_path,
            verify_candidate=pinned_verifier,
            verify_staged_bundle=pinned_verifier,
            finalize_installation=finalize,
            artifact_reservation=artifact_reservation,
            **transaction_arguments,
        )
    except BaseException:
        if not artifact_reservation.closed:
            artifact_reservation.abort()
        raise


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument(
        "--app",
        required=True,
        help="absolute canonical path to the signed AhoiDev/AhoiBrowser.app",
    )
    result.add_argument(
        "--output",
        required=True,
        help="new immutable JSON receipt path outside /Applications",
    )
    return result


def main(argv: Optional[list[str]] = None) -> int:
    arguments = parser().parse_args(argv)
    candidate = pathlib.Path(arguments.app)
    if not candidate.is_absolute():
        print("development install error: --app must be absolute", file=sys.stderr)
        return 2
    output = pathlib.Path(arguments.output).resolve()
    try:
        install_development_app(candidate, output=output)
    except ReleaseError as error:
        print(f"development install error: {error}", file=sys.stderr)
        return 2
    print(f"installed verified AhoiDev at {INSTALL_PATH}")
    print(f"development installation receipt: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
