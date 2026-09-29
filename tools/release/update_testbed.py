#!/usr/bin/env python3
"""Local Sparkle appcast and tamper testbed with a throwaway Ed25519 key.

The testbed exercises the pinned official Sparkle 2.9.6 ``generate_appcast``
and ``sign_update`` tools plus an independent OpenSSL Ed25519 verifier against
minimal dummy application archives. It never reads a Keychain account, never
uses a product key, never contacts the network and never publishes anything:
every enclosure and feed URL points at the reserved ``.invalid`` TLD, and the
private key lives only in a temporary directory that is deleted afterwards.
"""

from __future__ import annotations

import argparse
import base64
import datetime as dt
import json
import os
import pathlib
import plistlib
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from .common import ReleaseError, load_json, sha256_bytes, sha256_file
from .sparkle import (
    OFFICIAL_TOOL_NAMES,
    PINNED_ARCHIVE_SHA256,
    PINNED_VERSION,
    SPARKLE_NAMESPACE,
    validate_appcast_contract,
    validate_pin,
)


ROOT = pathlib.Path(__file__).resolve().parents[2]
BASE_URL = "https://updates.ahoi-testbed.invalid/nightly/"
FEED_URL = BASE_URL + "appcast.xml"
BUNDLE_ID = "app.ahoibrowser.update-testbed"
PREVIOUS_BUILD = 12
CANDIDATE_BUILD = 13
# Mirrors AllowedSparkleChannels() in
# overlay/chromium/src/ahoi/browser/updater/update_channel.cc.
ALLOWED_SPARKLE_CHANNELS = {
    "stable": frozenset(),
    "beta": frozenset({"beta"}),
    "nightly": frozenset({"beta", "nightly"}),
}
_ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")
_FEED_TRAILER = re.compile(
    rb"<!--\s*sparkle-signatures:\s*edSignature:\s*([A-Za-z0-9+/=]+)\s*"
    rb"length:\s*([0-9]+)\s*-->\s*\Z"
)


def default_sparkle_tools() -> pathlib.Path:
    """Returns the fetched official tools, also from a linked Git worktree."""
    relative = pathlib.Path(".work/state/sparkle") / PINNED_VERSION / "bin"
    candidates = [ROOT / relative]
    try:
        common = subprocess.run(
            ["git", "-C", str(ROOT), "rev-parse", "--path-format=absolute",
             "--git-common-dir"],
            check=True, capture_output=True, text=True,
        ).stdout.strip()
        candidates.append(pathlib.Path(common).parent / relative)
    except (OSError, subprocess.CalledProcessError):
        pass
    for candidate in candidates:
        if all((candidate / name).is_file() for name in OFFICIAL_TOOL_NAMES):
            return candidate
    return candidates[0]


def verify_tool_provenance(tools: pathlib.Path) -> dict:
    """Binds the tools to the fetch receipt written by scripts/fetch-sparkle.sh."""
    validate_pin(ROOT / "config/third-party-pins.json")
    receipt = load_json(tools.parent / "fetch-receipt.json")
    implementation = receipt.get("implementation", {})
    if (
        receipt.get("kind") != "sparkle-fetched-material"
        or implementation.get("version") != PINNED_VERSION
        or implementation.get("archiveSha256") != PINNED_ARCHIVE_SHA256
    ):
        raise ReleaseError("Sparkle fetch receipt does not bind the reviewed pin")
    hashes = {}
    for name in OFFICIAL_TOOL_NAMES:
        actual = sha256_file(tools / name)
        if actual != receipt.get("tools", {}).get(name):
            raise ReleaseError(f"official Sparkle tool differs from receipt: {name}")
        hashes[name] = actual
    return hashes


class ThrowawayKey:
    """An Ed25519 key pair that exists only inside one temporary directory."""

    def __init__(self, directory: pathlib.Path, name: str):
        directory.mkdir(parents=True, exist_ok=True)
        der = directory / f"{name}-private.der"
        subprocess.run(
            ["openssl", "genpkey", "-algorithm", "ED25519", "-outform", "DER",
             "-out", str(der)],
            check=True, capture_output=True,
        )
        der.chmod(0o600)
        public_der = subprocess.run(
            ["openssl", "pkey", "-inform", "DER", "-in", str(der), "-pubout",
             "-outform", "DER"],
            check=True, capture_output=True,
        ).stdout
        if not public_der.startswith(_ED25519_SPKI_PREFIX) or len(public_der) != 44:
            raise ReleaseError("OpenSSL returned an unexpected Ed25519 public key")
        # Sparkle's --ed-key-file format: base64 of the 32-byte private seed.
        self.sparkle_key_file = directory / f"{name}-sparkle-ed-key.txt"
        self.sparkle_key_file.write_text(base64.b64encode(der.read_bytes()[-32:]).decode())
        self.sparkle_key_file.chmod(0o600)
        self.public_pem = directory / f"{name}-public.pem"
        subprocess.run(
            ["openssl", "pkey", "-inform", "DER", "-in", str(der), "-pubout",
             "-out", str(self.public_pem)],
            check=True, capture_output=True,
        )
        self.public_raw = public_der[-32:]
        self.public_b64 = base64.b64encode(self.public_raw).decode()


def assert_not_product_key(public_b64: str) -> None:
    policy = load_json(ROOT / "config/release-policy.json")
    for channel, values in policy["updates"]["channels"].items():
        if values.get("publicEdKey") and values["publicEdKey"] == public_b64:
            raise ReleaseError(f"testbed key collides with the {channel} product key")


class SparkleTools:
    """Runs the official tools with an isolated HOME and only --ed-key-file."""

    def __init__(self, tools: pathlib.Path, home: pathlib.Path):
        self.tools = tools
        home.mkdir(parents=True, exist_ok=True)
        # generate_appcast caches extracted archives under ~/Library/Caches.
        self.env = dict(os.environ, HOME=str(home), CFFIXED_USER_HOME=str(home))
        self.commands: list[list[str]] = []

    def _run(self, name: str, *arguments: object) -> subprocess.CompletedProcess:
        command = [str(self.tools / name), *map(str, arguments)]
        if "--account" in command or "--ed-key-file" not in command:
            raise ReleaseError("testbed must never read a Keychain signing account")
        self.commands.append([name, *command[1:]])
        return subprocess.run(command, capture_output=True, text=True, env=self.env)

    def generate_appcast(self, key: ThrowawayKey, archives: pathlib.Path,
                         output: pathlib.Path, minimum_update_version: int) -> None:
        result = self._run(
            "generate_appcast", "--ed-key-file", key.sparkle_key_file,
            "--download-url-prefix", BASE_URL,
            "--minimum-update-version", minimum_update_version,
            "--maximum-deltas", 5, "--maximum-versions", 3,
            "--channel", "nightly", "-o", output, archives,
        )
        if result.returncode != 0:
            raise ReleaseError(f"generate_appcast failed: {result.stderr.strip()}")

    def sign_feed(self, key: ThrowawayKey, feed: pathlib.Path) -> None:
        result = self._run("sign_update", "--ed-key-file", key.sparkle_key_file, feed)
        if result.returncode != 0:
            raise ReleaseError(f"sign_update failed: {result.stderr.strip()}")

    def sign_archive(self, key: ThrowawayKey, archive: pathlib.Path) -> str:
        result = self._run(
            "sign_update", "--ed-key-file", key.sparkle_key_file, "-p", archive
        )
        if result.returncode != 0:
            raise ReleaseError(f"sign_update failed: {result.stderr.strip()}")
        return result.stdout.strip()

    def verify_archive(self, key: ThrowawayKey, archive: pathlib.Path,
                       signature: str) -> bool:
        return self._run(
            "sign_update", "--ed-key-file", key.sparkle_key_file, "--verify",
            archive, signature,
        ).returncode == 0

    def verify_feed(self, key: ThrowawayKey, feed: pathlib.Path) -> bool:
        return self._run(
            "sign_update", "--ed-key-file", key.sparkle_key_file, "--verify", feed
        ).returncode == 0


def openssl_verify(public_pem: pathlib.Path, payload: bytes, signature_b64: str,
                   scratch: pathlib.Path) -> bool:
    """Independent pure-Ed25519 check of the same bytes Sparkle signs."""
    try:
        signature = base64.b64decode(signature_b64, validate=True)
    except ValueError:
        return False
    if len(signature) != 64:
        return False
    payload_path = scratch / "openssl-payload"
    signature_path = scratch / "openssl-signature"
    payload_path.write_bytes(payload)
    signature_path.write_bytes(signature)
    return subprocess.run(
        ["openssl", "pkeyutl", "-verify", "-rawin", "-pubin", "-inkey",
         str(public_pem), "-in", str(payload_path), "-sigfile", str(signature_path)],
        capture_output=True,
    ).returncode == 0


def feed_signature(raw: bytes) -> tuple[bytes, str] | None:
    """Returns the signed prefix and signature of Sparkle's signed-feed trailer."""
    match = _FEED_TRAILER.search(raw)
    if match is None or int(match.group(2)) != match.start():
        return None
    return raw[: int(match.group(2))], match.group(1).decode()


def select_update(raw: bytes, installed_build: int, channel: str) -> int | None:
    """Mirrors Sparkle's best-item choice for an Ahoi build of `channel`.

    Items without a channel are Sparkle's default channel and visible to all;
    tagged items only when the tag is allowed; only a strictly newer
    CFBundleVersion than the installed one is ever offered.
    """
    allowed = ALLOWED_SPARKLE_CHANNELS[channel]
    best = None
    for item in ET.fromstring(raw).findall("./channel/item"):
        tag = item.findtext(f"{{{SPARKLE_NAMESPACE}}}channel")
        if tag and tag not in allowed:
            continue
        version = int(item.findtext(f"{{{SPARKLE_NAMESPACE}}}version"))
        if version > installed_build and (best is None or version > best):
            best = version
    return best


def enclosure_signature(raw: bytes, build: int) -> str:
    for item in ET.fromstring(raw).findall("./channel/item"):
        if item.findtext(f"{{{SPARKLE_NAMESPACE}}}version") == str(build):
            return item.find("enclosure").get(f"{{{SPARKLE_NAMESPACE}}}edSignature")
    raise ReleaseError(f"appcast has no item for build {build}")


def build_dummy_archive(stage: pathlib.Path, archives: pathlib.Path, build: int,
                        public_b64: str) -> pathlib.Path:
    """Creates a tiny, never-launched stand-in bundle and zips it with ditto."""
    app = stage / f"{build}" / "AhoiBrowser.app"
    executable = app / "Contents/MacOS/AhoiBrowser"
    executable.parent.mkdir(parents=True)
    executable.write_text("#!/bin/sh\n# update-testbed dummy; never executed\nexit 1\n")
    executable.chmod(0o755)
    (app / "Contents/Info.plist").write_bytes(plistlib.dumps({
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleExecutable": "AhoiBrowser",
        "CFBundleName": "AhoiBrowser",
        "CFBundlePackageType": "APPL",
        "CFBundleVersion": str(build),
        "CFBundleShortVersionString": f"0.0.{build}",
        "LSMinimumSystemVersion": "13.0",
        "AhoiUpdateChannel": "nightly",
        "SUFeedURL": FEED_URL,
        "SUPublicEDKey": public_b64,
        "SURequireSignedFeed": True,
        "SUVerifyUpdateBeforeExtraction": True,
        "SUSendProfileInfo": False,
    }))
    archive = archives / f"AhoiBrowser-{build}.zip"
    subprocess.run(
        ["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app),
         str(archive)],
        check=True, capture_output=True,
    )
    return archive


def _contract(path: pathlib.Path, **overrides) -> tuple[bool, str]:
    arguments = {
        "expected_channel": "nightly",
        "expected_build": CANDIDATE_BUILD,
        "expected_artifact_base_url": BASE_URL,
        "published_build_floor": CANDIDATE_BUILD,
    }
    arguments.update(overrides)
    try:
        validate_appcast_contract(path, **arguments)
    except ReleaseError as error:
        return False, str(error)
    return True, "accepted"


def _flip_byte(source: pathlib.Path, target: pathlib.Path) -> None:
    data = bytearray(source.read_bytes())
    data[len(data) // 2] ^= 0x01
    target.write_bytes(bytes(data))


def run_testbed(tools: pathlib.Path, work: pathlib.Path) -> dict:
    """Runs every scenario and returns a JSON-serialisable report."""
    tool_hashes = verify_tool_provenance(tools)
    keys = work / "keys"
    key = ThrowawayKey(keys, "testbed")
    attacker = ThrowawayKey(keys, "attacker")
    assert_not_product_key(key.public_b64)
    sparkle = SparkleTools(tools, work / "isolated-home")
    archives = work / "archives"
    archives.mkdir()
    for build in (PREVIOUS_BUILD, CANDIDATE_BUILD):
        build_dummy_archive(work / "stage", archives, build, key.public_b64)
    appcast = archives / "appcast.xml"
    sparkle.generate_appcast(key, archives, appcast, minimum_update_version=11)
    raw = appcast.read_bytes()
    candidate = archives / f"AhoiBrowser-{CANDIDATE_BUILD}.zip"
    deltas = sorted(archives.glob("*.delta"))
    scratch = work / "scratch"
    scratch.mkdir()
    variants = work / "variants"
    variants.mkdir()
    checks: list[dict] = []

    def check(check_id: str, group: str, description: str, *, expect_accept: bool,
              observations: dict, informational: dict | None = None) -> None:
        # Every observation answers "did this verifier accept the input?".
        accepted = {}
        reasons = {}
        for name, value in observations.items():
            if isinstance(value, tuple):
                accepted[name], reasons[name] = value
            else:
                accepted[name] = bool(value)
        passed = accepted and (
            all(accepted.values()) if expect_accept else not any(accepted.values()))
        entry = {
            "id": check_id,
            "group": group,
            "description": description,
            "expected": "accept" if expect_accept else "reject",
            "accepted": accepted,
            "result": "PASS" if passed else "FAIL",
        }
        if reasons:
            entry["reasons"] = reasons
        if informational:
            entry["informational"] = informational
        checks.append(entry)

    def variant(name: str, content: bytes, *, resign: ThrowawayKey | None = None
                ) -> pathlib.Path:
        path = variants / name
        path.write_bytes(content)
        if resign is not None:
            sparkle.sign_feed(resign, path)
        return path

    def feed_openssl(path: pathlib.Path) -> bool:
        signed = feed_signature(path.read_bytes())
        return signed is not None and openssl_verify(
            key.public_pem, signed[0], signed[1], scratch)

    signature = enclosure_signature(raw, CANDIDATE_BUILD)
    genuine_contract = _contract(appcast)
    check("A1", "a-genuine", "signed candidate archive verifies",
          expect_accept=True, observations={
              "sparkleSignUpdateVerify": sparkle.verify_archive(key, candidate, signature),
              "opensslEd25519Verify": openssl_verify(
                  key.public_pem, candidate.read_bytes(), signature, scratch),
          })
    delta_observations = {}
    for delta in deltas:
        delta_signature = None
        for element in ET.fromstring(raw).iter("enclosure"):
            if element.get("url", "").endswith("/" + delta.name):
                delta_signature = element.get(f"{{{SPARKLE_NAMESPACE}}}edSignature")
        delta_observations[f"sparkleVerify:{delta.name}"] = (
            delta_signature is not None
            and sparkle.verify_archive(key, delta, delta_signature))
    check("A2", "a-genuine", "signed delta archive verifies",
          expect_accept=True, observations=delta_observations or {"deltaPresent": False})
    check("A3", "a-genuine", "signed feed verifies and meets the release contract",
          expect_accept=True, observations={
              "sparkleSignUpdateVerifyFeed": sparkle.verify_feed(key, appcast),
              "opensslEd25519VerifyFeedPrefix": feed_openssl(appcast),
              "releaseContract": genuine_contract,
          })
    check("A4", "a-genuine", "nightly build 12 is offered candidate build 13",
          expect_accept=True, observations={
              "offeredBuildIs13": select_update(raw, PREVIOUS_BUILD, "nightly")
              == CANDIDATE_BUILD,
          })

    tampered = variants / candidate.name
    _flip_byte(candidate, tampered)
    check("B1", "b-tampered-archive", "one flipped byte in the full archive",
          expect_accept=False, observations={
              "sparkleSignUpdateVerify": sparkle.verify_archive(key, tampered, signature),
              "opensslEd25519Verify": openssl_verify(
                  key.public_pem, tampered.read_bytes(), signature, scratch),
          })
    if deltas:
        tampered_delta = variants / deltas[0].name
        _flip_byte(deltas[0], tampered_delta)
        delta_signature = [
            element.get(f"{{{SPARKLE_NAMESPACE}}}edSignature")
            for element in ET.fromstring(raw).iter("enclosure")
            if element.get("url", "").endswith("/" + deltas[0].name)
        ][0]
        check("B2", "b-tampered-archive", "one flipped byte in the delta archive",
              expect_accept=False, observations={
                  "sparkleSignUpdateVerify": sparkle.verify_archive(
                      key, tampered_delta, delta_signature),
              })
    appended = variants / ("appended-" + candidate.name)
    appended.write_bytes(candidate.read_bytes() + b"\0")
    check("B3", "b-tampered-archive", "one byte appended to the full archive",
          expect_accept=False, observations={
              "sparkleSignUpdateVerify": sparkle.verify_archive(key, appended, signature),
          })

    foreign_signature = sparkle.sign_archive(attacker, candidate)
    previous_signature = enclosure_signature(raw, PREVIOUS_BUILD)
    check("C1", "c-wrong-signature", "candidate signed by a foreign key",
          expect_accept=False, observations={
              "sparkleSignUpdateVerify": sparkle.verify_archive(
                  key, candidate, foreign_signature),
              "opensslEd25519Verify": openssl_verify(
                  key.public_pem, candidate.read_bytes(), foreign_signature, scratch),
          })
    check("C2", "c-wrong-signature", "signature of build 12 replayed for build 13",
          expect_accept=False, observations={
              "sparkleSignUpdateVerify": sparkle.verify_archive(
                  key, candidate, previous_signature),
          })
    check("C3", "c-wrong-signature", "64 zero bytes as signature",
          expect_accept=False, observations={
              "sparkleSignUpdateVerify": sparkle.verify_archive(
                  key, candidate, base64.b64encode(bytes(64)).decode()),
          })
    swapped = raw.replace(signature.encode(), foreign_signature.encode())
    swapped_path = variant("foreign-enclosure-signature.xml", swapped, resign=key)
    check("C4", "c-wrong-signature",
          "validly signed feed whose enclosure carries a foreign-key signature",
          expect_accept=False, observations={
              "sparkleSignUpdateVerifyArchive": sparkle.verify_archive(
                  key, candidate, enclosure_signature(
                      swapped_path.read_bytes(), CANDIDATE_BUILD)),
          })
    attacker_feed = variant("attacker-signed-feed.xml", raw, resign=attacker)
    check("C5", "c-wrong-signature", "feed re-signed with a foreign key",
          expect_accept=False, observations={
              "sparkleSignUpdateVerifyFeed": sparkle.verify_feed(key, attacker_feed),
              "opensslEd25519VerifyFeedPrefix": feed_openssl(attacker_feed),
          })

    # D: replay of an older, genuinely signed feed and version regressions.
    old_archives = work / "old-archives"
    old_archives.mkdir()
    (old_archives / f"AhoiBrowser-{PREVIOUS_BUILD}.zip").write_bytes(
        (archives / f"AhoiBrowser-{PREVIOUS_BUILD}.zip").read_bytes())
    old_feed = old_archives / "appcast.xml"
    sparkle.generate_appcast(key, old_archives, old_feed, minimum_update_version=11)
    old_raw = old_feed.read_bytes()
    check("D1", "d-downgrade", "replayed genuine feed for build 12 offered to build 13",
          expect_accept=False, observations={
              "updateOffered": select_update(old_raw, CANDIDATE_BUILD, "nightly")
              is not None,
              "releaseContract": _contract(old_feed, expected_build=None),
          }, informational={
              "sparkleFeedSignatureValid": sparkle.verify_feed(key, old_feed),
          })
    lowered = re.sub(
        rb"<sparkle:version>13</sparkle:version>",
        b"<sparkle:version>11</sparkle:version>", raw, count=1)
    lowered = lowered.replace(b"<sparkle:minimumUpdateVersion>11<",
                              b"<sparkle:minimumUpdateVersion>1<")
    lowered = re.sub(rb"\s*<sparkle:deltas>.*?</sparkle:deltas>", b"", lowered,
                     flags=re.DOTALL)
    lowered_path = variant("downgrade-item.xml", lowered, resign=key)
    check("D2", "d-downgrade", "validly signed feed whose newest item is build 11",
          expect_accept=False, observations={
              "updateOfferedToBuild12": select_update(
                  lowered_path.read_bytes(), PREVIOUS_BUILD, "nightly") is not None,
              "releaseContract": _contract(lowered_path, expected_build=None),
          })
    duplicate = raw.replace(
        b"<sparkle:version>12</sparkle:version>",
        b"<sparkle:version>13</sparkle:version>", 1)
    duplicate_path = variant("duplicate-version.xml", duplicate, resign=key)
    check("D3", "d-downgrade", "two items claim build 13 (ambiguous version)",
          expect_accept=False, observations={
              "releaseContract": _contract(duplicate_path),
          })
    check("D4", "d-downgrade", "release claims build 12 while 13 is newest",
          expect_accept=False, observations={
              "releaseContract": _contract(
                  appcast, expected_build=PREVIOUS_BUILD, published_build_floor=None),
          })

    http_full = variant("http-enclosure.xml", raw.replace(
        b'url="https://updates.ahoi-testbed.invalid/nightly/AhoiBrowser-13.zip"',
        b'url="http://updates.ahoi-testbed.invalid/nightly/AhoiBrowser-13.zip"'),
        resign=key)
    check("E1", "e-http-enclosure", "validly re-signed feed with HTTP full enclosure",
          expect_accept=False, observations={
              "releaseContract": _contract(http_full),
          }, informational={
              "sparkleFeedSignatureValid": sparkle.verify_feed(key, http_full),
          })
    http_delta = variant("http-delta.xml", re.sub(
        rb'url="https://(updates\.ahoi-testbed\.invalid/nightly/[^"]+\.delta)"',
        rb'url="http://\1"', raw), resign=key)
    check("E2", "e-http-enclosure", "validly re-signed feed with HTTP delta enclosure",
          expect_accept=False, observations={
              "releaseContract": _contract(http_delta),
          })
    unsigned_http = variant("http-enclosure-unsigned.xml", raw.replace(
        b"https://updates.ahoi-testbed.invalid/nightly/AhoiBrowser-13.zip",
        b"http://updates.ahoi-testbed.invalid/nightly/AhoiBrowser-13.zip"))
    check("E3", "e-http-enclosure", "HTTP enclosure without re-signing the feed",
          expect_accept=False, observations={
              "sparkleSignUpdateVerifyFeed": sparkle.verify_feed(key, unsigned_http),
              "releaseContract": _contract(unsigned_http),
          })
    traversal = variant("dot-segment-enclosure.xml", raw.replace(
        b"https://updates.ahoi-testbed.invalid/nightly/AhoiBrowser-13.zip",
        b"https://updates.ahoi-testbed.invalid/nightly/../evil/AhoiBrowser-13.zip"),
        resign=key)
    check("E4", "e-http-enclosure", "dot-segment escape from the artifact base",
          expect_accept=False, observations={
              "releaseContract": _contract(traversal),
          })

    unsigned_prefix = raw[: raw.index(b"<!-- sparkle-signatures:")]
    stripped = variant("stripped-feed-signature.xml", unsigned_prefix)
    check("F1", "f-feed-weakening", "feed signature trailer removed",
          expect_accept=False, observations={
              "sparkleSignUpdateVerifyFeed": sparkle.verify_feed(key, stripped),
              "releaseContract": _contract(stripped),
          })
    edited = variant("edited-signed-feed.xml", raw.replace(
        b"<sparkle:minimumUpdateVersion>11<", b"<sparkle:minimumUpdateVersion>10<", 1))
    check("F2", "f-feed-weakening",
          "same-length edit of the signed body (minimumUpdateVersion 11 to 10)",
          expect_accept=False, observations={
              "sparkleSignUpdateVerifyFeed": sparkle.verify_feed(key, edited),
              "opensslEd25519VerifyFeedPrefix": feed_openssl(edited),
          })
    appended_item = variant("item-after-trailer.xml", raw + b"<item/>\n")
    check("F3", "f-feed-weakening", "unsigned content appended after the trailer",
          expect_accept=False, observations={
              "releaseContract": _contract(appended_item),
          })
    foreign = variant("foreign-channel.xml", raw.replace(
        b"<sparkle:channel>nightly</sparkle:channel>",
        b"<sparkle:channel>future</sparkle:channel>", 1), resign=key)
    check("F4", "f-feed-weakening", "validly signed item in an unknown channel",
          expect_accept=False, observations={
              "releaseContract": _contract(foreign),
          })
    check("F5", "f-feed-weakening", "nightly items presented as a stable feed",
          expect_accept=False, observations={
              "releaseContract": _contract(appcast, expected_channel="stable"),
              "offeredToStableBuild12": select_update(raw, PREVIOUS_BUILD, "stable")
              is not None,
          })
    check("F6", "f-feed-weakening", "nightly items are not offered to beta builds",
          expect_accept=False, observations={
              "offeredToBetaBuild12": select_update(raw, PREVIOUS_BUILD, "beta")
              is not None,
          })

    return {
        "schemaVersion": 1,
        "kind": "sparkle-update-testbed",
        "createdAt": dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "sparkle": {"version": PINNED_VERSION, "toolSha256": tool_hashes},
        "key": {
            "kind": "throwaway Ed25519 generated by OpenSSL; private seed deleted",
            "publicKeySha256": sha256_bytes(key.public_raw),
            "attackerPublicKeySha256": sha256_bytes(attacker.public_raw),
            "productKeyCollision": False,
        },
        "feedUrl": FEED_URL,
        "network": "none; all URLs use the reserved .invalid TLD",
        "appcast": {
            "sha256": sha256_bytes(raw),
            **validate_appcast_contract(
                appcast, expected_channel="nightly", expected_build=CANDIDATE_BUILD,
                expected_artifact_base_url=BASE_URL,
                published_build_floor=CANDIDATE_BUILD),
        },
        "archives": {
            path.name: {"sha256": sha256_file(path), "size": path.stat().st_size}
            for path in sorted(archives.iterdir()) if path.suffix in {".zip", ".delta"}
        },
        "commands": [
            [part if not part.startswith(str(work)) else
             "<work>" + part[len(str(work)):] for part in command]
            for command in sparkle.commands
        ],
        "checks": checks,
        "summary": {
            "total": len(checks),
            "passed": sum(item["result"] == "PASS" for item in checks),
            "failed": sum(item["result"] == "FAIL" for item in checks),
        },
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sparkle-tools", type=pathlib.Path,
                        help="directory with the fetched official Sparkle tools")
    parser.add_argument("--report", type=pathlib.Path,
                        help="write the JSON report to this path")
    args = parser.parse_args(argv)
    tools = args.sparkle_tools or default_sparkle_tools()
    with tempfile.TemporaryDirectory(prefix="ahoi-update-testbed-") as directory:
        report = run_testbed(tools.resolve(), pathlib.Path(directory))
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text)
    for item in report["checks"]:
        print(f"{item['result']} {item['id']} expected {item['expected']}: "
              f"{item['description']}")
    summary = report["summary"]
    print(f"{summary['passed']}/{summary['total']} checks passed")
    return 0 if summary["failed"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
