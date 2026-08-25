#!/usr/bin/env python3
"""Notarization, stapling, ZIP/DMG packaging, and their receipts."""

from __future__ import annotations

import json
import os
import pathlib
import shutil
import tempfile

from .common import (
    ReleaseError,
    atomic_write_json,
    bundle_identity,
    canonical_json,
    run,
    sha256_bytes,
    sha256_file,
    tree_sha256,
)


def _temporary_output(destination: pathlib.Path) -> pathlib.Path:
    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(
        prefix=f".{destination.stem}.",
        suffix=destination.suffix,
        dir=str(destination.parent),
    )
    os.close(descriptor)
    temporary = pathlib.Path(name)
    temporary.unlink()
    return temporary


def create_zip(app: pathlib.Path, destination: pathlib.Path) -> None:
    temporary = _temporary_output(destination)
    try:
        run(
            [
                "ditto",
                "-c",
                "-k",
                "--sequesterRsrc",
                "--keepParent",
                str(app),
                str(temporary),
            ]
        )
        if not temporary.is_file() or temporary.stat().st_size == 0:
            raise ReleaseError("ditto did not create a non-empty ZIP")
        os.replace(temporary, destination)
    finally:
        if temporary.exists():
            temporary.unlink()


def create_dmg(app: pathlib.Path, destination: pathlib.Path, volume_name: str) -> None:
    if not volume_name or "/" in volume_name or ":" in volume_name:
        raise ReleaseError("DMG volume name is invalid")
    temporary = _temporary_output(destination)
    try:
        with tempfile.TemporaryDirectory(prefix="ahoi-dmg-root-") as directory:
            staging = pathlib.Path(directory)
            staged_app = staging / app.name
            run(["ditto", str(app), str(staged_app)])
            os.symlink("/Applications", staging / "Applications")
            run(
                [
                    "hdiutil",
                    "create",
                    "-quiet",
                    "-fs",
                    "HFS+",
                    "-format",
                    "UDZO",
                    "-imagekey",
                    "zlib-level=9",
                    "-volname",
                    volume_name,
                    "-srcfolder",
                    str(staging),
                    str(temporary),
                ]
            )
        if not temporary.is_file() or temporary.stat().st_size == 0:
            raise ReleaseError("hdiutil did not create a non-empty DMG")
        os.replace(temporary, destination)
    finally:
        if temporary.exists():
            temporary.unlink()


def submit_notary(subject: pathlib.Path, keychain_profile: str) -> tuple[dict, bytes]:
    if not keychain_profile or any(character.isspace() for character in keychain_profile):
        raise ReleaseError("notarytool keychain profile is missing or invalid")
    completed = run(
        [
            "xcrun",
            "notarytool",
            "submit",
            str(subject),
            "--keychain-profile",
            keychain_profile,
            "--wait",
            "--output-format",
            "json",
        ]
    )
    try:
        response = json.loads(completed.stdout)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ReleaseError("notarytool did not return valid JSON") from error
    if not isinstance(response, dict):
        raise ReleaseError("notarytool response must be an object")
    if response.get("status") != "Accepted":
        raise ReleaseError(
            f"Apple notarization was not accepted: {response.get('status', 'missing')}"
        )
    submission_id = response.get("id")
    if not isinstance(submission_id, str) or not submission_id:
        raise ReleaseError("accepted notarization response has no submission ID")
    return response, canonical_json(response)


def _notary_submission(
    subject: pathlib.Path,
    keychain_profile: str,
    log_path: pathlib.Path,
) -> dict:
    response, raw_log = submit_notary(subject, keychain_profile)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_bytes(raw_log)
    return {
        "subject": subject.name,
        "subjectSha256": sha256_file(subject),
        "submissionId": response["id"],
        "status": "Accepted",
        "log": log_path.name,
        "logSha256": sha256_bytes(raw_log),
    }


def notarize_and_package(
    app: pathlib.Path,
    *,
    keychain_profile: str,
    zip_output: pathlib.Path,
    dmg_output: pathlib.Path,
    volume_name: str,
    notary_receipt_output: pathlib.Path,
    package_receipt_output: pathlib.Path,
) -> tuple[dict, dict]:
    """Notarize the signed app payload, staple it, then notarize the DMG."""
    pre_staple_hash = tree_sha256(app)
    receipt_root = notary_receipt_output.parent
    receipt_root.mkdir(parents=True, exist_ok=True)
    upload_zip = receipt_root / "AhoiBrowser-notary-upload.zip"
    create_zip(app, upload_zip)
    app_submission = _notary_submission(
        upload_zip,
        keychain_profile,
        receipt_root / "notary-app-response.json",
    )
    run(["xcrun", "stapler", "staple", str(app)])
    run(["xcrun", "stapler", "validate", str(app)])
    run(["spctl", "--assess", "--type", "execute", "--verbose=4", str(app)])
    post_staple_hash = tree_sha256(app)
    if post_staple_hash == pre_staple_hash:
        raise ReleaseError("stapling did not change the exact app bundle tree")

    create_zip(app, zip_output)
    create_dmg(app, dmg_output, volume_name)
    dmg_notary_upload = receipt_root / "AhoiBrowser-dmg-notary-upload.dmg"
    shutil.copyfile(dmg_output, dmg_notary_upload)
    dmg_submission = _notary_submission(
        dmg_notary_upload,
        keychain_profile,
        receipt_root / "notary-dmg-response.json",
    )
    pre_dmg_staple_hash = sha256_file(dmg_output)
    run(["xcrun", "stapler", "staple", str(dmg_output)])
    run(["xcrun", "stapler", "validate", str(dmg_output)])
    post_dmg_staple_hash = sha256_file(dmg_output)
    if post_dmg_staple_hash == pre_dmg_staple_hash:
        raise ReleaseError("stapling did not change the DMG")

    identity = bundle_identity(app)
    package_receipt = {
        "schemaVersion": 1,
        "kind": "package-provenance",
        "stapledBundleTreeSha256": post_staple_hash,
        "artifacts": {
            "zip": {
                "file": zip_output.name,
                "sha256": sha256_file(zip_output),
                "size": zip_output.stat().st_size,
            },
            "dmg": {
                "file": dmg_output.name,
                "sha256": post_dmg_staple_hash,
                "size": dmg_output.stat().st_size,
            },
        },
    }
    notary_receipt = {
        "schemaVersion": 1,
        "kind": "notarization-receipt",
        "bundle": {
            "preStapleTreeSha256": pre_staple_hash,
            "postStapleTreeSha256": post_staple_hash,
            "identity": identity,
            "staplerValidated": True,
            "gatekeeperAccepted": True,
        },
        "submissions": [app_submission, dmg_submission],
        "dmg": {
            "preStapleSha256": pre_dmg_staple_hash,
            "postStapleSha256": post_dmg_staple_hash,
            "staplerValidated": True,
        },
    }
    atomic_write_json(package_receipt_output, package_receipt)
    atomic_write_json(notary_receipt_output, notary_receipt)
    return package_receipt, notary_receipt
