#!/usr/bin/env python3
"""Fetch and attest Ahoi's exact uBlock Origin Classic bootstrap artifact."""

from __future__ import annotations

import argparse
import base64
import datetime as dt
import hashlib
import http.client
import json
import os
import pathlib
import ssl
import sys
import tempfile
import urllib.parse
from dataclasses import dataclass
from typing import Optional

from overlay_fingerprint import fingerprint as overlay_and_patch_fingerprint
from ubo_attestation_crx3 import AttestationError, verify_crx


ROOT = pathlib.Path(__file__).resolve().parents[1]
PIN_PATH = ROOT / "config/third-party-pins.json"
EXTENSIONS_ROOT = ROOT / "overlay/chromium/src/ahoi/browser/extensions"
PRODUCT_CONFIG_HEADER = EXTENSIONS_ROOT / "ubo_product_config.h"
PRODUCT_CONFIG_SOURCE = EXTENSIONS_ROOT / "ubo_product_config.cc"
PACKAGE_VERIFIER_SOURCE = EXTENSIONS_ROOT / "ubo_package_verifier.cc"
CRX3_HELPER_PATH = ROOT / "tools/ubo_attestation_crx3.py"
SERIES_PATH = ROOT / "patches/chromium/series"
DEV_GN_PATH = ROOT / "config/build/ahoi-dev.gn"

EXPECTED_VERSION = "1.74.0"
EXPECTED_TAG = "1.74.0"
EXPECTED_RELEASE_COMMIT = "6dd2d95e50d134a477a4e183343c0b26e9147123"
EXPECTED_RELEASE_PAGE = "https://github.com/gorhill/uBlock/releases/tag/1.74.0"
EXPECTED_RELEASE_DOWNLOAD = "https://github.com/gorhill/uBlock/releases/download/1.74.0"
EXPECTED_PACKAGE_URL = f"{EXPECTED_RELEASE_DOWNLOAD}/uBlock0_1.74.0.chromium.crx"
EXPECTED_PACKAGE_SHA256 = (
    "b6be71ed3e3e85eaad8f02710b9071d06428e141d942c43d5f65d4526e82dc3e"
)
EXPECTED_PACKAGE_SIZE = 4_535_482
EXPECTED_PUBLIC_KEY_SHA256 = (
    "5a6a81097514fb940453d5d46329eca78100e3cc0c5fca508e1a413f77f567bf"
)
EXPECTED_EXTENSION_ID = "fkgkibajhfbepljeaefdnfnegdcjomkh"
EXPECTED_ASSET_HOST = "release-assets.githubusercontent.com"
EXPECTED_ASSET_ROOT = "/github-production-release-asset/33263118"
EXPECTED_ASSET_PATH = f"{EXPECTED_ASSET_ROOT}/ade4daf2-50e8-4953-8821-5c2d43f07a65"
MAX_CRX_BYTES = 32 * 1024 * 1024


@dataclass(frozen=True)
class Pins:
    version: str
    release_commit: str
    release_page: str
    package_url: str
    package_sha256: str
    package_size: int
    public_key_sha256: str
    extension_id: str
    asset_path: str


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat()


def sha256_file(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _require_exact_keys(value: object, expected: set[str], label: str) -> dict:
    if not isinstance(value, dict) or set(value) != expected:
        raise AttestationError(f"{label} has an unsupported shape")
    return value


def _require_sha256(value: object, label: str) -> str:
    if (
        not isinstance(value, str)
        or len(value) != 64
        or any(character not in "0123456789abcdef" for character in value)
    ):
        raise AttestationError(f"{label} is not an exact lowercase SHA-256")
    return value


def _require_git_sha(value: object, label: str) -> str:
    if (
        not isinstance(value, str)
        or len(value) != 40
        or any(character not in "0123456789abcdef" for character in value)
    ):
        raise AttestationError(f"{label} is not an exact lowercase Git SHA")
    return value


def _load_json(path: pathlib.Path, label: str) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise AttestationError(f"{label} cannot be read as JSON") from error
    if not isinstance(value, dict):
        raise AttestationError(f"{label} must contain a JSON object")
    return value


def load_pins() -> Pins:
    payload = _load_json(PIN_PATH, "third-party pin registry")
    dependencies = payload.get("dependencies")
    if payload.get("schemaVersion") != 1 or not isinstance(dependencies, dict):
        raise AttestationError("third-party pin registry schema is unsupported")
    pin = _require_exact_keys(
        dependencies.get("uBlockOriginClassic"),
        {"version", "commit", "source", "distribution", "archive", "enabled", "gate"},
        "uBO pin",
    )
    archive = _require_exact_keys(
        pin["archive"],
        {
            "url",
            "sha256",
            "size",
            "crxPublicKeySha256",
            "extensionId",
            "releaseAssetPath",
        },
        "uBO archive pin",
    )
    expected = {
        "version": EXPECTED_VERSION,
        "commit": EXPECTED_RELEASE_COMMIT,
        "source": EXPECTED_RELEASE_PAGE,
        "distribution": "Official GitHub release",
        "enabled": True,
        "gate": "selectiveUboClassicMv2",
    }
    if any(pin.get(key) != value for key, value in expected.items()):
        raise AttestationError("uBO product pin differs from the reviewed trust root")
    expected_archive = {
        "url": EXPECTED_PACKAGE_URL,
        "sha256": EXPECTED_PACKAGE_SHA256,
        "size": EXPECTED_PACKAGE_SIZE,
        "crxPublicKeySha256": EXPECTED_PUBLIC_KEY_SHA256,
        "extensionId": EXPECTED_EXTENSION_ID,
        "releaseAssetPath": EXPECTED_ASSET_PATH,
    }
    if any(archive.get(key) != value for key, value in expected_archive.items()):
        raise AttestationError("uBO archive pin differs from the reviewed trust root")
    return Pins(
        version=pin["version"],
        release_commit=pin["commit"],
        release_page=pin["source"],
        package_url=archive["url"],
        package_sha256=archive["sha256"],
        package_size=archive["size"],
        public_key_sha256=archive["crxPublicKeySha256"],
        extension_id=archive["extensionId"],
        asset_path=archive["releaseAssetPath"],
    )


def _cpp_string_constant(source: str, name: str) -> str:
    marker = f"{name}[]"
    start = source.find(marker)
    if start < 0:
        raise AttestationError(f"browser trust root is missing {name}")
    end = source.find(";", start)
    if end < 0:
        raise AttestationError(f"browser trust root has malformed {name}")
    declaration = source[start:end]
    pieces: list[str] = []
    index = 0
    while index < len(declaration):
        if declaration[index] != '"':
            index += 1
            continue
        close = declaration.find('"', index + 1)
        if close < 0 or "\\" in declaration[index + 1 : close]:
            raise AttestationError(f"browser trust root has unsupported {name}")
        pieces.append(declaration[index + 1 : close])
        index = close + 1
    if not pieces:
        raise AttestationError(f"browser trust root has empty {name}")
    return "".join(pieces)


def verify_browser_trust_root(pins: Pins) -> dict:
    try:
        header = PRODUCT_CONFIG_HEADER.read_text(encoding="utf-8")
        implementation = PRODUCT_CONFIG_SOURCE.read_text(encoding="utf-8")
        verifier = PACKAGE_VERIFIER_SOURCE.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise AttestationError(
            "browser uBO trust-root sources cannot be read"
        ) from error
    expected = {
        "kUboClassicExtensionId": pins.extension_id,
        "kUboClassicVersion": pins.version,
        "kUboClassicReleaseCommit": pins.release_commit,
        "kUboClassicPackageUrl": pins.package_url,
        "kUboClassicPackageSha256": pins.package_sha256,
        "kUboClassicCrxPublicKeySha256": pins.public_key_sha256,
        "kUboClassicReleaseAssetPath": pins.asset_path,
    }
    for name, value in expected.items():
        if _cpp_string_constant(header, name) != value:
            raise AttestationError(f"browser trust-root constant {name} differs")
    required_implementation_markers = (
        'url.host() == "release-assets.githubusercontent.com"',
        "url.path() == kUboClassicReleaseAssetPath",
        "url.has_query()",
        "!url.has_username()",
        "!url.has_password()",
        "redirect_url != requested_url",
    )
    if any(marker not in implementation for marker in required_implementation_markers):
        raise AttestationError("browser redirect trust boundary is incomplete")
    required_verifier_markers = (
        "crx_file::VerifierFormat::CRX3",
        "{key_hash}",
        "package_hash",
        "crx_id != entry.extension_id",
        "crypto::hash::Sha256",
    )
    if any(marker not in verifier for marker in required_verifier_markers):
        raise AttestationError("browser CRX verification trust boundary is incomplete")
    return {
        "productConfigHeaderSha256": sha256_file(PRODUCT_CONFIG_HEADER),
        "productConfigSourceSha256": sha256_file(PRODUCT_CONFIG_SOURCE),
        "packageVerifierSourceSha256": sha256_file(PACKAGE_VERIFIER_SOURCE),
        "pinsMatchReviewedTrustRoot": True,
    }


def _safe_url(value: str, label: str) -> urllib.parse.SplitResult:
    if (
        not value.isascii()
        or len(value) > 8192
        or any(ord(character) < 0x20 for character in value)
    ):
        raise AttestationError(f"{label} contains unsafe characters")
    parsed = urllib.parse.urlsplit(value)
    try:
        port = parsed.port
    except ValueError as error:
        raise AttestationError(f"{label} has an invalid port") from error
    if (
        parsed.scheme != "https"
        or parsed.username is not None
        or parsed.password is not None
        or port is not None
        or parsed.fragment
        or not parsed.hostname
        or parsed.geturl() != value
    ):
        raise AttestationError(f"{label} is not a canonical credentialless HTTPS URL")
    return parsed


def validate_response_chain(start_url: str, status: int, locations: list[str]) -> str:
    start = _safe_url(start_url, "package start URL")
    if start_url != EXPECTED_PACKAGE_URL or start.hostname != "github.com":
        raise AttestationError("package request did not start at the exact GitHub pin")
    if status != 302 or len(locations) != 1:
        raise AttestationError("package request requires exactly one HTTP 302 Location")
    final_url = locations[0]
    final = _safe_url(final_url, "package final URL")
    if (
        final.hostname != EXPECTED_ASSET_HOST
        or final.netloc != EXPECTED_ASSET_HOST
        or final.path != EXPECTED_ASSET_PATH
        or not final.query
    ):
        raise AttestationError("package redirect differs from the pinned release asset")
    return final_url


def _request_target(parsed: urllib.parse.SplitResult) -> str:
    return parsed.path + (f"?{parsed.query}" if parsed.query else "")


def _connection(host: str, timeout: float) -> http.client.HTTPSConnection:
    context = ssl.create_default_context()
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    return http.client.HTTPSConnection(host, timeout=timeout, context=context)


def fetch_package(path: pathlib.Path, pins: Pins, timeout: float) -> dict:
    started_at = utc_now()
    headers = {
        "Accept": "application/octet-stream",
        "Accept-Encoding": "identity",
        "Cache-Control": "no-cache, no-store",
        "Pragma": "no-cache",
        "User-Agent": "AhoiBrowser-uBO-release-attestation/1",
    }
    start = _safe_url(pins.package_url, "package start URL")
    first = _connection(start.hostname or "", timeout)
    try:
        first.request("GET", _request_target(start), headers=headers)
        response = first.getresponse()
        locations = [
            value
            for name, value in response.getheaders()
            if name.lower() == "location"
        ]
        final_url = validate_response_chain(
            pins.package_url, response.status, locations
        )
    except (OSError, ssl.SSLError, http.client.HTTPException) as error:
        raise AttestationError("initial GitHub package request failed") from error
    finally:
        first.close()

    final = _safe_url(final_url, "package final URL")
    second = _connection(final.hostname or "", timeout)
    digest = hashlib.sha256()
    byte_count = 0
    try:
        second.request("GET", _request_target(final), headers=headers)
        response = second.getresponse()
        if response.status != 200:
            raise AttestationError("release asset did not return HTTP 200")
        if any(name.lower() == "location" for name, _ in response.getheaders()):
            raise AttestationError("release asset attempted a second redirect")
        encoding = response.getheader("Content-Encoding")
        if encoding not in (None, "", "identity"):
            raise AttestationError("release asset used an unsupported content encoding")
        content_length = response.getheader("Content-Length")
        if content_length is not None:
            try:
                declared_length = int(content_length)
            except ValueError as error:
                raise AttestationError(
                    "release asset Content-Length is invalid"
                ) from error
            if declared_length != pins.package_size:
                raise AttestationError(
                    "release asset Content-Length differs from the pin"
                )
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            with os.fdopen(descriptor, "wb") as handle:
                while True:
                    chunk = response.read(1024 * 1024)
                    if not chunk:
                        break
                    byte_count += len(chunk)
                    if byte_count > MAX_CRX_BYTES or byte_count > pins.package_size:
                        raise AttestationError(
                            "release asset exceeded its exact size pin"
                        )
                    digest.update(chunk)
                    handle.write(chunk)
                handle.flush()
                os.fsync(handle.fileno())
        except BaseException:
            path.unlink(missing_ok=True)
            raise
    except (OSError, ssl.SSLError, http.client.HTTPException) as error:
        raise AttestationError("final GitHub release-asset request failed") from error
    finally:
        second.close()
    completed_at = utc_now()
    package_sha256 = digest.hexdigest()
    if byte_count != pins.package_size:
        raise AttestationError("downloaded CRX size differs from the exact pin")
    if package_sha256 != pins.package_sha256:
        raise AttestationError("downloaded CRX SHA-256 differs from the exact pin")
    return {
        "startedAt": started_at,
        "completedAt": completed_at,
        "method": "GET",
        "credentialsMode": "omit",
        "cacheMode": "no-store",
        "startUrl": pins.package_url,
        "finalUrl": final_url,
        "redirectCount": 1,
        "responses": [
            {"url": pins.package_url, "status": 302, "method": "GET"},
            {"url": final_url, "status": 200, "method": "GET"},
        ],
        "downloadedBytes": byte_count,
        "sha256": package_sha256,
    }


def _repo_file(path: pathlib.Path, label: str) -> tuple[pathlib.Path, str]:
    if not path.is_absolute():
        path = ROOT / path
    try:
        resolved = path.resolve(strict=True)
        relative = resolved.relative_to(ROOT.resolve())
    except (OSError, ValueError) as error:
        raise AttestationError(
            f"{label} must be a canonical repository file"
        ) from error
    if path.is_symlink() or not resolved.is_file():
        raise AttestationError(f"{label} must be a regular non-symlink file")
    return resolved, relative.as_posix()


def patch_series_fingerprint() -> str:
    digest = hashlib.sha256(b"ahoi-patch-series-v1\0")
    try:
        lines = SERIES_PATH.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError) as error:
        raise AttestationError("Chromium patch series cannot be read") from error
    names = [
        line.strip()
        for line in lines
        if line.strip() and not line.startswith("#")
    ]
    if not names or len(names) != len(set(names)):
        raise AttestationError("Chromium patch series is empty or contains duplicates")
    for name in names:
        if pathlib.PurePosixPath(name).name != name:
            raise AttestationError("Chromium patch series contains an unsafe path")
        patch = SERIES_PATH.parent / name
        if patch.is_symlink() or not patch.is_file():
            raise AttestationError("Chromium patch series references a missing file")
        for value in (name.encode("utf-8"), patch.read_bytes()):
            digest.update(value)
            digest.update(b"\0")
    return digest.hexdigest()


def overlay_tree_fingerprint() -> str:
    root = ROOT / "overlay/chromium/src"
    digest = hashlib.sha256(b"ahoi-overlay-tree-v1\0")
    for path in sorted(root.rglob("*")):
        if not (path.is_file() or path.is_symlink()):
            continue
        relative = path.relative_to(root).as_posix().encode("utf-8")
        metadata = path.lstat()
        if path.is_symlink():
            mode = b"120000"
            content = os.readlink(path).encode("utf-8", "surrogateescape")
        elif metadata.st_mode & 0o111:
            mode = b"100755"
            content = path.read_bytes()
        else:
            mode = b"100644"
            content = path.read_bytes()
        for value in (relative, mode, content):
            digest.update(value)
            digest.update(b"\0")
    return digest.hexdigest()


def bind_candidate(
    build_path: pathlib.Path,
    installation_path: pathlib.Path,
) -> dict:
    build_file, build_relative = _repo_file(build_path, "build provenance")
    install_file, install_relative = _repo_file(
        installation_path, "installation receipt"
    )
    build = _load_json(build_file, "build provenance")
    installation = _load_json(install_file, "installation receipt")
    if build.get("schemaVersion") != 2 or build.get("kind") != "ahoi-dev":
        raise AttestationError(
            "uBO dogfood attestation requires schema-v2 ahoi-dev provenance"
        )
    app = build.get("app")
    source = build.get("source")
    if not isinstance(app, dict) or not isinstance(source, dict):
        raise AttestationError("build provenance lacks app/source sections")
    source_commit = _require_git_sha(source.get("repositoryCommit"), "source commit")
    if (
        source.get("repositoryDirty") is not False
        or app.get("sourceCommit") != source_commit
    ):
        raise AttestationError("candidate is not bound to clean exact source")
    combined_fingerprint = _require_sha256(
        source.get("overlayFingerprint"), "build overlay/patch fingerprint"
    )
    current_fingerprint = overlay_and_patch_fingerprint(ROOT)
    if combined_fingerprint != current_fingerprint:
        raise AttestationError("current overlay/patch inputs differ from the candidate")
    candidate_hash = _require_sha256(app.get("bundleSha256"), "candidate bundle hash")
    gn_args_sha256 = _require_sha256(
        app.get("gnArgsSha256"), "candidate GN-args SHA-256"
    )
    try:
        dev_gn = DEV_GN_PATH.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise AttestationError("ahoi-dev GN configuration cannot be read") from error
    if (
        sha256_file(DEV_GN_PATH) != gn_args_sha256
        or dev_gn.splitlines().count("enable_ahoi_ubo_classic = true") != 1
    ):
        raise AttestationError(
            "candidate does not bind the exact uBO-enabled dev GN args"
        )
    if (
        installation.get("schemaVersion") != 1
        or installation.get("kind") != "development-installation-receipt"
        or installation.get("releaseEvidenceEligible") is not False
    ):
        raise AttestationError(
            "installation receipt is not the canonical dogfood receipt"
        )
    bundle = installation.get("bundle")
    transaction = installation.get("installation")
    verification = installation.get("verification")
    if not all(isinstance(item, dict) for item in (bundle, transaction, verification)):
        raise AttestationError("installation receipt lacks required sections")
    if (
        bundle.get("sourceCommit") != source_commit
        or bundle.get("bundleTreeSha256") != candidate_hash
        or transaction.get("candidateBundleTreeSha256") != candidate_hash
        or transaction.get("target") != "/Applications/AhoiBrowser.app"
        or transaction.get("sameVolumeStaging") is not True
        or transaction.get("processesQuiescent") is not True
        or transaction.get("automaticRollbackOnVerificationFailure") is not True
        or transaction.get("postInstallVerification") is not True
        or verification.get("candidateVerifiedBeforeStaging") is not True
        or verification.get("sameVolumeCopyVerified") is not True
        or verification.get("installedBundleVerifiedAfterActivation") is not True
    ):
        raise AttestationError(
            "candidate and atomic installation receipt are not bound"
        )
    for app_name, bundle_name in (
        ("chromiumCommit", "chromiumCommit"),
        ("chromiumVersion", "chromiumVersion"),
        ("gnArgsSha256", "gnArgsSha256"),
        ("buildProfile", "buildProfile"),
    ):
        if app.get(app_name) != bundle.get(bundle_name):
            raise AttestationError(f"installed bundle differs at {bundle_name}")
    checkout_delta = _require_sha256(
        source.get("checkoutDeltaFingerprint"), "checkout delta fingerprint"
    )
    return {
        "sourceCommit": source_commit,
        "chromiumCommit": _require_git_sha(
            app.get("chromiumCommit"), "candidate Chromium commit"
        ),
        "overlayAndPatchFingerprint": combined_fingerprint,
        "patchSeriesFingerprint": patch_series_fingerprint(),
        "overlayTreeFingerprint": overlay_tree_fingerprint(),
        "checkoutDeltaFingerprint": checkout_delta,
        "gnArgsSha256": gn_args_sha256,
        "bundleTreeSha256": candidate_hash,
        "executableSha256": _require_sha256(
            bundle.get("executableSha256"), "installed executable SHA-256"
        ),
        "buildProvenance": {
            "path": build_relative,
            "sha256": sha256_file(build_file),
        },
        "installationReceipt": {
            "path": install_relative,
            "sha256": sha256_file(install_file),
        },
    }


def _output_path(path: pathlib.Path) -> pathlib.Path:
    if not path.is_absolute():
        path = ROOT / path
    path = pathlib.Path(os.path.abspath(path))
    try:
        path.relative_to(ROOT.resolve())
    except ValueError as error:
        raise AttestationError(
            "output must remain below the canonical repository"
        ) from error
    parent = path.parent
    try:
        resolved_parent = parent.resolve(strict=True)
        resolved_parent.relative_to(ROOT.resolve())
    except (OSError, ValueError) as error:
        raise AttestationError(
            "output must remain below the canonical repository"
        ) from error
    if (
        resolved_parent != parent
        or not parent.is_dir()
        or parent.is_symlink()
        or path.exists()
        or path.is_symlink()
    ):
        raise AttestationError("output must be a new non-symlink receipt path")
    return resolved_parent / path.name


def publish_atomic(path: pathlib.Path, payload: dict) -> None:
    output = _output_path(path)
    encoded = (
        json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{output.name}.", suffix=".tmp", dir=output.parent
    )
    temporary = pathlib.Path(temporary_name)
    published = False
    descriptor_open = True
    try:
        os.fchmod(descriptor, 0o600)
        handle = os.fdopen(descriptor, "wb")
        descriptor_open = False
        with handle:
            handle.write(encoded)
            handle.flush()
            os.fsync(handle.fileno())
        os.link(temporary, output)
        published = True
        directory_fd = os.open(
            output.parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0)
        )
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    except FileExistsError as error:
        raise AttestationError(
            "refusing to overwrite an existing attestation"
        ) from error
    except BaseException:
        if published:
            output.unlink(missing_ok=True)
        raise
    finally:
        if descriptor_open:
            os.close(descriptor)
        temporary.unlink(missing_ok=True)


def create_attestation(arguments: argparse.Namespace) -> dict:
    tool_path = pathlib.Path(__file__).resolve()
    tool_sha256 = sha256_file(tool_path)
    helper_sha256 = sha256_file(CRX3_HELPER_PATH)
    pins = load_pins()
    browser_trust_root = verify_browser_trust_root(pins)
    candidate = bind_candidate(arguments.build_provenance, arguments.install_receipt)
    with tempfile.TemporaryDirectory(prefix="ahoi-ubo-release-") as raw_directory:
        directory = pathlib.Path(raw_directory)
        directory.chmod(0o700)
        package_path = directory / "uBlock0_1.74.0.chromium.crx"
        retrieval = fetch_package(package_path, pins, arguments.timeout)
        verified, verifier = verify_crx(package_path, pins, arguments.openssl)
    if load_pins() != pins:
        raise AttestationError("uBO pins changed during attestation")
    if verify_browser_trust_root(pins) != browser_trust_root:
        raise AttestationError("browser trust root changed during attestation")
    if bind_candidate(
        arguments.build_provenance, arguments.install_receipt
    ) != candidate:
        raise AttestationError("candidate binding changed during attestation")
    if sha256_file(tool_path) != tool_sha256:
        raise AttestationError("attestation tool changed during execution")
    if sha256_file(CRX3_HELPER_PATH) != helper_sha256:
        raise AttestationError("CRX3 verifier helper changed during execution")
    return {
        "schemaVersion": 1,
        "kind": "ubo-official-github-release-attestation",
        "status": "PASS",
        "testIds": ["UBO-01", "UBO-02"],
        "attestedAt": utc_now(),
        "distribution": "Official GitHub release",
        "upstream": {
            "releasePage": pins.release_page,
            "releaseTag": EXPECTED_TAG,
            "releaseCommit": pins.release_commit,
            "packageUrl": pins.package_url,
        },
        "retrieval": retrieval,
        "package": {
            "format": "CRX3",
            "size": pins.package_size,
            "sha256": pins.package_sha256,
            "allHeaderProofsVerified": True,
            "rsaProofCount": verified.rsa_proof_count,
            "ecdsaProofCount": verified.ecdsa_proof_count,
        },
        "developerPublicKey": {
            "format": "X.509 SubjectPublicKeyInfo DER",
            "algorithm": "RSA",
            "derBase64": base64.b64encode(verified.public_key).decode("ascii"),
            "derSize": len(verified.public_key),
            "sha256": verified.public_key_sha256,
        },
        "identity": {
            "declaredExtensionId": verified.declared_id,
            "derivedExtensionId": verified.derived_id,
            "derivation": (
                "first 128 bits of SHA-256(SPKI DER), "
                "nibbles 0-f mapped to a-p"
            ),
        },
        "manifest": {
            "sha256": verified.manifest_sha256,
            "manifestVersion": verified.manifest_version,
            "extensionVersion": verified.extension_version,
            "updateUrlPresent": False,
        },
        "browserTrustRoot": browser_trust_root,
        "candidate": candidate,
        "verifier": verifier,
        "inputs": {
            "thirdPartyPins": {
                "path": PIN_PATH.relative_to(ROOT).as_posix(),
                "sha256": sha256_file(PIN_PATH),
            },
            "tool": {
                "path": tool_path.relative_to(ROOT).as_posix(),
                "sha256": tool_sha256,
            },
            "crx3Verifier": {
                "path": CRX3_HELPER_PATH.relative_to(ROOT).as_posix(),
                "sha256": helper_sha256,
            },
        },
        "retention": {
            "crxPersisted": False,
            "credentialsOrCookiesSent": False,
            "privateProfileDataRecorded": False,
            "finalUrlContainsOnlyPublicTransientReleaseAssetAuthorization": True,
        },
        "scope": {
            "dogfoodTechnicalProvenance": True,
            "publicRedistributionApproved": False,
            "futureSignedUpdateCatalogProvisioned": False,
        },
    }


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument(
        "--build-provenance",
        required=True,
        type=pathlib.Path,
        help="schema-v2 clean ahoi-dev build-provenance JSON below the repository",
    )
    result.add_argument(
        "--install-receipt",
        required=True,
        type=pathlib.Path,
        help="matching development-installation-receipt JSON below the repository",
    )
    result.add_argument(
        "--output",
        required=True,
        type=pathlib.Path,
        help="new immutable JSON receipt path below the repository",
    )
    result.add_argument(
        "--openssl",
        type=pathlib.Path,
        default=pathlib.Path("/usr/bin/openssl"),
        help="absolute root-owned OpenSSL executable (default: /usr/bin/openssl)",
    )
    result.add_argument(
        "--timeout",
        type=float,
        default=30.0,
        help="timeout in seconds for each of the two bounded HTTPS requests",
    )
    return result


def main(argv: Optional[list[str]] = None) -> int:
    arguments = parser().parse_args(argv)
    if not 1.0 <= arguments.timeout <= 120.0:
        print(
            "uBO attestation error: timeout must be between 1 and 120 seconds",
            file=sys.stderr,
        )
        return 2
    try:
        payload = create_attestation(arguments)
        publish_atomic(arguments.output, payload)
    except (AttestationError, OSError, ValueError) as error:
        print(f"uBO attestation error: {error}", file=sys.stderr)
        return 2
    output = (
        arguments.output
        if arguments.output.is_absolute()
        else ROOT / arguments.output
    )
    print(output.resolve(strict=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
