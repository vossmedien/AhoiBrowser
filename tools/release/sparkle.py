#!/usr/bin/env python3
"""Sparkle appcast generation, contract validation, and provenance binding."""

from __future__ import annotations

import base64
import binascii
import pathlib
import plistlib
import re
import urllib.parse
import xml.etree.ElementTree as ET

from .common import (
    ReleaseError,
    atomic_write_json,
    load_json,
    run,
    sha256_bytes,
    sha256_file,
    tree_sha256,
)


CHANNELS = {"nightly", "beta", "stable"}
SPARKLE_NAMESPACE = "http://www.andymatuschak.org/xml-namespaces/sparkle"
PINNED_VERSION = "2.9.6"
PINNED_COMMIT = "ac2def288cbff5cfc7df3ffef6abdf45b72bcb0a"
PINNED_RELEASE_URL = "https://github.com/sparkle-project/Sparkle/releases/tag/2.9.6"
PINNED_ARCHIVE_URL = (
    "https://github.com/sparkle-project/Sparkle/releases/download/2.9.6/"
    "Sparkle-2.9.6.tar.xz"
)
PINNED_ARCHIVE_SHA256 = (
    "52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192"
)
PINNED_ARCHIVE_SIZE = 15554568
PINNED_LICENSE_SHA256 = (
    "389a4e4e9a32f059775b13a06e25a591445ba229d2838d26dd3e7c0c45127cfe"
)
OFFICIAL_TOOL_NAMES = ("generate_appcast", "sign_update")


def _https_url(value: str, name: str) -> str:
    parsed = urllib.parse.urlsplit(value)
    if (
        parsed.scheme != "https"
        or not parsed.hostname
        or parsed.username
        or parsed.password
        or parsed.fragment
    ):
        raise ReleaseError(f"{name} must be a credential-free HTTPS URL")
    return value


def validate_public_ed_key(value: str) -> str:
    try:
        decoded = base64.b64decode(value, validate=True)
    except (ValueError, binascii.Error) as error:
        raise ReleaseError("Sparkle public Ed25519 key is not valid base64") from error
    if len(decoded) != 32:
        raise ReleaseError("Sparkle public Ed25519 key must decode to 32 bytes")
    return value


def validate_https_url(value: str, name: str) -> str:
    return _https_url(value, name)


def validate_artifact_base_url(value: str, name: str) -> str:
    _https_url(value, name)
    parsed = urllib.parse.urlsplit(value)
    if parsed.query or not parsed.path.endswith("/"):
        raise ReleaseError(f"{name} must be a query-free directory URL")
    return value


def validate_pin(pin_path: pathlib.Path, framework: pathlib.Path | None = None) -> dict:
    pin = load_json(pin_path).get("dependencies", {}).get("sparkle", {})
    archive = pin.get("archive", {})
    if (
        pin.get("version") != PINNED_VERSION
        or pin.get("commit") != PINNED_COMMIT
        or pin.get("source") != PINNED_RELEASE_URL
        or archive.get("url") != PINNED_ARCHIVE_URL
        or archive.get("sha256") != PINNED_ARCHIVE_SHA256
        or archive.get("size") != PINNED_ARCHIVE_SIZE
        or pin.get("license") != "MIT"
        or pin.get("licenseSha256") != PINNED_LICENSE_SHA256
        or pin.get("enabled") is not True
    ):
        raise ReleaseError("Sparkle pin differs from the reviewed security baseline")
    security = pin.get("security", {})
    advisories = set(security.get("advisories", []))
    if (
        security.get("minimumSafeVersion") != PINNED_VERSION
        or security.get("replacesVulnerableVersion") != "2.9.5"
        or advisories != {"GHSA-3x7w-j75x-ppq5", "GHSA-4v99-qgq9-6pxp"}
    ):
        raise ReleaseError("Sparkle security advisory gate is incomplete")
    if pin.get("licenseFile") != "overlay/chromium/src/third_party/sparkle/LICENSE":
        raise ReleaseError("Sparkle license evidence path differs from the pin")
    license_path = pin_path.parents[1] / pin["licenseFile"]
    if sha256_file(license_path) != PINNED_LICENSE_SHA256:
        raise ReleaseError("Sparkle license evidence differs from the official archive")
    if framework is not None:
        plist = framework / "Resources/Info.plist"
        if not plist.is_file():
            raise ReleaseError("Sparkle framework Info.plist is missing")
        with plist.open("rb") as handle:
            info = plistlib.load(handle)
        if info.get("CFBundleShortVersionString") != PINNED_VERSION:
            raise ReleaseError("bundled Sparkle framework version differs from pin")
    return pin


def _material_receipt_payload(
    pin_path: pathlib.Path,
    framework: pathlib.Path,
    tools_directory: pathlib.Path,
    license_path: pathlib.Path,
) -> dict:
    validate_pin(pin_path, framework)
    if not license_path.is_file():
        raise ReleaseError("Sparkle license material is missing")
    tools = {}
    for name in OFFICIAL_TOOL_NAMES:
        path = tools_directory / name
        if not path.is_file():
            raise ReleaseError(f"official Sparkle tool is missing: {name}")
        tools[name] = sha256_file(path)
    return {
        "schemaVersion": 1,
        "kind": "sparkle-fetched-material",
        "implementation": {
            "version": PINNED_VERSION,
            "commit": PINNED_COMMIT,
            "releaseUrl": PINNED_RELEASE_URL,
            "archiveUrl": PINNED_ARCHIVE_URL,
            "archiveSha256": PINNED_ARCHIVE_SHA256,
            "archiveSize": PINNED_ARCHIVE_SIZE,
        },
        "frameworkTreeSha256": tree_sha256(framework),
        "tools": tools,
        "licenseSha256": sha256_file(license_path),
    }


def create_material_receipt(
    pin_path: pathlib.Path,
    framework: pathlib.Path,
    tools_directory: pathlib.Path,
    license_path: pathlib.Path,
    output: pathlib.Path,
) -> dict:
    receipt = _material_receipt_payload(
        pin_path, framework, tools_directory, license_path
    )
    atomic_write_json(output, receipt)
    return receipt


def validate_material_receipt(
    pin_path: pathlib.Path,
    framework: pathlib.Path,
    tools_directory: pathlib.Path,
    license_path: pathlib.Path,
    receipt_path: pathlib.Path,
) -> dict:
    receipt = load_json(receipt_path)
    if not isinstance(receipt, dict):
        raise ReleaseError("Sparkle material receipt has an invalid shape")
    expected = _material_receipt_payload(
        pin_path, framework, tools_directory, license_path
    )
    if receipt != expected:
        raise ReleaseError("Sparkle fetched material differs from its receipt")
    return receipt


def validate_component_inventory(
    inventory_path: pathlib.Path, pin_path: pathlib.Path
) -> dict:
    pin = validate_pin(pin_path)
    inventory = load_json(inventory_path)
    components = inventory.get("components") if isinstance(inventory, dict) else None
    if not isinstance(components, list):
        raise ReleaseError("release component inventory has no components")
    matches = [
        component
        for component in components
        if isinstance(component, dict) and component.get("name") == "Sparkle"
    ]
    if len(matches) != 1:
        raise ReleaseError("release component inventory must contain Sparkle once")
    component = matches[0]
    if (
        component.get("version") != PINNED_VERSION
        or component.get("downloadLocation") != PINNED_RELEASE_URL
        or component.get("licenseConcluded") != "MIT"
        or component.get("licenseFiles") != [pin["licenseFile"]]
    ):
        raise ReleaseError("Sparkle SBOM component differs from the reviewed pin")
    return component


def validate_sparkle_materials_receipt(
    receipt_path: pathlib.Path, pin_path: pathlib.Path
) -> dict:
    receipt = load_json(receipt_path)
    if not isinstance(receipt, dict) or receipt.get("kind") != "release-materials":
        raise ReleaseError("Sparkle appcast requires a release-materials receipt")
    inventory_reference = receipt.get("componentInventory", {})
    inventory_name = inventory_reference.get("file")
    if (
        not isinstance(inventory_name, str)
        or pathlib.PurePath(inventory_name).name != inventory_name
    ):
        raise ReleaseError("materials receipt inventory reference is invalid")
    inventory_path = receipt_path.parent / inventory_name
    if sha256_file(inventory_path) != inventory_reference.get("sha256"):
        raise ReleaseError("materials receipt inventory hash does not match")
    sparkle_component = validate_component_inventory(inventory_path, pin_path)
    sbom_reference = receipt.get("sbom", {})
    sbom_name = sbom_reference.get("file")
    if (
        not isinstance(sbom_name, str)
        or pathlib.PurePath(sbom_name).name != sbom_name
    ):
        raise ReleaseError("materials receipt SBOM reference is invalid")
    sbom_path = receipt_path.parent / sbom_name
    if sha256_file(sbom_path) != sbom_reference.get("sha256"):
        raise ReleaseError("materials receipt SBOM hash does not match")
    sbom = load_json(sbom_path)
    packages = sbom.get("packages") if isinstance(sbom, dict) else None
    package_matches = (
        [
            item
            for item in packages
            if isinstance(item, dict) and item.get("name") == "Sparkle"
        ]
        if isinstance(packages, list)
        else []
    )
    if len(package_matches) != 1 or any(
        package_matches[0].get(field) != expected
        for field, expected in {
            "versionInfo": PINNED_VERSION,
            "downloadLocation": PINNED_RELEASE_URL,
            "licenseConcluded": "MIT",
        }.items()
    ):
        raise ReleaseError("SPDX SBOM does not bind the pinned Sparkle component")
    licenses = receipt.get("licenses")
    matches = (
        [
            item
            for item in licenses
            if isinstance(item, dict) and item.get("name") == "Sparkle"
        ]
        if isinstance(licenses, list)
        else []
    )
    if len(matches) != 1 or matches[0].get("license") != "MIT":
        raise ReleaseError("materials receipt has no pinned Sparkle license evidence")
    expected_license = sparkle_component["licenseFiles"][0]
    files = matches[0].get("files")
    if (
        not isinstance(files, list)
        or not any(
            isinstance(item, dict) and item.get("file") == expected_license
            for item in files
        )
    ):
        raise ReleaseError("materials receipt does not bind the Sparkle license")
    return receipt


def validate_appcast_contract(
    path: pathlib.Path,
    *,
    expected_channel: str,
    expected_build: int | None = None,
    expected_artifact_base_url: str | None = None,
) -> dict:
    if expected_channel not in CHANNELS:
        raise ReleaseError("Sparkle channel is invalid")
    raw = path.read_bytes()
    if len(raw) > 10 * 1024 * 1024:
        raise ReleaseError("Sparkle appcast exceeds the review size limit")
    if b"<!DOCTYPE" in raw.upper():
        raise ReleaseError("Sparkle appcast must not contain a document type")
    if expected_artifact_base_url is not None:
        validate_artifact_base_url(
            expected_artifact_base_url, "expected Sparkle artifact base URL"
        )
    feed_signature = re.search(
        rb"<!--\s*sparkle-signatures:\s*edSignature:\s*"
        rb"([A-Za-z0-9+/=]+)\s*length:\s*([0-9]+)\s*-->",
        raw,
        re.DOTALL,
    )
    if feed_signature is None:
        raise ReleaseError("Sparkle appcast has no signed-feed trailer")
    if raw[feed_signature.end() :].strip():
        raise ReleaseError("Sparkle signed-feed trailer must be the final content")
    try:
        if len(base64.b64decode(feed_signature.group(1), validate=True)) != 64:
            raise ValueError
    except (ValueError, binascii.Error) as error:
        raise ReleaseError("Sparkle feed signature is malformed") from error
    if int(feed_signature.group(2)) != feed_signature.start():
        raise ReleaseError("Sparkle signed-feed length does not bind the XML prefix")
    try:
        root = ET.fromstring(raw)
    except ET.ParseError as error:
        raise ReleaseError("Sparkle appcast is invalid XML") from error
    if root.tag != "rss":
        raise ReleaseError("Sparkle appcast root must be rss")

    items = root.findall("./channel/item")
    if not items:
        raise ReleaseError("Sparkle appcast contains no update items")
    builds = []
    for item in items:
        version = item.findtext(f"{{{SPARKLE_NAMESPACE}}}version")
        if not version or not version.isdigit() or int(version) < 1:
            raise ReleaseError("Sparkle item has no positive numeric build version")
        builds.append(int(version))
        channel = item.findtext(f"{{{SPARKLE_NAMESPACE}}}channel")
        allowed_channels = {None, "", "stable"}
        if expected_channel in {"beta", "nightly"}:
            allowed_channels.add("beta")
        if expected_channel == "nightly":
            allowed_channels.add("nightly")
        if channel not in allowed_channels:
            raise ReleaseError("Sparkle appcast contains a foreign channel item")
        enclosure = item.find("enclosure")
        if enclosure is None:
            raise ReleaseError("Sparkle item has no enclosure")
        enclosure_url = _https_url(
            enclosure.get("url", ""), "Sparkle enclosure URL"
        )
        if (
            expected_artifact_base_url is not None
            and not enclosure_url.startswith(expected_artifact_base_url)
        ):
            raise ReleaseError("Sparkle enclosure escapes the reviewed artifact base")
        signature = enclosure.get(f"{{{SPARKLE_NAMESPACE}}}edSignature", "")
        try:
            if len(base64.b64decode(signature, validate=True)) != 64:
                raise ValueError
        except (ValueError, binascii.Error) as error:
            raise ReleaseError("Sparkle enclosure signature is malformed") from error
        length = enclosure.get("length", "")
        if not length.isdigit() or int(length) < 1:
            raise ReleaseError("Sparkle enclosure length is invalid")
    if expected_build is not None and expected_build not in builds:
        raise ReleaseError("Sparkle appcast does not contain the release build")
    return {"itemCount": len(items), "builds": sorted(builds, reverse=True)}


def generate_appcast(
    archives: pathlib.Path,
    *,
    output_name: str,
    channel: str,
    feed_url: str,
    download_url_prefix: str,
    public_ed_key: str,
    keychain_account: str,
    minimum_update_version: int,
    expected_build: int,
    tool: pathlib.Path,
    pin_path: pathlib.Path,
    framework: pathlib.Path,
    release_manifest: pathlib.Path,
    materials_receipt: pathlib.Path,
    receipt_output: pathlib.Path,
) -> dict:
    validate_pin(pin_path, framework)
    validate_sparkle_materials_receipt(materials_receipt, pin_path)
    if channel not in CHANNELS:
        raise ReleaseError("Sparkle channel is invalid")
    _https_url(feed_url, "Sparkle feed URL")
    validate_artifact_base_url(
        download_url_prefix, "Sparkle download URL prefix"
    )
    if not archives.is_dir() or not tool.is_file():
        raise ReleaseError("Sparkle archives directory or official tool is missing")
    if (
        pathlib.PurePath(output_name).name != output_name
        or not output_name.endswith(".xml")
    ):
        raise ReleaseError("Sparkle appcast output must be one XML filename")
    feed_name = pathlib.PurePosixPath(
        urllib.parse.unquote(urllib.parse.urlsplit(feed_url).path)
    ).name
    if feed_name != output_name:
        raise ReleaseError("Sparkle appcast output differs from the reviewed feed URL")
    validate_public_ed_key(public_ed_key)
    if not keychain_account or any(
        character.isspace() for character in keychain_account
    ):
        raise ReleaseError("Sparkle Keychain account is missing or invalid")
    if minimum_update_version < 1 or minimum_update_version >= expected_build:
        raise ReleaseError("Sparkle minimum update version is invalid")
    output = archives / output_name
    command = [
        str(tool),
        "--account",
        keychain_account,
        "--download-url-prefix",
        download_url_prefix,
        "--minimum-update-version",
        str(minimum_update_version),
        "--maximum-deltas",
        "5",
        "--maximum-versions",
        "3",
        "-o",
        str(output),
    ]
    if channel != "stable":
        command.extend(["--channel", channel])
    command.append(str(archives))
    run(command)
    signing_tool = tool.with_name("sign_update")
    if not signing_tool.is_file():
        raise ReleaseError("official Sparkle sign_update verifier is missing")
    run(
        [
            str(signing_tool),
            "--account",
            keychain_account,
            "--verify",
            str(output),
        ]
    )
    validation = validate_appcast_contract(
        output,
        expected_channel=channel,
        expected_build=expected_build,
        expected_artifact_base_url=download_url_prefix,
    )
    for evidence in (release_manifest, materials_receipt):
        if not evidence.is_file():
            raise ReleaseError(f"Sparkle provenance input is missing: {evidence}")
    receipt = {
        "schemaVersion": 1,
        "kind": "sparkle-appcast-provenance",
        "implementation": {
            "name": "Sparkle",
            "version": PINNED_VERSION,
            "commit": PINNED_COMMIT,
            "archiveSha256": PINNED_ARCHIVE_SHA256,
        },
        "channel": channel,
        "publication": {
            "feedUrl": feed_url,
            "artifactBaseUrl": download_url_prefix,
            "publicEdKeySha256": sha256_bytes(base64.b64decode(public_ed_key)),
        },
        "minimumUpdateVersion": minimum_update_version,
        "expectedBuild": expected_build,
        "appcast": {
            "file": output.name,
            "sha256": sha256_file(output),
            **validation,
        },
        "releaseManifest": {
            "file": release_manifest.name,
            "sha256": sha256_file(release_manifest),
        },
        "materialsReceipt": {
            "file": materials_receipt.name,
            "sha256": sha256_file(materials_receipt),
        },
    }
    atomic_write_json(receipt_output, receipt)
    return receipt
